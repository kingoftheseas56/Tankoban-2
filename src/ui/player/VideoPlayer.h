#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
class SeekSlider;
#include <QTimer>
#include <QIcon>
#include <QJsonArray>

class CoreBridge;
class KeyBindings;
class SidecarProcess;
class ShmFrameReader;
class FrameCanvas;
class VolumeHud;
class CenterFlash;
class ShortcutsOverlay;
class PlaylistDrawer;
class ToastHud;
class EqualizerPopover;
class FilterPopover;
class TrackPopover;
class SubtitleOverlay;

class VideoPlayer : public QWidget {
    Q_OBJECT

public:
    explicit VideoPlayer(CoreBridge* bridge, QWidget* parent = nullptr);
    ~VideoPlayer() override;

    void openFile(const QString& filePath,
                  const QStringList& playlist = {},
                  int playlistIndex = 0);
    void stopPlayback();

signals:
    void closeRequested();
    void fullscreenRequested(bool enter);
    void progressUpdated(const QString& path, double positionSec, double durationSec);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void buildUI();
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

    void saveProgress(double positionSec, double durationSec);
    void restoreTrackPreferences();
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
    VolumeHud*        m_volumeHud        = nullptr;
    CenterFlash*      m_centerFlash      = nullptr;
    ShortcutsOverlay* m_shortcutsOverlay = nullptr;
    PlaylistDrawer*   m_playlistDrawer   = nullptr;
    ToastHud*         m_toastHud         = nullptr;
    EqualizerPopover* m_eqPopover        = nullptr;
    FilterPopover*    m_filterPopover    = nullptr;
    TrackPopover*     m_trackPopover     = nullptr;
    SubtitleOverlay*  m_subOverlay       = nullptr;

    // Current/pending file
    QString     m_currentFile;
    QString     m_currentVideoId;
    QStringList m_playlist;
    int         m_playlistIdx = 0;
    QString     m_pendingFile;
    double      m_pendingStartSec = 0.0;

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
    bool   m_isHdr      = false;
    bool   m_paused     = false;
    bool   m_seeking    = false;
    bool   m_fullscreen = false;
    bool   m_muted      = false;
    int    m_volume     = 100;   // 0-100
    int    m_speedIdx   = 2;     // index into speed presets (1.0x)
    int    m_subDelayMs = 0;    // accumulated subtitle delay in ms
    int    m_audioDelayMs = 0;  // user-configurable audio delay (Bluetooth comp)
    QString m_audioDeviceKey;   // QSettings key for current device's offset
    bool   m_subsVisible = true;
    double m_durationSec = 0.0;
    int    m_pendingSeekVal = 0;
    int    m_seekDragOrigin = -1;

    // Track lists from sidecar
    QJsonArray m_audioTracks;
    QJsonArray m_subTracks;
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
};
