#include "store.h"
#include "engine.h"

#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtAlgorithms>

static const int kMaxHistory = 1500;     // addresses kept; the oldest go first
static const int kShownHistory = 300;

static QString hostOf(const QString &url) {
    QString h = QUrl(url).host();
    if (h.startsWith("www.")) h = h.mid(4);
    return h.isEmpty() ? url : h;
}

static QString clean(QString s) { return s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ').trimmed(); }

static void readFile(const QString &path, QList<Visit> *out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        QStringList p = QString::fromUtf8(f.readLine()).remove('\n').split('\t');
        if (p.size() < 3 || p.at(2).isEmpty()) continue;
        Visit v; v.when = p.at(0).toUInt(); v.count = p.at(1).toInt(); v.url = p.at(2); v.title = p.value(3);
        if (v.title.startsWith("about:")) v.title.clear();        // written by 0.2.1.1 for some pages
        out->append(v);
    }
}

static void writeFile(const QString &path, const QList<Visit> &rows) {
    QFile f(path + ".new");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    for (int i = 0; i < rows.size(); ++i) {
        const Visit &v = rows.at(i);
        f.write((QString::number(v.when) + "\t" + QString::number(v.count) + "\t" + v.url + "\t" + v.title + "\n").toUtf8());
    }
    f.close();
    QFile::remove(path);
    QFile::rename(path + ".new", path);
}

Store::Store(const QString &dataDir, QObject *parent)
    : QObject(parent), m_dir(dataDir), m_dirtyHistory(false), m_dirtyBookmarks(false), m_sample(false), m_rev(0) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(3000);                                   // a burst of changes is written once
    connect(m_timer, SIGNAL(timeout()), this, SLOT(save()));
}

void Store::load() {
    readFile(m_dir + "/history.tsv", &m_history);
    readFile(m_dir + "/bookmarks.tsv", &m_bookmarks);
}

void Store::loadSample() {
    m_sample = true;
    uint now = QDateTime::currentDateTime().toTime_t();
    const char *rows[][2] = {
        { "https://en.m.wikipedia.org/wiki/BlackBerry_Passport", "BlackBerry Passport - Wikipedia" },
        { "https://www.bbc.com/news", "BBC News - Breaking news, video and the latest top stories" },
        { "https://www.bbc.com/weather", "BBC Weather" },
        { "https://www.nationalrail.co.uk/", "Train times and tickets" },
        { "https://news.ycombinator.com/", "Hacker News" },
        { "https://www.openstreetmap.org/", "OpenStreetMap" },
    };
    for (int i = 0; i < 6; ++i) {
        Visit v; v.url = rows[i][0]; v.title = rows[i][1]; v.count = 9 - i;
        v.when = now - (i < 3 ? i * 1500 : i * 40000);
        m_history << v;
        if (i == 0 || i == 3) m_bookmarks << v;
    }
}

void Store::touch(bool history) {
    if (history) m_dirtyHistory = true; else m_dirtyBookmarks = true;
    if (!m_sample && !m_timer->isActive()) m_timer->start();
}

void Store::save() {
    if (m_sample) return;
    if (m_dirtyHistory) { writeFile(m_dir + "/history.tsv", m_history); m_dirtyHistory = false; }
    if (m_dirtyBookmarks) { writeFile(m_dir + "/bookmarks.tsv", m_bookmarks); m_dirtyBookmarks = false; }
}

void Store::visit(const QString &url) {
    if (!url.startsWith("http://") && !url.startsWith("https://")) return;
    uint now = QDateTime::currentDateTime().toTime_t();
    for (int i = 0; i < m_history.size(); ++i) {
        if (m_history[i].url == url) {
            m_history[i].when = now; ++m_history[i].count;
            touch(true); emit historyChanged(); emit topChanged();
            return;
        }
    }
    Visit v; v.url = url; v.when = now; v.count = 1;
    m_history << v;
    if (m_history.size() > kMaxHistory) {                         // drop the least recently visited
        int old = 0;
        for (int i = 1; i < m_history.size(); ++i) if (m_history.at(i).when < m_history.at(old).when) old = i;
        m_history.removeAt(old);
    }
    touch(true); emit historyChanged(); emit topChanged();
}

void Store::setTitle(const QString &url, const QString &title) {
    QString t = clean(title);
    if (t.isEmpty()) return;
    for (int i = 0; i < m_history.size(); ++i) {
        if (m_history[i].url == url) {
            if (m_history[i].title != t) { m_history[i].title = t; touch(true); emit historyChanged(); }
            break;
        }
    }
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks[i].url == url && m_bookmarks[i].title != t) { m_bookmarks[i].title = t; touch(false); emit bookmarksChanged(); }
    }
}

struct ByWhen {
    const QList<Visit> *rows;
    bool operator()(int a, int b) const { return rows->at(a).when > rows->at(b).when; }
};

QList<int> Store::newestFirst() const {
    QList<int> idx;
    for (int i = 0; i < m_history.size(); ++i) idx << i;
    ByWhen cmp; cmp.rows = &m_history;
    qStableSort(idx.begin(), idx.end(), cmp);
    return idx;
}

static QString whenText(uint when) {
    QDateTime t = QDateTime::fromTime_t(when);
    int days = t.date().daysTo(QDate::currentDate());
    if (days <= 0) return t.toString("HH:mm");
    if (days == 1) return "Yesterday";
    return t.toString("d MMM");
}

static QVariantMap row(const Visit &v, const QString &sub) {
    QVariantMap m;
    m["title"] = v.title.isEmpty() ? hostOf(v.url) : v.title;
    m["host"] = sub;
    m["url"] = v.url;
    m["current"] = false;
    return m;
}

QVariantList Store::history() const {
    QVariantList out;
    QList<int> idx = newestFirst();
    for (int i = 0; i < idx.size() && i < kShownHistory; ++i) {
        const Visit &v = m_history.at(idx.at(i));
        out << row(v, hostOf(v.url) + QString::fromUtf8("  \xC2\xB7  ") + whenText(v.when));
    }
    return out;
}

QVariantList Store::bookmarks() const {
    QVariantList out;
    for (int i = 0; i < m_bookmarks.size(); ++i) out << row(m_bookmarks.at(i), hostOf(m_bookmarks.at(i).url));
    return out;
}

struct ByCount {
    const QList<Visit> *rows;
    bool operator()(int a, int b) const { return rows->at(a).count > rows->at(b).count; }
};

// Start view: bookmarks first, then the most visited addresses, five rows in all.
QVariantList Store::top() const {
    QVariantList out;
    QStringList seen;
    for (int i = 0; i < m_bookmarks.size() && out.size() < 5; ++i) {
        QVariantMap m = row(m_bookmarks.at(i), hostOf(m_bookmarks.at(i).url));
        m["kind"] = "bookmark";
        out << m; seen << m_bookmarks.at(i).url;
    }
    QList<int> idx;
    for (int i = 0; i < m_history.size(); ++i) idx << i;
    ByCount cmp; cmp.rows = &m_history;
    qStableSort(idx.begin(), idx.end(), cmp);
    for (int i = 0; i < idx.size() && out.size() < 5; ++i) {
        const Visit &v = m_history.at(idx.at(i));
        if (seen.contains(v.url) || v.count < 2) continue;
        QVariantMap m = row(v, hostOf(v.url));
        m["kind"] = "history";
        out << m;
    }
    return out;
}

struct Scored { int score; const Visit *v; bool mark; };
static bool byScore(const Scored &a, const Scored &b) { return a.score > b.score; }

// What to offer while an address is typed: first what Enter will do, then up to four matches from bookmarks and
// history. Every word typed must appear in the address or the title. Nothing is sent anywhere.
QVariantList Store::suggest(const QString &text) const {
    QVariantList out;
    QString t = text.trimmed();
    if (t.isEmpty()) return out;
    QVariantMap first;
    bool address = Engine::isAddress(t);
    first["kind"] = address ? "go" : "search";
    first["title"] = t;
    first["host"] = address ? "Open this address" : "Search with " + (searchName.isEmpty() ? QString("Brave") : searchName);
    first["url"] = t;
    out << first;

    QStringList words = t.toLower().split(' ', QString::SkipEmptyParts);
    uint now = QDateTime::currentDateTime().toTime_t();
    QList<Scored> hits;
    QStringList seen;
    for (int pass = 0; pass < 2; ++pass) {
        const QList<Visit> &rows = pass == 0 ? m_bookmarks : m_history;
        for (int i = 0; i < rows.size(); ++i) {
            const Visit &v = rows.at(i);
            if (seen.contains(v.url)) continue;
            QString host = hostOf(v.url).toLower();
            QString hay = v.url.toLower() + " " + v.title.toLower();
            bool all = true;
            for (int w = 0; w < words.size() && all; ++w) all = hay.contains(words.at(w));
            if (!all) continue;
            Scored s; s.v = &v; s.mark = pass == 0;
            s.score = (pass == 0 ? 400 : 0) + (host.startsWith(words.first()) ? 600 : 0) + qMin(v.count, 30) * 10
                      + (now - v.when < 7 * 86400 ? 100 : 0) - qMin(v.url.length(), 200) / 4;
            hits << s; seen << v.url;
        }
    }
    qStableSort(hits.begin(), hits.end(), byScore);
    for (int i = 0; i < hits.size() && i < 4; ++i) {
        QVariantMap m = row(*hits.at(i).v, hostOf(hits.at(i).v->url));
        m["kind"] = hits.at(i).mark ? "bookmark" : "history";
        out << m;
    }
    return out;
}

bool Store::isBookmarked(const QString &url) const {
    for (int i = 0; i < m_bookmarks.size(); ++i) if (m_bookmarks.at(i).url == url) return true;
    return false;
}

void Store::toggleBookmark(const QString &url, const QString &title) {
    if (url.isEmpty()) return;
    bool had = false;
    for (int i = m_bookmarks.size() - 1; i >= 0; --i) if (m_bookmarks.at(i).url == url) { m_bookmarks.removeAt(i); had = true; }
    if (!had) {
        Visit v; v.url = url; v.title = clean(title); v.when = QDateTime::currentDateTime().toTime_t(); v.count = 1;
        m_bookmarks << v;
    }
    ++m_rev;
    touch(false); emit bookmarksChanged(); emit topChanged();
}

void Store::removeBookmark(int index) {
    if (index < 0 || index >= m_bookmarks.size()) return;
    m_bookmarks.removeAt(index);
    ++m_rev;
    touch(false); emit bookmarksChanged(); emit topChanged();
}

void Store::removeHistory(int index) {
    QList<int> idx = newestFirst();
    if (index < 0 || index >= idx.size()) return;
    m_history.removeAt(idx.at(index));
    touch(true); emit historyChanged(); emit topChanged();
}

void Store::clearHistory() {
    m_history.clear();
    touch(true); emit historyChanged(); emit topChanged();
}
