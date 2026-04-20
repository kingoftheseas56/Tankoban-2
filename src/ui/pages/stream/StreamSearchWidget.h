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
    QLabel*      m_moviesHeader = nullptr;
    TileStrip*   m_moviesStrip  = nullptr;
    QLabel*      m_seriesHeader = nullptr;
    TileStrip*   m_seriesStrip  = nullptr;
    // Tracked for the relevance scorer in onCatalogResults.
    QString      m_currentQuery;

    QString m_posterCacheDir;

    // Phase 1 Batch 1.2 — per-result MetaItemPreview cache for the click
    // handler's `metaActivated` emission + tile-list for external library-
    // change badge refresh (when user toggles Add/Remove from detail view,
    // the matching search tile's "In Library" cue needs to update too).
    QHash<QString, tankostream::addon::MetaItemPreview> m_previewsById;
    QList<TileCard*> m_tiles;
};
