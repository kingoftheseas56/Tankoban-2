#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QStyledItemDelegate>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QShortcut>

class CoreBridge;

// ─── Progress Icon Delegate ─────────────────────────────────────────
// Renders 12x12 icons in the READ column:
//   finished    → green circle #4CAF50 + white checkmark
//   in-progress → slate circle #94a3b8 + percentage text
//   no progress → "-" text
class ProgressIconDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    // Data roles for progress state
    static constexpr int ProgressStateRole = Qt::UserRole + 200; // 0=none, 1=in-progress, 2=finished
    static constexpr int ProgressPctRole   = Qt::UserRole + 201; // int 0-100
};

class SeriesView : public QWidget {
    Q_OBJECT
public:
    explicit SeriesView(CoreBridge* bridge, QWidget* parent = nullptr);

    void showSeries(const QString& seriesPath, const QString& seriesName,
                    const QString& coverThumbPath = QString());

signals:
    void backRequested();
    void issueSelected(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);

private:
    void buildBreadcrumb();
    void populateTable(const QString& folderPath);
    void navigateTo(const QString& relPath);
    void buildContinueBar();
    void goBack();
    void goForward();
    void updateFwdBtn();

    static QString formatSize(qint64 bytes);

    static constexpr int FolderRowRole = Qt::UserRole + 100;
    static constexpr int FolderRelRole = Qt::UserRole + 101;
    static constexpr int FilePathRole  = Qt::UserRole + 102;

    CoreBridge*  m_bridge = nullptr;

    // Header
    QWidget*     m_breadcrumbWidget = nullptr;
    QHBoxLayout* m_breadcrumbLayout = nullptr;
    QLineEdit*   m_searchBar = nullptr;
    QComboBox*   m_sortCombo = nullptr;
    QComboBox*   m_namingCombo = nullptr;
    QPushButton* m_fwdBtn = nullptr;

    // Continue reading bar
    QWidget*     m_continueBar = nullptr;
    QLabel*      m_continueTitle = nullptr;
    QPushButton* m_continueBtn = nullptr;
    QString      m_continueFilePath;

    // Cover panel
    QLabel*      m_coverLabel = nullptr;

    // Table
    QTableWidget* m_table = nullptr;

    // Navigation state
    QString m_seriesRootPath;
    QString m_seriesRootName;
    QString m_currentRel;

    // Forward/back history
    QStringList m_navHistory;
    int         m_navIndex = -1;

    QStringList m_allCbzFiles;

    QString m_searchText;
    QString m_sortKey = "title_asc";
    QString m_namingMode = "volumes";
};
