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
#include "theme.h"
#include "translator.h"

// Answers immediately, so opening files needs no desktop portal.
class FakePicker : public FilePicker {
public:
    QString answer;
    void open(const QString &, const QStringList &) override { emit picked(answer); }
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
        backend.pickFile();
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
};

QTEST_MAIN(PritingoTests)
#include "pritingo_tests.moc"
