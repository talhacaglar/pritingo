#include "backend.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "filepicker.h"
#include "portalfilepicker.h"

namespace {

// Seconds one page takes on the E410 from send to "back on USB", measured on
// plain paper; used only for the estimate shown before printing.
constexpr int SecondsPerPage = 30;

// A job still queued after this long usually means no paper or an error
// light on the printer, which the driver cannot report back.
constexpr int StallSeconds = 75;

const QStringList ImageSuffixes{"jpg", "jpeg", "png", "webp", "gif", "bmp", "tif", "tiff"};
const QStringList SheetSuffixes{"xlsx", "xls", "ods"};

const QVariantMap Defaults{
    {"duplex", false},     {"color", false},      {"quality", 3},       {"media", "plain"},
    {"paperSize", "A4"},   {"brightness", "1.8"}, {"density", 0},       {"contrast", 0},
    {"cyan", 0},           {"magenta", 0},        {"yellow", 0},        {"cartridges", "bkcolor"},
    {"copies", 1},         {"reverseBacks", false}, {"flipBacks", false}, {"autoRotate", true},
    {"sounds", true},      {"scaling", "fit"},    {"scalePercent", 100},
};

// These belong to one print, not to the next document.
const QStringList PerPrintKeys{"copies", "scaling", "scalePercent"};

const QStringList AdvancedKeys{"brightness", "density", "contrast", "cyan", "magenta", "yellow", "cartridges"};

QStringList printablePatterns() {
    QStringList patterns{"*.pdf", "*.PDF", "*.docx", "*.doc", "*.odt", "*.rtf", "*.txt", "*.xlsx", "*.xls",
                         "*.ods", "*.pptx", "*.ppt", "*.odp"};
    for (const auto &suffix : ImageSuffixes)
        patterns << "*." + suffix << "*." + suffix.toUpper();
    return patterns;
}

// Any USB interface of class 07 is a printer, whether or not usblp has it;
// this is how the app tells "connected" from "unplugged or off".
bool usbPrinterPresent(const QString &devices) {
    QDirIterator it(devices, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        QFile file(it.next() + "/bInterfaceClass");
        if (file.open(QIODevice::ReadOnly) && file.readAll().trimmed() == "07")
            return true;
    }
    return false;
}

} // namespace

Backend::Backend(FilePicker *picker, QObject *parent, const QString &usbDevices)
    : QObject(parent), m_picker(picker), m_settings("pritingo", "pritingo"), m_usbDevices(usbDevices) {
    if (!m_picker) {
        m_ownedPicker = std::make_unique<PortalFilePicker>();
        m_picker = m_ownedPicker.get();
    }
    connect(m_picker, &FilePicker::picked, this, [this](const QStringList &paths) {
        load(paths, true, m_pickToAdd);
    });
    connect(m_picker, &FilePicker::failed, this, &Backend::setProblem);

    // QSettings hands values back as strings; QML compares strictly, so give
    // each option its default's type again ("false" would read as true).
    for (auto it = Defaults.begin(); it != Defaults.end(); ++it) {
        QVariant value = m_settings.value("options/" + it.key(), it.value());
        if (!value.convert(it.value().metaType()))
            value = it.value();
        m_options[it.key()] = value;
    }
    // Copies are per print, not a preference.
    m_options["copies"] = 1;

    m_printerPoll.setInterval(3000);
    connect(&m_printerPoll, &QTimer::timeout, this, &Backend::pollPrinter);
    m_printerPoll.start();

    m_jobPoll.setInterval(1500);
    connect(&m_jobPoll, &QTimer::timeout, this, &Backend::waitForJob);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &Backend::jobChanged);

    refreshPrinters();
}

Backend::~Backend() {
    // Every helper process is a child, including the short printer probes.
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(500);
    }
}

QString Backend::driverPpd() {
    for (const auto &path : {QStringLiteral("/usr/share/ppd/canone400.ppd"),
                             QStringLiteral("/usr/share/cups/model/canone400.ppd")})
        if (QFileInfo::exists(path))
            return path;
    return {};
}

void Backend::run(const QString &program, const QStringList &arguments, Done done, const QString &stdoutFile,
                  const QProcessEnvironment &environment) {
    auto *process = new QProcess(this);
    m_running << process;
    if (!stdoutFile.isEmpty())
        process->setStandardOutputFile(stdoutFile);
    if (!environment.isEmpty())
        process->setProcessEnvironment(environment);
    connect(process, &QProcess::finished, this, [this, process, done](int code, QProcess::ExitStatus status) {
        m_running.removeAll(process);
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, error);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, program, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_running.removeAll(process);
        process->deleteLater();
        done(-1, tr("%1 is not installed").arg(program));
    });
    process->start(program, arguments);
}

void Backend::setProblem(const QString &problem) {
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged();
}

void Backend::setStep(const QString &step) {
    m_step = step;
    emit jobChanged();
}

// ---------------------------------------------------------------- printer

void Backend::refreshPrinters() {
    auto *probe = new QProcess(this);
    connect(probe, &QProcess::finished, this, [this, probe] {
        const QString out = QString::fromUtf8(probe->readAllStandardOutput());
        probe->deleteLater();
        QStringList printers = out.split('\n', Qt::SkipEmptyParts);
        for (auto &p : printers)
            p = p.trimmed();
        m_printers = printers;
        QString wanted = m_settings.value("printer").toString();
        if (!m_printers.contains(wanted))
            wanted = m_printers.contains("Canon_E410") ? "Canon_E410" : m_printers.value(0);
        m_printer.clear();
        setPrinter(wanted);
        if (m_printers.isEmpty())
            setProblem(tr("No printer is set up in CUPS."));
    });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError) {
        probe->deleteLater();
        setProblem(tr("CUPS is not installed (lpstat is missing)."));
    });
    probe->start("lpstat", {"-e"});
}

void Backend::setPrinter(const QString &printer) {
    if (printer == m_printer)
        return;
    m_printer = printer;
    m_settings.setValue("printer", printer);
    m_deviceUri.clear();
    if (!printer.isEmpty()) {
        auto *probe = new QProcess(this);
        connect(probe, &QProcess::finished, this, [this, probe, printer] {
            const QString out = QString::fromUtf8(probe->readAllStandardOutput());
            probe->deleteLater();
            const auto match = QRegularExpression(R"(device for [^:]+:\s*(\S+))").match(out);
            if (printer == m_printer && match.hasMatch())
                m_deviceUri = match.captured(1);
        });
        connect(probe, &QProcess::errorOccurred, probe, &QObject::deleteLater);
        probe->start("lpstat", {"-v", printer});
    }
    emit printerChanged();
    pollPrinter();
}

void Backend::pollPrinter() {
    const bool connected = usbPrinterPresent(m_usbDevices);
    if (connected != m_printerConnected) {
        m_printerConnected = connected;
        emit printerChanged();
        // The warning from pressing Print goes once the printer is back.
        if (connected && m_problem == offlineProblem()) {
            setProblem({});
            emit toast(tr("The printer is connected"));
        }
    }
    if (m_printer.isEmpty() || m_queueProbe)
        return;
    m_queueProbe = new QProcess(this);
    connect(m_queueProbe, &QProcess::finished, this, [this] {
        const int jobs = QString::fromUtf8(m_queueProbe->readAllStandardOutput()).count('\n');
        m_queueProbe->deleteLater();
        m_queueProbe = nullptr;
        if (jobs != m_printerQueue) {
            m_printerQueue = jobs;
            emit printerChanged();
        }
    });
    connect(m_queueProbe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_queueProbe->deleteLater();
        m_queueProbe = nullptr;
    });
    m_queueProbe->start("lpstat", {"-o", m_printer});
}

// Looks again right now rather than trusting the last poll, so a printer
// switched off a second ago does not get a job that only waits in the queue.
// Only a USB queue (or one whose device is not known yet; the E410 is USB)
// can be checked this way, so a network printer is always let through.
bool Backend::printerOffline() {
    pollPrinter();
    const bool usb = m_deviceUri.isEmpty() || m_deviceUri.startsWith("usb:");
    return usb && !m_printerConnected;
}

QString Backend::offlineProblem() const {
    return tr("The printer is not connected. Turn it on and check its USB cable, then try again.");
}

// ---------------------------------------------------------------- document

void Backend::pickFiles() {
    m_pickToAdd = false;
    m_picker->open(tr("Choose files to print"), printablePatterns());
}

void Backend::pickFilesToAdd() {
    if (m_parts.isEmpty())
        return pickFiles();
    m_pickToAdd = true;
    m_picker->open(tr("Choose files to add at the end"), printablePatterns());
}

void Backend::openFile(const QString &pathOrUrl) {
    load({pathOrUrl}, false, false);
}

void Backend::openFiles(const QStringList &pathsOrUrls, bool byName) {
    load(pathsOrUrls, byName, false);
}

void Backend::addFiles(const QStringList &pathsOrUrls, bool byName) {
    load(pathsOrUrls, byName, true);
}

void Backend::load(const QStringList &pathsOrUrls, bool byName, bool append) {
    if (m_state == "printing" || m_state == "flip" || (append && m_loading))
        return;
    QStringList paths;
    for (const auto &item : pathsOrUrls) {
        const QString path = item.startsWith("file:") ? QUrl(item).toLocalFile() : item;
        if (!QFileInfo(path).isFile()) {
            setProblem(tr("“%1” does not exist.").arg(path));
            return;
        }
        paths << path;
    }
    if (paths.isEmpty())
        return;
    if (byName) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(paths.begin(), paths.end(), [&collator](const QString &a, const QString &b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });
    }

    const int generation = ++m_loadGeneration;
    if (!append || m_parts.isEmpty()) {
        m_work = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/pritingo-XXXXXX");
        m_parts.clear();
        m_pageSizes.clear();
        m_pageRotations.clear();
        m_thumbnails.clear();
        m_grayThumbnails.clear();
        m_inkBoxes.clear();
        resetScale();
    }
    m_loading = true;
    m_state = "idle";
    setProblem({});
    emit documentChanged();
    emit jobChanged();
    convertNext(paths, 0, m_parts, generation);
}

// One file at a time: LibreOffice does not run twice at once on one profile.
// The document changes only once every file has converted, so a file that
// fails to open leaves what was already open as it was.
void Backend::convertNext(const QStringList &paths, int index, const QList<Part> &parts, int generation) {
    if (generation != m_loadGeneration)
        return;
    if (index == paths.size())
        return join(parts, generation);
    const QString name = QFileInfo(paths[index]).fileName();
    const QString base = m_work->filePath(QStringLiteral("part-%1").arg(m_nextPart++));
    convert(paths[index], base, generation, [=, this](int code, const QString &error) {
        if (generation != m_loadGeneration)
            return;
        const QString pdf = base + ".pdf";
        if (code != 0 || !QFileInfo::exists(pdf))
            return loadFailed(
                tr("Could not open “%1”: %2").arg(name, error.isEmpty() ? tr("conversion failed") : error));
        auto *info = new QProcess(this);
        connect(info, &QProcess::finished, this, [=, this] {
            const QString out = QString::fromUtf8(info->readAllStandardOutput());
            info->deleteLater();
            if (generation != m_loadGeneration)
                return;
            const int pages = QRegularExpression(R"(Pages:\s+(\d+))").match(out).captured(1).toInt();
            if (pages < 1)
                return loadFailed(tr("“%1” has no printable pages.").arg(name));
            convertNext(paths, index + 1, parts + QList<Part>{{name, pdf, pages}}, generation);
        });
        connect(info, &QProcess::errorOccurred, this, [this, info](QProcess::ProcessError) {
            info->deleteLater();
            loadFailed(tr("poppler is not installed (pdfinfo is missing)."));
        });
        info->start("pdfinfo", {pdf});
    });
}

// Turns one file into base.pdf. The original is never touched; everything
// works on copies next to base.
void Backend::convert(const QString &path, const QString &base, int generation, Done done) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString pdf = base + ".pdf";
    if (suffix == "pdf") {
        const bool copied = QFile::copy(path, pdf);
        return done(copied ? 0 : 1, copied ? QString() : tr("cannot read the file"));
    }
    if (ImageSuffixes.contains(suffix))
        return run("magick", {path, "-auto-orient", pdf}, done);

    // LibreOffice names its output after the input; copy under a plain name
    // first so odd characters in the original never matter.
    const QString copy = base + "." + suffix;
    QFile::copy(path, copy);
    const QString dir = QFileInfo(base).absolutePath();
    auto toPdf = [this, dir, done](const QString &input) {
        run("soffice", {"--headless", "--convert-to", "pdf", "--outdir", dir, input}, done);
    };
    if (!SheetSuffixes.contains(suffix))
        return toPdf(copy);

    // LibreOffice cuts a sheet wider than the paper into columns on pages of
    // their own, so a form ends up split down the middle. Every sheet is
    // fitted to the paper's width instead, as many pages tall as it needs.
    // xlsx is where that setting is simple to change, so the other formats
    // pass through it; if anything on the way fails, the sheet prints as is.
    const QString xlsx = base + ".xlsx";
    auto fit = [=, this] {
        fitSheetsToWidth(xlsx, [=, this] {
            if (generation == m_loadGeneration)
                toPdf(xlsx);
        });
    };
    if (suffix == "xlsx")
        return fit();
    const QString converted = base + "-xlsx";
    run("soffice", {"--headless", "--convert-to", "xlsx", "--outdir", converted, copy},
        [=, this](int code, const QString &) {
            if (generation != m_loadGeneration)
                return;
            if (code != 0 || !QFile::rename(converted + "/" + QFileInfo(base).fileName() + ".xlsx", xlsx))
                return toPdf(copy);
            fit();
        });
}

void Backend::fitSheetsToWidth(const QString &xlsx, std::function<void()> done) {
    // An xlsx is a zip of XML parts; bsdtar comes with libarchive, which
    // pacman itself needs.
    const QString dir = xlsx + "-parts";
    QDir().mkpath(dir);
    run("bsdtar", {"-xf", xlsx, "-C", dir}, [=, this](int code, const QString &) {
        bool changed = false;
        // A zip can hold symlinks; only files that really lie inside dir are
        // rewritten, never something a link points to elsewhere.
        const QString root = QDir(dir).canonicalPath() + "/";
        const auto sheets = QDir(dir + "/xl/worksheets").entryInfoList({"*.xml"}, QDir::Files);
        for (const auto &sheet : code == 0 ? sheets : QFileInfoList{}) {
            if (!sheet.canonicalFilePath().startsWith(root))
                continue;
            QFile file(sheet.filePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QString xml = QString::fromUtf8(file.readAll());
            const QString fitted = fitSheetToWidth(xml);
            file.close();
            if (fitted == xml || !file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                continue;
            file.write(fitted.toUtf8());
            changed = true;
        }
        if (!changed)
            return done();
        // The names come from the file, so each goes in as ./name: one that
        // starts with - or @ must not read as an option or an archive to
        // include. -s takes the ./ off again, since LibreOffice does not open
        // an xlsx whose parts are stored under ./.
        const QString fitted = dir + ".xlsx";
        QStringList entries;
        for (const auto &entry : QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            entries << "./" + entry;
        run("bsdtar", QStringList{"--format", "zip", "-s", R"(,^\./,,)", "-cf", fitted, "-C", dir, "--"} + entries,
            [=](int code, const QString &) {
                if (code == 0 && QFile::remove(xlsx))
                    QFile::rename(fitted, xlsx);
                done();
            });
    });
}

// Fits one worksheet's XML to the paper's width and leaves its height free:
// fitToPage on the sheet, fitToWidth 1 and fitToHeight 0 ("as many as it
// takes") on its page setup. A sheet whose author already fitted it keeps
// that choice, and one written in a way this does not recognise stays as is.
QString Backend::fitSheetToWidth(QString xml) {
    static const QRegularExpression fitted(R"re(<pageSetUpPr\b[^>]*\bfitToPage="(1|true)")re");
    static const QRegularExpression setup(R"(<pageSetup\b[^>]*>)");
    static const QRegularExpression margins(R"(<pageMargins\b[^>]*/>)");
    static const QRegularExpression fitAttributes(R"re(\s(fitToWidth|fitToHeight)="[^"]*")re");
    static const QRegularExpression setUpPr(R"(<pageSetUpPr\b[^>]*/>)");
    static const QRegularExpression emptySheetPr(R"(<sheetPr\b([^>]*)/>)");
    static const QRegularExpression worksheet(R"(<worksheet\b[^>]*>)");
    if (fitted.match(xml).hasMatch())
        return xml;

    // Without a pageSetup the fit would default to one page tall as well.
    // It belongs right after pageMargins in the schema's order.
    if (const auto match = setup.match(xml); match.hasMatch()) {
        QString element = match.captured(0);
        element.remove(fitAttributes).insert(10, R"( fitToWidth="1" fitToHeight="0")");
        xml.replace(match.capturedStart(), match.capturedLength(), element);
    } else if (const auto match = margins.match(xml); match.hasMatch()) {
        xml.insert(match.capturedEnd(), R"(<pageSetup fitToWidth="1" fitToHeight="0"/>)");
    } else {
        return xml;
    }

    // pageSetUpPr is the last thing in sheetPr, which comes first in the sheet.
    const QString fitToPage = R"(<pageSetUpPr fitToPage="1"/>)";
    if (const auto match = setUpPr.match(xml); match.hasMatch())
        xml.replace(match.capturedStart(), match.capturedLength(), fitToPage);
    else if (const auto match = emptySheetPr.match(xml); match.hasMatch())
        xml.replace(match.capturedStart(), match.capturedLength(),
                    "<sheetPr" + match.captured(1) + ">" + fitToPage + "</sheetPr>");
    else if (const int end = xml.indexOf("</sheetPr>"); end >= 0)
        xml.insert(end, fitToPage);
    else if (const auto match = worksheet.match(xml); match.hasMatch())
        xml.insert(match.capturedEnd(), "<sheetPr>" + fitToPage + "</sheetPr>");
    return xml;
}

// Joins every part into in.pdf, keeping the pages the user left out of what
// was already open; the new files' pages all come in chosen.
void Backend::join(const QList<Part> &parts, int generation) {
    const QString target = m_work->filePath("in.pdf"), joined = m_work->filePath("joined.pdf");
    std::optional<QList<int>> selection;
    if (!m_parts.isEmpty()) {
        QList<int> pages = m_selection;
        int total = 0;
        for (const auto &part : parts)
            total += part.pages;
        for (int page = pageCount() + 1; page <= total; ++page)
            pages << page;
        selection = pages;
    }
    auto finish = [=, this](int code, const QString &error) {
        if (generation != m_loadGeneration)
            return;
        if (code != 0 || !QFileInfo::exists(joined))
            return loadFailed(
                tr("Could not join the files: %1").arg(error.isEmpty() ? tr("conversion failed") : error));
        QFile::remove(target);
        QFile::rename(joined, target);
        m_parts = parts;
        loadPdf(selection);
    };
    QFile::remove(joined);
    if (parts.size() == 1) {
        const bool copied = QFile::copy(parts.first().pdf, joined);
        return finish(copied ? 0 : 1, copied ? QString() : tr("cannot read the file"));
    }
    QStringList arguments{"--warning-exit-0", "--no-warn", "--empty", "--pages"};
    for (const auto &part : parts)
        arguments << part.pdf;
    run("qpdf", arguments << "--" << joined, finish);
}

void Backend::loadFailed(const QString &message) {
    m_loading = false;
    emit documentChanged();
    setProblem(message);
}

QString Backend::fileName() const {
    if (m_parts.size() == 1)
        return m_parts.first().name;
    return m_parts.isEmpty() ? QString() : tr("%n file(s)", "", m_parts.size());
}

QStringList Backend::fileNames() const {
    QStringList names;
    for (const auto &part : m_parts)
        names << part.name;
    return names;
}

QVariantList Backend::fileStarts() const {
    QVariantList starts;
    int page = 1;
    for (const auto &part : m_parts) {
        starts << page;
        page += part.pages;
    }
    return starts;
}

void Backend::loadPdf(const std::optional<QList<int>> &selection) {
    const int generation = m_loadGeneration;
    const QString pdf = m_work->filePath("in.pdf");
    auto *info = new QProcess(this);
    connect(info, &QProcess::finished, this, [this, info, generation, pdf, selection](int code) {
        const QString out = QString::fromUtf8(info->readAllStandardOutput());
        info->deleteLater();
        if (generation != m_loadGeneration)
            return;
        const int pages = QRegularExpression(R"(Pages:\s+(\d+))").match(out).captured(1).toInt();
        if (code != 0 || pages < 1) {
            m_loading = false;
            emit documentChanged();
            setProblem(tr("This file has no printable pages."));
            return;
        }

        // Per-page sizes and rotations decide which pages turn for portrait paper.
        auto *detail = new QProcess(this);
        connect(detail, &QProcess::finished, this, [this, detail, generation, pdf, pages, selection] {
            const QString text = QString::fromUtf8(detail->readAllStandardOutput());
            detail->deleteLater();
            if (generation != m_loadGeneration)
                return;
            QList<QSizeF> sizes(pages, QSizeF(595, 842));
            QList<int> rotations(pages, 0);
            static const QRegularExpression size(R"(Page\s+(\d+)\s+size:\s+([\d.]+)\s+x\s+([\d.]+))");
            static const QRegularExpression rot(R"(Page\s+(\d+)\s+rot:\s+(-?\d+))");
            for (auto it = size.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                const int i = m.captured(1).toInt() - 1;
                if (i >= 0 && i < pages)
                    sizes[i] = QSizeF(m.captured(2).toDouble(), m.captured(3).toDouble());
            }
            for (auto it = rot.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                const int i = m.captured(1).toInt() - 1;
                if (i >= 0 && i < pages)
                    rotations[i] = m.captured(2).toInt();
            }
            m_pageSizes = sizes;
            m_pageRotations = rotations;
            QList<int> chosen = selection ? *selection : Planner::parseRange({}, pages);
            chosen.removeIf([pages](int page) { return page < 1 || page > pages; });
            setSelection(chosen);
            emit documentChanged();

            // Small renders for the sheet view; the window shows them at about
            // 150 px wide, and pdftoppm pads numbers by the page count's width.
            // Each load renders into a folder of its own, so the window never
            // shows a cached picture under a name that now means another page.
            const QString thumbs = m_work->filePath(QStringLiteral("thumbs-%1").arg(generation));
            QDir().mkpath(thumbs);
            // A grey set too, so black-and-white shows as it will print
            // without a shader effect.
            auto list = [thumbs](const QString &prefix) {
                QStringList files = QDir(thumbs).entryList({prefix + "-*.png"}, QDir::Files);
                QCollator collator;
                collator.setNumericMode(true);
                std::sort(files.begin(), files.end(), collator);
                QStringList urls;
                for (const auto &file : files)
                    urls << QUrl::fromLocalFile(thumbs + "/" + file).toString();
                return urls;
            };
            run("pdftoppm", {"-r", "40", "-png", pdf, thumbs + "/t"},
                [this, generation, pdf, thumbs, list](int, const QString &) {
                    if (generation != m_loadGeneration)
                        return;
                    run("pdftoppm", {"-r", "40", "-png", "-gray", pdf, thumbs + "/g"},
                        [this, generation, thumbs, list](int, const QString &) {
                            if (generation != m_loadGeneration)
                                return;
                            m_thumbnails = list("t");
                            m_grayThumbnails = list("g");
                            // The renders are at hand, and unlike the PDF they
                            // know what really shows: a white background or a
                            // page's hidden leftovers are not marks.
                            m_inkBoxes.clear();
                            for (const auto &url : std::as_const(m_thumbnails))
                                m_inkBoxes << Planner::inkBox(QImage(QUrl(url).toLocalFile()));
                            m_loading = false;
                            touchLayout();
                            emit documentChanged();
                            replan();
                            const QDir work(m_work->path());
                            for (const auto &old : work.entryList({"thumbs-*"}, QDir::Dirs))
                                if (work.filePath(old) != thumbs)
                                    QDir(work.filePath(old)).removeRecursively();
                        });
                });
        });
        detail->start("pdfinfo", {"-f", "1", "-l", QString::number(pages), pdf});
    });
    connect(info, &QProcess::errorOccurred, this, [this, info](QProcess::ProcessError) {
        info->deleteLater();
        m_loading = false;
        emit documentChanged();
        setProblem(tr("poppler is not installed (pdfinfo is missing)."));
    });
    info->start("pdfinfo", {pdf});
}

void Backend::closeFile() {
    if (m_state == "printing" || m_state == "flip")
        return;
    ++m_loadGeneration;
    m_work.reset();
    m_parts.clear();
    m_pageSizes.clear();
    m_pageRotations.clear();
    m_thumbnails.clear();
    m_grayThumbnails.clear();
    m_inkBoxes.clear();
    resetScale();
    m_loading = false;
    m_state = "idle";
    setSelection({});
    emit documentChanged();
    emit jobChanged();
}

void Backend::newPrint() {
    if (m_state == "printing" || m_state == "flip")
        return;
    dismiss();
    closeFile();
    // Copies belong to the print that is done, like the files.
    setOption("copies", 1);
    setProblem({});
    pickFiles();
}

QVariantList Backend::landscapePages() const {
    QVariantList pages;
    for (int i = 0; i < m_pageSizes.size(); ++i)
        if (Planner::rotation(m_pageSizes[i].width(), m_pageSizes[i].height(), m_pageRotations.value(i), true,
                              false) == 90)
            pages << i + 1;
    return pages;
}

void Backend::requestPreview(int page) {
    if (!m_work || page < 1 || page > pageCount())
        return;
    const bool gray = !m_options.value("color").toBool();
    const QString base = m_work->filePath(QStringLiteral("preview-%1%2").arg(page).arg(gray ? "g" : ""));
    if (QFileInfo::exists(base + ".png")) {
        emit previewReady(page, QUrl::fromLocalFile(base + ".png").toString());
        return;
    }
    QStringList arguments{"-r", "110", "-png", "-singlefile", "-f", QString::number(page), "-l", QString::number(page)};
    if (gray)
        arguments << "-gray";
    run("pdftoppm", arguments + QStringList{m_work->filePath("in.pdf"), base},
        [this, page, base](int code, const QString &) {
            if (code == 0)
                emit previewReady(page, QUrl::fromLocalFile(base + ".png").toString());
        });
}

// ---------------------------------------------------------------- choices

void Backend::setOption(const QString &key, const QVariant &input) {
    if (!Defaults.contains(key))
        return;
    QVariant value = input;
    if (!value.convert(Defaults.value(key).metaType()) || m_options.value(key) == value)
        return;
    m_options[key] = value;
    if (!PerPrintKeys.contains(key))
        m_settings.setValue("options/" + key, value);
    emit optionsChanged();
    replan();
    touchLayout();
}

void Backend::resetScale() {
    m_options["scaling"] = Defaults.value("scaling");
    m_options["scalePercent"] = Defaults.value("scalePercent");
    emit optionsChanged();
    touchLayout();
}

void Backend::touchLayout() {
    ++m_layoutRevision;
    emit layoutChanged();
}

void Backend::resetAdvanced() {
    for (const auto &key : AdvancedKeys) {
        m_options[key] = Defaults.value(key);
        m_settings.setValue("options/" + key, Defaults.value(key));
    }
    emit optionsChanged();
}

void Backend::setSelection(const QList<int> &pages) {
    m_selection = pages;
    m_rangeText = pages.size() == pageCount() && pageCount() > 0 ? QString() : Planner::formatRange(pages);
    m_rangeValid = true;
    emit selectionChanged();
    replan();
}

QVariantList Backend::selectedPages() const {
    QVariantList pages;
    for (int page : m_selection)
        pages << page;
    return pages;
}

void Backend::setRange(const QString &text) {
    bool ok = true;
    const auto pages = Planner::parseRange(text, pageCount(), &ok);
    m_selection = pages;
    m_rangeText = text;
    m_rangeValid = ok && !pages.isEmpty();
    emit selectionChanged();
    replan();
}

void Backend::togglePage(int page) {
    if (page < 1 || page > pageCount())
        return;
    QList<int> pages = m_selection;
    if (pages.contains(page))
        pages.removeAll(page);
    else
        pages << page;
    std::sort(pages.begin(), pages.end());
    setSelection(pages);
}

void Backend::selectPages(const QString &which) {
    QList<int> pages;
    for (int page = 1; page <= pageCount(); ++page)
        if (which == "all" || (which == "odd" && page % 2) || (which == "even" && !(page % 2)))
            pages << page;
    setSelection(pages);
}

void Backend::replan() {
    m_sheets = Planner::sheets(m_selection, m_options.value("duplex").toBool(), m_options.value("copies").toInt());
    emit planChanged();
}

QVariantList Backend::sheetModel() const {
    // One copy is enough to show; the rest repeat it.
    const auto single = Planner::sheets(m_selection, m_options.value("duplex").toBool(), 1);
    QVariantList model;
    for (const auto &sheet : single)
        model << QVariantMap{{"front", sheet.front}, {"back", sheet.back}};
    return model;
}

int Backend::pagesToPrint() const {
    return m_selection.size() * std::max(1, m_options.value("copies").toInt());
}

int Backend::estimateMinutes() const {
    return std::max(1, int(std::ceil(pagesToPrint() * SecondsPerPage / 60.0)));
}

int Backend::cutPages() const {
    return std::count_if(m_selection.begin(), m_selection.end(),
                         [this](int page) { return placementOf(page, false).cut; });
}

Planner::Placement Backend::placementOf(int page, bool flipBack) const {
    const QSizeF size = m_pageSizes.value(page - 1);
    const int pageRotation = m_pageRotations.value(page - 1);
    const int degrees = Planner::rotation(size.width(), size.height(), pageRotation,
                                          m_options.value("autoRotate").toBool(), flipBack);
    // The size is the one in the file; /Rotate and the turn both swap its sides.
    const bool sideways = (((pageRotation % 180) + 180) % 180 == 90) != (degrees % 180 == 90);
    return Planner::place(sideways ? size.transposed() : size,
                          Planner::turned(m_inkBoxes.value(page - 1, QRectF(0, 0, 1, 1)), degrees),
                          Planner::paper(m_options.value("paperSize").toString()),
                          m_options.value("scaling").toString(), m_options.value("scalePercent").toInt());
}

// The window draws every page upright. A back that prints upside down differs
// by how lopsided the printer's reach is, which only moves it a little.
QVariantMap Backend::placement(int page) const {
    const auto paper = Planner::paper(m_options.value("paperSize").toString());
    const double width = paper.size.width(), height = paper.size.height();
    QVariantMap map{{"aspect", height / width},
                    {"areaX", paper.printable.x() / width},
                    {"areaY", paper.printable.y() / height},
                    {"areaWidth", paper.printable.width() / width},
                    {"areaHeight", paper.printable.height() / height}};
    const bool known = page >= 1 && page <= pageCount();
    const auto placed = known ? placementOf(page, false) : Planner::Placement{1, QRectF(0, 0, width, height), false};
    const QSizeF size = m_pageSizes.value(page - 1);
    map["x"] = placed.page.x() / width;
    map["y"] = placed.page.y() / height;
    map["width"] = placed.page.width() / width;
    map["height"] = placed.page.height() / height;
    map["percent"] = int(std::round(placed.scale * 100));
    map["cut"] = placed.cut;
    map["turned"] = known && Planner::rotation(size.width(), size.height(), m_pageRotations.value(page - 1),
                                               m_options.value("autoRotate").toBool(), false) == 90;
    return map;
}

QSizeF Backend::paperPoints() const {
    return Planner::paper(m_options.value("paperSize").toString()).size;
}

// ---------------------------------------------------------------- printing

void Backend::print() {
    if (m_state == "printing" || m_state == "flip" || !m_work || m_loading)
        return;
    if (m_selection.isEmpty() || !m_rangeValid) {
        setProblem(tr("Choose at least one page."));
        return;
    }
    if (m_printer.isEmpty()) {
        setProblem(tr("No printer is set up in CUPS."));
        return;
    }
    if (printerOffline()) {
        setProblem(offlineProblem());
        return;
    }
    if (driverPpd().isEmpty()) {
        setProblem(tr("The Canon driver (cnijfilter-e400series) is not installed."));
        return;
    }
    setProblem({});

    const bool duplex = m_options.value("duplex").toBool();
    const auto fronts = Planner::fronts(m_sheets);
    const auto backs = duplex ? Planner::backs(m_sheets, m_options.value("reverseBacks").toBool()) : QList<int>{};
    m_items.clear();
    for (int page : fronts)
        m_items << QVariantMap{{"page", page}, {"side", "front"}, {"state", "queued"}};
    m_firstBackItem = m_items.size();
    for (int page : backs)
        m_items << QVariantMap{{"page", page}, {"side", "back"}, {"state", "queued"}};

    ++m_jobGeneration;
    m_jobClock.start();
    m_tick.start();
    m_printerPoll.stop();
    startPass(1);
}

void Backend::startPass(int pass) {
    m_pass = pass;
    m_state = "printing";
    m_queueIndex = pass == 1 ? 0 : m_firstBackItem;
    m_stalled = false;
    emit jobChanged();
    sendItem();
}

void Backend::sendItem() {
    const int end = m_pass == 1 ? m_firstBackItem : m_items.size();
    if (m_queueIndex >= end) {
        finishPass();
        return;
    }
    m_current = m_queueIndex;
    QVariantMap item = m_items[m_current].toMap();
    item["state"] = "active";
    m_items[m_current] = item;
    m_stalled = false;
    m_itemClock.start();

    const int page = item["page"].toInt();
    const bool back = item["side"] == "back";
    const int degrees = Planner::rotation(m_pageSizes.value(page - 1).width(), m_pageSizes.value(page - 1).height(),
                                          m_pageRotations.value(page - 1), m_options.value("autoRotate").toBool(),
                                          back && m_options.value("flipBacks").toBool());
    const Planner::Placement placed = placementOf(page, back && m_options.value("flipBacks").toBool());
    const QString pdf = m_work->filePath("page.pdf"), sheet = m_work->filePath("sheet.pdf"),
                  ps = m_work->filePath("page.ps"), bin = m_work->filePath("page.bin");
    const int generation = m_jobGeneration;
    auto alive = [this, generation] { return generation == m_jobGeneration && m_state == "printing"; };

    setStep(tr("Preparing page %1").arg(page));
    QStringList extract{"--warning-exit-0", "--no-warn", "--empty", "--pages", m_work->filePath("in.pdf"),
                        QString::number(page), "--"};
    if (degrees)
        extract << QStringLiteral("--rotate=+%1").arg(degrees);
    extract << pdf;

    run("qpdf", extract, [=, this](int code, const QString &error) {
        if (!alive())
            return;
        if (code != 0)
            return fail(tr("Could not prepare page %1: %2").arg(page).arg(error));
        const QSizeF paper = paperPoints();
        run("gs", Planner::placeArguments(placed, paper, pdf, sheet), [=, this](int code, const QString &error) {
            if (!alive())
                return;
            if (code != 0)
                return fail(tr("Could not prepare page %1: %2").arg(page).arg(error));
            run("pdftops", {"-paperw", QString::number(int(paper.width())), "-paperh",
                            QString::number(int(paper.height())), sheet, ps},
                [=, this](int code, const QString &error) {
                    if (!alive())
                        return;
                    if (code != 0)
                        return fail(tr("Could not prepare page %1: %2").arg(page).arg(error));

                    auto env = QProcessEnvironment::systemEnvironment();
                    env.insert("PPD", driverPpd());
                    env.insert("DEVICE_URI", m_deviceUri.isEmpty() ? "usb://" : m_deviceUri);
                    setStep(tr("Rendering page %1").arg(page));
                    run("/usr/lib/cups/filter/pstocanonij",
                        {"1", qEnvironmentVariable("USER"), "pritingo", "1", Planner::driverOptions(m_options), ps},
                        [=, this](int code, const QString &) {
                            if (!alive())
                                return;
                            if (code != 0 || QFileInfo(bin).size() == 0)
                                return fail(tr("The Canon driver could not render page %1.").arg(page));
                            setStep(tr("Sending page %1").arg(page));
                            auto *lp = new QProcess(this);
                            m_running << lp;
                            connect(lp, &QProcess::finished, this, [=, this](int code) {
                                m_running.removeAll(lp);
                                const QString out = QString::fromUtf8(lp->readAllStandardOutput());
                                const QString error = QString::fromUtf8(lp->readAllStandardError()).trimmed();
                                lp->deleteLater();
                                if (!alive())
                                    return;
                                const auto id = QRegularExpression(R"(request id is (\S+))").match(out);
                                if (code != 0 || !id.hasMatch())
                                    return fail(tr("CUPS refused the page: %1").arg(error));
                                m_jobId = id.captured(1);
                                m_waitingForUsb = false;
                                setStep(tr("Printing page %1").arg(page));
                                m_jobPoll.start();
                            });
                            lp->start("lp", {"-d", m_printer, "-o", "raw", "-t",
                                             QStringLiteral("pritingo %1 p%2").arg(fileName()).arg(page), bin});
                        },
                        bin, env);
                });
        });
    });
}

void Backend::waitForJob() {
    if (m_state != "printing")
        return;
    if (m_waitingForUsb) {
        // Give the printer time to settle before the next page. The usblp
        // node coming back is the early sign; when the backend leaves it
        // detached, the 30 seconds that worked in testing are the fallback.
        const bool back = !QDir("/dev/usb").entryList({"lp*"}, QDir::System).isEmpty();
        if ((back && m_usbClock.elapsed() > 2500) || m_usbClock.elapsed() > 30000) {
            m_jobPoll.stop();
            itemFinished();
        }
        return;
    }
    if (m_queueProbe)
        return;
    m_queueProbe = new QProcess(this);
    const int generation = m_jobGeneration;
    connect(m_queueProbe, &QProcess::finished, this, [this, generation] {
        const QString out = QString::fromUtf8(m_queueProbe->readAllStandardOutput());
        m_queueProbe->deleteLater();
        m_queueProbe = nullptr;
        if (generation != m_jobGeneration || m_state != "printing")
            return;
        const bool queued = out.contains(m_jobId + " ") || out.contains(m_jobId + "\t");
        if (!queued) {
            m_waitingForUsb = true;
            m_usbClock.start();
            setStep(tr("Waiting for the printer"));
            return;
        }
        const bool stalled = m_itemClock.elapsed() > StallSeconds * 1000;
        if (stalled != m_stalled) {
            m_stalled = stalled;
            emit jobChanged();
            if (stalled)
                notify(tr("The printer is waiting"),
                       tr("Is there paper in the tray? If a light blinks, press the Resume button."), "dialog-warning");
        }
    });
    connect(m_queueProbe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_queueProbe->deleteLater();
        m_queueProbe = nullptr;
    });
    m_queueProbe->start("lpstat", {"-o", m_printer});
}

void Backend::itemFinished() {
    QVariantMap item = m_items[m_current].toMap();
    item["state"] = "done";
    m_items[m_current] = item;
    m_jobId.clear();
    m_stalled = false;
    ++m_queueIndex;
    emit jobChanged();
    sendItem();
}

void Backend::finishPass() {
    m_current = -1;
    if (m_pass == 1 && m_firstBackItem < m_items.size()) {
        m_state = "flip";
        m_step.clear();
        emit jobChanged();
        notify(tr("Turn the pages over"),
               tr("The fronts are done. Put %n sheet(s) back in the tray, printed side away from you, then press Continue.",
                  "", m_items.size() - m_firstBackItem),
               "message-new-instant");
        return;
    }
    m_state = "done";
    m_step.clear();
    m_tick.stop();
    m_printerPoll.start();
    emit jobChanged();
    notify(tr("Printing finished"),
           m_parts.size() == 1 ? tr("%n page(s) of “%1” are ready.", "", m_items.size()).arg(fileName())
                               : tr("%n page(s) from %1 are ready.", "", m_items.size()).arg(fileName()),
           "complete");
}

void Backend::continueAfterFlip() {
    if (m_state != "flip")
        return;
    startPass(2);
}

void Backend::cancel() {
    if (m_state != "printing" && m_state != "flip")
        return;
    ++m_jobGeneration;
    m_jobPoll.stop();
    m_tick.stop();
    for (auto *process : std::as_const(m_running))
        process->kill();
    if (!m_jobId.isEmpty())
        QProcess::startDetached("cancel", {m_jobId});
    m_jobId.clear();
    m_state = "idle";
    m_current = -1;
    m_step.clear();
    m_printerPoll.start();
    emit jobChanged();
    emit toast(tr("Printing cancelled"));
}

void Backend::dismiss() {
    if (m_state == "done" || m_state == "error") {
        m_state = "idle";
        m_items.clear();
        emit jobChanged();
    }
}

void Backend::fail(const QString &message) {
    ++m_jobGeneration;
    m_jobPoll.stop();
    m_tick.stop();
    m_state = "error";
    m_step = message;
    m_printerPoll.start();
    emit jobChanged();
    notify(tr("Printing stopped"), message, "dialog-error");
}

void Backend::notify(const QString &title, const QString &body, const QString &sound) {
    QProcess::startDetached("notify-send", {"-a", "Pritingo", "-i", "pritingo", title, body});
    if (m_options.value("sounds").toBool())
        QProcess::startDetached("canberra-gtk-play", {"-i", sound});
}

double Backend::progress() const {
    if (m_items.isEmpty())
        return 0;
    const int done = std::count_if(m_items.begin(), m_items.end(),
                                   [](const QVariant &v) { return v.toMap().value("state") == "done"; });
    return double(done) / m_items.size();
}

int Backend::elapsedSeconds() const {
    return m_jobClock.isValid() ? int(m_jobClock.elapsed() / 1000) : 0;
}

int Backend::lastBlankPage() const {
    // The front page of the one sheet whose back stays blank, if any, so the
    // flip screen can say which sheet stays out of the tray.
    for (const auto &sheet : m_sheets)
        if (!sheet.back && m_options.value("duplex").toBool())
            return sheet.front;
    return 0;
}

// ---------------------------------------------------------------- maintenance

void Backend::maintenance(const QString &kind) {
    static const QHash<QString, QString> files{{"clean", "cleaning.utl"},
                                               {"nozzle", "nozzlecheck.utl"},
                                               {"align", "autoalign.utl"}};
    const QString file = "/usr/share/cmdtocanonij/" + files.value(kind);
    if (!files.contains(kind) || !QFileInfo::exists(file)) {
        setProblem(tr("This printer has no maintenance commands."));
        return;
    }
    if (printerOffline()) {
        setProblem(offlineProblem());
        return;
    }
    // These files start with #CUPS-COMMAND, so CUPS routes them through the
    // driver's cmdtocanonij filter instead of printing them.
    run("lp", {"-d", m_printer, file}, [this, kind](int code, const QString &error) {
        if (code != 0)
            setProblem(tr("CUPS refused the command: %1").arg(error));
        else
            emit toast(kind == "clean" ? tr("Head cleaning sent") : kind == "nozzle" ? tr("Nozzle check sent")
                                                                                    : tr("Alignment sent"));
    });
}

void Backend::testDuplexOrientation() {
    if (m_state == "printing" || m_state == "flip")
        return;
    // A two-page sheet with big arrows: print it two-sided once and the arrows
    // show whether the backs need flipping or reversing on this printer.
    // A private, randomly named directory (mode 0700) rather than a fixed
    // path in /tmp; it lives until openFile has copied the result.
    auto temporary = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/pritingo-test-XXXXXX");
    if (!temporary->isValid())
        return setProblem(tr("Could not make the test page: %1").arg(temporary->errorString()));
    const QString dir = temporary->path();
    const QString pdf = dir + "/" + tr("two-sided-test") + ".pdf";
    auto page = [&](const QString &text, const QString &out) {
        return QStringList{"-size", "1240x1754", "xc:white", "-gravity", "north", "-pointsize", "110", "-fill",
                           "black", "-annotate", "+0+120", "▲ " + tr("TOP"), "-gravity", "center", "-pointsize",
                           "300", "-annotate", "+0-80", text, "-pointsize", "60", "-annotate", "+0+260",
                           tr("The arrow on both sides should point the same way"), "-density", "150", out};
    };
    run("magick", page(tr("FRONT"), dir + "/1.pdf"), [=, this](int code, const QString &error) {
        if (code != 0)
            return setProblem(tr("Could not make the test page: %1").arg(error));
        run("magick", page(tr("BACK"), dir + "/2.pdf"), [=, this](int code, const QString &error) {
            if (code != 0)
                return setProblem(tr("Could not make the test page: %1").arg(error));
            run("qpdf", {"--empty", "--pages", dir + "/1.pdf", dir + "/2.pdf", "--", pdf},
                [=, this](int code, const QString &error) {
                    Q_UNUSED(temporary);
                    if (code != 0)
                        return setProblem(tr("Could not make the test page: %1").arg(error));
                    setOption("duplex", true);
                    openFile(pdf);
                });
        });
    });
}
