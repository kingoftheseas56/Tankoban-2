#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QStyledItemDelegate>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>

class CoreBridge;

// ─── Progress Icon Delegate ─────────────────────────────────────────
// Renders 12x12 icons in the PROGRESS column:
//   finished    → green circle #4CAF50 + white checkmark
//   in-progress → slate circle #94a3b8 + percentage text
//   no progress → "-" text
class ShowProgressIconDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

class ShowView : public QWidget {
    Q_OBJECT
public:
    explicit ShowView(CoreBridge* bridge, QWidget* parent = nullptr);

    void showFolder(const QString& folderPath, const QString& showName,
                    const QString& coverThumbPath = QString(),
                    bool isLoose = false);
    void setFileDurations(const QMap<QString, double>& durations);

signals:
    void backRequested();
    void episodeSelected(const QString& filePath);

private:
    void buildBreadcrumb();
    void populateTable(const QString& folderPath);
    void navigateTo(const QString& relPath);
    void navigateBack();
    void navigateForward();
    void buildContinueBar();

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
    QPushButton* m_forwardBtn = nullptr;
    QLineEdit*   m_searchBar = nullptr;
    QComboBox*   m_sortCombo = nullptr;

    // Continue watching bar (40px, groundwork spec)
    QWidget*      m_continueBar = nullptr;
    QLabel*       m_continueTitle = nullptr;
    QLabel*       m_continueItemLabel = nullptr;
    QProgressBar* m_continueProgress = nullptr;
    QLabel*       m_continuePctLabel = nullptr;
    QPushButton*  m_continueBtn = nullptr;
    QString       m_continueFilePath;
    QString       m_continueVideoId;

    // Cover panel
    QLabel*       m_coverLabel = nullptr;

    // Table
    QTableWidget* m_table = nullptr;

    // Navigation state
    QString m_showRootPath;
    QString m_showRootName;
    QString m_currentRel;
    QStringList m_navHistory;
    int         m_navIndex = -1;

    // Loose files mode (no folder rows)
    bool m_isLoose = false;

    // Search/sort state
    QString m_searchText;
    QString m_sortKey = "title_asc";

    // Scan-time durations (file path → seconds)
    QMap<QString, double> m_fileDurations;
};
