#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class ShowView : public QWidget {
    Q_OBJECT
public:
    explicit ShowView(QWidget* parent = nullptr);

    void showFolder(const QString& folderPath, const QString& displayName);

signals:
    void backRequested();
    void episodeSelected(const QString& filePath);

private:
    static QString formatSize(qint64 bytes);

    QLabel*      m_titleLabel = nullptr;
    QVBoxLayout* m_listLayout = nullptr;

    QString      m_showRootPath;
    QString      m_showRootName;
    QString      m_currentPath;
};
