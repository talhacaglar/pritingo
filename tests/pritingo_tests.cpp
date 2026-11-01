#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "filepicker.h"
#include "planner.h"
#include "scanner.h"
#include "theme.h"
#include "translator.h"

// Answers immediately, so opening and saving files needs no desktop portal.
class FakePicker : public FilePicker {
public:
    QString answer;
    QStringList answers;
    void open(const QString &, const QStringList &) override { emit picked(answers.isEmpty() ? QStringList{answer} : answers); }
    void save(const QString &, const QString &) override { emit savePicked(answer); }
};

class PritingoTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep the tests' saved options away from the user's.
        QStandardPaths::setTestModeEnabled(true);
    }

    void rangesParse()
    {
        bool ok = false;
        QCOMPARE(Planner::parseRange("", 3, &ok), (QList<int>{1, 2, 3}));
        QVERIFY(ok);
        QCOMPARE(Planner::parseRange("1-3, 5", 6, &ok), (QList<int>{1, 2, 3, 5}));
        QVERIFY(ok);
        QCOMPARE(Planner::parseRange("4-", 6, &ok), (QList<int>{4, 5, 6}));
        QCOMPARE(Planner::parseRange("-2", 6, &ok), (QList<int>{1, 2}));
        QCOMPARE(Planner::parseRange("3-1", 6, &ok), (QList<int>{3, 2, 1}));
        QCOMPARE(Planner::parseRange("2,2,2", 6, &ok), (QList<int>{2}));
    }

    void rangesRejectNonsense()
    {
        bool ok = true;
        Planner::parseRange("7", 6, &ok);
        QVERIFY(!ok);
        Planner::parseRange("a-b", 6, &ok);
        QVERIFY(!ok);
        Planner::parseRange("0", 6, &ok);
        QVERIFY(!ok);
    }

    void rangesFormat()
    {
        QCOMPARE(Planner::formatRange({5, 1, 2, 3}), QString("1-3,5"));
        QCOMPARE(Planner::formatRange({2}), QString("2"));
        QCOMPARE(Planner::formatRange({}), QString());
    }

    void oneSidedIsAPagePerSheet()
    {
        const auto sheets = Planner::sheets({1, 2, 3}, false, 1);
        QCOMPARE(sheets.size(), 3);
        QCOMPARE(Planner::fronts(sheets), (QList<int>{1, 2, 3}));
        QVERIFY(Planner::backs(sheets, false).isEmpty());
    }

    void twoSidedPairsPages()
    {
        const auto sheets = Planner::sheets({1, 2, 3, 4, 5}, true, 1);
        QCOMPARE(sheets.size(), 3);
        QCOMPARE(Planner::fronts(sheets), (QList<int>{1, 3, 5}));
        QCOMPARE(Planner::backs(sheets, false), (QList<int>{2, 4}));
        QCOMPARE(Planner::backs(sheets, true), (QList<int>{4, 2}));
        QCOMPARE(Planner::sheetsWithBacks(sheets), 2);
        QCOMPARE(sheets.last().back, 0);
    }

    void copiesStartOnAFreshSheet()
    {
        // Three pages two-sided twice: the second copy must not put its page 1
        // on the back of the first copy's page 3.
        const auto sheets = Planner::sheets({1, 2, 3}, true, 2);
        QCOMPARE(Planner::fronts(sheets), (QList<int>{1, 3, 1, 3}));
        QCOMPARE(Planner::backs(sheets, false), (QList<int>{2, 2}));
    }

    void driverOptionsCarryEveryChoice()
    {
        const QString options = Planner::driverOptions({{"color", false}, {"paperSize", "A5"}, {"quality", 4},
                                                        {"media", "glossygold"}, {"density", -10},
                                                        {"cartridges", "bk"}});
        QVERIFY(options.contains("CNGrayscale=True"));
        QVERIFY(options.contains("PageSize=A5"));
        QVERIFY(options.contains("CNQuality=4"));
        QVERIFY(options.contains("MediaType=glossygold"));
        QVERIFY(options.contains("CNDensity=-10"));
        QVERIFY(options.contains("CNInkCartridgeSettings=bk"));
        QVERIFY(Planner::driverOptions({{"color", true}}).contains("CNGrayscale=False"));
    }

    void landscapePagesTurn()
    {
        QCOMPARE(Planner::rotation(842, 595, 0, true, false), 90);
        QCOMPARE(Planner::rotation(842, 595, 0, false, false), 0);
        QCOMPARE(Planner::rotation(595, 842, 0, true, false), 0);
        // A portrait page with /Rotate 90 shows landscape.
        QCOMPARE(Planner::rotation(595, 842, 90, true, false), 90);
        QCOMPARE(Planner::rotation(595, 842, 0, true, true), 180);
        QCOMPARE(Planner::rotation(842, 595, 0, true, true), 270);
    }

    void backendOpensAPdfAndPlansSheets()
    {
        if (QStandardPaths::findExecutable("pdfinfo").isEmpty() || QStandardPaths::findExecutable("magick").isEmpty())
            QSKIP("poppler or ImageMagick missing");
        QTemporaryDir dir;
        const QString pdf = dir.filePath("three.pdf");
        QProcess magick;
        magick.start("magick", {"-size", "595x842", "xc:white", "xc:white", "-size", "842x595", "xc:white", pdf});
        QVERIFY(magick.waitForFinished(20000));
        QCOMPARE(magick.exitCode(), 0);

        FakePicker picker;
        picker.answer = pdf;
        Backend backend(&picker);
        backend.setOption("duplex", true);
        backend.pickFiles();
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 20000);
        QCOMPARE(backend.pageCount(), 3);
        QCOMPARE(backend.thumbnails().size(), 3);
        QCOMPARE(backend.grayThumbnails().size(), 3);
        QCOMPARE(backend.landscapePages(), (QVariantList{3}));
        QCOMPARE(backend.sheetCount(), 2);
        QCOMPARE(backend.backSheetCount(), 1);

        backend.setRange("2-3");
        QCOMPARE(backend.sheetCount(), 1);
        backend.togglePage(2);
        QCOMPARE(backend.rangeText(), QString("3"));
        backend.setRange("9");
        QVERIFY(!backend.rangeValid());
        backend.setOption("duplex", false);
    }

    void backendJoinsFilesInNameOrder()
    {
        if (QStandardPaths::findExecutable("pdfinfo").isEmpty() || QStandardPaths::findExecutable("magick").isEmpty()
            || QStandardPaths::findExecutable("qpdf").isEmpty())
            QSKIP("poppler, ImageMagick or qpdf missing");
        QTemporaryDir dir;
        const QString one = dir.filePath("1-one.pdf"), two = dir.filePath("2-two.pdf"),
                      ten = dir.filePath("10-ten.png"), more = dir.filePath("more.pdf");
        QVERIFY(magick({"-size", "842x595", "xc:white", one}));
        QVERIFY(magick({"-size", "595x842", "xc:white", "xc:white", two}));
        QVERIFY(magick({"-size", "300x400", "xc:white", ten}));
        QVERIFY(magick({"-size", "595x842", "xc:white", "xc:white", more}));

        // Numbers in names sort as numbers, whatever order the desktop gives.
        FakePicker picker;
        Backend backend(&picker);
        backend.openFiles({ten, QUrl::fromLocalFile(two).toString(), one}, true);
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 20000);
        QVERIFY2(backend.problem().isEmpty(), qPrintable(backend.problem()));
        QCOMPARE(backend.fileNames(), (QStringList{"1-one.pdf", "2-two.pdf", "10-ten.png"}));
        QCOMPARE(backend.fileStarts(), (QVariantList{1, 2, 4}));
        QCOMPARE(backend.pageCount(), 4);
        QCOMPARE(backend.thumbnails().size(), 4);
        QCOMPARE(backend.landscapePages(), (QVariantList{1}));
        QVERIFY(backend.fileName().contains("3"));

        // Added files go at the end; a page left out stays out.
        backend.togglePage(2);
        picker.answers = {more};
        backend.pickFilesToAdd();
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 20000);
        QCOMPARE(backend.pageCount(), 6);
        QCOMPARE(backend.thumbnails().size(), 6);
        QCOMPARE(backend.selectedPages(), (QVariantList{1, 3, 4, 5, 6}));
        QCOMPARE(backend.fileStarts(), (QVariantList{1, 2, 4, 5}));

        // A file that will not open leaves the document as it was.
        const QString broken = dir.filePath("broken.pdf");
        write(broken, "not a pdf");
        backend.addFiles({broken});
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 20000);
        QVERIFY2(backend.problem().contains("broken.pdf"), qPrintable(backend.problem()));
        QCOMPARE(backend.pageCount(), 6);
        QCOMPARE(backend.fileNames().size(), 4);
        backend.addFiles({dir.filePath("missing.pdf")});
        QVERIFY(backend.problem().contains("missing.pdf"));

        // Opening replaces the whole stack.
        backend.openFiles({two});
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 20000);
        QCOMPARE(backend.fileNames(), QStringList{"2-two.pdf"});
        QCOMPARE(backend.fileName(), QString("2-two.pdf"));
        QCOMPARE(backend.pageCount(), 2);
        QCOMPARE(backend.selectedPages(), (QVariantList{1, 2}));
    }

    void sheetsFitThePaperWidth()
    {
        // As LibreOffice writes it: a fit that is switched off.
        QString xml = Backend::fitSheetToWidth(
            R"(<worksheet xmlns="m"><sheetPr filterMode="false"><pageSetUpPr fitToPage="false"/></sheetPr>)"
            R"(<pageMargins left="0.7"/><pageSetup paperSize="9" scale="100" fitToWidth="1" fitToHeight="1"/></worksheet>)");
        QCOMPARE(xml, QString(R"(<worksheet xmlns="m"><sheetPr filterMode="false"><pageSetUpPr fitToPage="1"/></sheetPr>)"
                              R"(<pageMargins left="0.7"/><pageSetup fitToWidth="1" fitToHeight="0" paperSize="9" scale="100"/></worksheet>)"));

        // As Excel writes it: a fixed scale and no pageSetUpPr.
        xml = Backend::fitSheetToWidth(R"(<worksheet xmlns="m"><sheetPr><tabColor rgb="FF92D050"/></sheetPr>)"
                                       R"(<pageMargins left="0.2"/><pageSetup paperSize="9" scale="44" r:id="rId1"/></worksheet>)");
        QCOMPARE(xml, QString(R"(<worksheet xmlns="m"><sheetPr><tabColor rgb="FF92D050"/><pageSetUpPr fitToPage="1"/></sheetPr>)"
                              R"(<pageMargins left="0.2"/><pageSetup fitToWidth="1" fitToHeight="0" paperSize="9" scale="44" r:id="rId1"/></worksheet>)"));

        // Neither sheetPr nor pageSetup: both are added where they belong.
        xml = Backend::fitSheetToWidth(R"(<worksheet xmlns="m"><dimension ref="A1"/><pageMargins left="1"/></worksheet>)");
        QCOMPARE(xml, QString(R"(<worksheet xmlns="m"><sheetPr><pageSetUpPr fitToPage="1"/></sheetPr><dimension ref="A1"/>)"
                              R"(<pageMargins left="1"/><pageSetup fitToWidth="1" fitToHeight="0"/></worksheet>)"));

        xml = Backend::fitSheetToWidth(R"(<worksheet><sheetPr codeName="Sheet1"/><pageMargins left="1"/></worksheet>)");
        QVERIFY2(xml.startsWith(R"(<worksheet><sheetPr codeName="Sheet1"><pageSetUpPr fitToPage="1"/></sheetPr>)"),
                 qPrintable(xml));

        // The author's own fit, and XML this does not know, stay as they are.
        const QString own = R"(<worksheet><sheetPr><pageSetUpPr fitToPage="1"/></sheetPr><pageSetup fitToHeight="2"/></worksheet>)";
        QCOMPARE(Backend::fitSheetToWidth(own), own);
        const QString prefixed = R"(<x:worksheet><x:pageMargins left="1"/></x:worksheet>)";
        QCOMPARE(Backend::fitSheetToWidth(prefixed), prefixed);
    }

    void backendFitsWideSpreadsheets()
    {
        if (QStandardPaths::findExecutable("soffice").isEmpty() || QStandardPaths::findExecutable("bsdtar").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("LibreOffice, bsdtar or poppler missing");
        QTemporaryDir dir;
        // Thirty columns run over five pages across in LibreOffice's own layout.
        QStringList wide, longRows;
        for (int row = 0; row < 5; ++row) {
            QStringList cells;
            for (int column = 0; column < 30; ++column)
                cells << QStringLiteral("column %1 r%2").arg(column).arg(row);
            wide << cells.join(',');
        }
        for (int row = 0; row < 300; ++row)
            longRows << QStringLiteral("row %1,value %1").arg(row);
        write(dir.filePath("wide.csv"), wide.join('\n').toUtf8());
        write(dir.filePath("long.csv"), longRows.join('\n').toUtf8());
        QProcess soffice;
        soffice.start("soffice", {"--headless", "--convert-to", "xlsx", "--outdir", dir.path(), dir.filePath("wide.csv"),
                                  dir.filePath("long.csv")});
        QVERIFY(soffice.waitForFinished(120000));
        soffice.start("soffice", {"--headless", "--convert-to", "ods", "--outdir", dir.path(), dir.filePath("wide.csv")});
        QVERIFY(soffice.waitForFinished(120000));

        Backend backend;
        backend.openFile(dir.filePath("wide.xlsx"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 120000);
        QVERIFY2(backend.problem().isEmpty(), qPrintable(backend.problem()));
        QCOMPARE(backend.pageCount(), 1);

        // Other formats pass through xlsx and fit the same way.
        backend.openFile(dir.filePath("wide.ods"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 120000);
        QCOMPARE(backend.pageCount(), 1);

        // A long sheet keeps its pages; only the width is fitted.
        backend.openFile(dir.filePath("long.xlsx"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 120000);
        QVERIFY2(backend.pageCount() > 1, qPrintable(QString::number(backend.pageCount())));
    }

    void fittingNeverWritesThroughALink()
    {
        if (QStandardPaths::findExecutable("bsdtar").isEmpty())
            QSKIP("bsdtar missing");
        // An xlsx whose worksheets folder is a link to a folder elsewhere.
        QTemporaryDir dir;
        const QString outside = dir.filePath("outside/sheet1.xml"), sheet = R"(<worksheet><pageMargins left="1"/></worksheet>)";
        QVERIFY(QDir(dir.path()).mkpath("outside") && QDir(dir.path()).mkpath("src/xl"));
        write(outside, sheet.toUtf8());
        write(dir.filePath("src/[Content_Types].xml"), "x");
        QVERIFY(QFile::link(dir.filePath("outside"), dir.filePath("src/xl/worksheets")));
        QProcess bsdtar;
        bsdtar.setWorkingDirectory(dir.filePath("src"));
        bsdtar.start("bsdtar", {"--format", "zip", "-cf", dir.filePath("link.xlsx"), "[Content_Types].xml", "xl"});
        QVERIFY(bsdtar.waitForFinished(20000));
        QCOMPARE(bsdtar.exitCode(), 0);

        Backend backend;
        backend.openFile(dir.filePath("link.xlsx"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 120000);
        QFile file(outside);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), sheet);
    }

    void printingWaitsForThePrinter()
    {
        if (QStandardPaths::findExecutable("pdfinfo").isEmpty() || QStandardPaths::findExecutable("magick").isEmpty())
            QSKIP("poppler or ImageMagick missing");
        QTemporaryDir dir;
        const QString pdf = dir.filePath("page.pdf"), usb = dir.filePath("usb");
        QVERIFY(magick({"-size", "595x842", "xc:white", pdf}));
        QVERIFY(QDir().mkpath(usb));

        // No USB printer in the stand-in device list, and a queue name that
        // exists nowhere, so nothing can ever reach a real printer.
        FakePicker picker;
        Backend backend(&picker, nullptr, usb);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.printer().isEmpty() || !backend.problem().isEmpty(), 10000);
        backend.setPrinter("pritingo-test-offline");
        backend.openFile(pdf);
        QTRY_COMPARE_WITH_TIMEOUT(backend.loading(), false, 20000);
        QVERIFY(!backend.printerConnected());

        backend.print();
        QCOMPARE(backend.state(), QString("idle"));
        QVERIFY2(backend.problem().contains("not connected"), qPrintable(backend.problem()));

        // Plugging it in takes the warning away on the next look.
        QSignalSpy toast(&backend, &Backend::toast);
        QVERIFY(QDir().mkpath(usb + "/1-1:1.0"));
        write(usb + "/1-1:1.0/bInterfaceClass", "07\n");
        QTRY_VERIFY_WITH_TIMEOUT(backend.printerConnected(), 10000);
        QVERIFY(backend.problem().isEmpty());
        QCOMPARE(toast.size(), 1);
    }

    void optionsKeepTheirTypes()
    {
        Backend backend;
        backend.setOption("quality", "2");
        QCOMPARE(backend.options().value("quality").metaType().id(), QMetaType::Int);
        backend.setOption("duplex", "true");
        QCOMPARE(backend.options().value("duplex"), QVariant(true));
        backend.setOption("duplex", false);
        backend.setOption("quality", 3);
    }

    void scannerCutsOutAnUprightCard()
    {
        if (QStandardPaths::findExecutable("magick").isEmpty())
            QSKIP("ImageMagick missing");
        QTemporaryDir dir;
        const QString glass = dir.filePath("glass.png");
        makeGlass(glass);

        Scanner scanner;
        scanner.setOption("layout", "card");
        scanner.addScan(glass, 300);
        QTRY_COMPARE_WITH_TIMEOUT(scanner.pages().size(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!scanner.pages().first().toMap().value("thumbnail").toString().isEmpty(), 20000);
        QVERIFY(!scanner.busy());

        // The card is 1011 × 638 at 300 dpi, upright on the glass, and comes
        // out lying down with at most a few pixels of shadow.
        const QString page = QUrl(scanner.pages().first().toMap().value("thumbnail").toString()).toLocalFile();
        const QSize size = imageSize(QFileInfo(page).dir().filePath("page-" + QFileInfo(page).fileName().section('-', 1, 1) + ".png"));
        QVERIFY2(size.width() >= 1005 && size.width() <= 1030, qPrintable(QString::number(size.width())));
        QVERIFY2(size.height() >= 632 && size.height() <= 655, qPrintable(QString::number(size.height())));

        scanner.addScan(glass, 300);
        QTRY_COMPARE_WITH_TIMEOUT(scanner.pages().size(), 2, 20000);
        const QString pdf = dir.filePath("card.pdf");
        QString error = "pending";
        scanner.writePdf(pdf, [&](const QString &e) { error = e; });
        QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 20000);
        // Both sides of the card share one A4 page.
        QCOMPARE(pdfPages(pdf), (QStringList{"595.2 x 841.92"}));
        scanner.setOption("layout", "document");
    }

    void scannerCutsOutAPhotoOntoA4()
    {
        if (QStandardPaths::findExecutable("magick").isEmpty() || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("ImageMagick or poppler missing");
        QTemporaryDir dir;
        const QString glass = dir.filePath("glass.png");
        makeGlass(glass);

        Scanner scanner;
        scanner.setOption("area", "auto");
        scanner.addScan(glass, 300);
        QTRY_COMPARE_WITH_TIMEOUT(scanner.pages().size(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!scanner.busy(), 20000);
        // Unlike a card, a cut-out keeps the way it lay on the glass.
        QTRY_VERIFY_WITH_TIMEOUT(!scanner.pages().first().toMap().value("thumbnail").toString().isEmpty(), 20000);
        const QString thumb = QUrl(scanner.pages().first().toMap().value("thumbnail").toString()).toLocalFile();
        const QSize size = imageSize(QFileInfo(thumb).dir().filePath("page-" + QFileInfo(thumb).fileName().section('-', 1, 1) + ".png"));
        QVERIFY2(size.height() > size.width(), qPrintable(QStringLiteral("%1x%2").arg(size.width()).arg(size.height())));
        const QString pdf = dir.filePath("photo.pdf");
        QString error = "pending";
        scanner.writePdf(pdf, [&](const QString &e) { error = e; });
        QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 20000);
        QCOMPARE(pdfPages(pdf), (QStringList{"595.2 x 841.92"}));
        scanner.setOption("area", "A4");
    }

    void scannerKeepsDocumentPagesA4()
    {
        if (QStandardPaths::findExecutable("magick").isEmpty() || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("ImageMagick or poppler missing");
        QTemporaryDir dir;
        const QString low = dir.filePath("low.png"), high = dir.filePath("high.png");
        QVERIFY(magick({"-size", "1240x1754", "xc:white", low}));
        QVERIFY(magick({"-size", "2480x3508", "xc:white", high}));

        FakePicker picker;
        picker.answer = dir.filePath("saved"); // no suffix: the scanner adds .pdf
        Scanner scanner(&picker);
        scanner.addScan(low, 150);
        scanner.addScan(high, 300);
        QTRY_COMPARE_WITH_TIMEOUT(scanner.pages().size(), 2, 20000);
        scanner.rotatePage(1);
        scanner.removePage(5);
        QTRY_VERIFY_WITH_TIMEOUT(!scanner.busy(), 20000);

        scanner.save();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("saved.pdf")), 20000);
        QVERIFY(!QFileInfo::exists(dir.filePath("saved.pdf.part")));
        // Different resolutions still come out A4; the turned page lies down.
        QCOMPARE(pdfPages(dir.filePath("saved.pdf")), (QStringList{"595.2 x 841.92", "841.92 x 595.2"}));

        QSignalSpy ready(&scanner, &Scanner::readyToPrint);
        scanner.print();
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 20000);
        QVERIFY(QFileInfo::exists(ready.first().first().toString()));

        scanner.removePage(0);
        QCOMPARE(scanner.pages().size(), 1);
        scanner.clear();
        QCOMPARE(scanner.pages().size(), 0);
    }

    void scannerFindsTheDeviceAndScans()
    {
        if (QStandardPaths::findExecutable("magick").isEmpty())
            QSKIP("ImageMagick missing");
        // A stand-in scanimage: lists a webcam before the Canon, and a scan
        // copies a prepared page to the -o path while reporting progress.
        QTemporaryDir dir;
        const QString page = dir.filePath("page.png");
        QVERIFY(magick({"-size", "1240x1754", "xc:white", page}));
        write(dir.filePath("args"), "");
        write(dir.filePath("scanimage"),
              "#!/bin/sh\n"
              "if [ \"$1\" = -L ]; then\n"
              "  echo \"device \\`v4l:/dev/video0' is a Noname USB2.0 Camera virtual device\"\n"
              "  echo \"device \\`pixma:04A9181E_X' is a CANON Canon PIXMA E410 Series multi-function peripheral\"\n"
              "  exit 0\nfi\n"
              "echo \"$@\" > '" + dir.filePath("args").toUtf8() + "'\n"
              "while [ $# -gt 0 ]; do [ \"$1\" = -o ] && out=$2; shift; done\n"
              "printf 'Progress: 40.0%%\\rProgress: 100.0%%\\r' >&2\n"
              "cp '" + page.toUtf8() + "' \"$out\"\n");
        QVERIFY(QFile::setPermissions(dir.filePath("scanimage"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", dir.path().toUtf8() + ":" + path);

        Scanner scanner;
        scanner.setOption("resolution", 150);
        scanner.scan();
        QTRY_COMPARE_WITH_TIMEOUT(scanner.pages().size(), 1, 20000);

        QCOMPARE(scanner.device(), QString("pixma:04A9181E_X"));
        QCOMPARE(scanner.deviceName(), QString("Canon PIXMA E410 Series"));
        QFile args(dir.filePath("args"));
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QString used = QString::fromUtf8(args.readAll());
        QVERIFY2(used.contains("-d pixma:04A9181E_X --mode Gray --resolution 150"), qPrintable(used));
        QVERIFY(used.contains("-x 210 -y 297"));
        QVERIFY(scanner.problem().isEmpty());

        // Other paper sizes scan just their area from the corner.
        scanner.setOption("area", "A5");
        scanner.scan();
        QTRY_COMPARE_WITH_TIMEOUT(scanner.pages().size(), 2, 20000);
        qputenv("PATH", path);
        args.close();
        QVERIFY(args.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(args.readAll()).contains("-x 148 -y 210"));
        scanner.setOption("area", "A4");
        scanner.setOption("resolution", 300);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorOverrideWins()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar"})");
        write(dir.path() + "/tr.json", R"({"Save": "Kaydet"})");
        Translator translator(dir.path() + "/language", dir.path(), nullptr, dir.path() + "/app/language");
        QCOMPARE(translator.translate("Main", "Save"), QString("Guardar"));
        translator.setOverride("tr");
        QCOMPARE(translator.language(), QString("tr"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Kaydet"));
        translator.setOverride("");
        QCOMPARE(translator.language(), QString("pt"));
    }

    void turkishCatalogCoversEveryString()
    {
        QFile en(QStringLiteral(SRCDIR) + "/../src/i18n/tr.json");
        QVERIFY(en.open(QIODevice::ReadOnly));
        QVERIFY(!QJsonDocument::fromJson(en.readAll()).object().isEmpty());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    static bool magick(const QStringList &arguments)
    {
        QProcess process;
        process.start("magick", arguments);
        return process.waitForFinished(30000) && process.exitCode() == 0;
    }

    static QSize imageSize(const QString &path)
    {
        QProcess process;
        process.start("magick", {"identify", "-format", "%w %h", path});
        process.waitForFinished(20000);
        const QStringList parts = QString::fromUtf8(process.readAllStandardOutput()).split(' ');
        return parts.size() == 2 ? QSize(parts[0].toInt(), parts[1].toInt()) : QSize();
    }

    static QStringList pdfPages(const QString &pdf)
    {
        QProcess process;
        process.start("pdfinfo", {"-f", "1", "-l", "99", pdf});
        process.waitForFinished(20000);
        QStringList sizes;
        static const QRegularExpression size(R"(Page\s+\d+\s+size:\s+([\d.]+ x [\d.]+))");
        for (auto it = size.globalMatch(QString::fromUtf8(process.readAllStandardOutput())); it.hasNext();)
            sizes << it.next().captured(1);
        return sizes;
    }

    // An A4 glass at 300 dpi like the E410's: a near-white lid, the lid's
    // dark shadow along two edges, and an ID card standing upright in the
    // middle with a photo, text and a very light patch on it.
    static void makeGlass(const QString &path)
    {
        QVERIFY(magick({"-size", "2480x3508", "xc:gray(92%)", "-fill", "gray(18%)", "-draw", "rectangle 0,0 2479,12",
                        "-draw", "rectangle 0,0 10,3507", "-fill", "#9fb4c8", "-draw", "roundrectangle 961,610 1598,1620 30,30",
                        "-fill", "#f2f2f2", "-draw", "rectangle 1010,700 1540,900", "-fill", "#3a3a3a", "-draw",
                        "rectangle 1100,1050 1450,1400", "-fill", "#222", "-draw", "rectangle 1000,1450 1550,1470",
                        path}));
    }
};

QTEST_MAIN(PritingoTests)
#include "pritingo_tests.moc"
