#include "PosterCache.h"

#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>

PosterCache& PosterCache::instance()
{
    static PosterCache cache;
    return cache;
}

QPixmap PosterCache::get(const QString& key) const
{
    if (key.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    const auto it = m_cache.constFind(key);
    if (it == m_cache.constEnd()) {
        return {};
    }

    touchLocked(key);
    return it.value();
}

void PosterCache::put(const QString& key, const QPixmap& pixmap)
{
    if (key.isEmpty() || pixmap.isNull()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    m_cache.insert(key, pixmap);
    touchLocked(key);
    trimLocked();
}

void PosterCache::remove(const QString& key)
{
    if (key.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    m_cache.remove(key);
    m_lruOrder.removeAll(key);
}

void PosterCache::decodeFileAsync(const QString& key,
                                  const QString& path,
                                  QObject* context,
                                  std::function<void(QPixmap)> callback,
                                  const QSize& scaledSize)
{
    if (key.isEmpty() || path.isEmpty()) {
        if (callback) {
            QObject* receiver = context ? context : QCoreApplication::instance();
            if (receiver) {
                QTimer::singleShot(0, receiver, [callback]() { callback({}); });
            } else {
                callback({});
            }
        }
        return;
    }

    const QPixmap cached = get(key);
    if (!cached.isNull()) {
        if (callback) {
            QObject* receiver = context ? context : QCoreApplication::instance();
            if (receiver) {
                QTimer::singleShot(0, receiver, [callback, cached]() { callback(cached); });
            } else {
                callback(cached);
            }
        }
        return;
    }

    const bool hasContext = context != nullptr;
    QPointer<QObject> guard(context);
    QThreadPool::globalInstance()->start(QRunnable::create([key, path, scaledSize, callback, hasContext, guard]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
        const QImage image = reader.read();

        QObject* app = QCoreApplication::instance();
        if (!app) {
            return;
        }

        QMetaObject::invokeMethod(app, [key, image, callback, hasContext, guard]() {
            if (hasContext && guard.isNull()) {
                return;
            }

            QPixmap pixmap;
            if (!image.isNull()) {
                pixmap = QPixmap::fromImage(image);
                PosterCache::instance().put(key, pixmap);
            }

            if (callback) {
                callback(pixmap);
            }
        }, Qt::QueuedConnection);
    }));
}

void PosterCache::touchLocked(const QString& key) const
{
    m_lruOrder.removeAll(key);
    m_lruOrder.append(key);
}

void PosterCache::trimLocked()
{
    while (m_lruOrder.size() > kCapacity) {
        const QString oldest = m_lruOrder.takeFirst();
        m_cache.remove(oldest);
    }
}
