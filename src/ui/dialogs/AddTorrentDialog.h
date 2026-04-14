#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QJsonArray>
#include <QMap>
#include <QVector>
#include <QEvent>

// ── Return value from the dialog ────────────────────────────────────────────
struct AddTorrentConfig {
    QString category;         // "comics", "books", "audiobooks", "videos"
    QString destinationPath;
    QString contentLayout;    // "original", "subfolder", "no_subfolder"
    bool    sequential   = false;
    bool    startPaused  = false;
    QVector<int>   selectedIndices;   // file indices with priority > 0
    QMap<int, int> filePriorities;    // fileIndex → priority (0/1/6/7)
};

// ── AddTorrentDialog ────────────────────────────────────────────────────────
class AddTorrentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddTorrentDialog(const QString& torrentName,
                              const QString& infoHash,
                              const QMap<QString, QString>& defaultPaths,
                              QWidget* parent = nullptr);

    // Call when metadata arrives from engine
    void populateFiles(const QString& name, qint64 totalSize, const QJsonArray& files);

    // Call on timeout / error
    void showMetadataError(const QString& message);

    // Get the config after user clicks Download
    AddTorrentConfig config() const;

private slots:
    void onPresetClicked(const QString& preset);
    void onCategoryChanged(int index);
    void onDestinationBrowse();
    void onSelectAll();
    void onDeselectAll();
    void onTreeItemChanged(QTreeWidgetItem* item, int column);
    void onPriorityChanged(QTreeWidgetItem* item, int newPriority);
    void showTreeContextMenu(const QPoint& pos);

private:
    void buildUI();
    void buildFileTree(const QJsonArray& files);
    void syncChildrenFromParent(QTreeWidgetItem* parent);
    void syncParentFromChildren(QTreeWidgetItem* child);
    void updateItemColors(QTreeWidgetItem* item);
    void setItemPriority(QTreeWidgetItem* item, int priority, bool propagate = true);
    int  itemPriority(QTreeWidgetItem* item) const;
    void updateStatusLabel();
    QString defaultDestForCategory(const QString& category) const;

    // Widgets
    QLineEdit*    m_nameEdit     = nullptr;
    QLabel*       m_hashLabel    = nullptr;
    QLabel*       m_sizeLabel    = nullptr;
    QLabel*       m_statusLabel  = nullptr;
    QLineEdit*    m_destEdit     = nullptr;
    QLabel*       m_destPreview  = nullptr;
    QComboBox*    m_categoryCombo = nullptr;
    QComboBox*    m_layoutCombo  = nullptr;
    QCheckBox*    m_sequentialCb = nullptr;
    QCheckBox*    m_startCb      = nullptr;
    QTreeWidget*  m_fileTree     = nullptr;
    QPushButton*  m_downloadBtn  = nullptr;

    // State
    QString              m_infoHash;
    QMap<QString, QString> m_defaultPaths;  // category → default path
    bool                 m_userChangedDest = false;
    bool                 m_treeSyncing     = false;
    bool                 m_metadataReady   = false;
    qint64               m_totalSize       = 0;
    QMap<QTreeWidgetItem*, int> m_fileIndices;  // tree item → file index (leaves only)

    static constexpr int PRIORITY_SKIP    = 0;
    static constexpr int PRIORITY_NORMAL  = 1;
    static constexpr int PRIORITY_HIGH    = 6;
    static constexpr int PRIORITY_MAX     = 7;
    static constexpr int PRIORITY_MIXED   = -1;

    static constexpr int ROLE_FILE_INDEX  = Qt::UserRole + 1;
    static constexpr int ROLE_PRIORITY    = Qt::UserRole + 2;
    static constexpr int ROLE_IS_FOLDER   = Qt::UserRole + 3;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};
