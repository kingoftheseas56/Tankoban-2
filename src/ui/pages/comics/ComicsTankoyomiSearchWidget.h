#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 -- forked from
// src/ui/pages/stream/StreamSearchWidget.{h,cpp} per brainstorm 6.1.
//
// TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- collapsed from the prior
// multi-section shape (Premium / Manga / Comics) to a single AniList-primary
// tile strip. Premium catalog routing + scraper fan-out + Manga/Comics type
// split all REMOVED.
//
// WEEBCENTRAL_IDENTITY_PIVOT Tasks 9+10 (2026-05-19) -- backbone swapped
// from AniListClient to MangaSourceRegistry. Search now routes through
// m_sourceRegistry->find("weebcentral")->search(query, 60). Results are
// MangaResult-typed; tile click emits resultPicked(MangaResult).

#include "core/manga/MangaResult.h"

#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

class TileStrip;
class TileCard;
class MangaSourceRegistry;
class QNetworkAccessManager;

class ComicsTankoyomiSearchWidget : public QWidget
{
    Q_OBJECT

public:
    // Tasks 9+10: ctor now takes MangaSourceRegistry (non-owning) instead of
    // AniListClient. The shared NAM is retained for poster thumbnail fetches.
    explicit ComicsTankoyomiSearchWidget(MangaSourceRegistry* sourceRegistry,
                                         QNetworkAccessManager* nam,
                                         QWidget* parent = nullptr);

    void search(const QString& query);   // entry point from ComicsPage's search bar
    void clearResults();                  // when Back is clicked

signals:
    void backRequested();
    // Tasks 9+10: replaces seriesActivated(MediaPreview). ComicsPage routes
    // this into ComicsSeriesView::showSeries(MangaResult).
    void resultPicked(const MangaResult& result);

private slots:
    void onSearchFinished(const QList<MangaResult>& results);

private:
    void buildUI();
    void addResultCard(const MangaResult& r);

    MangaSourceRegistry*   m_sourceRegistry = nullptr;  // non-owning
    QNetworkAccessManager* m_nam            = nullptr;  // non-owning

    QString m_currentQuery;

    QPushButton* m_backBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QScrollArea* m_scroll       = nullptr;

    // Single ranked strip + header. WeebCentral returns a ranked result set;
    // we render it verbatim. No de-dup needed (scraper does not double-emit).
    QLabel*      m_resultsHeader = nullptr;
    TileStrip*   m_resultsStrip  = nullptr;
};
