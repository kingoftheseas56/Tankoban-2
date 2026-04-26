#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
class SeekSlider;
#include <QTimer>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>

#include "SyncClock.h"

#include <QHash>
#include <QList>

#include "core/stream/addon/SubtitleInfo.h"

class CoreBridge;
class KeyBindings;
class SidecarProcess;
class ShmFrameReader;
class FrameCanvas;
class VolumeHud;
class CenterFlash;
class PlaylistDrawer;
class StatsBadge;
class ToastHud;
class SubtitleOverlay;
class SubtitlePopover;
class AudioPopover;
class SettingsPopover;

class VideoPlayer : public QWidget {
    Q_OBJECT

public:
    explicit VideoPlayer(CoreBridge* bridge, QWidget* parent = nullptr);
    ~VideoPlayer() override;

    // Phase 1 Batch 1.3 (STREAM_UX_PARITY) — `startPositionSec` (seconds)
    // overrides the PersistenceMode-gated library-progress resume lookup.
    // Callers who already have a resume target in hand (Stream mode reads
    // from the "stream" progress domain rather than "videos") pass it here
    // and the seek lands regardless of PersistenceMode. 0.0 preserves
    // existing behavior byte-for-byte.
    void openFile(const QString& filePath,
                  const QStringList& playlist = {},
                  int playlistIndex = 0,
                  double startPositionSec = 0.0,
                  const QString& displayTitle = {});

    // PLAYER_LIFECYCLE_FIX Phase 3 Batch 3.1 — isIntentional distinguishes
    // user-driven stops (Escape, close, APPCOMMAND_MEDIA_STOP — default)
    // from crash-recovery paths that need `m_currentFile` / `m_pendingFile`
    // / `m_lastKnownPosSec` preserved for resume. Intentional=true clears
    // all identity state so a post-stop `onSidecarReady` event cannot re-
    // open the just-closed file (audit P1-5). Default preserves existing
    // call-site semantics — all present callers are user-close paths.
    void stopPlayback(bool isIntentional = true);

    // Persistence mode — gates the three "videos"-domain bridge reads/writes
    // inside VideoPlayer (resume position read, progress write, track-
    // preference restore read) so Stream-mode playback doesn't pollute the
    // Videos-mode continue-watching store.
    //
    // LibraryVideos (default) preserves pre-change behavior byte-for-byte:
    // Videos-mode hot path saves/reads exactly as before.
    //
    // None skips all three "videos" bridge calls but keeps the
    // progressUpdated(path, positionSec, durationSec) signal firing —
    // StreamPage listens and writes into the "stream" domain itself.
    //
    // Caller-driven reset: callers that switch VideoPlayer to None must
    // reset to LibraryVideos when they're done (typical pattern:
    // StreamPage sets None before openFile, resets to LibraryVideos on
    // close). VideoPlayer does not auto-reset across openFile boundaries.
    //
    // Opened for Agent 4's Tankostream Phase 5 stream→videos continue-
    // watching leak fix (HELP.md 2026-04-14; three-site leak traced to
    // VideoPlayer.cpp:234/:1223/:1259).
    enum class PersistenceMode { LibraryVideos, None };
    void setPersistenceMode(PersistenceMode mode);
    PersistenceMode persistenceMode() const { return m_persistenceMode; }

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 — stream-mode flag that
    // gates the SeekSlider buffered-range overlay paint (Batch 1.4). True
    // for torrent-backed stream playback; false for library-file playback
    // (local files have no buffer state — painting a "buffered" layer over
    // a fully-on-disk file would be meaningless noise). Separate from
    // PersistenceMode because the two flags serve orthogonal concerns:
    // PersistenceMode gates library-progress save/read, setStreamMode
    // gates buffered-range rendering. Coupling them via a single enum
    // would pollute PersistenceMode's 8+ check-sites
    // (VideoPlayer.cpp:341/:362/:394/:2384/:2425/:2458/:2579) with
    // rendering concerns they shouldn't know about.
    // StreamPage calls setStreamMode(true) before openFile for stream
    // playback + setStreamMode(false) on close / file-switch, mirroring
    // its existing setPersistenceMode bookend discipline.
    void setStreamMode(bool on);
    bool streamMode() const { return m_streamMode; }

    // STREAM_STALL_UX_FIX Batch 1 — mid-playback stall gate for the HUD.
    // StreamPage pulls StreamEngineStats.stalled each progressUpdated tick
    // (~1 Hz) and pushes it here. When true, onTimeUpdate skips positionSec
    // writes to the seek slider + time label so the HUD doesn't lie during
    // a stall (sidecar keeps emitting timeUpdate from the audio PTS clock
    // while the video decoder is dry — the decoupled clock creates the
    // "screen frozen but timer ticking" illusion Hemanth reported
    // 2026-04-21). Duration label + crash-recovery last-known-position +
    // saveProgress all stay unconditional — the gate is specifically on
    // positionSec, which is the only HUD surface that misreads reality
    // during a stall. Defaults false; library mode never touches this.
    void setStreamStalled(bool stalled);
    bool streamStalled() const { return m_streamStalled; }

    // STREAM_AV_SUB_SYNC_AFTER_STALL 2026-04-21 — edge-driven sidecar IPC
    // forwarder. Called from StreamPage when StreamEngine emits
    // stallDetected (detected=true) or stallRecovered (detected=false).
    // Routes to m_sidecar->sendStallPause / sendStallResume which
    // implements Option A (mpv paused-for-cache) in the sidecar: freezes
    // AVSyncClock + halts PortAudio writes on pause; re-anchors clock to
    // current video PTS via seek_anchor on resume. Separate from
    // setStreamStalled (which is polling-based + drives HUD/overlay) so
    // the sidecar IPC fires on the actual edge (~2s stall-watchdog
    // cadence) instead of the ~4s worst-case polling latency. Both
    // paths coexist; setStreamStalled remains the authority for UI
    // state, onStreamStallEdgeFromEngine is the authority for decoder
    // coordination.
    void onStreamStallEdgeFromEngine(bool detected);

    // STREAM_AUTO_NEXT_ESTIMATE_FIX 2026-04-21 — expose the sidecar handle
    // so StreamPage can connect to SidecarProcess::nearEndEstimate from
    // its onReadyToPlay wiring. No ownership transfer; pointer is managed
    // by VideoPlayer's own lifecycle. May be null during pre-session /
    // post-teardown — callers must null-check.
    SidecarProcess* sidecarProcess() const { return m_sidecar; }

    // STREAM_STALL_UX_FIX Batch 2 — per-tick enrichment for the stall
    // overlay text. Pushed from StreamPage alongside setStreamStalled on
    // every progressUpdated tick while StreamEngineStats.stalled is true.
    // Forwards to LoadingOverlay::setStallDiagnostic which repaints in
    // place when the overlay is currently showing Stage::Buffering. No-op
    // before setStreamStalled(true) has shown the overlay (stall cache on
    // LoadingOverlay side updates silently until the overlay appears).
    void setStreamStallInfo(int piece, int peerHaveCount);

    // REPO_HYGIENE Phase 3 (2026-04-26) — dev-control bridge snapshot.
    // Returns the player's load-bearing state as a JSON object for the
    // `get_player` command. Pure read; no behavior change. Per §6 P3.3
    // spec — exposes m_currentFile / m_pendingFile / m_openPending /
    // m_paused / m_streamMode / m_persistenceMode / m_streamStalled /
    // m_currentAspect / m_currentCrop / m_durationSec /
    // m_lastKnownPosSec / m_sidecarRetryCount / m_firstFrameSeen + stats.
    QJsonObject devSnapshot() const;

public slots:
    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 — slot consumed by
    // StreamPlayerController::bufferedRangesChanged signal. Forwards the
    // ranges to the SeekSlider overlay when m_streamMode is active;
    // defensive guard drops calls in library mode so a stale connection
    // doesn't paint over a library file. infoHash parameter unused by
    // VideoPlayer directly (we don't care which torrent) but preserved
    // for diagnostic traceability — agent-side log readers can correlate
    // renders with hash across multi-stream scenarios.
    void onBufferedRangesChanged(const QString& infoHash,
                                 const QList<QPair<qint64, qint64>>& ranges,
                                 qint64 fileSize);

    // Batch 5.3 (Tankostream Phase 5) — StreamPage pushes addon-fetched
    // subtitle tracks here after SubtitlesAggregator finishes for the
    // currently-playing stream. Forwarded to the SubtitleMenu. Passing
    // empty lists clears the external section.
    void setExternalSubtitleTracks(
        const QList<tankostream::addon::SubtitleTrack>& tracks,
        const QHash<QString, QString>& originByTrackKey);

signals:
    void closeRequested();
    void fullscreenRequested(bool enter);
    void progressUpdated(const QString& path, double positionSec, double durationSec);

    // PLAYER_UX_FIX Phase 1.2 — sidecar lifecycle plumbing. Fired when the
    // sidecar reports `state_changed{opening}` (right after handle_open
    // ack'd the open command; probe + decoder setup in flight) and
    // `state_changed{idle}` (decoder torn down via eof / stop / open
    // failure). Phase 2.3 will connect these to a Loading HUD widget; for
    // Phase 1 they fire with debug logs only so the timing can be
    // verified empirically in _player_debug.txt.
    void playerOpeningStarted(const QString& filename);
    void playerIdle();
    // STREAM_UX_PARITY Batch 2.6 — emitted when the Shift+N
    // `stream_next_episode` binding fires. Stream mode (StreamPage)
    // connects to this; local-playlist playback ignores it. VideoPlayer
    // stays agnostic of stream-specific logic; receiver decides whether
    // a next episode exists and plays it.
    void streamNextEpisodeRequested();

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.2 — re-emission of SidecarProcess
    // classified open-pipeline events for downstream consumers outside
    // the player domain (Batch 1.3: StreamPlayerController cross-correlates
    // probe/decoder timings with StreamEngineStats for stream-mode-specific
    // diagnostic log lines — Agent 4's surface, future consumer). Internal
    // LoadingOverlay stage transitions (Phase 2.1) also trigger off these
    // same sidecar-level signals via lambda connects in setupUi, so this
    // signal layer is pure pass-through for external consumers — adding
    // it now keeps the external contract stable when Batch 1.3 lands.
    void probeStarted();
    void probeDone();
    void decoderOpenStarted();
    void decoderOpenDone();
    void firstPacketRead();
    void firstDecoderReceive();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // VIDEO_PLAYER_FIX Batch 4.3 — drag/drop handling. Accepts URL lists
    // (local files + remote schemes) and URL-shaped text. Classifies
    // video vs subtitle by extension and dispatches per plan-file rules.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void buildUI();

    // PLAYER_LIFECYCLE_FIX Phase 2 — UI-only teardown. Detaches canvas/
    // reader + clears cached track lists + stops restart-retry timer.
    // Does NOT touch the sidecar process. Called unconditionally from
    // both openFile (file-switch path) and stopPlayback (user-close
    // path); the process-teardown step is what differs between them.
    void teardownUi();

    // PLAYER_UX_FIX Phase 6.4 — cross-chip popover exclusion helper.
    // VIDEO_HUD_MINIMALIST 2026-04-25 reshape: now hides whichever of
    // m_subtitlePopover / m_audioPopover / m_settingsPopover /
    // m_playlistDrawer is currently open, EXCEPT the one passed as
    // `keep` (may be nullptr to dismiss all). Called from each chip
    // click handler before toggling that chip's own popover, and from
    // keyPressEvent's ESC handler with keep=nullptr. Also syncs the
    // corresponding chip's :checked state to false when a popover is
    // force-hidden here.
    void dismissOtherPopovers(class QWidget* keep);

    // Phase 6.1 — called from openFile (enable=true) and teardownUi
    // intentional-stop (enable=false) to reflect file-open state on the
    // chip :disabled pseudo-state.
    void setChipsEnabled(bool enable);

    void togglePause();
    void toggleFullscreen();
    void toggleMute();
    void adjustVolume(int delta);
    void speedUp();
    void speedDown();
    void speedReset();

    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — true if any of the
    // three bottom-HUD popovers is currently visible. Called from
    // hideControls (early-return guard so the HUD doesn't fade while
    // a popover is open and the user is mid-task). Playlist drawer is
    // its own widget class with its own auto-hide semantics; not
    // included here.
    bool isAnyPopoverOpen() const;

    // VIDEO_HUD_MINIMALIST 2026-04-25 — single dispatch path for
    // audio + subtitle delay adjustments. Called from both the keyboard
    // action handlers (Ctrl+= / Ctrl+- / Ctrl+0 for audio; ./, for sub)
    // and the SettingsPopover's +/- button signal connections.
    //  - adjustAudioDelay: delta is +/- 50 ms; resets to 0 when delta
    //    sentinel is treated by callers (keyboard reset clears
    //    m_audioDelayMs directly before calling). Persists per-Bluetooth-
    //    device under m_audioDeviceKey + "/manual" flag.
    //  - adjustSubDelay: delta is +/- 100 ms; delta == 0 clears
    //    m_subDelayMs absolutely (reset sentinel). Session-only state
    //    (also flows through saveShowPrefs).
    //  - adjustSubPosition: delta is +/- 5 percent; absolute value
    //    clamped 0..100 (default 100 = bottom, mpv `sub-pos` parity);
    //    persisted under "videoPlayer/subtitlePosition" QSettings key.
    void adjustAudioDelay(int delta);
    void adjustSubDelay(int delta);
    void adjustSubPosition(int delta);
    void cycleAudioTrack();
    void cycleSubtitleTrack();
    void toggleSubtitles();

    // VIDEO_PLAYER_FIX Batch 3.1 — Always-on-top toggle. Flips
    // Qt::WindowStaysOnTopHint on the top-level window (retrieved via
    // window()), re-shows it (Qt requires setWindowFlag + show() for
    // runtime changes), persists to QSettings key "player/alwaysOnTop",
    // shows a toast. Applied at startup from the saved setting.
    void toggleAlwaysOnTop();

    // VIDEO_PLAYER_FIX Batch 3.2 — Take snapshot of current displayed
    // frame. Saves PNG to `{Pictures}/Tankoban Snapshots/{baseName}_
    // {HH-MM-SS}_{ptsSec}s.png` at native video resolution. Includes
    // burned-in subs (sidecar-rendered libass/PGS). Toasts the path on
    // success. Handles both SHM and D3D11 zero-copy sources via
    // FrameCanvas::captureCurrentFrame.
    void takeSnapshot();

    // VIDEO_PLAYER_FIX Batch 3.3 — Picture-in-picture. Shipped as the
    // mini-mode fallback path per the TODO's scope clause: shrinks the
    // top-level window to ~320x180, strips the frame, pins always-on-top,
    // parks bottom-right of the current screen, hides the HUD. No second
    // FrameCanvas / second D3D11 pipeline. Toggles. Enter/exit preserves
    // pre-PiP geometry + window flags. Escape exits PiP (takes priority
    // over the normal back-to-library binding while m_inPip is true).
    void togglePictureInPicture();
    bool isInPip() const { return m_inPip; }

    // VIDEO_PLAYER_FIX Batch 4.1 — show the Open URL dialog; accepted URL
    // routes through the existing openFile() path. Sidecar handles HTTP
    // URLs natively (payload["path"] preserved as-is rather than run
    // through QDir::toNativeSeparators per SidecarProcess.cpp:118-120).
    void showOpenUrlDialog();

    // VIDEO_PLAYER_FIX Batch 4.3 — append a file to the active playlist
    // (PlaylistDrawer rebuilds on `populate`). Safe to call with an empty
    // playlist; no-op if the path is already queued adjacently. Emits no
    // signal — caller is responsible for the "Added to queue" toast.
    void appendToQueue(const QString& filePath);

    // VIDEO_PLAYER_FIX Batch 5.2 — playlist save/load. `saveQueue` writes
    // the current m_playlist as a UTF-8 .m3u file with #EXTM3U header +
    // per-entry #EXTINF metadata. `loadQueue` opens a .m3u / .pls and
    // either replaces or appends (prompted when playlist non-empty) via
    // the existing openFile/appendToQueue paths.
    void saveQueue();
    void loadQueue();

    // VIDEO_PLAYER_FIX Batch 6.1 — opens the KeybindingEditor modal. Bound
    // to the `show_shortcuts` action (? key) and the context-menu entry.
    // Replaces the old static ShortcutsOverlay reference card with a
    // live editor backed by the shared KeyBindings instance.
    void openKeybindingEditor();

    // VIDEO_PLAYER_FIX Batch 7.1 — toggle the compact stats badge. State
    // persists to QSettings("player/showStats"); applied at startup from
    // the saved value. Bound to `toggle_stats` action (I key) + context
    // menu entry.
    void toggleStatsBadge();

    // Batch 1.2 (VIDEO_PLAYER_FIX Phase 1) — canonical subtitle-Off path.
    // All three Off entry points (context menu, TrackPopover, SubtitleMenu
    // via SidecarProcess::sendSetSubtitleTrack(-1)) route through visibility-
    // only semantics: sendSetSubVisibility(false) + m_subsVisible=false + toast.
    // No set_tracks command is sent — prevents the std::stoi("off") sidecar
    // crash at main.cpp:850. Track selection is preserved so picking a
    // numeric track later re-enables the previously-selected stream.
    void setSubtitleOff();
    void prevEpisode();
    void nextEpisode();
    void togglePlaylistDrawer();
    void onTracksChanged(const QJsonArray& audio, const QJsonArray& subtitle,
                         const QString& activeAudioId, const QString& activeSubId);
    void updatePlayPauseIcon();
    void updateEpisodeButtons();
    void showControls();
    void hideControls();
    // Subtitle baseline lift in physical pixels (~6% of canvas height,
    // Netflix/YouTube safe-zone). Floor for setSubtitleLift in both
    // showControls + hideControls so subs never sit flush against the
    // frame bottom regardless of the underlying ASS script's MarginV.
    int subtitleBaselineLiftPx() const;

    // Sidecar event handlers
    void onSidecarReady();
    void onFirstFrame(const QJsonObject& payload);
    void onTimeUpdate(double positionSec, double durationSec);
    void onStateChanged(const QString& state);
    void onEndOfFile();
    void onError(const QString& message);

    // Batch 6.1 (Player Polish Phase 6) — sidecar crash auto-restart.
    // onSidecarCrashed reacts to SidecarProcess::processCrashed; schedules
    // a respawn via m_sidecarRestartTimer with exponential backoff
    // (250/500/1000 ms for retries 0/1/2). restartSidecar executes the
    // scheduled respawn — reopens m_currentFile at m_lastKnownPosSec.
    // m_sidecarRetryCount clears on onFirstFrame (recovery confirmed) or
    // openFile (fresh user intent).
    void onSidecarCrashed(int exitCode, QProcess::ExitStatus status);
    void restartSidecar();

    void saveProgress(double positionSec, double durationSec);
    void restoreTrackPreferences();
    void sendCanvasSizeToSidecar();
    // Update m_titleLabel text with an ellipsis-elided version of
    // m_fullTitle fit to the label's current width. Called on openFile
    // (title change) and on resizeEvent (width change).
    void updateTitleElision();

    // Scan m_currentFile's parent folder for a sibling subtitle file
    // (same basename, common sub extension — priority ass > srt > ssa >
    // vtt > sub). If found, sendLoadExternalSub to the sidecar. Called
    // from onTracksChanged when the tracks_changed event reports zero
    // embedded sub streams AND auto-load hasn't already fired for this
    // open. Gated on QSettings("video_sub_auto_load", default true).
    void tryAutoLoadSiblingSubtitle();

    // Per-show preference persistence. "shows" CoreBridge domain, keyed
    // by m_currentShowId (parent folder path). Read-modify-write so a
    // single field mutation doesn't clobber unrelated fields in the
    // record. Gated on PersistenceMode::LibraryVideos. Called on user-
    // explicit action sites (aspect menu, track popover, visibility
    // toggle) — never from progress-tick saveProgress.
    void saveShowPrefs();
    QJsonObject loadShowPrefs() const;

    // VIDEO_PLAYER_FIX Batch 4.2 — persist a just-opened path/URL to the
    // recents QSettings list. Dedupes; prepends; caps at 20.
    void pushRecentFile(const QString& filePath);
    static QString langForTrackId(const QJsonArray& tracks, const QString& id);
    static QString findTrackByLang(const QJsonArray& tracks, const QString& lang);
    static QString videoIdForFile(const QString& filePath);
    static QString showIdForFile(const QString& filePath);
    static double  aspectStringToDouble(const QString& token);
    static double  cropStringToDouble(const QString& token);
    static QString formatTime(qint64 ms);
    static QIcon iconFromSvg(const QByteArray& svg, int size = 20);

    CoreBridge*     m_bridge    = nullptr;
    KeyBindings*    m_keys      = nullptr;

    // Components
    SidecarProcess* m_sidecar   = nullptr;
    ShmFrameReader* m_reader    = nullptr;
    FrameCanvas*    m_canvas    = nullptr;

    // Batch 1.2 — master A/V clock. VideoPlayer owns it; FrameCanvas gets
    // a pointer via setSyncClock() in buildUI(). Today only FrameCanvas
    // writes to it (reportFrameLatency after each Present). Phase 4 will
    // read accumulated velocity to drive a sidecar audio-speed adjustment
    // command. Lives here (not AudioDecoder, which is dead code) because
    // VideoPlayer is the live orchestrator.
    SyncClock       m_syncClock;
    VolumeHud*        m_volumeHud        = nullptr;
    CenterFlash*      m_centerFlash      = nullptr;
    // PLAYER_UX_FIX Phase 2.3 — centered overlay for "Loading — filename"
    // (during sidecar opening window) and "Buffering…" (during stream
    // HTTP stall). Bound to playerOpeningStarted / playerIdle (Phase 1.2)
    // and SidecarProcess::bufferingStarted / bufferingEnded (Phase 2.2),
    // plus firstFrame as the primary dismiss trigger.
    class LoadingOverlay* m_loadingOverlay = nullptr;
    PlaylistDrawer*   m_playlistDrawer   = nullptr;
    StatsBadge*       m_statsBadge       = nullptr;
    ToastHud*         m_toastHud         = nullptr;
    SubtitleOverlay*  m_subOverlay       = nullptr;
    // VIDEO_HUD_MINIMALIST 2026-04-25 — three icon-only chips replace
    // the prior {1.0x, Filters, EQ, Tracks} cluster. SubtitlePopover
    // also absorbs the Tankostream addon-fetched external-subs +
    // load-from-file logic that used to live in SubtitleMenu.
    SubtitlePopover*  m_subtitlePopover  = nullptr;
    AudioPopover*     m_audioPopover     = nullptr;
    SettingsPopover*  m_settingsPopover  = nullptr;

    // Current/pending file
    QString     m_currentFile;
    QString     m_currentVideoId;
    // Parent-folder-path key (normalized, lowercased on Windows) used to
    // look up / write the per-show preference record in the "shows"
    // CoreBridge domain. Treats every folder as one "show" — per-season
    // TV layouts get independent prefs per season (intended). Reorg or
    // drive-letter remap invalidates the key; accepted trade-off.
    QString     m_currentShowId;
    QStringList m_playlist;
    int         m_playlistIdx = 0;
    QString     m_pendingFile;
    double      m_pendingStartSec = 0.0;

    // PLAYER_LIFECYCLE_FIX Phase 3 Batch 3.2 — one-shot pending-open token.
    // Set true when openFile or restartSidecar queues a pending open that
    // will be dispatched by onSidecarReady (cold-start path, crash-recovery
    // respawn, timeout-fallback reset). Consumed (cleared) inside
    // onSidecarReady before the sendOpen call. Defensively cleared on
    // intentional stopPlayback. Guards against a spurious onSidecarReady
    // event re-opening a just-closed file in the race window between
    // user-close and sidecar tear-down (audit P1-5 companion to the
    // identity-state clear in 3.1).
    bool m_openPending = false;

    // Controls
    QWidget*     m_controlBar      = nullptr;
    // Row 1 (seek)
    QLabel*      m_timeLabel       = nullptr;
    QPushButton* m_seekBackBtn     = nullptr;
    SeekSlider*  m_seekBar         = nullptr;
    QPushButton* m_seekFwdBtn      = nullptr;
    QLabel*      m_durLabel        = nullptr;
    // Row 2 (controls)
    QPushButton* m_backBtn         = nullptr;
    QPushButton* m_prevEpisodeBtn  = nullptr;
    QPushButton* m_playPauseBtn    = nullptr;
    QPushButton* m_nextEpisodeBtn  = nullptr;
    // Video title — subtle, left-aligned in the space between the play
    // controls and the chip row. Elides with ellipsis when too long for
    // the available width. Source: QFileInfo(m_currentFile).completeBaseName()
    // set on each openFile; cleared on teardownUi.
    QLabel*      m_titleLabel      = nullptr;
    QString      m_fullTitle;
    // VIDEO_HUD_MINIMALIST 2026-04-25 — Speed/Filters/EQ/Tracks chips
    // removed. Z/X/C still adjust speed via keyboard with toast
    // feedback. Filters + EQ functionality dropped per Hemanth (player
    // shows "original audio and video qualities").
    QPushButton* m_subtitleChip    = nullptr;
    QPushButton* m_audioChip       = nullptr;
    QPushButton* m_settingsChip    = nullptr;
    QPushButton* m_playlistChip    = nullptr;
    QLabel*      m_timeBubble     = nullptr;

    // Icons
    QIcon m_playIcon;
    QIcon m_pauseIcon;
    QIcon m_backIcon;
    QIcon m_prevEpIcon;
    QIcon m_nextEpIcon;
    QIcon m_seekBackIcon;
    QIcon m_seekFwdIcon;

    // State
    PersistenceMode m_persistenceMode = PersistenceMode::LibraryVideos;
    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 — gates buffered-range
    // overlay rendering in SeekSlider. False default = library mode (no
    // overlay). StreamPage toggles via setStreamMode around openFile.
    bool   m_streamMode = false;
    // STREAM_STALL_UX_FIX Batch 1 — cached stall flag pushed from StreamPage
    // via setStreamStalled each progressUpdated tick. Gates positionSec
    // writes in onTimeUpdate.
    bool   m_streamStalled = false;
    // STREAM_STALL_UX_FIX Batch 2 — overlay ownership tracking. Set true
    // when the stream-engine stall flipped false→true and we called
    // showBuffering; reset + dismiss on the true→false transition.
    // Separate from sidecar buffering tracking (m_sidecarBuffering) so we
    // don't dismiss an overlay the sidecar is also holding up.
    bool   m_streamStallOverlayOwner = false;
    // STREAM_STALL_UX_FIX Batch 2 — tracked sidecar HTTP stall state so
    // our stream-engine stall-clear path doesn't prematurely dismiss the
    // overlay when sidecar independently still wants it visible. Flipped
    // by connects to SidecarProcess::bufferingStarted / bufferingEnded.
    bool   m_sidecarBuffering = false;

    // STREAM_STALL_RECOVERY_UX investigation 2026-04-22 — Direction C
    // instrumentation. Dedupes [STALL_DEBUG] piece_change log so long stalls
    // on the same piece don't spam. Reset not required — int field;
    // negative sentinel means "never logged".
    int    m_lastLoggedStallPiece = -1;
    bool   m_isHdr      = false;
    bool   m_paused     = false;
    bool   m_seeking    = false;
    bool   m_fullscreen = false;
    bool   m_muted      = false;
    int    m_volume     = 100;   // 0–200 (Batch 4.2: amp zone 101–200 = +6dB max)
    int    m_speedIdx   = 2;     // index into speed presets (1.0x)
    int    m_subDelayMs = 0;    // accumulated subtitle delay in ms
    int    m_audioDelayMs = 0;  // user-configurable audio delay (Bluetooth comp)
    int    m_subPositionPct = 100; // subtitle vertical position (0=top, 100=bottom); mpv sub-pos parity
    QString m_audioDeviceKey;   // QSettings key for current device's offset
    bool   m_subsVisible = true;
    // VIDEO_PLAYER_FIX Batch 3.1 — persisted across app restart via
    // QSettings("player/alwaysOnTop"). Applied in the constructor after
    // the widget is constructed so the top-level window honors it from
    // first paint.
    bool   m_alwaysOnTop = false;

    // Current aspect-ratio override token ("original"/"4:3"/"16:9"/
    // "2.35:1"/"1.85:1"). Reset to "original" on every openFile so the
    // user's ad-hoc override on a prior file doesn't persist. Drives
    // the context-menu Aspect Ratio submenu check mark.
    QString m_currentAspect = QStringLiteral("original");
    // Current crop-target token ("none"/"16:9"/"2.35:1"/"2.39:1"/"1.85:1"/
    // "4:3"). Zooms the video to eliminate baked letterbox strips — see
    // FrameCanvas::setCropAspect. Persists per-show / per-file alongside
    // aspectOverride.
    QString m_currentCrop = QStringLiteral("none");

    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge state. Source metadata
    // stashed from sidecar firstFrame event; drops polled from
    // FrameCanvas on a 1 Hz ticker when the badge is visible.
    bool    m_showStats    = false;
    QString m_statsCodec;
    int     m_statsWidth   = 0;
    int     m_statsHeight  = 0;
    double  m_statsFps     = 0.0;
    QTimer  m_statsTicker;

    // VIDEO_PLAYER_FIX Batch 3.3 — PiP mini-mode state. m_inPip is the
    // live mode flag; the remaining members capture pre-PiP state so
    // toggling back can restore the user's window exactly as they had
    // it (including any pre-existing always-on-top / fullscreen state).
    // m_pipDragOrigin is a sentinel for "no drag in progress" (-1, -1).
    bool          m_inPip             = false;
    QRect         m_prePipGeometry;
    Qt::WindowFlags m_prePipFlags     = Qt::WindowFlags{};
    bool          m_prePipFullscreen  = false;
    QPoint        m_pipDragOrigin     = QPoint(-1, -1);
    double m_durationSec = 0.0;
    // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — pushed from
    // SidecarProcess::probeDone signal. When true, m_durLabel prefixes
    // the formatted duration with `~` (tilde) to honestly signal that
    // the value is a bitrate × fileSize estimate (~10-50% VBR error)
    // rather than ground-truth container/stream duration. False by
    // default + reset in teardownUi on session boundary.
    bool   m_durationIsEstimate = false;
    int    m_pendingSeekVal = 0;
    int    m_seekDragOrigin = -1;

    // Track lists from sidecar
    QJsonArray m_audioTracks;
    QJsonArray m_subTracks;
    // restoreTrackPreferences runs on the FIRST tracks_changed per file
    // only. Re-running on every subsequent event would override manual
    // picks — user picks sub 3, sidecar echoes tracks_changed, preference
    // match overrides back to the preferred-language track. Reset on
    // openFile so each new file re-applies preferences once.
    bool       m_tracksRestored = false;
    // One-shot flag for external-sub auto-load: once we've scanned the
    // parent folder and (maybe) loaded a sibling sub for the current
    // file, don't re-fire on subsequent tracks_changed events. Reset in
    // openFile like m_tracksRestored.
    bool       m_autoSubAttempted = false;
    QJsonArray m_chapters;  // [{start, end, title}, ...]
    QString    m_activeAudioId;
    QString    m_activeSubId;

    // Playlist carry-forward (track language preferences across episodes)
    QString m_carryAudioLang;
    QString m_carrySubLang;
    QString m_carryAspect;
    QString m_carryCrop;

    // Auto-hide
    QTimer m_hideTimer;
    QTimer m_seekThrottle;

    // Batch 4.1 (Player Polish Phase 4) — audio-speed drift-correction
    // forwarder. Polls SyncClock::getClockVelocity() every 500ms and
    // pushes the value to the sidecar via sendSetAudioSpeed when it
    // moves past the deadband (0.0005 = 0.05%). Sidecar applies via
    // swr_set_compensation. Started alongside m_hideTimer in buildUI,
    // fires regardless of playback state — cheap, and keeps the loop
    // alive across pause/resume without any additional plumbing.
    QTimer m_audioSpeedTicker;
    double m_lastSentAudioSpeed = 1.0;

    // Batch 6.1 (Player Polish Phase 6) — sidecar crash auto-restart state.
    // m_lastKnownPosSec mirrors positionSec from the last clean timeUpdate
    // so a crash recovery can resume near the crash point. m_sidecarRetryCount
    // counts consecutive respawn attempts — caps at 3, resets on onFirstFrame
    // (recovery confirmed) or openFile (new user intent). m_sidecarRestartTimer
    // drives the exponential backoff (250/500/1000 ms for retries 0/1/2).
    double m_lastKnownPosSec   = 0.0;
    int    m_sidecarRetryCount = 0;
    QTimer m_sidecarRestartTimer;

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — 30-second first-frame
    // watchdog. Starts on openFile entry, fires if no `firstFrame` event
    // arrives within 30s, flips LoadingOverlay to Stage::TakingLonger
    // ("Taking longer than expected — close to retry"). Cancelled on
    // firstFrame (normal path) or teardownUi (file-switch / close).
    // Duration chosen to match the sidecar's existing STREAM_TIMEOUT
    // ("no data for 30 seconds" at video_decoder.cpp:1087) for internal
    // consistency — if the sidecar itself gives up at 30s, user-facing
    // UX should flip to the close-to-retry state at the same threshold.
    // Identity is handled by explicit stop() at the three lifecycle
    // sites (openFile re-arm, firstFrame dismiss, teardownUi teardown);
    // Qt's single-thread GUI event loop serializes those handlers so
    // no generation-race exists.
    QTimer m_firstFrameWatchdog;

    // REPO_HYGIENE Phase 3 (2026-04-26) — true once the firstFrame slot
    // has fired for the current open. Reset to false in teardownUi (file
    // switch / close). Read by devSnapshot() for the dev-bridge's
    // exit-criterion check that play_file actually rendered a frame.
    bool m_firstFrameSeen = false;
};
