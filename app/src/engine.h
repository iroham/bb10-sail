// Sail: the app's side of the engine. Starts sail-engine as a child process whose page window joins this app's
// window group, talks to it over a private socket pair, and exposes the page's state to QML.
#ifndef SAIL_ENGINE_H
#define SAIL_ENGINE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QVariantList>

class QSocketNotifier;
class Store;
namespace bb { namespace cascades { class AbstractPane; class ForeignWindowControl; class Invocation; } }

// One tab as the app sees it. It has an engine tab (eid) only once it has a page to show; a tab on the start view,
// or one not looked at since the engine restarted, has none and costs nothing.
struct Tab {
    int key;                             // stable name for the tab inside the app
    int eid;                             // the engine's id for it, -1: none
    bool secure, loading, canBack, canForward, errRetry;
    int progress;
    QString view, url, host, title, errTitle, errText, errCode, errKind, lastUrl, pending;
    Tab() : key(0), eid(-1), secure(false), loading(false), canBack(false), canForward(false),
            errRetry(true), progress(0), view("start") {}
};

struct Download {
    unsigned id;                         // the engine's id while it runs; 0 for one from an earlier session
    QString state, path;                 // run, done, fail, cancel
    qint64 got, total;
    Download() : id(0), got(0), total(0) {}
};

class Engine : public QObject {
    Q_OBJECT
    // which view fills the page area: "start" (nothing loaded), "page", "error" (the load failed), "stopped" (engine died)
    Q_PROPERTY(QString view READ view NOTIFY viewChanged)
    Q_PROPERTY(QString url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString host READ host NOTIFY urlChanged)
    Q_PROPERTY(bool secure READ secure NOTIFY urlChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool canBack READ canBack NOTIFY navChanged)
    Q_PROPERTY(bool canForward READ canForward NOTIFY navChanged)
    Q_PROPERTY(QString errorTitle READ errorTitle NOTIFY errorChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(bool errorRetry READ errorRetry NOTIFY errorChanged)
    Q_PROPERTY(QString errorKind READ errorKind NOTIFY errorChanged)   // offline | cert | other
    Q_PROPERTY(int currentTab READ currentTab NOTIFY currentTabChanged)
    Q_PROPERTY(int tabCount READ tabCount NOTIFY tabsChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)          // for the tab switcher: title, host, current
    Q_PROPERTY(int selLength READ selLength NOTIFY selChanged)           // text selected on the page (asked for by querySelection)
    Q_PROPERTY(bool selEditable READ selEditable NOTIFY selChanged)      // a text field on the page has the focus
    Q_PROPERTY(QVariantList downloads READ downloads NOTIFY downloadsChanged)   // newest first: title, host (status), current
    Q_PROPERTY(QVariantList settings READ settings NOTIFY settingsChanged)      // choices as list rows: title, host, current, plain
    Q_PROPERTY(int pageWidth READ pageWidth CONSTANT)
    Q_PROPERTY(int pageHeight READ pageHeight CONSTANT)
    Q_PROPERTY(int barHeight READ barHeight CONSTANT)
    Q_PROPERTY(bool keysViaApp READ keysViaApp CONSTANT)
    Q_PROPERTY(bool quiet READ quiet CONSTANT)                // a test launch: the address field must not ask for the keyboard
    Q_PROPERTY(QString uiTest READ uiTest CONSTANT)          // design check: start in a canned state (data/uitest)
public:
    Engine(const QString &appDir, const QString &dataDir, QObject *parent = 0);
    ~Engine();
    void attach(bb::cascades::AbstractPane *pane, const QString &group);
    void setStore(Store *s);
    static bool isAddress(const QString &text);      // typed text that is an address rather than a search

    QString view() const { return cur().view; }
    QString url() const { return cur().url; }
    QString host() const { return cur().host; }
    bool secure() const { return cur().secure; }
    QString title() const { return cur().title; }
    bool loading() const { return cur().loading; }
    int progress() const { return cur().progress; }
    bool canBack() const { return cur().canBack; }
    bool canForward() const { return cur().canForward; }
    QString errorTitle() const { return cur().errTitle; }
    QString errorText() const { return cur().errText; }
    QString errorCode() const { return cur().errCode; }
    bool errorRetry() const { return cur().errRetry; }
    QString errorKind() const { return cur().errKind; }
    int currentTab() const { return m_cur; }
    int tabCount() const { return m_tabs.size(); }
    QVariantList tabs() const;
    QVariantList downloads() const;
    QVariantList settings() const;
    int selLength() const { return m_selLen; }
    bool selEditable() const { return m_selEditable; }
    int pageWidth() const { return m_pageW; }
    int pageHeight() const { return m_pageH; }
    int barHeight() const { return m_barH; }
    bool keysViaApp() const { return m_keysViaApp; }
    QString uiTest() const { return m_uiTest; }
    bool quiet() const { return m_quiet; }

    Q_INVOKABLE void open(const QString &text);      // an address or a search, as typed
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void home();                         // back to the start view
    Q_INVOKABLE void newTab();                       // a new tab on the start view, shown at once
    Q_INVOKABLE void selectTab(int index);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void restart();                      // start the engine again after it died
    Q_INVOKABLE void setOverlay(bool on);            // true: app controls are drawn over the page (page stops taking touches)
    Q_INVOKABLE void querySelection();               // ask the page; the answer arrives as selChanged
    Q_INVOKABLE void copySelection();                // the page's selected text, to the phone's clipboard
    Q_INVOKABLE bool canPaste();                     // the phone's clipboard holds text
    Q_INVOKABLE void paste();                        // the phone's clipboard, into the page's focused field
    Q_INVOKABLE void openDownload(int index);
    Q_INVOKABLE void removeDownload(int index);      // stops it if it is running; the file stays
    Q_INVOKABLE void pickSetting(int index);         // a row of settings() was tapped
    Q_INVOKABLE void clearSiteData();                // cookies, site storage, cache
    Q_INVOKABLE void note(const QString &text);      // a line in app.log from the interface (first 40 only)
    Q_INVOKABLE void copyLink();
    Q_INVOKABLE void share();
    Q_INVOKABLE void key(int sym, bool shift, bool ctrl, bool alt, bool down);
    Q_INVOKABLE void keyboardTouch(int phase, double x, double y, int finger);   // phase 0 down, 1 move, 2 up/cancel
    Q_INVOKABLE void hideKeyboardBar();
    Q_INVOKABLE void focusEngine();                  // the page should take what is typed

signals:
    void viewChanged();
    void urlChanged();
    void titleChanged();
    void loadingChanged();
    void progressChanged();
    void navChanged();
    void errorChanged();
    void currentTabChanged();
    void tabsChanged();
    void selChanged();
    void downloadsChanged();
    void settingsChanged();
    void leaving();                      // the app is about to exit: give up keyboard focus now

public slots:
    void onRespawn();
    void onReadable();
    void onBound(bool bound);
    void onForeground();
    void onInvisible();
    void onQuit();
    void onLeave();                      // exit in two steps: drop keyboard focus, then quit a moment later
    void onLeaveDone();
    void onHideAgain();
    void onShareArmed();
    void onSelfTest();
    void onScript();

private:
    void spawn();
    void send(const QByteArray &line);
    void handle(const QByteArray &line);
    Tab &cur() { return m_tabs[m_cur]; }
    const Tab &cur() const { return m_tabs.at(m_cur); }
    Tab *tabByEid(int eid);
    Tab *tabByKey(int key);
    bool isCur(const Tab *t) const { return t == &m_tabs.at(m_cur); }
    void handleDownload(const QString &arg);
    void saveDownloads();
    void toast(const QString &text);
    void handleTab(int eid, const QByteArray &cmd, const QString &arg);
    void loadIn(Tab *t, const QString &url);
    void showCurrent();                  // tell the engine which tab to show, loading it first if it has no page yet
    void emitAll();
    void setView(Tab *t, const QString &v);
    void setUrl(Tab *t, const QString &u);
    void setError(Tab *t, int code, const QString &url);
    void engineGone();
    void applyZ();
    static QString resolve(const QString &text);

    QString m_appDir, m_dataDir, m_group;
    int m_fd, m_pid;
    QSocketNotifier *m_notifier;
    QByteArray m_buf;
    bool m_ready;
    QList<Tab> m_tabs;                   // never empty
    int m_cur, m_nextKey;
    QList<int> m_spares;                 // empty engine tabs ready to be given a page
    QList<int> m_newQueue;               // keys of tabs waiting for the engine tab they asked for
    int m_shown;                         // the engine tab the engine is showing, -1: not known
    QString m_uiTest;
    Store *m_store;
    QList<Download> m_downloads;         // oldest first
    int m_selLen;
    bool m_selEditable, m_clipTest, m_respawn;
    int m_notes;
    QString m_quality;                   // fast, balanced, sharp
    bool m_overlay, m_keysViaApp, m_quiet, m_leavingNow;
    int m_pageW, m_pageH, m_barH;
    int m_kbFinger, m_kbLog;             // the keyboard's touch surface: the finger being followed
    bool m_kbActive;
    QStringList m_script;                // test switch "script": timed commands, see onScript
    int m_testStep;                      // test switch "selftest": a canned keyboard slide after the page loads
    double m_kbX, m_kbY;
    bb::cascades::ForeignWindowControl *m_fw;
    bb::cascades::Invocation *m_share;
};

#endif
