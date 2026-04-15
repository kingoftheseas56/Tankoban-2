#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QMap>
#include <QList>

#include <optional>

class QNetworkAccessManager;

#include "core/stream/MetaAggregator.h"
#include "core/stream/addon/MetaItem.h"
#include "StreamSourceChoice.h"

class CoreBridge;
class StreamLibrary;
struct StreamLibraryEntry;

namespace tankostream::stream {
class MetaAggregator;
class StreamSourceList;
}

class StreamDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit StreamDetailView(CoreBridge* bridge,
                              tankostream::stream::MetaAggregator* meta,
                              StreamLibrary* library,
                              QWidget* parent = nullptr);

    // Default invocation shows the entry without pre-selecting any episode.
    // Batch 6.2: Calendar → Detail nav passes (preselectSeason, preselectEpisode)
    // to auto-switch the season combo + focus the matching episode row once
    // series meta resolves.
    //
    // Phase 1 Batch 1.1: `previewHint` carries the MetaItemPreview from a
    // catalog/home/search tile so the detail view can paint title/year/poster/
    // description immediately without the title appearing in the library. When
    // unset (the library-tile path), the StreamLibrary::get(imdbId) lookup
    // remains the source — existing callers keep working unchanged.
    void showEntry(const QString& imdbId,
                   int            preselectSeason  = -1,
                   int            preselectEpisode = -1,
                   const std::optional<tankostream::addon::MetaItemPreview>& previewHint = std::nullopt);

    // Stream-picker UX rework — right-pane source list state transitions.
    // StreamPage drives these: `Loading` the instant the user clicks an
    // episode (or movie opens); `Sources` when StreamAggregator::streamsReady
    // fires; `Error` / `Placeholder` for edge states.
    void setStreamSourcesLoading();
    void setStreamSources(const QList<tankostream::stream::StreamPickerChoice>& choices,
                          const QString&                                        savedChoiceKey = {});
    void setStreamSourcesError(const QString& message);
    void setStreamSourcesPlaceholder(const QString& message);

    // Phase 2 Batch 2.4 — passthroughs to the embedded StreamSourceList's
    // auto-launch toast. StreamPage arms the toast when a resume-match fires
    // and hides it on cancel / navigation-away.
    void showAutoLaunchToast(const QString& label);
    void hideAutoLaunchToast();

    // Read-only accessor used by StreamPage::showDetail to dedupe back-to-back
    // open requests for the same imdbId (e.g. the single-click + double-click
    // combination that fires both TileStrip::tileSingleClicked and
    // tileDoubleClicked for one user double-click gesture).
    const QString& currentImdb() const { return m_currentImdb; }

signals:
    void backRequested();

    // Emitted when the user clicks an episode row (single-click) or a movie
    // detail opens. StreamPage listens and runs the StreamAggregator fan-out
    // to backfill the right pane via setStreamSources.
    void playRequested(const QString& imdbId, const QString& mediaType,
                       int season, int episode);

    // Emitted when the user clicks a source card in the right pane.
    // StreamPage saves the choice and hands off to StreamPlayerController.
    void sourceActivated(const tankostream::stream::StreamPickerChoice& choice);

    // Phase 2 Batch 2.4 — forwarded from StreamSourceList's Pick-different
    // button; StreamPage listens to abort the auto-launch timer.
    void autoLaunchCancelRequested();

    // Phase 3 Batch 3.5 (deferred ship) — direct-URL trailer playback.
    // StreamPage consumes this by synthesizing an ad-hoc play through
    // StreamPlayerController (same pattern as Batch 4.3 URL-paste). YouTube
    // trailers are handled directly in StreamDetailView via QDesktopServices
    // and do NOT emit this signal.
    void trailerDirectPlayRequested(const QUrl& url);

private:
    void buildUI();
    void onSeriesMetaReady(const QString& imdbId,
                           const QMap<int, QList<tankostream::stream::StreamEpisode>>& seasons);
    void onSeasonChanged(int comboIndex);
    void populateEpisodeTable(int season);
    void onEpisodeActivated(int row, int col);
    void updateProgressColumn();

    // Phase 3 Batch 3.1 — MetaItem arrival handler; paints hero image +
    // enriches the metadata chip row (runtime, genres) once fetchMetaItem
    // resolves. Best-effort: missing fields are hidden; initial paint from
    // showEntry's preview hint stays authoritative for the title/year/desc
    // surface. Filtered by imdbId match to ignore stale meta callbacks that
    // arrive after the user has navigated to a different title.
    void onMetaItemReady(const tankostream::addon::MetaItem& item);
    void applyHeroImage(const QString& imdbId, const QUrl& backgroundUrl,
                        const QUrl& posterFallbackUrl);
    void downloadBackgroundArt(const QString& imdbId, const QUrl& url,
                               bool usePosterFallback);
    QString heroCachePath(const QString& imdbId) const;
    void renderHeroPixmap(const QString& imagePath);
    void clearHero();
    void applyChips(const QString& year, const QString& runtime,
                    const QStringList& genres, const QString& rating,
                    const QString& type);

    // Phase 3 Batch 3.4 — episode thumbnail fetch + cache helpers. Reuses
    // the hero-pipeline NAM; writes to m_episodeThumbsCacheDir. Returns the
    // cached-disk-path on hit so the caller can paint synchronously.
    QString episodeThumbPath(const QString& imdbId, int season, int episode) const;
    void fetchEpisodeThumbnail(const QString& imdbId, int season, int episode,
                               const QUrl& url, QLabel* target);
    void applyEpisodeThumbnail(QLabel* target, const QString& imagePath);

    // Phase 3 Batch 3.2 (deferred ship) — director + cast row population.
    // Walks MetaItem.preview.links filtering on category=="Director"/"Cast";
    // renders a single "Director: X · Cast: A, B, C..." row with elided
    // overflow on the cast list.
    void applyCastDirector(const QList<tankostream::addon::MetaLink>& links);

    // Phase 3 Batch 3.5 (deferred ship) — trailer button state. Walks
    // MetaItem.preview.trailerStreams, prefers Url/Http kinds (direct play),
    // falls back to first YouTube-kind (browser open). Hides button when
    // neither is available.
    void applyTrailerButton(
        const QList<tankostream::addon::Stream>& trailerStreams);
    void onTrailerClicked();

    // Phase 3 Batch 3.3 — description clamp + show-more toggle.
    // `setDescription` rewrites text, resets expanded state, runs
    // `updateDescriptionClamp` to decide whether the toggle button should
    // surface. `updateDescriptionClamp` computes whether the text overflows
    // the 3-line height at the label's current width via QFontMetrics.
    void setDescription(const QString& text);
    void updateDescriptionClamp();
    void onDescShowMoreClicked();

    CoreBridge*                        m_bridge;
    tankostream::stream::MetaAggregator* m_meta;
    StreamLibrary*                     m_library;

    // Current state
    QString m_currentImdb;
    QString m_currentType;
    QMap<int, QList<tankostream::stream::StreamEpisode>> m_seasons;

    // UI — left column
    QPushButton*  m_backBtn       = nullptr;
    QLabel*       m_titleLabel    = nullptr;
    // 2026-04-15 — removed m_infoLabel. Chips row (year + type + rating)
    // conveys the same data; the info line was redundant and eats
    // vertical space from the episode table below.
    QLabel*       m_descLabel     = nullptr;
    // Phase 3 Batch 3.3 — "Show more / Show less" toggle under the
    // description. Hidden when the description fits within the 3-line clamp;
    // shown when clamping hides content. Click toggles m_descExpanded.
    QPushButton*  m_descShowMoreBtn = nullptr;
    bool          m_descExpanded    = false;

    // Phase 3 Batch 3.2 (deferred → shipped later) — director + cast row
    // rendered as a single word-wrapped QLabel below the description.
    // Hidden when both fields are empty.
    QLabel*       m_castDirectorLabel = nullptr;

    // Phase 3 Batch 3.1 — hero background art (full-width, 240px tall)
    // sits above the left/right columns. Fallback chain:
    //   1. MetaItem.preview.background (from fetchMetaItem)
    //   2. Poster (preview hint or library entry) — scaled + darkened
    //   3. Hidden if neither is available
    QLabel*       m_heroLabel     = nullptr;
    // Phase 3 Batch 3.1 — chip row replacing the old single-line m_infoLabel.
    // Individual chips hide themselves when their field is empty. m_infoLabel
    // remains as the first-paint surface (from showEntry's preview hint);
    // chips are populated by onMetaItemReady once the richer meta lands.
    QWidget*      m_chipsRow      = nullptr;
    QLabel*       m_chipYear      = nullptr;
    QLabel*       m_chipRuntime   = nullptr;
    QLabel*       m_chipGenres    = nullptr;
    QLabel*       m_chipRating    = nullptr;
    QLabel*       m_chipType      = nullptr;
    // Phase 1 Batch 1.2 — Add/Remove Library toggle in the header area.
    // Text + styling refresh on every showEntry + on libraryChanged. Phase 3
    // Batch 3.1 will restyle when the hero image lands.
    QPushButton*  m_libraryBtn    = nullptr;
    // Phase 3 Batch 3.5 (deferred ship) — Watch Trailer button. Visible
    // only when the current MetaItem has a Url/Http-kind trailer OR a
    // YouTube-kind trailer. Direct-URL trailers play in-app via an emitted
    // signal; YouTube opens in the default browser.
    QPushButton*  m_trailerBtn    = nullptr;
    QUrl          m_currentTrailerDirectUrl;   // populated from Url/Http trailer
    QString       m_currentTrailerYouTubeId;   // populated from YouTube trailer
    QWidget*      m_seasonRow     = nullptr;
    QComboBox*    m_seasonCombo   = nullptr;
    QTableWidget* m_episodeTable  = nullptr;
    QLabel*       m_statusLabel   = nullptr;

    // UI — right column (stream-picker UX rework)
    QLabel*                                m_sourcesHeader = nullptr;
    tankostream::stream::StreamSourceList* m_sourcesList   = nullptr;

    // Batch 6.2 — preselection staged between showEntry and onSeriesMetaReady.
    // Consumed once, then cleared so a second showEntry without preselect
    // doesn't re-apply stale values.
    int m_pendingPreselectSeason  = -1;
    int m_pendingPreselectEpisode = -1;

    // Phase 1 Batch 1.2 — MetaItemPreview from the most-recent showEntry
    // call. Used when the user clicks "Add to Library" from a non-library
    // title — we construct a StreamLibraryEntry from these fields.
    std::optional<tankostream::addon::MetaItemPreview> m_lastPreviewHint;

    // Phase 3 Batch 3.1 — network manager + on-disk cache for background art.
    // Shape mirrors StreamLibraryLayout's poster cache:
    //   {AppLocalDataLocation}/Tankoban/data/stream_backgrounds/{imdb}.jpg
    // Shared across all titles; NAM reused for the view lifetime.
    QNetworkAccessManager* m_nam             = nullptr;
    QString                m_heroCacheDir;
    // Phase 3 Batch 3.4 — episode-thumbnail disk cache:
    //   {AppLocalDataLocation}/Tankoban/data/stream_episode_thumbnails/
    //       {imdb}_{season}_{episode}.jpg
    // Same NAM as the hero fetcher; download lifetimes are QPointer-guarded
    // against view destruction + imdb-mismatch stale-callback guarded.
    QString                m_episodeThumbsCacheDir;

private:
    void refreshLibraryButton();
    void onLibraryButtonClicked();
};
