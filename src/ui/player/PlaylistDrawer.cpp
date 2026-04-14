#include "ui/player/PlaylistDrawer.h"

#include <QApplication>
#include <QCheckBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

PlaylistDrawer::PlaylistDrawer(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("PlaylistDrawer");
    setFixedWidth(320);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#PlaylistDrawer {"
        "  background: rgba(16, 16, 16, 242);"
        "  border: 1px solid rgba(255, 255, 255, 30);"
        "  border-radius: 12px;"
        "}"
    );

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(8);

    // Header row
    auto* header = new QHBoxLayout();
    auto* title = new QLabel("Playlist");
    title->setStyleSheet(
        "color: rgb(214, 194, 164); font-size: 14px; font-weight: 700; border: none;"
    );
    header->addWidget(title);
    header->addStretch();

    auto* closeBtn = new QPushButton("\u2715");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { color: rgba(255,255,255,140); background: transparent;"
        "  border: none; font-size: 14px; }"
        "QPushButton:hover { color: rgba(255,255,255,245); }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &PlaylistDrawer::dismiss);
    header->addWidget(closeBtn);
    lay->addLayout(header);

    // Divider
    auto* div = new QFrame();
    div->setFrameShape(QFrame::HLine);
    div->setFixedHeight(1);
    div->setStyleSheet("background: rgba(255,255,255,20); border: none;");
    lay->addWidget(div);

    // Episode list
    m_list = new QListWidget();
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setStyleSheet(
        "QListWidget {"
        "  background: transparent; border: none; outline: none;"
        "}"
        "QListWidget::item {"
        "  color: rgba(245, 245, 245, 200);"
        "  font-size: 13px;"
        "  padding: 6px 4px;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QListWidget::item:selected {"
        "  background: rgba(255,255,255,18);"
        "  color: rgba(245, 245, 245, 245);"
        "}"
        "QListWidget::item:hover:!selected {"
        "  background: rgba(255,255,255,10);"
        "}"
    );
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int idx = item->data(Qt::UserRole).toInt();
        emit episodeSelected(idx);
        dismiss();
    });
    lay->addWidget(m_list, 1);

    // Auto-advance checkbox
    m_autoAdvance = new QCheckBox("Auto-advance");
    m_autoAdvance->setChecked(true);
    m_autoAdvance->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,140); font-size: 12px; border: none; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    lay->addWidget(m_autoAdvance);

    hide();
}

void PlaylistDrawer::populate(const QStringList& paths, int currentIndex)
{
    m_list->clear();
    for (int i = 0; i < paths.size(); ++i) {
        QString stem = QFileInfo(paths[i]).completeBaseName();
        if (stem.isEmpty()) stem = QString("Track %1").arg(i + 1);
        QString prefix = (i == currentIndex) ? "\u25b6 " : "   ";
        auto* item = new QListWidgetItem(prefix + stem);
        item->setData(Qt::UserRole, i);
        if (i == currentIndex) item->setSelected(true);
        m_list->addItem(item);
    }
}

bool PlaylistDrawer::isAutoAdvance() const
{
    return m_autoAdvance && m_autoAdvance->isChecked();
}

void PlaylistDrawer::toggle()
{
    if (isVisible()) {
        dismiss();
    } else {
        show();
        raise();
        installClickFilter();
    }
}

void PlaylistDrawer::dismiss()
{
    removeClickFilter();
    hide();
}

void PlaylistDrawer::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    // Prevent VideoPlayer's HUD auto-hide while hovering the drawer
    if (parent())
        QMetaObject::invokeMethod(parent(), "showControls");
}

void PlaylistDrawer::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
}

void PlaylistDrawer::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void PlaylistDrawer::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance())
        app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

bool PlaylistDrawer::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        QPoint local = mapFromGlobal(me->globalPosition().toPoint());
        if (!rect().contains(local))
            dismiss();
    }
    return false;
}
