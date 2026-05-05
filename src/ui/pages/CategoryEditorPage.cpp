#include "CategoryEditorPage.h"
#include "core/CoreBridge.h"
#include "core/library/VideoCategoryStore.h"

#include <QAbstractItemView>
#include <QIcon>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

namespace {

class CheckListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (auto* item = itemAt(event->pos())) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            setCurrentItem(item);
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Space) {
            if (auto* item = currentItem()) {
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                event->accept();
                return;
            }
        }
        QListWidget::keyPressEvent(event);
    }
};

}

CategoryEditorPage::CategoryEditorPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("CategoryEditorPage");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 24);
    root->setSpacing(16);

    auto* header = new QHBoxLayout();
    auto* backButton = new QPushButton("Back", this);
    backButton->setObjectName("CategoryEditorBackButton");
    connect(backButton, &QPushButton::clicked, this, &CategoryEditorPage::backRequested);
    header->addWidget(backButton, 0, Qt::AlignLeft);

    m_title = new QLabel(this);
    m_title->setObjectName("CategoryEditorTitle");
    header->addWidget(m_title, 1);
    root->addLayout(header);

    auto* body = new QHBoxLayout();
    body->setSpacing(18);

    auto* leftPane = new QWidget(this);
    leftPane->setObjectName("CategoryEditorPane");
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(14, 14, 14, 14);
    leftLayout->setSpacing(10);
    m_leftHeader = new QLabel("Uncategorised", leftPane);
    buildPane(m_leftList, m_leftSearch, m_leftSelectAll, m_leftDeselectAll, m_leftHeader, leftPane);
    leftLayout->addWidget(m_leftHeader);
    leftLayout->addWidget(m_leftSearch);
    auto* leftActions = new QHBoxLayout();
    leftActions->addWidget(m_leftSelectAll);
    leftActions->addWidget(m_leftDeselectAll);
    leftLayout->addLayout(leftActions);
    leftLayout->addWidget(m_leftList, 1);
    m_addButton = new QPushButton(leftPane);
    m_addButton->setObjectName("CategoryEditorPrimaryButton");
    m_addButton->setIcon(QIcon(":/icons/plus.svg"));
    m_addButton->setIconSize(QSize(18, 18));
    m_addButton->setToolTip("Add checked items");
    m_addButton->setFixedHeight(34);
    leftLayout->addWidget(m_addButton);

    auto* rightPane = new QWidget(this);
    rightPane->setObjectName("CategoryEditorPane");
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(14, 14, 14, 14);
    rightLayout->setSpacing(10);
    m_rightHeader = new QLabel(rightPane);
    buildPane(m_rightList, m_rightSearch, m_rightSelectAll, m_rightDeselectAll, m_rightHeader, rightPane);
    rightLayout->addWidget(m_rightHeader);
    rightLayout->addWidget(m_rightSearch);
    auto* rightActions = new QHBoxLayout();
    rightActions->addWidget(m_rightSelectAll);
    rightActions->addWidget(m_rightDeselectAll);
    rightLayout->addLayout(rightActions);
    rightLayout->addWidget(m_rightList, 1);
    m_removeButton = new QPushButton(rightPane);
    m_removeButton->setObjectName("CategoryEditorPrimaryButton");
    m_removeButton->setIcon(QIcon(":/icons/minus.svg"));
    m_removeButton->setIconSize(QSize(18, 18));
    m_removeButton->setToolTip("Remove checked items");
    m_removeButton->setFixedHeight(34);
    rightLayout->addWidget(m_removeButton);

    body->addWidget(leftPane, 1);
    body->addWidget(rightPane, 1);
    root->addLayout(body, 1);

    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        setSelectedCategory(m_leftList, m_category);
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        setSelectedCategory(m_rightList, VideoCategory::Miscellaneous);
    });

    setStyleSheet(
        "QWidget#CategoryEditorPage { background: transparent; }"
        "QLabel#CategoryEditorTitle { color: #f2f2f2; font-size: 22px; font-weight: 600; }"
        "QWidget#CategoryEditorPane { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.12); border-radius: 8px; }"
        "QLabel#CategoryEditorPaneHeader { color: #f2f2f2; font-size: 16px; font-weight: 600; }"
        "QLineEdit#CategoryEditorSearch { background: rgba(0,0,0,0.24); border: 1px solid rgba(255,255,255,0.14); border-radius: 6px; color: #eee; padding: 7px 10px; }"
        "QListWidget#CategoryEditorList { background: rgba(0,0,0,0.18); border: 1px solid rgba(255,255,255,0.10); color: #eee; outline: 0; }"
        "QListWidget#CategoryEditorList::item { padding: 8px; }"
        "QListWidget#CategoryEditorList::item:hover { background: rgba(255,255,255,0.08); }"
        "QListWidget#CategoryEditorList::item:selected { background: rgba(255,255,255,0.10); color: #fff; }"
        "QPushButton#CategoryEditorBackButton, QPushButton#CategoryEditorSmallButton, QPushButton#CategoryEditorPrimaryButton { background: rgba(255,255,255,0.08); color: #eee; border: 1px solid rgba(255,255,255,0.14); border-radius: 6px; padding: 7px 12px; }"
        "QPushButton#CategoryEditorPrimaryButton { font-weight: 600; }"
        "QPushButton#CategoryEditorBackButton:hover, QPushButton#CategoryEditorSmallButton:hover, QPushButton#CategoryEditorPrimaryButton:hover { background: rgba(255,255,255,0.14); }"
        "QPushButton#CategoryEditorPrimaryButton:disabled { color: #777; background: rgba(255,255,255,0.04); }");
}

void CategoryEditorPage::buildPane(QListWidget*& list,
                                   QLineEdit*& search,
                                   QPushButton*& selectAll,
                                   QPushButton*& deselectAll,
                                   QLabel* header,
                                   QWidget* parent)
{
    header->setObjectName("CategoryEditorPaneHeader");
    search = new QLineEdit(parent);
    search->setObjectName("CategoryEditorSearch");
    search->setPlaceholderText("Search");
    list = new CheckListWidget(parent);
    list->setObjectName("CategoryEditorList");
    list->setSelectionMode(QAbstractItemView::NoSelection);
    selectAll = new QPushButton("Select all", parent);
    selectAll->setObjectName("CategoryEditorSmallButton");
    deselectAll = new QPushButton("Deselect all", parent);
    deselectAll->setObjectName("CategoryEditorSmallButton");

    connect(search, &QLineEdit::textChanged, this, [this, list](const QString& text) {
        filterList(list, text);
    });
    connect(selectAll, &QPushButton::clicked, this, [list]() {
        for (int i = 0; i < list->count(); ++i) {
            auto* item = list->item(i);
            if (!item->isHidden())
                item->setCheckState(Qt::Checked);
        }
    });
    connect(deselectAll, &QPushButton::clicked, this, [list]() {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Unchecked);
    });
}

void CategoryEditorPage::setShows(const QList<ShowInfo>& shows)
{
    m_shows = shows;
    refreshLists();
}

void CategoryEditorPage::setCategory(VideoCategory category)
{
    m_category = category;
    refreshLists();
}

void CategoryEditorPage::refreshLists()
{
    if (!m_leftList || !m_rightList)
        return;

    m_leftList->clear();
    m_rightList->clear();
    m_title->setText(videoCategoryLabel(m_category));
    m_rightHeader->setText(QString("In %1").arg(videoCategoryLabel(m_category)));

    VideoCategoryStore store(m_bridge->store());
    const bool editingMisc = m_category == VideoCategory::Miscellaneous;
    for (const ShowInfo& show : m_shows) {
        const QString id = VideoCategoryStore::itemIdForShow(show);
        const VideoCategory current = store.categoryFor(id);
        QListWidget* target = nullptr;
        if (!editingMisc && current == VideoCategory::Miscellaneous)
            target = m_leftList;
        else if (current == m_category)
            target = m_rightList;
        if (!target)
            continue;

        auto* item = new QListWidgetItem(show.showName, target);
        item->setData(Qt::UserRole, id);
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        item->setCheckState(Qt::Unchecked);
    }

    m_leftSearch->clear();
    m_rightSearch->clear();
    m_addButton->setEnabled(!editingMisc);
    m_removeButton->setEnabled(!editingMisc);
    m_leftList->setEnabled(!editingMisc);
    m_leftSelectAll->setEnabled(!editingMisc);
    m_leftDeselectAll->setEnabled(!editingMisc);
}

void CategoryEditorPage::filterList(QListWidget* list, const QString& query)
{
    const QString needle = query.trimmed().toLower();
    for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        item->setHidden(!needle.isEmpty() && !item->text().toLower().contains(needle));
    }
}

QStringList CategoryEditorPage::selectedIds(QListWidget* list) const
{
    QStringList ids;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        if (item->checkState() == Qt::Checked)
            ids.append(item->data(Qt::UserRole).toString());
    }
    return ids;
}

void CategoryEditorPage::setSelectedCategory(QListWidget* list, VideoCategory category)
{
    const QStringList ids = selectedIds(list);
    if (ids.isEmpty())
        return;
    VideoCategoryStore(m_bridge->store()).setCategories(ids, category);
    refreshLists();
    emit assignmentsChanged();
}
