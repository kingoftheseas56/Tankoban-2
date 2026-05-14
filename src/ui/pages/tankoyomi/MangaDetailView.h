// src/ui/pages/tankoyomi/MangaDetailView.h
#pragma once

#include <QWidget>
#include <QSet>
#include <QJsonObject>

#include <functional>

#include "core/manga/MangaResult.h"

class QLabel;
class QPushButton;
class QToolButton;
class QTableWidget;
class QVBoxLayout;
class MangaScraper;
class MangaDownloader;
class ChapterDownloadIndicator;

class MangaDetailView : public QWidget
{
    Q_OBJECT
public:
    explicit MangaDetailView(QWidget* parent = nullptr);

    void setDownloader(MangaDownloader* dl);
    void setScraper(MangaScraper* scraper);
    void setBridgeDestinationProvider(std::function<QString()> destProvider);

    // Entry point — load this manga, show loading state, fetch chapters.
    void show(const MangaResult& result, const QString& coverPath);

    // GLOBAL_NAV_HISTORY Task 13 — state snapshot/restore helpers.
    // snapshotState() writes mangaId + source + coverPath into a blob.
    // restoreFromSnapshot() returns false if the blob is empty or mangaId is
    // missing — the scraper fetch path requires a full MangaResult struct that
    // cannot be reconstructed from just an id without a network round-trip
    // (which is out of scope for INavStateProvider v1). v1 behaviour: detail
    // restore always returns false so NavHistory drops the entry and the user
    // lands on the search-results view instead. Improves in a future iteration
    // once a scraper-cache lookup path is available.
    QJsonObject snapshotState() const;
    bool restoreFromSnapshot(const QJsonObject& blob);

protected:
    void hideEvent(QHideEvent* event) override;

signals:
    void backRequested();
    void showInFolderRequested(const QString& seriesTitle, const QString& source);
    void openInBrowserRequested(const QUrl& url);
    void deleteChaptersRequested(const QString& seriesTitle,
                                  const QString& source,
                                  const QList<QString>& chapterIds);
    void openChapterFolderRequested(const QString& seriesTitle,
                                     const QString& source,
                                     const QString& chapterId);
    void showChapterFileRequested(const QString& seriesTitle,
                                   const QString& source,
                                   const QString& chapterId);

private slots:
    void onChaptersReady(const QList<ChapterInfo>& chapters);
    void onScraperError(const QString& message);
    void onChapterUpdated(const QString& seriesId, const QString& chapterId);
    void updateMultiSelectBar();
    void downloadNextN(int n);
    void openRangeDialog();
    void showChapterContextMenu(const QPoint& pos);

private:
    void buildUI();
    void renderChapters();
    void onChapterIconClicked(int row);
    void deriveChapterState(const QString& chapterId,
                             ChapterDownloadIndicator& indicator) const;

    MangaResult        m_result;
    QString            m_coverPath;   // local cached cover path (Hemanth-smoke-fix-3 2026-05-13)
    QList<ChapterInfo> m_chapters;
    QSet<QString>      m_selectedChapterIds;

    MangaDownloader*  m_downloader = nullptr;
    MangaScraper*     m_scraper    = nullptr;
    std::function<QString()> m_destProvider;

    // UI members
    QPushButton*   m_backBtn          = nullptr;
    QLabel*        m_titleLabel       = nullptr;
    QLabel*        m_coverLabel       = nullptr;
    QLabel*        m_metaLine         = nullptr;
    QToolButton*   m_downloadDropdown = nullptr;
    QToolButton*   m_overflowBtn      = nullptr;
    QWidget*       m_multiSelectBar   = nullptr;
    QLabel*        m_multiSelectLabel = nullptr;
    QPushButton*   m_msDownloadBtn    = nullptr;
    QPushButton*   m_msDeleteBtn      = nullptr;
    QPushButton*   m_msClearBtn       = nullptr;
    QTableWidget*  m_chapterTable     = nullptr;
    QLabel*        m_loadingLabel     = nullptr;
    QLabel*        m_errorLabel       = nullptr;
};
