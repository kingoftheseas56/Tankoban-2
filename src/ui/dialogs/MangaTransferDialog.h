#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QLabel>

class MangaDownloader;
class QTimer;

class MangaTransferDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MangaTransferDialog(const QString& recordId, MangaDownloader* downloader,
                                 QWidget* parent = nullptr);

private:
    void refresh();

    QString          m_recordId;
    MangaDownloader* m_downloader = nullptr;

    QLabel*      m_titleLabel    = nullptr;
    QLabel*      m_subtitleLabel = nullptr;
    QLabel*      m_statusLabel   = nullptr;
    QTreeWidget* m_tree          = nullptr;
    QTimer*      m_refreshTimer  = nullptr;
};
