#include "scanner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "filepicker.h"
#include "portalfilepicker.h"

namespace {

const QVariantMap Defaults{{"color", false}, {"resolution", 300}, {"layout", "document"}, {"area", "A4"}};

// Millimetres of each paper size, scanned from the glass's corner.
QSizeF paperArea(const QString &area) {
    static const QHash<QString, QSizeF> sizes{{"A4", {210, 297}}, {"A5", {148, 210}}, {"Letter", {215.9, 279.4}}};
    return sizes.value(area);
}

// Card detection looks at the scan at about 75 dpi; that is plenty to find a
// card's edges and keeps it fast at 600 dpi.
constexpr double AnalysisDpi = 75;

// The darkest strip along the glass's edges is the lid's shadow, not the
// card, so it is cut off before looking.
constexpr double EdgeMillimeters = 3;

// A card at 75 dpi is about 250 × 160 pixels; anything much smaller is dust
// or a mark on the glass, not the card or the page.
constexpr int MinimumShapeArea = 10000;

int pixels(double millimeters, int dpi) { return int(std::lround(millimeters / 25.4 * dpi)); }

} // namespace

Scanner::Scanner(FilePicker *picker, QObject *parent)
    : QObject(parent), m_picker(picker), m_settings("pritingo", "pritingo"),
      m_work(QDir::tempPath() + "/pritingo-scan-XXXXXX") {
    if (!m_picker) {
        m_ownedPicker = std::make_unique<PortalFilePicker>();
        m_picker = m_ownedPicker.get();
    }
    connect(m_picker, &FilePicker::savePicked, this, &Scanner::saveTo);
    connect(m_picker, &FilePicker::failed, this, &Scanner::setProblem);

    for (auto it = Defaults.begin(); it != Defaults.end(); ++it) {
        QVariant value = m_settings.value("scan/" + it.key(), it.value());
        if (!value.convert(it.value().metaType()))
            value = it.value();
        m_options[it.key()] = value;
    }
}

Scanner::~Scanner() {
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(500);
    }
}

void Scanner::run(const QString &program, const QStringList &arguments, Done done) {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(process->readAllStandardOutput());
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, output, error);
    });
    connect(process, &QProcess::errorOccurred, this, [process, program, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        process->deleteLater();
        done(-1, {}, tr("%1 is not installed").arg(program));
    });
    process->start(program, arguments);
}

void Scanner::setProblem(const QString &problem) {
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged();
}

void Scanner::setStep(const QString &step, double progress) {
    m_step = step;
    m_progress = progress;
    emit stateChanged();
}

QVariantList Scanner::pages() const {
    QVariantList pages;
    for (const auto &page : m_pages)
        pages << QVariantMap{{"thumbnail", page.thumb.isEmpty() ? QString() : QUrl::fromLocalFile(page.thumb).toString()},
                             {"resolution", page.resolution}};
    return pages;
}

int Scanner::indexOf(int id) const {
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages[i].id == id)
            return i;
    return -1;
}

void Scanner::setOption(const QString &key, const QVariant &input) {
    if (!Defaults.contains(key))
        return;
    QVariant value = input;
    if (!value.convert(Defaults.value(key).metaType()) || m_options.value(key) == value)
        return;
    m_options[key] = value;
    m_settings.setValue("scan/" + key, value);
    emit optionsChanged();
}

// ---------------------------------------------------------------- device

void Scanner::findDevice() {
    if (m_searching)
        return;
    m_searching = true;
    setProblem({});
    emit deviceChanged();
    run("scanimage", {"-L"}, [this](int code, const QString &output, const QString &error) {
        m_searching = false;
        if (code == -1 && error.contains("not installed")) {
            m_scanAfterSearch = false;
            emit deviceChanged();
            return setProblem(tr("Scanning needs SANE: install the sane package."));
        }
        // Webcams show up as v4l devices; they are not what anyone means here.
        // The Canon's own pixma backend comes first when there are several.
        static const QRegularExpression line(R"(device `([^']+)' is a (.+))");
        QString device, name;
        for (auto it = line.globalMatch(output); it.hasNext();) {
            const auto m = it.next();
            if (m.captured(1).startsWith("v4l:"))
                continue;
            if (device.isEmpty() || (m.captured(1).startsWith("pixma:") && !device.startsWith("pixma:"))) {
                device = m.captured(1);
                name = m.captured(2);
            }
        }
        // "CANON Canon PIXMA E410 Series multi-function peripheral" reads
        // better as "Canon PIXMA E410 Series".
        name.remove(QRegularExpression(R"(\s+(multi-function peripheral|flatbed scanner|scanner)$)"));
        const QStringList words = name.split(' ');
        if (words.size() > 1 && words[0].compare(words[1], Qt::CaseInsensitive) == 0)
            name = words.mid(1).join(' ');
        m_device = device;
        m_deviceName = name;
        emit deviceChanged();
        if (m_device.isEmpty()) {
            m_scanAfterSearch = false;
            return setProblem(tr("No scanner found. Is the printer on and connected with USB?"));
        }
        if (m_scanAfterSearch) {
            m_scanAfterSearch = false;
            scan();
        }
    });
}

// ---------------------------------------------------------------- scanning

void Scanner::scan() {
    if (busy() || m_searching)
        return;
    if (m_device.isEmpty()) {
        m_scanAfterSearch = true;
        findDevice();
        return;
    }
    setProblem({});
    const int resolution = m_options.value("resolution").toInt();
    const QSizeF area = m_options.value("layout") == "card" ? QSizeF() : paperArea(m_options.value("area").toString());
    const QString raw = m_work.filePath(QStringLiteral("raw-%1.png").arg(++m_counter));
    QStringList arguments{"-d", m_device, "--mode", m_options.value("color").toBool() ? "Color" : "Gray",
                          "--resolution", QString::number(resolution), "--format=png", "--progress", "-o", raw};
    // A paper size lies against the glass's corner; a card, the whole glass
    // and "auto" scan everything, and auto and card find the shape afterwards.
    if (area.isValid())
        arguments << "-l" << "0" << "-t" << "0" << "-x" << QString::number(area.width()) << "-y"
                  << QString::number(area.height());

    setStep(tr("Scanning"), 0);
    m_scan = new QProcess(this);
    connect(m_scan, &QProcess::readyReadStandardError, this, [this] {
        // scanimage reports "Progress: 42.1%" on stderr, separated by carriage returns.
        static const QRegularExpression progress(R"(Progress:\s*([\d.]+)%)");
        const QString text = QString::fromUtf8(m_scan->readAllStandardError());
        double value = -1;
        for (auto it = progress.globalMatch(text); it.hasNext();)
            value = it.next().captured(1).toDouble();
        if (value >= 0)
            setStep(m_step, value / 100.0);
        else if (!text.trimmed().isEmpty())
            m_scan->setProperty("lastError", text.trimmed().section('\n', -1));
    });
    connect(m_scan, &QProcess::finished, this, [this, raw, resolution](int code, QProcess::ExitStatus status) {
        const QString error = m_scan->property("lastError").toString();
        m_scan->deleteLater();
        m_scan = nullptr;
        if (m_step.isEmpty())
            return; // cancelled
        if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(raw)) {
            setStep({});
            // The device may have gone away; look for it again next time.
            m_device.clear();
            emit deviceChanged();
            return setProblem(tr("The scan failed: %1").arg(error.isEmpty() ? tr("the scanner did not answer") : error));
        }
        addScan(raw, resolution);
    });
    connect(m_scan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_scan->deleteLater();
        m_scan = nullptr;
        setStep({});
        setProblem(tr("Scanning needs SANE: install the sane package."));
    });
    m_scan->start("scanimage", arguments);
}

void Scanner::cancel() {
    if (!busy())
        return;
    setStep({});
    if (m_scan)
        m_scan->kill();
    emit toast(tr("Scan cancelled"));
}

void Scanner::addScan(const QString &image, int resolution) {
    Page page;
    page.id = ++m_counter;
    page.resolution = resolution;
    page.file = m_work.filePath(QStringLiteral("page-%1.png").arg(page.id));

    auto finish = [this](const Page &page) {
        m_pages << page;
        setStep({});
        emit pagesChanged();
        thumbnail(page.id);
    };

    const bool card = m_options.value("layout") == "card";
    page.cut = card || m_options.value("area") == "auto";
    if (!page.cut) {
        if (!QFile::copy(image, page.file)) {
            setStep({});
            return setProblem(tr("Could not keep the scan."));
        }
        if (image.startsWith(m_work.path()))
            QFile::remove(image);
        return finish(page);
    }

    // The card (or photo, receipt…) is the largest dark shape on the white
    // lid: threshold a little below the lid's own brightness (its median,
    // since the lid covers most of the glass), close the gaps its light areas
    // leave, and take the biggest connected shape.
    setStep(card ? tr("Finding the card") : tr("Finding the edges"), 1);
    const double scale = AnalysisDpi / resolution;
    const int edge = pixels(EdgeMillimeters, resolution);
    const QStringList prepare{image, "-shave", QStringLiteral("%1x%1").arg(edge), "-resize",
                              QString::number(scale * 100, 'f', 3) + "%", "-colorspace", "gray", "-blur", "0x1"};
    run("magick", prepare + QStringList{"-format", "%[fx:median]", "info:"},
        [=, this](int code, const QString &output, const QString &error) mutable {
            if (code != 0) {
                setStep({});
                return setProblem(tr("Could not read the scan: %1").arg(error));
            }
            const double lid = output.trimmed().toDouble();
            const QString threshold = QString::number(std::clamp(lid - 0.045, 0.3, 0.97) * 100, 'f', 1) + "%";
            run("magick",
                prepare + QStringList{"-threshold", threshold, "-negate", "-morphology", "Close", "Disk:4", "-define",
                                      "connected-components:verbose=true", "-define",
                                      "connected-components:exclude-header=true", "-connected-components", "8",
                                      "null:"},
                [=, this](int code, const QString &output, const QString &error) mutable {
                    if (code != 0) {
                        setStep({});
                        return setProblem(tr("Could not read the scan: %1").arg(error));
                    }
                    static const QRegularExpression shape(R"((\d+)x(\d+)\+(\d+)\+(\d+)\s+\S+\s+(\d+)\s+(\S+))");
                    QRect best;
                    qint64 bestArea = 0;
                    for (auto it = shape.globalMatch(output); it.hasNext();) {
                        const auto m = it.next();
                        const qint64 area = m.captured(5).toLongLong();
                        if (!m.captured(6).contains("255") || area < MinimumShapeArea || area <= bestArea)
                            continue;
                        bestArea = area;
                        best = QRect(m.captured(3).toInt(), m.captured(4).toInt(), m.captured(1).toInt(),
                                     m.captured(2).toInt());
                    }
                    QStringList cut{image};
                    if (best.isValid()) {
                        const QRect box(int(best.x() / scale) + edge, int(best.y() / scale) + edge,
                                        int(std::ceil(best.width() / scale)), int(std::ceil(best.height() / scale)));
                        // The box is a little generous after the blur; trimming
                        // the lid's colour off its edges takes it to the card.
                        cut << "-crop"
                            << QStringLiteral("%1x%2+%3+%4").arg(box.width()).arg(box.height()).arg(box.x()).arg(box.y())
                            << "+repage" << "-fuzz" << "10%" << "-trim" << "+repage";
                        // Cards are copied lying down; a card scanned upright turns.
                        if (card && box.height() > box.width())
                            cut << "-rotate" << "90";
                    } else {
                        // Kept as scanned: a whole glass is already page-sized.
                        page.cut = card;
                        emit toast(card ? tr("Could not find the card's edges; kept the whole glass")
                                        : tr("Could not find the edges; kept the whole glass"));
                    }
                    run("magick", cut << page.file, [=, this](int code, const QString &, const QString &error) mutable {
                        if (image.startsWith(m_work.path()))
                            QFile::remove(image);
                        if (code != 0) {
                            setStep({});
                            return setProblem(tr("Could not read the scan: %1").arg(error));
                        }
                        finish(page);
                    });
                });
        });
}

void Scanner::thumbnail(int id) {
    const int index = indexOf(id);
    if (index < 0)
        return;
    Page &page = m_pages[index];
    // A new name per version, so the window's image cache never shows the
    // page before it was turned.
    const QString thumb = m_work.filePath(QStringLiteral("thumb-%1-%2.jpg").arg(id).arg(page.version));
    run("magick", {page.file, "-thumbnail", "420x420", "-quality", "85", thumb},
        [this, id, thumb](int code, const QString &, const QString &) {
            const int index = indexOf(id);
            if (code != 0 || index < 0)
                return;
            if (!m_pages[index].thumb.isEmpty())
                QFile::remove(m_pages[index].thumb);
            m_pages[index].thumb = thumb;
            emit pagesChanged();
        });
}

void Scanner::rotatePage(int index) {
    if (index < 0 || index >= m_pages.size() || busy())
        return;
    const int id = m_pages[index].id;
    setStep(tr("Turning the page"), 1);
    run("magick", {m_pages[index].file, "-rotate", "90", m_pages[index].file},
        [this, id](int code, const QString &, const QString &error) {
            setStep({});
            const int index = indexOf(id);
            if (index < 0)
                return;
            if (code != 0)
                return setProblem(tr("Could not turn the page: %1").arg(error));
            ++m_pages[index].version;
            thumbnail(id);
        });
}

void Scanner::removePage(int index) {
    if (index < 0 || index >= m_pages.size() || busy())
        return;
    QFile::remove(m_pages[index].file);
    QFile::remove(m_pages[index].thumb);
    m_pages.removeAt(index);
    emit pagesChanged();
}

void Scanner::clear() {
    if (busy())
        return;
    for (const auto &page : std::as_const(m_pages)) {
        QFile::remove(page.file);
        QFile::remove(page.thumb);
    }
    m_pages.clear();
    setProblem({});
    emit pagesChanged();
}

// ---------------------------------------------------------------- output

void Scanner::writePdf(const QString &target, std::function<void(const QString &)> done) {
    if (m_pages.isEmpty())
        return done(tr("Nothing is scanned yet."));
    const bool gray = !m_options.value("color").toBool();
    QStringList arguments;

    if (m_options.value("layout") != "card") {
        // Each scan keeps its own resolution, so a page scanned at 150 dpi
        // and one at 600 dpi both come out at their paper size. A cut-out
        // goes on an A4 page at its real size, shrunk only if it would not fit.
        for (const auto &page : m_pages) {
            const QString dpi = QString::number(page.resolution);
            if (!page.cut) {
                arguments << "(" << page.file << "-set" << "units" << "PixelsPerInch" << "-set" << "density" << dpi
                          << ")";
                continue;
            }
            const int width = pixels(210, page.resolution), height = pixels(297, page.resolution);
            const int margin = pixels(15, page.resolution);
            arguments << "(" << "-size" << QStringLiteral("%1x%2").arg(width).arg(height) << "xc:white" << "("
                      << page.file << "-resize"
                      << QStringLiteral("%1x%2>").arg(width - 2 * margin).arg(height - 2 * margin) << ")"
                      << "-gravity" << "north" << "-geometry" << QStringLiteral("+0+%1").arg(margin) << "-composite"
                      << "-set" << "units" << "PixelsPerInch" << "-set" << "density" << dpi << ")";
        }
    } else {
        // Two sides to a sheet at their real size: front above back, the way
        // a photocopy of an ID card looks.
        for (int i = 0; i < m_pages.size(); i += 2) {
            const int dpi = std::max(m_pages[i].resolution, m_pages.value(i + 1, m_pages[i]).resolution);
            arguments << "(" << "-size"
                      << QStringLiteral("%1x%2").arg(pixels(210, dpi)).arg(pixels(297, dpi)) << "xc:white"
                      << "-gravity" << "north";
            int top = pixels(30, dpi);
            for (int side = i; side < std::min(i + 2, int(m_pages.size())); ++side) {
                const Page &page = m_pages[side];
                const int height = int(std::lround(QImageReader(page.file).size().height() * double(dpi) / page.resolution));
                arguments << "(" << page.file << "-set" << "units" << "PixelsPerInch" << "-set" << "density"
                          << QString::number(page.resolution) << "-resample" << QString::number(dpi) << ")"
                          << "-geometry" << QStringLiteral("+0+%1").arg(top) << "-composite";
                top += height + pixels(15, dpi);
            }
            arguments << "-set" << "units" << "PixelsPerInch" << "-set" << "density" << QString::number(dpi) << ")";
        }
    }
    if (gray)
        arguments << "-colorspace" << "Gray";
    // Named explicitly: the target may end in .part while it is written.
    arguments << "-compress" << "jpeg" << "-quality" << "90" << "pdf:" + target;

    setStep(tr("Making the PDF"), 1);
    run("magick", arguments, [this, done](int code, const QString &, const QString &error) {
        setStep({});
        done(code == 0 ? QString() : (error.isEmpty() ? tr("ImageMagick failed") : error));
    });
}

void Scanner::save() {
    if (m_pages.isEmpty() || busy())
        return;
    m_picker->save(tr("Save the scan as PDF"),
                   tr("Scan %1.pdf").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm")));
}

void Scanner::saveTo(const QString &chosen) {
    QString path = chosen;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";
    // Written beside the target and renamed into place, so a failure never
    // leaves half a file where the user expects their scan.
    const QString partial = path + ".part";
    writePdf(partial, [this, path, partial](const QString &error) {
        if (!error.isEmpty()) {
            QFile::remove(partial);
            return setProblem(tr("Could not save the PDF: %1").arg(error));
        }
        QFile::remove(path); // the portal already asked before replacing it
        if (!QFile::rename(partial, path)) {
            QFile::remove(partial);
            return setProblem(tr("Could not save the PDF to “%1”.").arg(path));
        }
        emit toast(tr("Saved “%1”").arg(QFileInfo(path).fileName()));
    });
}

void Scanner::print() {
    if (m_pages.isEmpty() || busy())
        return;
    const QString pdf = m_work.filePath(tr("Scan") + ".pdf");
    writePdf(pdf, [this, pdf](const QString &error) {
        if (!error.isEmpty())
            return setProblem(tr("Could not make the PDF: %1").arg(error));
        emit readyToPrint(pdf);
    });
}
