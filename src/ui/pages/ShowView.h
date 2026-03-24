#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

class CoreBridge;

class ShowView : public QWidget {
    Q_OBJECT
public:
    explicit ShowView(CoreBridge* bridge, QWidget* parent = nullptr);

    void showFolder(const QString& folderPath, const QString& showName);

signals:
    void backRequested();
    void episodeSelected(const QString& filePath);

private:
    void buildBreadcrumb();
    void populateTable(const QString& folderPath);
    void navigateTo(const QString& relPath);

    static QString videoId(const QString& path, qint64 size, qint64 mtimeMs);
    static QString formatSize(qint64 bytes);
    static QString formatDate(const QDateTime& dt);
    static QString formatDuration(double seconds);

    static constexpr int FolderRowRole = Qt::UserRole + 100;
    static constexpr int FolderRelRole = Qt::UserRole + 101;
    static constexpr int FilePathRole  = Qt::UserRole + 102;

    CoreBridge*  m_bridge = nullptr;

    // Header
    QWidget*     m_breadcrumbWidget = nullptr;
    QHBoxLayout* m_breadcrumbLayout = nullptr;
    QLineEdit*   m_searchBar = nullptr;
    QComboBox*   m_sortCombo = nullptr;

    // Table
    QTableWidget* m_table = nullptr;

    // Navigation state
    QString m_showRootPath;
    QString m_showRootName;
    QString m_currentRel;

    // Search/sort state
    QString m_searchText;
    QString m_sortKey = "title_asc";
};
