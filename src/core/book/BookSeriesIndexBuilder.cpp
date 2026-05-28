#include "core/book/BookSeriesIndexBuilder.h"

#include <QDateTime>
#include <QSet>
#include <QTimer>

#include "core/book/BookSeriesIndex.h"

BookSeriesIndexBuilder::BookSeriesIndexBuilder(FictionDbClient* client,
                                               BookSeriesIndex* index,
                                               QObject* parent)
    : QObject(parent), m_client(client), m_index(index)
{
    if (m_client) {
        connect(m_client, &FictionDbClient::seriesIndexPageReady,
                this, &BookSeriesIndexBuilder::onPage);
        connect(m_client, &FictionDbClient::seriesIndexPageFailed,
                this, &BookSeriesIndexBuilder::onPageFailed);
    }
}

void BookSeriesIndexBuilder::start()
{
    if (m_running || !m_client || !m_index) return;
    m_running = true;
    m_accum.clear();
    m_letterIdx = 0;
    m_page = 1;
    requestNext();
}

void BookSeriesIndexBuilder::requestNext()
{
    if (m_letterIdx > 25) { finish(); return; }
    const QString letter = QString(QChar::fromLatin1(static_cast<char>('a' + m_letterIdx)));
    m_client->fetchSeriesIndexPage(letter, m_page);
}

void BookSeriesIndexBuilder::onPage(const QString& letter, int page,
                                    const QList<SeriesIndexEntry>& entries, bool hasNext)
{
    if (!m_running) return;
    Q_UNUSED(letter);
    m_accum += entries;
    emit progress(m_letterIdx, page, m_accum.size());

    if (hasNext && !entries.isEmpty()) {
        ++m_page;
    } else {
        ++m_letterIdx;
        m_page = 1;
    }
    if (m_letterIdx > 25) { finish(); return; }
    QTimer::singleShot(250, this, [this] { requestNext(); });
}

void BookSeriesIndexBuilder::onPageFailed(const QString& letter, int page, const QString& error)
{
    if (!m_running) return;
    Q_UNUSED(letter);
    Q_UNUSED(page);
    Q_UNUSED(error);
    // Skip the rest of this letter; advance.
    ++m_letterIdx;
    m_page = 1;
    if (m_letterIdx > 25) { finish(); return; }
    QTimer::singleShot(250, this, [this] { requestNext(); });
}

void BookSeriesIndexBuilder::finish()
{
    if (!m_running) return;
    m_running = false;

    // Dedupe by seriesId (sidebar/cross-letter links can repeat a series).
    QList<SeriesIndexEntry> unique;
    QSet<QString> seen;
    for (const auto& e : m_accum) {
        if (e.seriesId.isEmpty() || seen.contains(e.seriesId)) continue;
        seen.insert(e.seriesId);
        unique.append(e);
    }
    m_index->setEntries(unique, QDateTime::currentSecsSinceEpoch());
    m_index->save();
    emit finished(unique.size());
}
