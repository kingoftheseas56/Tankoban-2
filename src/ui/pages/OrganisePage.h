#pragma once

#include <QWidget>
#include <QList>
#include "core/VideosScanner.h"
#include "core/library/VideoCategory.h"

class CategoryEditorPage;
class CoreBridge;
class QStackedWidget;

class OrganisePage : public QWidget {
    Q_OBJECT
public:
    explicit OrganisePage(CoreBridge* bridge, QWidget* parent = nullptr);

    void setShows(const QList<ShowInfo>& shows);
    void activate();

signals:
    void backToVideosRequested();
    void assignmentsChanged();

private:
    void openEditor(VideoCategory category);
    void rebuildCategoryButtons();

    CoreBridge* m_bridge = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_homePage = nullptr;
    CategoryEditorPage* m_editor = nullptr;
    QList<ShowInfo> m_shows;
};
