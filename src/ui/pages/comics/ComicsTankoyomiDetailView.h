#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/ui/pages/stream/StreamDetailView.{h,cpp} per brainstorm §6.1.
// Phase 4: preview-first hero + read-only chapter list + Add/Remove
// silent-bookmark button + offline-source banner. No chapter
// download wiring yet (Phase 5 adds ChapterDownloadIndicator + range
// modal + multi-select + MangaDownloader integration).

#include "core/manga/MangaResult.h"
#include "core/manga/MangaSeriesDetail.h"
#include "core/manga/ComicsLibraryRecord.h"
#include "core/manga/PremiumCatalogSchema.h"
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <functional>
#include <optional>

class CoreBridge;
class MangaSourceRegistry;
class MangaScraper;
class MangaDownloader;          // forward-decl; Phase 5 wires usage
class MangaDownloadIndex;        // forward-decl; Phase 5 creates the type
class ComicsTankoyomiLibrary;
class ChapterDownloadIndicator; // forward-decl; Phase 5 wires
class ChapterRangeDialog;        // forward-decl; Phase 5 wires
class QNetworkAccessManager;

namespace tankoban::manga::premium { class PremiumCatalog; }

class ComicsTankoyomiDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit ComicsTankoyomiDetailView(CoreBridge* bridge,
                                        MangaSourceRegistry* registry,
                                        ComicsTankoyomiLibrary* tyLibrary,
                                        MangaDownloader* downloader,
                                        MangaDownloadIndex* downloadIndex,
                                        QNetworkAccessManager* nam,
                                        QWidget* parent = nullptr);

    void showEntry(const MangaResult& previewHint);
    void refreshDownloadMarkers();

    // TANKOYOMI_PREMIUM Phase 6 -- inject the PremiumCatalog so the chapter-
    // table render branch can flip into volume-row mode for catalog-backed
    // titles. Set by ComicsPage right after detail-view construction. Setter
    // (vs ctor extension) keeps the existing 6-arg ctor stable.
    void setPremiumCatalog(tankoban::manga::premium::PremiumCatalog* catalog);

    // TANKOYOMI_PREMIUM Phase 9 -- adopt-existing-folder lookup callback.
    // ComicsPage injects a function that, given a Premium-catalog title,
    // returns the canonicalSeriesPath of a pre-existing folder-imported
    // series whose normalized title matches exactly, or QString() when
    // ambiguous (zero or many matches). Consulted in addCurrentToLibrary
    // BEFORE uniqueSeriesFolderName so adopting a folder bypasses
    // disambiguated new-folder creation entirely (no file move/rename).
    using AdoptLookup = std::function<QString(const QString& title)>;
    void setAdoptLookup(AdoptLookup fn);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 50 —
    // expose the in-flight preview so ComicsPage::captureNavState can
    // serialize the tankoyomiDetail-mode blob (sourceId/seriesId pair).
    const MangaResult& currentPreview() const { return m_currentPreview; }

signals:
    void backRequested();
    void openComicRequested(const QString& cbzPath,
                            const QStringList& seriesCbzList,
                            const QString& seriesName);
    // TANKOYOMI_PREMIUM Phase 7 -- emitted on Premium-volume Download button
    // click. ComicsPage resolves the catalog entry by seriesId and forwards
    // to TorrentVolumeProvider::requestVolume.
    void premiumVolumeDownloadRequested(QString seriesId, int volumeNumber);
    // Reserved for future "refresh state of one row" cross-talk between
    // ComicsPage and the detail view (e.g. on showEvent for catalog-backed
    // series). Emitted in Phase 7 only by the detail view itself for symmetry.
    void premiumVolumeStateRefreshRequested(QString seriesId, int volumeNumber);

public slots:
    // TANKOYOMI_PREMIUM Phase 7 -- repaint matching volume row in response
    // to TorrentVolumeProvider signals. Each slot early-returns if the
    // currently-shown series has no Premium entry or the seriesId mismatches
    // (the user navigated away mid-flight).
    void onPremiumVolumeProgress(const QString& seriesId, int volumeNumber, double pct);
    void onPremiumVolumeCompleted(const QString& seriesId, int volumeNumber);
    void onPremiumVolumeFailed(const QString& seriesId, int volumeNumber,
                                const QString& code, const QString& message);
    void onPremiumSwarmStatus(const QString& seriesId, int volumeNumber, int piecePeersOnline);

    // TANKOYOMI_PREMIUM Phase 10 -- per-volume cover thumbnail arrived from
    // PremiumCoverExtractor (via TorrentVolumeProvider::volumeCoverReady).
    // Walks m_chapterTable for the column-4 QPushButton whose
    // (premiumSeriesId, premiumVolume) properties match, then sets the
    // matching column-0 item's DecorationRole to QIcon(coverPath).
    void setPremiumVolumeCover(const QString& seriesId, int volumeNumber,
                                const QString& coverPath);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void buildUI();
    void renderPreviewHero(const MangaResult& preview);
    void renderDetailHero(const MangaSeriesDetail& detail);
    void renderChapters(const QList<ChapterInfo>& chapters);
    void refreshChapterMarkers();
    void applyOfflineStateToRow(int row, ChapterDownloadIndicator* ind, bool onDisk);
    void setCoverFromPath(const QString& path);
    void loadCoverFromUrl(const QString& imageUrl);
    std::optional<ComicsLibraryRecord> ensureAddedForDownload();
    std::optional<ComicsLibraryRecord> addCurrentToLibrary();
    QString uniqueSeriesFolderName(const QString& root) const;
    bool folderCandidateCollides(const QString& folderPath) const;
    bool openDownloadedChapter(const ChapterInfo& ch);
    void onAddRemoveClicked();
    void onChapterRowClicked(int row, int col);
    void onChapterContextMenu(const QPoint& pos);
    void onSeriesHeaderContextMenu(const QPoint& pos);
    // Phase 5 Task 31: per-chapter download-arrow click. Wired to
    // ChapterDownloadIndicator::clicked. Task 35 fills the body.
    void onIndicatorClicked(const ChapterInfo& ch, ChapterDownloadIndicator* ind);
    // Phase 5 Task 36: chapter-list header buttons.
    void onDownloadRangeClicked();
    void onDownloadSelectedClicked();
    void updateDownloadSelectedButton();
    // Shared dispatch path for both Range + N-selected. Auto-adds the series
    // if missing, fires Toast, then queues the picks via MangaDownloader.
    void dispatchDownload(const QList<ChapterInfo>& picks);
    void onFetchDetailReady(const MangaSeriesDetail& detail);
    void onChaptersReady(const QList<ChapterInfo>& chapters);
    void onSourceError(const QString& msg);
    bool isInLibrary() const;

    // TANKOYOMI_PREMIUM Phase 7 Task 7.3 -- filter chip behavior.
    // onFilterChanged enforces single-checked mutex across the chip row
    // and re-applies row visibility via rowMatchesActiveFilter(row).
    void onFilterChanged();
    bool rowMatchesActiveFilter(int row) const;

    // TANKOYOMI_PREMIUM Phase 6 -- volume-row variant of the chapter table.
    // renderChapters() short-circuits to populateVolumeAndLooseTailTable()
    // when premiumEntryForCurrentSeries() resolves to a catalog entry.
    std::optional<tankoban::manga::premium::PremiumCatalogEntry>
        premiumEntryForCurrentSeries() const;
    void populateVolumeAndLooseTailTable(
        const tankoban::manga::premium::PremiumCatalogEntry& entry);
    void appendLooseTailChaptersAfter(
        const QString& weebcentralSlug, const QString& lastCoveredChapterNum);

    CoreBridge*             m_bridge;
    MangaSourceRegistry*    m_registry;
    ComicsTankoyomiLibrary* m_tyLibrary;
    MangaDownloader*        m_downloader;
    MangaDownloadIndex*     m_downloadIndex;
    QNetworkAccessManager*  m_nam;

    MangaScraper* m_currentScraper = nullptr;
    MangaResult   m_currentPreview;
    std::optional<MangaSeriesDetail> m_currentDetail;
    QList<ChapterInfo> m_currentChapters;
    bool m_sourceOffline = false;

    // TANKOYOMI_PREMIUM Phase 6 -- catalog injected via setPremiumCatalog().
    // Used by premiumEntryForCurrentSeries() to detect catalog-backed titles.
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;

    // TANKOYOMI_PREMIUM Phase 9 -- adopt-existing-folder lookup. Injected
    // via setAdoptLookup. Consulted in addCurrentToLibrary before computing
    // a new disambiguated folder. Owned by ComicsPage (the only place that
    // can walk m_folderSeries).
    AdoptLookup m_adoptLookup;
    // Phase 6 stub: last covered chapter number for the WeebCentral loose-tail
    // filter. Phase 7 wires the actual fetch + filter against this threshold.
    QString m_looseTailThresholdChapterNum;

    QPushButton*  m_backBtn        = nullptr;
    QPushButton*  m_addRemoveBtn   = nullptr;
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 36 — chapter-
    // header buttons (Range modal launcher + N-selected dispatcher).
    QPushButton*  m_downloadRangeBtn    = nullptr;
    QPushButton*  m_downloadSelectedBtn = nullptr;
    QLabel*       m_coverLabel     = nullptr;
    QLabel*       m_titleLabel     = nullptr;
    QLabel*       m_metaLabel      = nullptr;
    QLabel*       m_synopsisLabel  = nullptr;
    QLabel*       m_genresLabel    = nullptr;
    QLabel*       m_offlineBanner  = nullptr;
    QTableWidget* m_chapterTable   = nullptr;

    // TANKOYOMI_PREMIUM Phase 7 Task 7.3 -- filter chip row, mutex-exclusive
    // checked state. Visible above the chapter table; mutates row visibility
    // via setRowHidden + rowMatchesActiveFilter.
    QPushButton*  m_filterAll        = nullptr;
    QPushButton*  m_filterDownloaded = nullptr;
    QPushButton*  m_filterUnread     = nullptr;
    QPushButton*  m_filterPremium    = nullptr;
    QPushButton*  m_filterLoose      = nullptr;

    // Per-session scraper connections — re-bound on every showEntry call.
    // Stored as members (not shared_ptr<Connection>) so showEntry can
    // disconnect the prior session at the top before re-wiring. Prevents
    // stale errorOccurred lambdas from a previous series painting the
    // offline banner on the current one (code-quality review C1), and
    // collapses any showEntry re-entrancy double-fire on detailReady /
    // chaptersReady (review I1).
    QMetaObject::Connection m_detailConn;
    QMetaObject::Connection m_chaptersConn;
    QMetaObject::Connection m_errConn;

    static constexpr int kColCheckbox  = 0;
    static constexpr int kColIndicator = 1;
    static constexpr int kColTitle     = 2;
    static constexpr int kColDate      = 3;
    static constexpr int kColCount     = 4;
};
