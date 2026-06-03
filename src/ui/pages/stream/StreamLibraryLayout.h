#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QSlider>

#include <atomic>
#include <memory>

class CoreBridge;
class StreamLibrary;
class StreamDownloadIndex;
class TorrentClient;
class TileStrip;
class TileCard;
class QNetworkAccessManager;

class StreamLibraryLayout : public QWidget
{
    Q_OBJECT

public:
    explicit StreamLibraryLayout(CoreBridge* bridge, StreamLibrary* library,
                                 QWidget* parent = nullptr);

    void refresh();

    // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — wire the download
    // index so tiles can render the DOWNLOADED chip and refresh on
    // entriesChanged.
    void setStreamDownloadIndex(StreamDownloadIndex* idx);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — wire TorrentClient so the
    // DOWNLOADING chip can query imdbHasActiveCohort and refresh on
    // streamBulkGroupsChanged.
    void setTorrentClient(TorrentClient* client);

signals:
    void showClicked(const QString& imdbId);
    void showRightClicked(const QString& imdbId, const QPoint& globalPos);

protected:
    // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — eager disk-state
    // validateAll on home open. Spec §10.4. See impl for rationale.
    void showEvent(QShowEvent* event) override;

private slots:
    // STREAM_DOWNLOADED_LIBRARY Phase 3 — re-evaluate the DOWNLOADED chip
    // visibility on every existing tile. Cheap (per-tile property lookup +
    // findChild for the chip label).
    void refreshTileBadges();

private:
    void buildUI();
    void populateTiles();
    void downloadPoster(const QString& imdbId, const QString& posterUrl);
    void cleanupOrphanPosters();
    QString posterCachePath(const QString& imdbId) const;

    CoreBridge*    m_bridge;
    StreamLibrary* m_library;
    StreamDownloadIndex* m_downloadIndex = nullptr;
    TorrentClient*       m_torrentClient = nullptr;
    QNetworkAccessManager* m_nam;

    // UI
    QLabel*    m_sectionLabel = nullptr;
    QComboBox* m_sortCombo    = nullptr;
    QSlider*   m_densitySlider = nullptr;
    TileStrip* m_strip        = nullptr;
    QLabel*    m_emptyLabel   = nullptr;

    QString m_posterCacheDir;

    // A005 hardening C3 (2026-06-03) — coalesces overlapping orphan sweeps so a
    // rapid refresh() burst can't launch multiple concurrent QtConcurrent poster
    // sweeps racing to QFile::remove the same orphan. A shared_ptr (not a plain
    // member) so the flag OUTLIVES this widget if a sweep is still running at
    // teardown: the worker clears it through the shared copy it captured, never
    // writing through a freed member. This shared atomic also replaces the old
    // cross-thread QPointer guard read (C2) — the worker now touches only
    // by-value captures, so nothing is read across the GUI/worker boundary.
    std::shared_ptr<std::atomic_bool> m_orphanSweepRunning =
        std::make_shared<std::atomic_bool>(false);
};
