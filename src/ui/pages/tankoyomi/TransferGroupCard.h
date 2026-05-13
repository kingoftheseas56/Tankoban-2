#pragma once

#include <QWidget>
#include "core/manga/MangaDownloader.h"   // MangaDownloadRecord

class QLabel;
class QPushButton;
class QToolButton;
class QProgressBar;
class QVBoxLayout;

class TransferGroupCard : public QWidget
{
    Q_OBJECT
public:
    explicit TransferGroupCard(MangaDownloader* downloader,
                                QWidget* parent = nullptr);

    void setRecord(const MangaDownloadRecord& rec);
    QString recordId() const { return m_recordId; }

signals:
    void cancelSeriesRequested(const QString& id);
    void contextMenuRequested(const QPoint& globalPos, const QString& id);

private slots:
    void onPauseToggleClicked();
    void onCancelClicked();
    void onChapterUpdated(const QString& seriesId, const QString& chapterId);
    void onDownloadUpdated(const QString& seriesId);

private:
    void buildUI();
    void refreshFromRecord();
    void rebuildChapterList(const MangaDownloadRecord& rec);

    MangaDownloader* m_downloader = nullptr;
    QString          m_recordId;

    // UI members
    QLabel*       m_coverLabel    = nullptr;
    QLabel*       m_titleLabel    = nullptr;
    QLabel*       m_statusLabel   = nullptr;
    QProgressBar* m_progressBar   = nullptr;
    QPushButton*  m_pauseToggle   = nullptr;
    QToolButton*  m_cancelBtn     = nullptr;
    QVBoxLayout*  m_chapterColumn = nullptr;
};
