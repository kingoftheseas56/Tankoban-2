#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QShortcut>

class CoreBridge;

class BookSeriesView : public QWidget {
    Q_OBJECT
public:
    explicit BookSeriesView(CoreBridge* bridge, QWidget* parent = nullptr);

    void showSeries(const QString& seriesPath, const QString& seriesName,
                    const QString& coverThumbPath = QString());

signals:
    void backRequested();
    void bookSelected(const QString& filePath);

private:
    void buildBreadcrumb();
    void populateTable(const QString& folderPath);
    void navigateTo(const QString& relPath);
    void buildContinueBar();
    void goBack();
    void goForward();

    static QString formatSize(qint64 bytes);

    static constexpr int FolderRowRole = Qt::UserRole + 100;
    static constexpr int FolderRelRole = Qt::UserRole + 101;
    static constexpr int FilePathRole  = Qt::UserRole + 102;

    CoreBridge*  m_bridge = nullptr;

    QWidget*     m_breadcrumbWidget = nullptr;
    QHBoxLayout* m_breadcrumbLayout = nullptr;
    QLineEdit*   m_searchBar = nullptr;
    QComboBox*   m_sortCombo = nullptr;

    // Continue reading bar
    QWidget*     m_continueBar = nullptr;
    QLabel*      m_continueTitle = nullptr;
    QLabel*      m_continuePctLabel = nullptr;
    QProgressBar* m_continueProgress = nullptr;
    QPushButton* m_continueBtn = nullptr;
    QString      m_continueFilePath;

    QLabel*       m_coverLabel = nullptr;
    QTableWidget* m_table = nullptr;

    // Forward/back navigation
    QPushButton* m_forwardBtn = nullptr;
    QStringList  m_navHistory;
    int          m_navIndex = -1;

    QString m_seriesRootPath;
    QString m_seriesRootName;
    QString m_currentRel;

    QString m_searchText;
    QString m_sortKey = "title_asc";
};
