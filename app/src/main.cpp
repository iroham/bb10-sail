// Sail: the app. Every bit of interface is Cascades (assets/*.qml); the page is drawn by sail-engine, a child
// process whose window joins this app's window group (see engine.h).
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/Window>
#include <QDebug>
#include <QDir>
#include <QtDeclarative/QDeclarativeContext>
#include <QSocketNotifier>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "engine.h"
#include "store.h"

using namespace bb::cascades;

// A kill signal ends the app the same way closing it does (the event loop quits, Cascades closes its sessions
// with the system services). Dying on the spot can leave a thread waiting on the input service for good.
static int g_quitPipe[2] = { -1, -1 };
static void onTerm(int) { char c = 1; if (g_quitPipe[1] >= 0) { ssize_t n = write(g_quitPipe[1], &c, 1); (void)n; } }

static void logMsg(QtMsgType, const char *m) { fprintf(stderr, "app: %s\n", m); }

Q_DECL_EXPORT int main(int argc, char **argv) {
    qputenv("CASCADES_THEME", "dark");
    QString root = QDir::currentPath();                           // the app's sandbox
    QString data = root + "/data", native = root + "/app/native";
    if (freopen((data + "/app.log").toUtf8().constData(), "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
    qInstallMsgHandler(logMsg);
    Application app(argc, argv);
    Engine engine(native, data);
    Store store(data);
    if (engine.uiTest().isEmpty()) store.load(); else store.loadSample();
    engine.setStore(&store);
    QmlDocument *qml = QmlDocument::create("asset:///main.qml");
    qml->documentContext()->setContextProperty("engine", &engine);
    qml->documentContext()->setContextProperty("store", &store);
    if (qml->hasErrors()) {
        foreach (const QDeclarativeError &e, qml->errors()) qWarning() << e.toString();
        return 1;
    }
    AbstractPane *pane = qml->createRootObject<AbstractPane>();
    if (!pane) return 1;
    Application::instance()->setScene(pane);
    QObject::connect(&app, SIGNAL(fullscreen()), &engine, SLOT(onForeground()));
    QObject::connect(&app, SIGNAL(thumbnail()), &engine, SLOT(onForeground()));
    QObject::connect(&app, SIGNAL(invisible()), &engine, SLOT(onInvisible()));
    QObject::connect(&app, SIGNAL(aboutToQuit()), &engine, SLOT(onQuit()));
    QObject::connect(&app, SIGNAL(aboutToQuit()), &store, SLOT(save()));
    QObject::connect(&app, SIGNAL(invisible()), &store, SLOT(save()));
    if (pipe(g_quitPipe) == 0) {
        QSocketNotifier *term = new QSocketNotifier(g_quitPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(term, SIGNAL(activated(int)), &engine, SLOT(onLeave()));
        signal(SIGTERM, onTerm);
    }
    app.setAutoExit(false);                                       // closing the app goes through the same two steps
    QObject::connect(&app, SIGNAL(manualExit()), &engine, SLOT(onLeave()));
    engine.attach(pane, Application::instance()->mainWindow()->groupId());
    return Application::exec();
}
