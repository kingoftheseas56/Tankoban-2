// src/ui/pages/comics/ComicsSourcesPanel.h
#pragma once

// TANKOYOMI_VOLUME_PIVOT Phase 8 -- right-pane Sources widget inside the
// post-pivot ComicsSeriesView. Replaces the Phase 7 placeholder QLabel.
//
// Populated on volume-row click by ComicsSeriesView. Shows a ranked list of
// UnifiedSourceRow entries collapsed from three provider types:
//   - Catalog hit         (PremiumCatalog::entryForAnilistIdAndVolume)
//   - NyaaRuntime         (NyaaRuntimeSource async search)
//   - WeebCentralPacker   (synthesized fallback when chapterIds available)
//
// Ranking: stable_sort by (tier asc, seeders desc). Catalog rows insert
// first among tier=1 entries so they outrank tier-1 nyaa via the stable
// ordering boost.
//
// Click on a row emits downloadRequested; ComicsSeriesView forwards the
// signal verbatim to ComicsPage (Phase 9 owns the provider pointers + the
// actual dispatch to TorrentVolumeProvider or WeebCentralVolumePacker).

#include "core/manga/NyaaRuntimeSource.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QList>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace tankoban::manga {
class NyaaRuntimeSource;
class WeebCentralVolumePacker;
namespace premium {
class PremiumCatalog;
class TorrentVolumeProvider;
} // namespace premium
} // namespace tankoban::manga

namespace tankoban::manga::comics {

class ComicsSourceCard;

// One source row, generic across the three provider types. Stored verbatim
// inside m_rows; the panel re-renders m_list contents from m_rows on every
// renderRanked() call.
struct UnifiedSourceRow {
    enum class Kind { Catalog, NyaaRuntime, WeebCentralPacker };
    Kind     kind         = Kind::NyaaRuntime;
    int      tier         = 99;   // 1 (catalog/tier-1 nyaa) / 2 (tier-2 nyaa) / 99 (WC)
    QString  title;               // user-facing label
    QString  uploaderHint;        // e.g. "1r0n" / "VIZ Digital" / "WeebCentral"
    int      seeders      = -1;   // -1 for WC; nyaa/catalog: positive int
    qint64   sizeBytes    = 0;    // best-effort estimate; 0 when unknown (PHASE 13)
    QString  magnetUri;           // for nyaa/catalog
    QString  infoHash;            // for nyaa/catalog
    QStringList weebCentralChapterIds; // for WeebCentralVolumePacker
};

class ComicsSourcesPanel : public QWidget
{
    Q_OBJECT
public:
    // catalog + nyaa are non-owning. catalog may be null (panel works fine
    // without a catalog hit; the WC + nyaa rows still appear). nyaa may be
    // null (panel will skip the runtime-search fan-out; catalog + WC still
    // render). parent ownership is standard Qt.
    ComicsSourcesPanel(premium::PremiumCatalog* catalog,
                        NyaaRuntimeSource*       nyaa,
                        QWidget*                 parent = nullptr);

    // Clear panel and show the "Select a volume to see sources" empty state.
    void clear();

    // Populate panel with ranked sources for the given series + volume.
    // anilistSeriesId keys the catalog lookup (catalog stores anilistId on
    // PremiumCatalogEntry); seriesTitle keys the nyaa runtime search.
    // chapterIds drives the WeebCentralPacker synthesis fallback (skipped
    // when empty -- no chapter mapping means no WC pack to synthesize).
    void populate(const QString& seriesTitle,
                  int            anilistSeriesId,
                  const anilist::VolumeRow& vol,
                  const QStringList& chapterIds);

    void setContext(int volumeNumber, const QString& volumeTitle);
    void appendWeebCentralRow(int volumeNumber, const QStringList& chapterIds);

    // COMICS_WESTERN_ADD 2026-06-02 — Western (RCO) editions download via GetComics
    // in a future arc, not the manga (WeebCentral/Nyaa) pipeline. Shown instead of
    // firing those bogus searches when a Western edition is selected.
    void showComingSoon();

    QJsonObject devSnapshot() const;
    bool devDispatchSource(const QString& source, QString* errorMessage = nullptr);

signals:
    // Fired when user clicks a row in the source list. Caller dispatches to
    // the appropriate provider (Catalog/NyaaRuntime -> TorrentVolumeProvider;
    // WeebCentralPacker -> WeebCentralVolumePacker). ComicsSeriesView
    // forwards this verbatim via its own downloadDispatchRequested signal
    // for Phase 9 ComicsPage to land the dispatch.
    void downloadRequested(const UnifiedSourceRow& row,
                           const QString& seriesTitle,
                           int            anilistSeriesId,
                           int            volumeNumber,
                           const QStringList& chapterIds);

private slots:
    void onNyaaResults(int reqId, const QList<NyaaSourceCandidate>& results);
    void onNyaaFailed(int reqId, const QString& reason);

private:
    void appendRow(const UnifiedSourceRow& row);
    void clearCards();
    void setPlaceholder();
    void setLoading();
    void setSources(const QList<UnifiedSourceRow>& rows, bool nyaaStillInFlight);
    void setEmpty();
    void sortRows();
    void armAutoPickIfEligible();
    void cancelAutoPick();
    void emitTopRowDownload();
    void emitRowDownload(const UnifiedSourceRow& row);

    premium::PremiumCatalog*      m_catalog  = nullptr;  // non-owning
    NyaaRuntimeSource*            m_nyaa     = nullptr;  // non-owning
    QLabel*                       m_headerLabel = nullptr;
    QScrollArea*                  m_scroll = nullptr;
    QWidget*                      m_cardsContainer = nullptr;
    QVBoxLayout*                  m_cardsLayout = nullptr;
    QLabel*                       m_statusLabel = nullptr;
    QLabel*                       m_statusSubLabel = nullptr;
    QTimer*                       m_autoPickTimer = nullptr;

    QString                       m_currentSeriesTitle;
    int                           m_currentAnilistId  = 0;
    int                           m_currentVolNumber  = 0;
    QStringList                   m_currentChapterIds;
    int                           m_pendingNyaaReqId  = -1;
    int                           m_nextNyaaReqId     = 1;
    bool                          m_autoPickArmed = false;
    bool                          m_autoPickSuppressed = false;

    QList<UnifiedSourceRow>       m_rows;
    QList<ComicsSourceCard*>      m_cards;
};

} // namespace tankoban::manga::comics
