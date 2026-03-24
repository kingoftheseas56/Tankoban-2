#include "PageCache.h"

PageCache::PageCache(qint64 budgetBytes)
    : m_budget(budgetBytes)
{
}

void PageCache::setBudget(qint64 bytes)
{
    m_budget = bytes;
    evict();
}

bool PageCache::contains(int pageIndex) const
{
    return m_entries.contains(pageIndex);
}

QPixmap PageCache::get(int pageIndex)
{
    auto it = m_entries.find(pageIndex);
    if (it == m_entries.end())
        return {};
    it->accessOrder = ++m_accessCounter;
    return it->pixmap;
}

void PageCache::insert(int pageIndex, const QPixmap& pixmap)
{
    // Remove old entry if exists
    if (m_entries.contains(pageIndex)) {
        m_usedBytes -= pixmapBytes(m_entries[pageIndex].pixmap);
        m_entries.remove(pageIndex);
    }

    CacheEntry entry;
    entry.pixmap = pixmap;
    entry.accessOrder = ++m_accessCounter;
    entry.pinned = false;

    qint64 size = pixmapBytes(pixmap);
    m_entries[pageIndex] = entry;
    m_usedBytes += size;

    evict();
}

void PageCache::clear()
{
    m_entries.clear();
    m_usedBytes = 0;
    m_accessCounter = 0;
}

void PageCache::pin(int pageIndex)
{
    auto it = m_entries.find(pageIndex);
    if (it != m_entries.end())
        it->pinned = true;
}

void PageCache::unpin(int pageIndex)
{
    auto it = m_entries.find(pageIndex);
    if (it != m_entries.end())
        it->pinned = false;
}

void PageCache::evict()
{
    while (m_usedBytes > m_budget && !m_entries.isEmpty()) {
        // Find least recently used non-pinned entry
        int lruKey = -1;
        int lruOrder = INT_MAX;

        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (!it->pinned && it->accessOrder < lruOrder) {
                lruOrder = it->accessOrder;
                lruKey = it.key();
            }
        }

        if (lruKey < 0)
            break; // all entries pinned, can't evict

        m_usedBytes -= pixmapBytes(m_entries[lruKey].pixmap);
        m_entries.remove(lruKey);
    }
}

qint64 PageCache::pixmapBytes(const QPixmap& pix)
{
    if (pix.isNull()) return 0;
    return static_cast<qint64>(pix.width()) * pix.height() * 4; // RGBA
}
