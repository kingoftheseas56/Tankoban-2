#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QSlider>

class CoreBridge;
class StreamLibrary;
class StreamDownloadIndex;
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
    QNetworkAccessManager* m_nam;

    // UI
    QLabel*    m_sectionLabel = nullptr;
    QComboBox* m_sortCombo    = nullptr;
    QSlider*   m_densitySlider = nullptr;
    TileStrip* m_strip        = nullptr;
    QLabel*    m_emptyLabel   = nullptr;

    QString m_posterCacheDir;
};
