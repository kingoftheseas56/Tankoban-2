#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
class SeekSlider;
#include <QTimer>
#include <QIcon>
#include <QJsonArray>
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
class EqualizerPopover;
class FilterPopover;
class TrackPopover;
class SubtitleOverlay;
class SubtitleMenu;

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
                  double startPositionSec = 0.0);

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
    // STREAM_UX_PARITY Batch 2.6 — emitted when the Shift+N
    // `stream_next_episode` binding fires. Stream mode (StreamPage)
    // connects to this; local-playlist playback ignores it. VideoPlayer
    // stays agnostic of stream-specific logic; receiver decides whether
    // a next episode exists and plays it.
    void streamNextEpisodeRequested();

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

    void togglePause();
    void toggleFullscreen();
    void toggleMute();
    void adjustVolume(int delta);
    void speedUp();
    void speedDown();
    void speedReset();
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

    // VIDEO_PLAYER_FIX Batch 4.2 — persist a just-opened path/URL to the
    // recents QSettings list. Dedupes; prepends; caps at 20.
    void pushRecentFile(const QString& filePath);
    static QString langForTrackId(const QJsonArray& tracks, const QString& id);
    static QString findTrackByLang(const QJsonArray& tracks, const QString& lang);
    static QString videoIdForFile(const QString& filePath);
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
    PlaylistDrawer*   m_playlistDrawer   = nullptr;
    StatsBadge*       m_statsBadge       = nullptr;
    ToastHud*         m_toastHud         = nullptr;
    EqualizerPopover* m_eqPopover        = nullptr;
    FilterPopover*    m_filterPopover    = nullptr;
    TrackPopover*     m_trackPopover     = nullptr;
    SubtitleOverlay*  m_subOverlay       = nullptr;
    SubtitleMenu*     m_subMenu          = nullptr;  // Batch 5.3

    // Current/pending file
    QString     m_currentFile;
    QString     m_currentVideoId;
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
    QPushButton* m_speedChip       = nullptr;
    QPushButton* m_filtersChip     = nullptr;
    QPushButton* m_eqChip          = nullptr;
    QPushButton* m_trackChip       = nullptr;
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
    bool   m_isHdr      = false;
    bool   m_paused     = false;
    bool   m_seeking    = false;
    bool   m_fullscreen = false;
    bool   m_muted      = false;
    int    m_volume     = 100;   // 0–200 (Batch 4.2: amp zone 101–200 = +6dB max)
    int    m_speedIdx   = 2;     // index into speed presets (1.0x)
    int    m_subDelayMs = 0;    // accumulated subtitle delay in ms
    int    m_audioDelayMs = 0;  // user-configurable audio delay (Bluetooth comp)
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
    QJsonArray m_chapters;  // [{start, end, title}, ...]
    QString    m_activeAudioId;
    QString    m_activeSubId;

    // Playlist carry-forward (track language preferences across episodes)
    QString m_carryAudioLang;
    QString m_carrySubLang;

    // Auto-hide
    QTimer m_hideTimer;
    QTimer m_cursorTimer;
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
};
