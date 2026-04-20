#pragma once

#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

#include "core/stream/addon/MetaItem.h"

class StreamLibrary;
class TileStrip;
class TileCard;
class QNetworkAccessManager;

namespace tankostream::stream {
class MetaAggregator;
}

class StreamSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StreamSearchWidget(tankostream::stream::MetaAggregator* meta,
                                StreamLibrary* library,
                                QWidget* parent = nullptr);

    void search(const QString& query);

signals:
    void backRequested();
    void libraryChanged();
    // Phase 1 Batch 1.2 — click on a search result opens the detail view
    // (instead of the old library-toggle behavior). Carries the tile's full
    // MetaItemPreview so StreamPage::showDetail can paint the detail header
    // immediately for non-library titles.
    void metaActivated(const tankostream::addon::MetaItemPreview& preview);

private:
    void buildUI();
    void clearResults();
    void onCatalogResults(const QList<tankostream::addon::MetaItemPreview>& results);
    void onCatalogError(const QString& message);
    void addResultCard(const tankostream::addon::MetaItemPreview& entry);
    void downloadPoster(const QString& imdbId, const QString& posterUrl, TileCard* card);
    void updateInLibraryBadge(TileCard* card);

    void refreshAllBadges();

    // Per-section initial-display cap. Results beyond this are stashed
    // in m_{movies,series}Overflow and surfaced via the "Show N more"
    // button. 6 fits in a single 1700-px-wide row of 200-px tiles so a
    // freshly-loaded search page shows one tight row per section
    // instead of a multi-row wall before the user has oriented.
    static constexpr int kInitialCap = 6;
    void revealMoviesOverflow();
    void revealSeriesOverflow();

    tankostream::stream::MetaAggregator* m_meta;
    StreamLibrary*                       m_library;
    QNetworkAccessManager*               m_nam;

    // UI
    QPushButton* m_backBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QScrollArea* m_scroll       = nullptr;
    // Split by MetaItemPreview::type (Stremio parity 2026-04-20): two
    // sections — Movies then Series — each with its own header label.
    // Header hides when its section is empty.
    QLabel*      m_moviesHeader    = nullptr;
    TileStrip*   m_moviesStrip     = nullptr;
    QPushButton* m_moviesShowMore  = nullptr;
    QLabel*      m_seriesHeader    = nullptr;
    TileStrip*   m_seriesStrip     = nullptr;
    QPushButton* m_seriesShowMore  = nullptr;
    // Tracked for the relevance scorer in onCatalogResults.
    QString      m_currentQuery;

    // Overflow stashes for the initial-cap / "Show more" reveal. Filled
    // at the tail of onCatalogResults (post-sort), drained by
    // reveal{Movies,Series}Overflow when the user clicks Show more.
    QList<tankostream::addon::MetaItemPreview> m_moviesOverflow;
    QList<tankostream::addon::MetaItemPreview> m_seriesOverflow;

    QString m_posterCacheDir;

    // Phase 1 Batch 1.2 — per-result MetaItemPreview cache for the click
    // handler's `metaActivated` emission + tile-list for external library-
    // change badge refresh (when user toggles Add/Remove from detail view,
    // the matching search tile's "In Library" cue needs to update too).
    QHash<QString, tankostream::addon::MetaItemPreview> m_previewsById;
    QList<TileCard*> m_tiles;
};
