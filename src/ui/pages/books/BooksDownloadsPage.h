#pragma once

// BOOKS_DOWNLOADS_SIDEBAR_PAGE 2026-05-28 (Agent 2). Books-mode Downloads page
// reached from SidebarDrawer's "Downloads" entry (Books-only). Two sections:
//   - Downloading: in-progress transfers (live %), from BooksPage::activeDownloads().
//   - Downloaded:  completed books grouped by series, from BooksCatalogueLibraryStore.
// Read-only display in v1 (mirrors ComicsDownloadsPage).

#include <QFrame>
#include <QString>

#include <functional>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class BooksCatalogueLibraryStore;
class BooksPage;

class BooksDownloadsPage : public QFrame
{
    Q_OBJECT
public:
    explicit BooksDownloadsPage(QWidget* parent = nullptr);
    ~BooksDownloadsPage() override = default;

    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    void setBooksPage(BooksPage* page);

signals:
    void backRequested();
    // A Downloaded series tile was clicked — open its series detail page.
    void openSeriesRequested(const QString& seriesId);
    // A Downloaded standalone book was clicked — open it in the reader.
    void openBookRequested(const QString& filePath);

private slots:
    void refresh();

private:
    void buildUi();
    int  populateDownloading();   // returns row count
    int  populateDownloaded();    // returns row count
    QWidget* makeRow(QWidget* parent, const QString& coverPath, const QString& title,
                     const QString& subtitle, std::function<void()> onClick = {});

    BooksCatalogueLibraryStore* m_store = nullptr;
    BooksPage* m_booksPage = nullptr;
    QString m_coverDir;   // shared catalogue cover cache (fallback for missing cachedCoverPath)

    QPushButton* m_backBtn = nullptr;
    QScrollArea* m_scroll = nullptr;

    QLabel*      m_downloadingHeader = nullptr;
    QWidget*     m_downloadingBody = nullptr;
    QVBoxLayout* m_downloadingLayout = nullptr;

    QLabel*      m_downloadedHeader = nullptr;
    QWidget*     m_downloadedBody = nullptr;
    QVBoxLayout* m_downloadedLayout = nullptr;

    QLabel*      m_emptyState = nullptr;
};
