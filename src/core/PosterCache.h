#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QPixmap>
#include <QSize>
#include <QString>

#include <functional>

class QObject;

// Session-scoped poster pixmap cache shared by Stream catalog, Videos tiles,
// and the poster picker. Disk decode is done as QImage work on a background
// thread; QPixmap conversion and cache mutation stay on the GUI thread.
class PosterCache {
public:
    static PosterCache& instance();

    QPixmap get(const QString& key) const;
    void put(const QString& key, const QPixmap& pixmap);
    void remove(const QString& key);

    void decodeFileAsync(const QString& key,
                         const QString& path,
                         QObject* context,
                         std::function<void(QPixmap)> callback,
                         const QSize& scaledSize = QSize());

    // Session hit/miss/eviction counters for the dev-control bridge
    // (tankoctl cache-get-stats). Cheap atomically-meaningless ints guarded
    // by the same mutex as the cache itself.
    struct Stats {
        qint64 hits = 0;
        qint64 misses = 0;
        qint64 evictions = 0;
        qint64 puts = 0;
        int    size = 0;
        int    capacity = 0;
    };
    Stats stats() const;

private:
    PosterCache() = default;

    void touchLocked(const QString& key) const;
    void trimLocked();

    static constexpr int kCapacity = 1000;

    mutable QMutex m_mutex;
    mutable QList<QString> m_lruOrder;
    QHash<QString, QPixmap> m_cache;

    mutable qint64 m_hits = 0;
    mutable qint64 m_misses = 0;
    qint64 m_evictions = 0;
    qint64 m_puts = 0;
};
