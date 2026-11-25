// Sail: what the browser remembers on the phone. History (one row per address: last visit, visit count) and
// bookmarks, each a small tab-separated file in the app's data directory. Nothing here leaves the phone.
#ifndef SAIL_STORE_H
#define SAIL_STORE_H

#include <QObject>
#include <QList>
#include <QString>
#include <QVariantList>

class QTimer;

struct Visit {
    QString url, title;
    uint when;                           // seconds since 1970, last visit
    int count;
    Visit() : when(0), count(0) {}
};

class Store : public QObject {
    Q_OBJECT
public:
    QString searchName;                  // the chosen search engine, for the first suggestion row
private:
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)       // newest first: title, host (site and time), url
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY bookmarksChanged) // in the order added: title, host, url
    Q_PROPERTY(QVariantList top READ top NOTIFY topChanged)                   // start view: bookmarks, then most visited
    Q_PROPERTY(int rev READ rev NOTIFY bookmarksChanged)                      // changes when bookmarks do (for bindings)
public:
    Store(const QString &dataDir, QObject *parent = 0);
    void load();
    void loadSample();                   // design check: sample rows, never saved
    void visit(const QString &url);
    void setTitle(const QString &url, const QString &title);

    QVariantList history() const;
    QVariantList bookmarks() const;
    QVariantList top() const;
    int rev() const { return m_rev; }

    Q_INVOKABLE QVariantList suggest(const QString &text) const;   // rows for the address being typed
    Q_INVOKABLE bool isBookmarked(const QString &url) const;
    Q_INVOKABLE void toggleBookmark(const QString &url, const QString &title);
    Q_INVOKABLE void removeBookmark(int index);
    Q_INVOKABLE void removeHistory(int index);                     // index into history()
    Q_INVOKABLE void clearHistory();

signals:
    void historyChanged();
    void bookmarksChanged();
    void topChanged();

public slots:
    void save();

private:
    void touch(bool history);
    QList<int> newestFirst() const;

    QString m_dir;
    QList<Visit> m_history, m_bookmarks;
    bool m_dirtyHistory, m_dirtyBookmarks, m_sample;
    int m_rev;
    QTimer *m_timer;
};

#endif
