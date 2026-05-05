#pragma once

#include <QWidget>
#include <QList>
#include "core/VideosScanner.h"
#include "core/library/VideoCategory.h"

class CoreBridge;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class CategoryEditorPage : public QWidget {
    Q_OBJECT
public:
    explicit CategoryEditorPage(CoreBridge* bridge, QWidget* parent = nullptr);

    void setShows(const QList<ShowInfo>& shows);
    void setCategory(VideoCategory category);
    VideoCategory category() const { return m_category; }

signals:
    void backRequested();
    void assignmentsChanged();

private:
    void buildPane(QListWidget*& list,
                   QLineEdit*& search,
                   QPushButton*& selectAll,
                   QPushButton*& deselectAll,
                   QLabel* header,
                   QWidget* parent);
    void refreshLists();
    void filterList(QListWidget* list, const QString& query);
    QStringList selectedIds(QListWidget* list) const;
    void setSelectedCategory(QListWidget* list, VideoCategory category);

    CoreBridge* m_bridge = nullptr;
    VideoCategory m_category = VideoCategory::TVShows;
    QList<ShowInfo> m_shows;

    QLabel* m_title = nullptr;
    QLabel* m_leftHeader = nullptr;
    QLabel* m_rightHeader = nullptr;
    QLineEdit* m_leftSearch = nullptr;
    QLineEdit* m_rightSearch = nullptr;
    QListWidget* m_leftList = nullptr;
    QListWidget* m_rightList = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_leftSelectAll = nullptr;
    QPushButton* m_leftDeselectAll = nullptr;
    QPushButton* m_rightSelectAll = nullptr;
    QPushButton* m_rightDeselectAll = nullptr;
};
