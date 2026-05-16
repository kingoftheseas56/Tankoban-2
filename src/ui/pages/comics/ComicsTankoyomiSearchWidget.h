#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 -- forked from
// src/ui/pages/stream/StreamSearchWidget.{h,cpp} per brainstorm 6.1.
//
// TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- collapsed from the prior
// multi-section shape (Premium / Manga / Comics) to a single AniList-primary
// tile strip. Premium catalog routing + scraper fan-out + Manga/Comics type
// split all REMOVED. Backbone is now AniListClient::searchByTitle; results
// are MediaPreview-typed and rendered into a single "RESULTS" strip. Click
// on a tile emits seriesActivated(MediaPreview) which ComicsPage routes
// into ComicsSeriesView::showSeries.

#include "core/manga/anilist/AniListTypes.h"

#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

class TileStrip;
class TileCard;
class QNetworkAccessManager;

namespace tankoban::manga::anilist {
class AniListClient;
} // namespace tankoban::manga::anilist

class ComicsTankoyomiSearchWidget : public QWidget
{
    Q_OBJECT

public:
    // Phase 9: ctor takes the AniList client (non-owning) instead of a
    // MangaSourceRegistry. The shared NAM is retained for future poster
    // download paths even though MediaPreview ships coverThumbUrl directly.
    explicit ComicsTankoyomiSearchWidget(tankoban::manga::anilist::AniListClient* client,
                                         QNetworkAccessManager* nam,
                                         QWidget* parent = nullptr);

    void search(const QString& query);   // entry point from ComicsPage's search bar
    void clearResults();                  // when Back is clicked

signals:
    void backRequested();
    // Phase 9: signature changed from MangaResult to MediaPreview. The
    // page-side handler routes by anilistId into ComicsSeriesView.
    void seriesActivated(const tankoban::manga::anilist::MediaPreview& preview);

private slots:
    void onSearchSucceeded(int requestId,
                           const QList<tankoban::manga::anilist::MediaPreview>& results);
    void onSearchFailed(int requestId, const QString& reason);

private:
    void buildUI();
    void addResultCard(const tankoban::manga::anilist::MediaPreview& r);

    tankoban::manga::anilist::AniListClient* m_client = nullptr;  // non-owning
    QNetworkAccessManager*                   m_nam    = nullptr;  // non-owning

    QString m_currentQuery;
    int     m_pendingReqId    = -1;
    int     m_nextRequestId   = 1;

    QPushButton* m_backBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QScrollArea* m_scroll       = nullptr;

    // Phase 9: single ranked strip + header. No sectioning, no overflow,
    // no synthetic catalog injection. AniList already returns a ranked
    // result set; we render it verbatim.
    QLabel*      m_resultsHeader = nullptr;
    TileStrip*   m_resultsStrip  = nullptr;

    // anilistId -> de-dup gate (defensive against duplicate AniList hits;
    // current API doesn't double-emit, but a one-line HashSet is cheap).
    QHash<int, bool> m_seenAnilistIds;
};
