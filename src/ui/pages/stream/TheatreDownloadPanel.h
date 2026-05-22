#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task C1) - the unified Theatre-native
// download panel. Replaces the Sources panel slot in StreamDetailView's
// right pane when the user clicks Download. Two internal states:
//   PackList     - default after open; shows aggregated Stremio + Tankorent
//                  pack rows with badges + filter chips.
//   ScopePicker  - after a pack is selected; shows episode tiles with
//                  per-season toggle + pre-uncheck-already-have logic.
//
// UI/UX details (pixel values, color tokens, timings, transitions) live
// in Section 5 of docs/superpowers/specs/2026-05-16-theatre-download-
// overhaul-brainstorm.md under AGENT_7_EXPAND markers.

#include "core/stream/UnifiedPackSearchEngine.h"
#include "core/stream/TitleMetadataEstimator.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QJsonArray;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class StreamDownloadIndex;
class TorrentClient;
struct AddTorrentConfig;

namespace tankoban::stream::theatre {

class PackListItem;
class EpisodeTile;

class TheatreDownloadPanel : public QWidget {
    Q_OBJECT
public:
    enum class State { PackList, ScopePicker };

    explicit TheatreDownloadPanel(QWidget* parent = nullptr);

    void setSearchEngine(UnifiedPackSearchEngine* engine);
    void setStreamDownloadIndex(StreamDownloadIndex* index);
    void setTorrentClient(TorrentClient* client);

    // Called by StreamDetailView when the user clicks the new Download button.
    // imdbId + showName + season identify the search; mediaType is "series"
    // or "movie" (drives the degenerate movie scope-picker mode).
    // THEATRE_BULK_PICKER_EPISODE_COUNT_FIX 2026-05-22 — knownEpisodeCounts
    // is Cinemeta's per-season episode count (season -> count). Used by
    // rerenderScopePicker() to size the per-season tile list when libtorrent
    // metadata hasn't arrived yet (and to permanently size it for downloads
    // the file probe can't resolve). Empty map = movies, or unknown series
    // meta; estimator fallback path remains for that case.
    void openFor(const QString& imdbId, const QString& showName,
                 const QString& showYear, int season,
                 const QString& mediaType,
                 const QMap<int, int>& knownEpisodeCounts);

    // Called by StreamDetailView when the user dismisses the panel
    // (Cancel / back-nav / focus change). Resets internal state to empty.
    void reset();

signals:
    // Emitted when the user confirms a download. StreamDetailView routes
    // this to TorrentClient::startDownload with the supplied config.
    void downloadRequested(const QString& imdbId, int season,
                           const QString& magnetUri,
                           const QString& infoHash,
                           const AddTorrentConfig& config,
                           const QList<int>& selectedEpisodes,
                           const QMap<int, int>& fileIndexByEpisode,
                           const QString& packTitle);

    // Emitted on Cancel or back-from-scope-picker. Panel itself transitions
    // internally; this is for the host (StreamDetailView) to maybe re-show
    // the Sources panel.
    void dismissRequested();

private slots:
    void onPackResults(const QString& imdbId, int season,
                       const QList<EnrichedPack>& results);
    void onSearchComplete(const QString& imdbId, int season, int totalPacks);
    void onPackRowSelected(int row);
    void onScopeBackClicked();
    void onDownloadClicked();
    void onFilterChipClicked();
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onSourceComboChanged(int index);

private:
    void buildUI();
    void buildPackListState();
    void buildScopePickerState();
    void transitionTo(State newState);
    void rerenderPackList();
    void rerenderScopePicker();
    void updateSeriesDownloadButton();
    void autoFallbackToShowWide();

    // F2 fix (Codex audit 2026-05-16): when title-only estimate yields no
    // seasons (common for "Complete Series" packs), probe real files for
    // S/E patterns to synthesize episode list. Returns the same shape as
    // ScopeEstimate would carry: detectedSeasons + per-season episode lists.
    struct DerivedScope {
        QList<int> seasons;
        QMap<int, QList<EpisodeEstimate>> episodesBySeason;
        // I2/I3 fix (code-quality review 2026-05-16): tile-key (season<<16|episode)
        // -> m_realFiles index, so addSeasonGroup doesn't re-scan files per tile.
        QMap<quint32, int> tileKeyToFileIndex;
    };
    DerivedScope deriveScopeFromFiles() const;

    UnifiedPackSearchEngine* m_searchEngine = nullptr;
    StreamDownloadIndex*     m_downloadIndex = nullptr;
    TorrentClient*           m_torrentClient = nullptr;

    // THEATRE_DOWNLOAD_OVERHAUL Task D3 (2026-05-16): real-metadata refresh
    // state. m_pendingMetadataHash is the infoHash we're awaiting from
    // TorrentEngine::metadataReady; m_realFiles holds the file list once
    // resolution completes, for D4 to consume via BulkPackVerifier.
    QString    m_pendingMetadataHash;
    QJsonArray m_realFiles;

    // I2 fix (code-quality review 2026-05-16): cache deriveScopeFromFiles
    // result keyed by m_pendingMetadataHash. Without this, every re-render
    // re-runs ~25K regex evals on Complete-Series packs (500+ files x 50
    // season probes). Invalidate on m_pendingMetadataHash change in
    // onPackRowSelected + onMetadataReady + reset.
    mutable QString m_derivedScopeCacheKey;
    mutable DerivedScope m_derivedScopeCache;

    // Current search context.
    QString m_imdbId;
    QString m_showName;
    QString m_showYear;
    int     m_season = 0;
    QString m_mediaType;

    // THEATRE_BULK_PICKER_EPISODE_COUNT_FIX 2026-05-22 — Cinemeta-known
    // per-season episode counts (season -> count). Set by openFor() from
    // StreamDetailView::episodeCountsBySeason(). Consulted by
    // rerenderScopePicker() when m_realFiles is still empty. Empty = no
    // Cinemeta data; fall through to title-estimate behavior.
    QMap<int, int> m_knownEpisodeCounts;

    // PackList state.
    QList<EnrichedPack> m_packs;
    QList<EnrichedPack> m_filteredPacks;  // post-filter view
    QString m_typeFilter   = QStringLiteral("All");
    bool    m_widenedAutoFallback = false;

    // ScopePicker state.
    EnrichedPack    m_selectedPack;
    ScopeEstimate   m_scopeEstimate;
    // Tile selection: keyed by (season << 16) | episode -> bool checked.
    // QMap for stable iteration order.
    QMap<quint32, bool> m_tileChecked;

    // UI hierarchy.
    QStackedWidget* m_stack = nullptr;   // 0: PackList, 1: ScopePicker
    QWidget*        m_packListPage = nullptr;
    QWidget*        m_scopePickerPage = nullptr;

    QLabel*         m_packHeading = nullptr;
    QComboBox*      m_sourceCombo = nullptr;
    QString         m_sourceFilter = QStringLiteral("all");
    QWidget*        m_filterChipRow = nullptr;
    QLabel*         m_statusLine = nullptr;
    QProgressBar*   m_loadingBar = nullptr;
    QListWidget*    m_packList = nullptr;

    QLabel*         m_scopeHeading = nullptr;
    QWidget*        m_scopeTileContainer = nullptr;
    QLabel*         m_scopeStatusLine = nullptr;
    QPushButton*    m_scopeBackBtn = nullptr;
    QPushButton*    m_scopeDownloadBtn = nullptr;
    QPushButton*    m_scopeCancelBtn = nullptr;
};

}  // namespace tankoban::stream::theatre
