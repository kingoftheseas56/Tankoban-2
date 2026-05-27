#pragma once

// COMICS_DOWNLOADS_SIDEBAR_PAGE 2026-05-26 (Agent 9 commission via Trigger D)
// Comics-mode Downloads page accessible from SidebarDrawer's new "Downloads"
// entry (Comics-only). Renders one section:
//   - Downloaded: completed comic/manga volumes + chapters, grouped by series,
//     sourced from MangaDownloadIndex::entriesForAllSeries() +
//     entriesForSeries().
//
// v1 is read-only display. Active/in-progress transfer UI is future work
// (MangaDownloadIndex tracks completed entries only; there is no per-series
// progress-state aggregation API mirroring TorrentClient::streamBulkGroups).

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class MangaDownloadIndex;
class ComicsPage;

class ComicsDownloadsPage : public QFrame
{
    Q_OBJECT
public:
    explicit ComicsDownloadsPage(QWidget* parent = nullptr);
    ~ComicsDownloadsPage() override = default;

    // Injection point. Same-pointer guard prevents re-subscribing.
    void setMangaDownloadIndex(MangaDownloadIndex* index);

    // COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — non-owning
    // reference for canonical grouping + title resolution + source labels.
    // Set by MainWindow after construction.
    void setComicsPage(ComicsPage* page);

signals:
    // Topbar back-button click - MainWindow's slot returns to Comics mode.
    void backRequested();

private slots:
    void refresh();

private:
    void buildUi();
    void updateEmptyState();

    MangaDownloadIndex* m_mangaDownloadIndex = nullptr;
    ComicsPage*         m_comicsPage         = nullptr;

    QPushButton*  m_backBtn       = nullptr;
    QLabel*       m_titleLabel    = nullptr;
    QScrollArea*  m_scroll        = nullptr;
    QWidget*      m_scrollContent = nullptr;
    QVBoxLayout*  m_contentLayout = nullptr;
    QLabel*       m_sectionHeader = nullptr;
    QWidget*      m_sectionBody   = nullptr;
    QVBoxLayout*  m_sectionBodyLayout = nullptr;
    QLabel*       m_emptyState    = nullptr;
};
