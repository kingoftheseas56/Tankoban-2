#pragma once

#include <QPixmap>
#include <QMap>
#include <QList>
#include <QPair>

struct PageMeta {
    int index = 0;
    QString filename;
    int width = 0;
    int height = 0;
    bool isSpread = false;
    bool decoded = false;
};

class PageCache {
public:
    explicit PageCache(qint64 budgetBytes = 512 * 1024 * 1024);

    void setBudget(qint64 bytes);
    qint64 budget() const { return m_budget; }
    qint64 usedBytes() const { return m_usedBytes; }

    bool contains(int pageIndex) const;
    QPixmap get(int pageIndex);
    void insert(int pageIndex, const QPixmap& pixmap);
    void clear();
    void pin(int pageIndex);
    void unpin(int pageIndex);

private:
    void evict();
    static qint64 pixmapBytes(const QPixmap& pix);

    qint64 m_budget;
    qint64 m_usedBytes = 0;

    struct CacheEntry {
        QPixmap pixmap;
        int accessOrder = 0;
        bool pinned = false;
    };

    QMap<int, CacheEntry> m_entries;
    int m_accessCounter = 0;
};
