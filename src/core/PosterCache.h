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

private:
    PosterCache() = default;

    void touchLocked(const QString& key) const;
    void trimLocked();

    static constexpr int kCapacity = 1000;

    mutable QMutex m_mutex;
    mutable QList<QString> m_lruOrder;
    QHash<QString, QPixmap> m_cache;
};
