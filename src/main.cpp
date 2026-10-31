// pritingo — prints any document on a Canon E410, one page per job, with a
// guided two-sided mode for a printer that has no duplexer, and scans from
// its glass to a PDF or straight to a copy.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "scanner.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("pritingo");
    app.setApplicationDisplayName("Pritingo");
    app.setApplicationVersion("0.2.1");

    // Matches the window to pritingo.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("pritingo");
    app.setWindowIcon(QIcon::fromTheme("pritingo"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    Scanner scanner;
    // A copy is a scan printed like any other document, with every print
    // setting still available.
    QObject::connect(&scanner, &Scanner::readyToPrint, &backend, &Backend::openFile);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("scanner", &scanner);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // A file handed over by the launcher or a file manager opens right away.
    const QStringList arguments = app.arguments().mid(1);
    if (!arguments.isEmpty())
        QTimer::singleShot(0, &backend, [&backend, file = arguments.first()] { backend.openFile(file); });

    return app.exec();
}
