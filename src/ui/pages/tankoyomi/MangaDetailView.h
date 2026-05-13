// src/ui/pages/tankoyomi/MangaDetailView.h
#pragma once

#include <QWidget>
#include <QSet>

#include <functional>

#include "core/manga/MangaResult.h"

class QLabel;
class QPushButton;
class QToolButton;
class QTableWidget;
class QVBoxLayout;
class MangaScraper;
class MangaDownloader;

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

signals:
    void backRequested();
    void showInFolderRequested(const QString& seriesTitle, const QString& source);
    void openInBrowserRequested(const QUrl& url);

private slots:
    void onChaptersReady(const QList<ChapterInfo>& chapters);
    void onScraperError(const QString& message);
    void onChapterUpdated(const QString& seriesId, const QString& chapterId);

private:
    void buildUI();
    void renderChapters();
    void updateMultiSelectBar();
    void onChapterIconClicked(int row);

    MangaResult        m_result;
    QList<ChapterInfo> m_chapters;
    QSet<QString>      m_selectedChapterIds;

    MangaDownloader*  m_downloader = nullptr;
    MangaScraper*     m_scraper    = nullptr;
    std::function<QString()> m_destProvider;

    // UI members
    QPushButton*   m_backBtn          = nullptr;
    QLabel*        m_titleLabel       = nullptr;
    QLabel*        m_coverLabel       = nullptr;
    QLabel*        m_authorLabel      = nullptr;
    QLabel*        m_statusLabel      = nullptr;
    QLabel*        m_chapterCount     = nullptr;
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
