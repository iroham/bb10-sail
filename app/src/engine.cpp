#include "engine.h"
#include "store.h"

#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/ForeignWindowControl>
#include <bb/cascades/Invocation>
#include <bb/cascades/InvokeQuery>
#include <bb/system/Clipboard>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/SystemToast>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>
#include <QStringList>
#include <QUrl>
#include <QDebug>

#include <bps/virtualkeyboard.h>
#include <errno.h>
#include <fcntl.h>
#include <screen/screen.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace bb::cascades;

extern char **environ;

// Search engines the address bar can use. The first is the default.
static const char *const kSearch[][3] = {
    { "brave", "Brave Search", "https://search.brave.com/search?q=" },
    { "ddg", "DuckDuckGo", "https://duckduckgo.com/?q=" },
    { "google", "Google", "https://www.google.com/search?q=" },
    { "bing", "Bing", "https://www.bing.com/search?q=" },
};
static const int kSearchCount = 4;
static int s_search = 0;
static int searchIndex(const QString &key) {
    for (int i = 0; i < kSearchCount; ++i) if (key == kSearch[i][0]) return i;
    return 0;
}
static void writeHook(const QString &path, const QString &value) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(value.toUtf8() + "\n");
}

static QString readHook(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readLine(2048)).trimmed();
}

static Tab sampleTab(int key, const QString &title, const QString &url) {
    Tab t; t.key = key; t.title = title; t.url = url; t.view = "page";
    t.host = QUrl(url).host(); t.secure = true;
    if (t.host.startsWith("www.")) t.host = t.host.mid(4);
    return t;
}

Engine::Engine(const QString &appDir, const QString &dataDir, QObject *parent)
    : QObject(parent), m_appDir(appDir), m_dataDir(dataDir), m_fd(-1), m_pid(-1), m_notifier(0), m_ready(false),
      m_cur(0), m_nextKey(1), m_shown(-1), m_store(0), m_selLen(0), m_selEditable(false), m_clipTest(false), m_respawn(false), m_notes(0), m_overlay(false), m_keysViaApp(false), m_quiet(false), m_leavingNow(false),
      m_kbFinger(-1), m_kbLog(0), m_kbActive(false), m_kbX(0), m_kbY(0), m_testStep(-1), m_fw(0), m_share(0) {
    int w = qgetenv("WIDTH").toInt(), h = qgetenv("HEIGHT").toInt();
    if (w <= 0) w = 1440;
    if (h <= 0) h = 1440;
    m_barH = 140;
    m_pageW = w;
    m_pageH = h - m_barH;
    Tab first; first.key = m_nextKey++;
    m_tabs << first;
    // Keys: the app takes them and passes them to the page over the socket. The other way (the page window
    // holds keyboard focus, "keymode" = window) makes the system treat the page as a text field: its symbol
    // row stays up, shrinks the app by its height and pushes the bar under the page.
    m_keysViaApp = readHook(m_dataDir + "/keymode") != "window";
    // Design check: "uitest" names a state to start in, with sample values and no engine, so each screen can be
    // looked at without a hand on the phone: page, loading, menu, edit, plain (http), error, cert, stopped, tabs,
    // start, suggest, history, bookmarks (the last four with sample history and bookmarks).
    m_uiTest = readHook(m_dataDir + "/uitest");
    s_search = searchIndex(readHook(m_dataDir + "/search"));
    m_quality = readHook(m_dataDir + "/quality");
    if (m_quality != "fast" && m_quality != "sharp") m_quality = "balanced";
    // Test launches happen with the phone locked. A text field that asks for the keyboard then leaves a thread
    // waiting on the input service that never returns, and the process cannot fully exit. So they don't ask.
    m_quiet = !m_uiTest.isEmpty() || QFile::exists(m_dataDir + "/script") || QFile::exists(m_dataDir + "/url");
    if (!m_uiTest.isEmpty() && m_uiTest != "start" && m_uiTest != "suggest") {
        Tab *t = &cur();
        if (m_uiTest == "error") setError(t, -105, "https://wikpedia.org/");
        else if (m_uiTest == "cert") setError(t, -201, "https://expired.badssl.com/");
        else if (m_uiTest == "stopped") t->view = "stopped";
        else t->view = "page";
        setUrl(t, m_uiTest == "plain" ? "http://neverssl.com/" : m_uiTest == "cert" ? "https://expired.badssl.com/"
                   : m_uiTest == "error" ? "https://wikpedia.org/" : "https://en.m.wikipedia.org/wiki/BlackBerry_Passport");
        t->title = "BlackBerry Passport - Wikipedia";
        t->canBack = true;
        t->canForward = m_uiTest == "menu";
        if (m_uiTest == "loading") { t->loading = true; t->progress = 55; }
        if (m_uiTest == "tabs") {
            m_tabs << sampleTab(100, "BBC News - Breaking news, video and the latest top stories from the UK and around the world", "https://www.bbc.com/news");
            m_tabs << sampleTab(101, "Train times and tickets", "https://www.nationalrail.co.uk/");
            m_nextKey = 102;
            Tab blank; blank.key = m_nextKey++;
            m_tabs << blank;
        }
    }
    // downloads from earlier sessions: "<state>\t<bytes>\t<path>" per line; one that was running did not finish
    QFile df(m_dataDir + "/downloads.tsv");
    if (m_uiTest.isEmpty() && df.open(QIODevice::ReadOnly)) {
        while (!df.atEnd()) {
            QStringList p = QString::fromUtf8(df.readLine()).remove('\n').split('\t');
            if (p.size() < 3 || p.at(2).isEmpty()) continue;
            Download d; d.state = p.at(0) == "run" ? "fail" : p.at(0); d.got = d.total = p.at(1).toLongLong(); d.path = p.at(2);
            m_downloads << d;
        }
    }
    if (m_uiTest == "downloads") {
        Download a; a.state = "done"; a.got = a.total = 2411724; a.path = "/shared/downloads/timetable-winter.pdf";
        Download b; b.state = "fail"; b.got = 300000; b.total = 9000000; b.path = "/shared/downloads/photos.zip";
        Download c; c.id = 3; c.state = "run"; c.got = 31457280; c.total = 73400320; c.path = "/shared/downloads/lineage-recovery.img";
        m_downloads << a << b << c;
    }
    signal(SIGCHLD, SIG_IGN);                                     // the engine is reaped by the system
    signal(SIGPIPE, SIG_IGN);
}

Engine::~Engine() { onQuit(); }

void Engine::attach(AbstractPane *pane, const QString &group) {
    m_group = group;
    m_fw = pane ? pane->findChild<ForeignWindowControl *>("page") : 0;
    if (m_fw) connect(m_fw, SIGNAL(boundToWindowChanged(bool)), this, SLOT(onBound(bool)));
    else qWarning() << "no page window control";
    if (!m_uiTest.isEmpty()) return;
    QString first = readHook(m_dataDir + "/url");                 // test hook: open this at start
    QFile sc(m_dataDir + "/script");                              // test switch: "<seconds to wait> <command> [argument]" per line
    if (sc.open(QIODevice::ReadOnly)) {
        m_script = QString::fromUtf8(sc.readAll()).split('\n', QString::SkipEmptyParts);
        if (!m_script.isEmpty()) QTimer::singleShot(int(m_script.first().section(' ', 0, 0).toDouble() * 1000), this, SLOT(onScript()));
    }
    if (!first.isEmpty()) { cur().pending = resolve(first); setUrl(&cur(), cur().pending); }
    spawn();
}

// The page's rendering size. The panel is 1440 wide; pages are laid out 480 CSS px wide (3 device px per CSS px
// at full sharpness). "quality" in the data dir picks how many of those pixels are really drawn.
static void renderSize(const QString &quality, int outW, int outH, int *rw, int *rh, QByteArray *dpr) {
    int num = 3, den = 4;                                         // balanced: 1080 wide
    if (quality == "fast") { num = 1; den = 2; }                  // 720 wide
    else if (quality == "sharp") { num = 1; den = 1; }            // 1440 wide
    *rw = outW * num / den;
    *rh = outH * num / den;
    *dpr = QByteArray::number(3.0 * num / den, 'f', 2);
}

void Engine::spawn() {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { qWarning() << "socketpair failed" << errno; engineGone(); return; }
    fcntl(sv[0], F_SETFD, FD_CLOEXEC);                            // our end must not leak into the engine
    int rw, rh;
    QByteArray dpr;
    renderSize(m_quality, m_pageW, m_pageH, &rw, &rh, &dpr);

    QList<QByteArray> env;
    for (char **e = environ; e && *e; ++e) env << QByteArray(*e);
    env << "SAIL_CTL_FD=" + QByteArray::number(sv[1]) << "SAIL_GROUP=" + m_group.toLatin1() << "SAIL_WINDOW_ID=sailPage"
        << "SAIL_OUT_W=" + QByteArray::number(m_pageW) << "SAIL_OUT_H=" + QByteArray::number(m_pageH)
        << "SAIL_DOWNLOADS=" + QDir::cleanPath(m_dataDir + "/../shared/downloads").toUtf8()
        << "SAIL_RENDER_W=" + QByteArray::number(rw) << "SAIL_RENDER_H=" + QByteArray::number(rh) << "SAIL_DPR=" + dpr;
    QVector<char *> envp;
    for (int i = 0; i < env.size(); ++i) envp << env[i].data();
    envp << (char *)0;
    QByteArray exe = (m_appDir + "/sail-launch").toUtf8();
    char *argv[] = { exe.data(), (char *)0 };
    pid_t pid = -1;
    int rc = posix_spawn(&pid, exe.constData(), 0, 0, argv, envp.data());
    close(sv[1]);
    if (rc != 0) { qWarning() << "engine did not start:" << strerror(rc); close(sv[0]); engineGone(); return; }
    m_pid = pid;
    m_fd = sv[0];
    m_ready = false;
    m_shown = -1;
    m_spares.clear();
    m_newQueue.clear();
    m_buf.clear();
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onReadable()));
    qDebug() << "engine started, pid" << pid << "render" << rw << "x" << rh << "dpr" << dpr;
}

void Engine::send(const QByteArray &line) {
    if (m_fd < 0) return;
    QByteArray b = line + "\n";
    const char *p = b.constData();
    int left = b.size();
    while (left > 0) {
        ssize_t n = ::write(m_fd, p, left);
        if (n <= 0) { if (errno == EINTR) continue; return; }
        p += n; left -= n;
    }
}

void Engine::onReadable() {
    char tmp[4096];
    ssize_t n = ::read(m_fd, tmp, sizeof tmp);
    if (n <= 0) { if (n < 0 && errno == EINTR) return; engineGone(); return; }
    m_buf.append(tmp, n);
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (!line.isEmpty()) handle(line);
    }
}

void Engine::engineGone() {
    if (m_notifier) { m_notifier->setEnabled(false); m_notifier->deleteLater(); m_notifier = 0; }
    if (m_fd >= 0) { close(m_fd); m_fd = -1; }
    m_pid = -1;
    m_ready = false;
    m_shown = -1;
    m_spares.clear();
    m_newQueue.clear();
    for (int i = 0; i < m_tabs.size(); ++i) {                     // every tab lost its page; each is loaded again when looked at
        Tab &t = m_tabs[i];
        t.eid = -1; t.loading = false; t.canBack = t.canForward = false;
        if (!t.pending.isEmpty()) { t.lastUrl = t.pending; t.pending.clear(); }
    }
    bool lost = false;
    for (int i = 0; i < m_downloads.size(); ++i) if (m_downloads[i].state == "run") { m_downloads[i].state = "fail"; m_downloads[i].id = 0; lost = true; }
    if (lost) { saveDownloads(); emit downloadsChanged(); }
    qWarning() << "engine is gone";
    emit loadingChanged();
    emit navChanged();
    if (m_respawn) {                                              // stopped on purpose (new sharpness): come straight back
        m_respawn = false;
        QTimer::singleShot(400, this, SLOT(onRespawn()));
        return;
    }
    setView(&cur(), cur().view == "start" ? "start" : "stopped");
}

void Engine::onRespawn() { restart(); }

void Engine::setStore(Store *s) {
    m_store = s;
    if (s) s->searchName = QString::fromLatin1(kSearch[s_search][1]).section(' ', 0, 0);
}

QVariantList Engine::settings() const {
    QVariantList out;
    for (int i = 0; i < kSearchCount; ++i) {
        QVariantMap m;
        m["title"] = QString::fromLatin1(kSearch[i][1]); m["host"] = QString("Search engine");
        m["current"] = i == s_search; m["plain"] = true;
        out << m;
    }
    static const char *const q[][3] = {
        { "fast", "Fast pages", "Smoothest scrolling, softer text" },
        { "balanced", "Balanced pages", "The usual choice" },
        { "sharp", "Sharp pages", "Crisp text, heavier scrolling" },
    };
    for (int i = 0; i < 3; ++i) {
        QVariantMap m;
        m["title"] = QString::fromLatin1(q[i][1]); m["host"] = QString::fromLatin1(q[i][2]);
        m["current"] = m_quality == q[i][0]; m["plain"] = true;
        out << m;
    }
    {   // engine credit (the author granted permission to distribute Sail on berry-v3; chromium-for-bb10 issue #7)
        QVariantMap m;
        m["title"] = QString("About & engine credit");
        m["host"] = QString("Engine: Chromium for BB10 by sw7ft (berry-v3), used with permission. Tap to open the project.");
        m["current"] = false; m["plain"] = true;
        out << m;
    }
    return out;
}

void Engine::pickSetting(int index) {
    if (index < 0) return;
    if (index < kSearchCount) {
        if (index == s_search) return;
        s_search = index;
        if (m_uiTest.isEmpty()) writeHook(m_dataDir + "/search", kSearch[index][0]);
        if (m_store) m_store->searchName = QString::fromLatin1(kSearch[index][1]).section(' ', 0, 0);
    } else if (index < kSearchCount + 3) {
        static const char *const q[] = { "fast", "balanced", "sharp" };
        if (m_quality == q[index - kSearchCount]) return;
        m_quality = q[index - kSearchCount];
        if (m_uiTest.isEmpty()) {
            writeHook(m_dataDir + "/quality", m_quality);
            if (m_fd >= 0) { m_respawn = true; send("quit"); }    // the engine draws at one size for its whole life
        }
    } else {
        open("https://github.com/sw7ft/chromium-for-bb10");       // engine source + credit / link back (issue #7)
        return;
    }
    emit settingsChanged();
}

void Engine::clearSiteData() {
    if (m_fd >= 0) send("cleardata");
    else toast("Open a page first, then clear again");
}

static QString unescape(const QString &in);
static QString escapeText(const QString &in);

static bool blank(const QString &u) { return u.isEmpty() || u.startsWith("about:"); }

Tab *Engine::tabByEid(int eid) {
    for (int i = 0; i < m_tabs.size(); ++i) if (m_tabs[i].eid == eid) return &m_tabs[i];
    return 0;
}
Tab *Engine::tabByKey(int key) {
    for (int i = 0; i < m_tabs.size(); ++i) if (m_tabs[i].key == key) return &m_tabs[i];
    return 0;
}

QVariantList Engine::tabs() const {
    QVariantList out;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const Tab &t = m_tabs.at(i);
        QVariantMap m;
        bool empty = t.url.isEmpty();
        m["title"] = empty ? QString("New tab") : t.title.isEmpty() ? t.host : t.title;
        m["host"] = empty ? QString() : t.host;
        m["current"] = i == m_cur;
        out << m;
    }
    return out;
}

void Engine::emitAll() {
    emit currentTabChanged();
    emit viewChanged(); emit urlChanged(); emit titleChanged(); emit loadingChanged(); emit progressChanged();
    emit navChanged(); emit errorChanged(); emit tabsChanged();
}

void Engine::handle(const QByteArray &line) {
    int sp = line.indexOf(' ');
    QByteArray cmd = sp < 0 ? line : line.left(sp);
    if (cmd == "ready") {
        m_ready = true;
        showCurrent();
    } else if (cmd == "sel") {
        QStringList a = QString::fromLatin1(line.constData() + sp + 1).split(' ');
        m_selLen = a.value(0).toInt(); m_selEditable = a.value(1) == "1";
        emit selChanged();
        if (!m_script.isEmpty() || m_clipTest) qDebug() << "script: selection" << m_selLen << "editable" << m_selEditable;
    } else if (cmd == "clip") {
        QString text = unescape(QString::fromUtf8(line.constData() + sp + 1));
        if (text.isEmpty()) return;
        if (m_clipTest) { m_clipTest = false; qDebug() << "script: copied text of length" << text.size(); return; }   // test: the phone's clipboard is left alone
        bb::system::Clipboard clip;
        clip.clear();
        clip.insert("text/plain", text.toUtf8());
        toast("Copied");
    } else if (cmd == "cleared") {
        if (!m_script.isEmpty()) { qDebug() << "script: cleared"; return; }
        toast("Cookies and site data cleared");
    } else if (cmd == "dl") {
        handleDownload(QString::fromUtf8(line.constData() + sp + 1));
    } else if (cmd == "t") {                                      // t <id> <event> [argument]
        int s2 = line.indexOf(' ', sp + 1);
        int eid = (s2 < 0 ? line.mid(sp + 1) : line.mid(sp + 1, s2 - sp - 1)).toInt();
        QByteArray rest = s2 < 0 ? QByteArray() : line.mid(s2 + 1);
        int s3 = rest.indexOf(' ');
        handleTab(eid, s3 < 0 ? rest : rest.left(s3), s3 < 0 ? QString() : QString::fromUtf8(rest.constData() + s3 + 1));
    }
}

static QString sizeText(qint64 b) {
    if (b < 1024) return QString::number(b) + " B";
    if (b < 1024 * 1024) return QString::number(b / 1024) + " KB";
    if (b < 1024LL * 1024 * 1024) return QString::number(b / 1048576.0, 'f', 1) + " MB";
    return QString::number(b / 1073741824.0, 'f', 2) + " GB";
}

QVariantList Engine::downloads() const {
    QVariantList out;
    for (int i = m_downloads.size() - 1; i >= 0; --i) {
        const Download &d = m_downloads.at(i);
        QVariantMap m;
        m["title"] = QFileInfo(d.path).fileName();
        QString st;
        if (d.state == "run") st = d.total > 0 ? QString("%1 of %2").arg(sizeText(d.got), sizeText(d.total)) : sizeText(d.got) + " so far";
        else if (d.state == "done") st = sizeText(d.total > 0 ? d.total : d.got);
        else if (d.state == "cancel") st = "Stopped";
        else st = "Did not finish";
        m["host"] = st;
        m["current"] = d.state == "run";
        out << m;
    }
    return out;
}

void Engine::saveDownloads() {
    if (!m_uiTest.isEmpty()) return;
    QFile f(m_dataDir + "/downloads.tsv");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    for (int i = qMax(0, m_downloads.size() - 100); i < m_downloads.size(); ++i) {
        const Download &d = m_downloads.at(i);
        f.write((d.state + "\t" + QString::number(d.total > 0 ? d.total : d.got) + "\t" + d.path + "\n").toUtf8());
    }
}

void Engine::toast(const QString &text) {
    bb::system::SystemToast *t = new bb::system::SystemToast(this);
    t->setBody(text);
    connect(t, SIGNAL(finished(bb::system::SystemUiResult::Type)), t, SLOT(deleteLater()));
    t->show();
}

// dl <id> <state> <received> <total> <path>
void Engine::handleDownload(const QString &arg) {
    unsigned id = arg.section(' ', 0, 0).toUInt();
    QString state = arg.section(' ', 1, 1), path = arg.section(' ', 4);
    qint64 got = arg.section(' ', 2, 2).toLongLong(), total = arg.section(' ', 3, 3).toLongLong();
    Download *d = 0;
    for (int i = 0; i < m_downloads.size(); ++i) if (m_downloads[i].id == id && id != 0) d = &m_downloads[i];
    if (!d) {
        if (path.isEmpty()) return;                               // ended before it had a name
        Download n; n.id = id;
        m_downloads << n;
        d = &m_downloads.last();
        d->path = path;
        if (state == "run") toast("Downloading " + QFileInfo(path).fileName());
    }
    bool ended = state != "run" && d->state != state;
    if (!path.isEmpty()) d->path = path;
    d->state = state; d->got = got; d->total = total;
    if (ended) {
        QString name = QFileInfo(d->path).fileName();
        if (state == "done") toast("Downloaded " + name);
        else if (state == "fail") toast(name + " did not finish");
        d->id = 0;                                                // the engine's id means nothing once it is over
    }
    if (ended) saveDownloads();
    emit downloadsChanged();
}

void Engine::openDownload(int index) {
    int i = m_downloads.size() - 1 - index;
    if (i < 0 || i >= m_downloads.size() || m_downloads.at(i).state != "done") return;
    bb::system::InvokeManager *im = new bb::system::InvokeManager(this);
    bb::system::InvokeRequest req;
    req.setUri(QUrl::fromLocalFile(m_downloads.at(i).path));
    im->invoke(req);
}

void Engine::removeDownload(int index) {
    int i = m_downloads.size() - 1 - index;
    if (i < 0 || i >= m_downloads.size()) return;
    if (m_downloads.at(i).state == "run" && m_downloads.at(i).id) {   // stop it; it leaves the list on the next tap
        send("dlcancel " + QByteArray::number(m_downloads.at(i).id));
        return;
    }
    m_downloads.removeAt(i);
    saveDownloads();
    emit downloadsChanged();
}

static QString escapeText(const QString &in) {
    QString out = in;
    out.replace('\\', "\\\\").replace('\r', "").replace('\n', "\\n");
    return out;
}

static QString unescape(const QString &in) {
    QString out;
    for (int i = 0; i < in.size(); ++i) {
        if (in.at(i) == '\\' && i + 1 < in.size()) { ++i; out += in.at(i) == 'n' ? QChar('\n') : in.at(i); }
        else out += in.at(i);
    }
    return out;
}

void Engine::querySelection() {
    if (m_selLen != 0 || m_selEditable) { m_selLen = 0; m_selEditable = false; emit selChanged(); }
    if (cur().eid >= 0 && cur().view == "page") send("selq");
}

void Engine::copySelection() { if (cur().eid >= 0) send("copy"); }

bool Engine::canPaste() {
    bb::system::Clipboard clip;
    return !clip.value("text/plain").isEmpty();
}

void Engine::paste() {
    bb::system::Clipboard clip;
    QString text = QString::fromUtf8(clip.value("text/plain"));
    if (text.isEmpty() || cur().eid < 0) return;
    send("paste " + escapeText(text).toUtf8());
}

void Engine::handleTab(int eid, const QByteArray &cmd, const QString &arg) {
    if (m_respawn) return;                                        // the engine is being stopped on purpose; its tabs come back
    if (cmd == "spare") {                                         // an empty engine tab we may give a page to
        Tab *t = tabByEid(eid);
        if (t) t->eid = -1;
        if (!m_spares.contains(eid)) m_spares << eid;
        m_shown = -1;
        if (m_ready) showCurrent();
        return;
    }
    if (cmd == "new") {
        m_shown = eid;                                            // the engine shows a new tab as soon as it exists
        Tab *t = 0;
        while (!m_newQueue.isEmpty() && !t) t = tabByKey(m_newQueue.takeFirst());
        if (t) {                                                  // the one we asked for
            t->eid = eid;
            t->pending.clear();
        } else if (tabByEid(eid) == 0) {                          // a page opened it (a link to a new window)
            Tab n; n.key = m_nextKey++; n.eid = eid; n.view = "page";
            m_tabs.insert(m_cur + 1, n);
            ++m_cur;
            qDebug() << "tab opened by a page";
            emitAll();
        }
        showCurrent();
        return;
    }
    if (cmd == "closed") {
        m_spares.removeAll(eid);
        if (m_shown == eid) m_shown = -1;
        Tab *t = tabByEid(eid);
        if (t) {                                                  // the page closed itself
            t->eid = -1;
            for (int i = 0; i < m_tabs.size(); ++i) if (&m_tabs[i] == t) { closeTab(i); break; }
        }
        return;
    }
    Tab *t = tabByEid(eid);
    if (!t) return;                                               // a spare, or a tab already closed here
    bool c = isCur(t);
    if (cmd == "url") {
        if (blank(arg) && !t->lastUrl.isEmpty()) return;          // the empty tab's own page, reported after we gave it a real one
        setUrl(t, arg);
    } else if (cmd == "title") {
        if (arg.startsWith("about:")) return;                     // the empty tab's own title
        if (arg != t->title) { t->title = arg; if (c) emit titleChanged(); emit tabsChanged(); }
        if (m_store && t->view == "page" && !t->loading) m_store->setTitle(t->lastUrl, arg);
    } else if (cmd == "loading") {
        bool on = arg == "1";
        if (on != t->loading) { t->loading = on; if (c) emit loadingChanged(); }
        if (!on && m_store && t->view == "page" && !t->title.isEmpty()) m_store->setTitle(t->lastUrl, t->title);   // the page's settled title
        if (on && t->progress != 0) { t->progress = 0; if (c) emit progressChanged(); }
    } else if (cmd == "progress") {
        int p = arg.toInt();
        if (p != t->progress) { t->progress = p; if (c) emit progressChanged(); }
    } else if (cmd == "nav") {
        QStringList a = arg.split(' ');
        bool b = a.value(0) == "1", f = a.value(1) == "1";
        if (b != t->canBack || f != t->canForward) { t->canBack = b; t->canForward = f; if (c) emit navChanged(); }
    } else if (cmd == "commit") {
        if (!blank(arg)) { t->lastUrl = arg; t->title.clear(); setView(t, "page"); if (m_store) m_store->visit(arg); }
        if (!blank(arg) && m_testStep < 0 && QFile::exists(m_dataDir + "/selftest")) { m_testStep = 0; QTimer::singleShot(6000, this, SLOT(onSelfTest())); }
    } else if (cmd == "fail") {
        int s2 = arg.indexOf(' ');
        int code = arg.left(s2).toInt();
        if (code != -3) setError(t, code, arg.mid(s2 + 1));       // -3: the load was cancelled, not an error
    }
}

void Engine::setView(Tab *t, const QString &v) {
    if (v == t->view) return;
    t->view = v;
    if (!isCur(t)) return;
    qDebug() << "view:" << v;
    emit viewChanged();
}

void Engine::setUrl(Tab *t, const QString &u) {
    QString shown = blank(u) ? QString() : u;
    if (shown == t->url) return;
    t->url = shown;
    QUrl q(shown);
    t->host = q.host();
    if (t->host.startsWith("www.")) t->host = t->host.mid(4);
    if (t->host.isEmpty()) t->host = shown;
    t->secure = q.scheme() == "https";
    if (isCur(t)) emit urlChanged();
    emit tabsChanged();
}

void Engine::setError(Tab *t, int code, const QString &url) {
    QString host = QUrl(url).host();
    if (host.isEmpty()) host = "this page";
    t->errRetry = true;
    t->errKind = "other";
    if (code == -106) {
        t->errKind = "offline";
        t->errTitle = "You're offline";
        t->errText = "Sail can't reach the internet. Check Wi-Fi or mobile data, then try again.";
    } else if (code == -105 || code == -137) {
        t->errTitle = "Can't find " + host;
        t->errText = "No site answers at that address. Check the spelling, or search for it instead.";
    } else if (code == -7 || code == -118) {
        t->errTitle = host + " took too long";
        t->errText = "The site didn't answer in time. It may be busy, or the connection may be slow.";
    } else if (code <= -200 && code > -300) {
        t->errTitle = "This connection isn't private";
        t->errText = host + " sent a security certificate that Sail doesn't trust, so the page was not loaded. "
                     "Someone may be intercepting the connection, or the site may be misconfigured.";
        t->errRetry = false;
        t->errKind = "cert";
    } else if (code <= -100 && code > -200) {
        t->errTitle = "Can't connect to " + host;
        t->errText = "The site refused or dropped the connection. It may be down for now.";
    } else {
        t->errTitle = "This page didn't load";
        t->errText = "Something went wrong while loading " + host + ".";
    }
    t->errCode = "Error " + QString::number(-code);
    t->lastUrl = url;
    if (isCur(t)) { qDebug() << "load failed:" << code << t->errTitle; emit errorChanged(); }
    setView(t, "error");
}

bool Engine::isAddress(const QString &text) {
    QString t = text.trimmed();
    if (t.contains("://") || t.startsWith("about:") || t.startsWith("data:")) return true;
    QString hostPart = t.section('/', 0, 0).section(':', 0, 0);
    return !t.contains(' ') && (hostPart.contains('.') || hostPart == "localhost") && !hostPart.endsWith('.');
}

// What the person typed: an address (with or without https://) or a search.
QString Engine::resolve(const QString &text) {
    QString t = text.trimmed();
    if (t.isEmpty()) return QString();
    if (t.contains("://") || t.startsWith("about:") || t.startsWith("data:")) return t;
    if (isAddress(t)) return "https://" + t;
    return QString::fromLatin1(kSearch[s_search][2]) + QString::fromLatin1(QUrl::toPercentEncoding(t));
}

void Engine::open(const QString &text) {
    QString u = resolve(text);
    if (u.isEmpty()) return;
    setUrl(&cur(), u);                                            // show where we're going straight away
    loadIn(&cur(), u);
}

// Load a URL in a tab, giving the tab an engine tab first if it has none: a spare one, or a new one.
void Engine::loadIn(Tab *t, const QString &u) {
    t->lastUrl = u;
    if (m_fd < 0) { t->pending = u; if (isCur(t)) restart(); return; }
    if (!m_ready) { t->pending = u; return; }
    t->pending.clear();
    if (t->eid < 0 && !m_spares.isEmpty()) t->eid = m_spares.takeFirst();
    if (t->eid < 0) {
        if (m_newQueue.contains(t->key)) return;                  // already asked; one page per request
        t->pending = u;
        m_newQueue << t->key;
        send("new 0 " + u.toUtf8());
        return;
    }
    if (m_shown != t->eid) { send("select " + QByteArray::number(t->eid)); m_shown = t->eid; }
    send("load " + u.toUtf8());
}

void Engine::showCurrent() {
    Tab *t = &cur();
    if (m_fd < 0 || !m_ready) return;
    if (t->eid >= 0) {
        if (m_shown != t->eid) { send("select " + QByteArray::number(t->eid)); m_shown = t->eid; }
        return;
    }
    if (m_newQueue.contains(t->key)) return;
    QString u = !t->pending.isEmpty() ? t->pending : t->url.isEmpty() ? QString() : t->lastUrl;
    if (!u.isEmpty()) loadIn(t, u);
}

void Engine::back() { if (cur().eid >= 0) send("back"); }
void Engine::forward() { if (cur().eid >= 0) send("forward"); }
void Engine::reload() {
    Tab *t = &cur();
    if (m_fd < 0) { restart(); return; }
    if (t->eid >= 0) send("reload");
    else if (!t->lastUrl.isEmpty()) loadIn(t, t->lastUrl);
}
void Engine::stop() { if (cur().eid >= 0) send("stop"); }

// Empty a tab: its engine tab is closed (the engine keeps its last one as a spare).
static void clearTab(Tab *t) {
    int key = t->key;
    *t = Tab();
    t->key = key;
}

void Engine::home() {
    Tab *t = &cur();
    if (t->eid >= 0) { send("close " + QByteArray::number(t->eid)); if (m_shown == t->eid) m_shown = -1; }
    m_newQueue.removeAll(t->key);
    clearTab(t);
    emitAll();
}

void Engine::newTab() {
    Tab t; t.key = m_nextKey++;
    m_tabs << t;
    m_cur = m_tabs.size() - 1;
    emitAll();
}

void Engine::selectTab(int index) {
    if (index < 0 || index >= m_tabs.size()) return;
    m_cur = index;
    Tab *t = &cur();
    if (m_fd < 0 && t->view != "start") t->view = "stopped";
    emitAll();
    showCurrent();
}

void Engine::closeTab(int index) {
    if (index < 0 || index >= m_tabs.size()) return;
    Tab t = m_tabs.at(index);
    if (t.eid >= 0) { send("close " + QByteArray::number(t.eid)); if (m_shown == t.eid) m_shown = -1; }
    m_newQueue.removeAll(t.key);
    m_tabs.removeAt(index);
    if (m_tabs.isEmpty()) { Tab n; n.key = m_nextKey++; m_tabs << n; }
    if (index < m_cur || m_cur >= m_tabs.size()) --m_cur;
    if (m_cur < 0) m_cur = 0;
    if (m_fd < 0 && cur().view != "start") cur().view = "stopped";
    emitAll();
    showCurrent();
}

void Engine::restart() {
    if (m_fd >= 0) return;
    Tab *t = &cur();
    if (t->pending.isEmpty()) t->pending = t->lastUrl;
    setView(t, t->pending.isEmpty() ? "start" : "page");
    spawn();
}

void Engine::key(int sym, bool shift, bool ctrl, bool alt, bool down) {
    int mods = (shift ? 1 : 0) | (ctrl ? 2 : 0) | (alt ? 4 : 0);    // KEYMOD_SHIFT, _CTRL, _ALT
    send("key " + QByteArray::number(sym) + " " + QByteArray::number(mods) + " " + (down ? "1" : "0"));
}

// The Passport's keyboard is also a touch surface. Its touches come to the app, not to the page window, so a
// slide on it is passed on as a finger dragging the middle of the page: scrolling and flinging then behave
// exactly as they do on the screen. A touch that doesn't move (typing) is never passed on.
void Engine::keyboardTouch(int phase, double x, double y, int finger) {
    if (m_kbLog < 6) { ++m_kbLog; qDebug() << "keyboard touch" << phase << x << y << finger; }
    const double slop = 14, gain = 1.6;
    if (phase == 0) {
        if (m_kbFinger >= 0) return;                              // a second finger: ignore
        m_kbFinger = finger; m_kbActive = false; m_kbX = x; m_kbY = y;
        return;
    }
    if (finger != m_kbFinger) return;
    int cx = m_pageW / 2, cy = m_pageH / 2;
    if (phase == 1) {
        if (!m_kbActive) {
            if (qAbs(x - m_kbX) < slop && qAbs(y - m_kbY) < slop) return;
            m_kbActive = true; m_kbX = x; m_kbY = y;              // the drag starts here
            send("touch 0 " + QByteArray::number(cx) + " " + QByteArray::number(cy));
            return;
        }
        send("touch 1 " + QByteArray::number(cx + int((x - m_kbX) * gain)) + " " + QByteArray::number(cy + int((y - m_kbY) * gain)));
        return;
    }
    if (m_kbActive) send("touch 2 " + QByteArray::number(cx + int((x - m_kbX) * gain)) + " " + QByteArray::number(cy + int((y - m_kbY) * gain)));
    m_kbFinger = -1; m_kbActive = false;
}

// Test switch: slide a finger up the keyboard (20 steps of 12 px), as a person would to scroll down.
void Engine::onSelfTest() {
    int i = m_testStep++;
    if (i == 0) keyboardTouch(0, 700, 1800, 1);
    else if (i <= 20) keyboardTouch(1, 700, 1800 - i * 12, 1);
    else { keyboardTouch(2, 700, 1800 - 20 * 12, 1); qDebug() << "selftest: slide sent"; return; }
    QTimer::singleShot(16, this, SLOT(onSelfTest()));
}

// Test switch: drive the tabs without a hand on the phone. Commands: open <text>, newtab, select <n>,
// close <n>, home, back, state (log the tabs).
void Engine::onScript() {
    if (m_script.isEmpty()) return;
    QString line = m_script.takeFirst();
    QString cmd = line.section(' ', 1, 1), arg = line.section(' ', 2);
    if (cmd == "open") open(arg);
    else if (cmd == "newtab") newTab();
    else if (cmd == "select") selectTab(arg.toInt());
    else if (cmd == "close") closeTab(arg.toInt());
    else if (cmd == "home") home();
    else if (cmd == "back") back();
    else if (cmd == "selq") querySelection();
    else if (cmd == "setting") pickSetting(arg.toInt());
    else if (cmd == "cleardata") send("cleardata");
    else if (cmd == "copylen") { m_clipTest = true; copySelection(); }      // reports the length only; the clipboard is not touched
    else if (cmd == "pastetext") send("paste " + escapeText(arg).toUtf8()); // pastes this text, not the phone's clipboard
    else if (cmd == "keys") {                                     // type text into the page, as the app's key listener would
        for (int i = 0; i < arg.size(); ++i) { int sym = arg.at(i).unicode(); key(sym, false, false, false, true); key(sym, false, false, false, false); }
    }
    QString st;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const Tab &t = m_tabs.at(i);
        st += QString(" [%1%2 e%3 %4 %5]").arg(i == m_cur ? "*" : "").arg("").arg(t.eid).arg(t.view).arg(t.host);
    }
    qDebug() << "script:" << cmd << arg << "->" << st << "shown" << m_shown << "spares" << m_spares.size();
    if (!m_script.isEmpty()) QTimer::singleShot(int(m_script.first().section(' ', 0, 0).toDouble() * 1000), this, SLOT(onScript()));
}

// The row of symbols the system shows above the keyboard while a text field is being typed in.
void Engine::hideKeyboardBar() {
    virtualkeyboard_hide();
    QTimer::singleShot(250, this, SLOT(onHideAgain()));           // once more after the focus change has gone through
}
void Engine::onHideAgain() { virtualkeyboard_hide(); }
void Engine::focusEngine() { send("focus"); }

void Engine::note(const QString &text) { if (m_notes < 40) { ++m_notes; qDebug() << "note:" << text; } }

// The page window sits just above the app's window so it takes touches directly. While app controls must be
// drawn over the page (menu, address editing) it drops below; Cascades shows it through the page area.
void Engine::applyZ() {
    if (!m_fw || !m_fw->isBoundToWindow()) return;
    screen_window_t win = m_fw->windowHandle();
    if (!win) return;
    int z = m_overlay ? -1 : 1;
    int rc = screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z);
    screen_context_t ctx = 0;
    if (!screen_get_window_property_pv(win, SCREEN_PROPERTY_CONTEXT, (void **)&ctx) && ctx) screen_flush_context(ctx, 0);
    qDebug() << "page window z" << z << (rc == 0 ? "set" : "NOT set");
    if (m_keysViaApp) {
        // Keys and the keyboard's touch surface are read by the app. A touched window normally takes the input
        // focus, and then both would go to the page window instead (seen as: sliding on the keyboard stops
        // scrolling once the page has been touched). This keeps the focus with the app's window.
        int sens = 0, kf = -1;
        screen_get_window_property_iv(win, SCREEN_PROPERTY_SENSITIVITY, &sens);
        screen_get_window_property_iv(win, SCREEN_PROPERTY_KEYBOARD_FOCUS, &kf);
        if (!(sens & SCREEN_SENSITIVITY_MASK_NO_FOCUS)) {
            int want = sens | SCREEN_SENSITIVITY_MASK_NO_FOCUS;
            int r2 = screen_set_window_property_iv(win, SCREEN_PROPERTY_SENSITIVITY, &want);
            if (ctx) screen_flush_context(ctx, 0);
            qDebug() << "page window sensitivity" << sens << "->" << want << (r2 == 0 ? "set" : "NOT set") << "had keyboard focus" << kf;
        } else if (kf > 0) qDebug() << "page window has keyboard focus although it must not take it";
    }
}

void Engine::setOverlay(bool on) {
    if (on == m_overlay) return;
    m_overlay = on;
    applyZ();
}

void Engine::onBound(bool bound) {
    qDebug() << "page window bound:" << bound;
    if (bound) applyZ();
}

void Engine::onForeground() { send("visible 1"); }
void Engine::onInvisible() {
    if (QFile::exists(m_dataDir + "/keepdrawing")) return;        // test switch: lets the page be inspected with the screen off
    send("visible 0");
}

void Engine::onQuit() {
    if (m_fd < 0) return;
    send("quit");
    if (m_notifier) { m_notifier->setEnabled(false); m_notifier = 0; }
    close(m_fd);
    m_fd = -1;
}

// Leaving while the page window (or a text field) holds keyboard focus can leave one of the app's threads waiting
// on the input service for good, and the process then never fully exits. So focus is given up first, and the
// app quits once that has gone through.
void Engine::onLeave() {
    if (m_leavingNow) return;
    m_leavingNow = true;
    emit leaving();
    onQuit();                                                     // the engine can start closing meanwhile
    QTimer::singleShot(600, this, SLOT(onLeaveDone()));
}

void Engine::onLeaveDone() { Application::instance()->quit(); }

void Engine::copyLink() {
    if (cur().url.isEmpty()) return;
    bb::system::Clipboard clip;
    clip.clear();
    clip.insert("text/plain", cur().url.toUtf8());
    bb::system::SystemToast *toast = new bb::system::SystemToast(this);
    toast->setBody("Link copied");
    connect(toast, SIGNAL(finished(bb::system::SystemUiResult::Type)), toast, SLOT(deleteLater()));
    toast->show();
}

void Engine::share() {
    if (cur().url.isEmpty()) return;
    if (m_share) { m_share->deleteLater(); m_share = 0; }
    m_share = Invocation::create(InvokeQuery::create().parent(this).mimeType("text/plain").data(cur().url.toUtf8()));
    m_share->setParent(this);
    connect(m_share, SIGNAL(armed()), this, SLOT(onShareArmed()));
}

void Engine::onShareArmed() {
    if (m_share) m_share->trigger("bb.action.SHARE");
}
