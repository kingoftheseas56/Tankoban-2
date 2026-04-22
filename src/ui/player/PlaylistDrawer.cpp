#include "ui/player/PlaylistDrawer.h"

#include "ui/player/PlayerUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QWheelEvent>
#include <QSettings>
#include <QToolButton>
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

    // VIDEO_PLAYER_UI_POLISH follow-up 2026-04-23 (hemanth-reported after
    // Phase 4 ship): the glyph+label attempt ("⇄ Shuffle" etc.) ran out
    // of horizontal space in the 320 px-wide drawer once Save + Load
    // were laid out alongside, eliding the labels to nonsense like
    // "⇄...e" / "∞...l" / "1...e" / "...". Reverting to icon-only but
    // using real SVG (`resources/icons/{shuffle,repeat_all,repeat_one,
    // loop_file}.svg`) instead of unicode glyphs — matches Tankoyomi
    // / Sources icon aesthetic, stays crisp at any DPR, and keeps the
    // tooltip as the name carrier for hover discovery. Button bounds
    // shrunk to 34×30 so all four toggles + Save + Load fit without
    // truncation.
    const QString toolbarBtnStyle =
        "QToolButton {"
        "  color: rgba(255,255,255,170);"
        "  background: transparent;"
        "  border: 1px solid rgba(255,255,255,30);"
        "  border-radius: 4px;"
        "  padding: 3px;"
        "}"
        "QToolButton:hover {"
        "  background: rgba(255,255,255,14);"
        "  border: 1px solid rgba(255,255,255,60);"
        "}"
        "QToolButton:checked {"
        "  background: rgba(255,255,255,26);"
        "  border: 1px solid rgba(255,255,255,90);"
        "}";

    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(6);
    toolbar->setContentsMargins(0, 0, 0, 0);

    const QSettings s("Tankoban", "Tankoban");

    auto makeBtn = [&](const QString& iconPath, const QString& tip,
                       const QString& key, QToolButton*& slot) {
        slot = new QToolButton();
        slot->setIcon(QIcon(iconPath));
        slot->setIconSize(QSize(18, 18));
        slot->setCheckable(true);
        slot->setFixedSize(34, 30);
        slot->setCursor(Qt::PointingHandCursor);
        slot->setFocusPolicy(Qt::NoFocus);
        slot->setToolTip(tip);
        slot->setStyleSheet(toolbarBtnStyle);
        slot->setChecked(s.value(key, false).toBool());
        toolbar->addWidget(slot);
    };

    makeBtn(":/icons/shuffle.svg", tr("Shuffle playback order"),
            "player/queueMode/shuffle",   m_btnShuffle);
    makeBtn(":/icons/repeat_all.svg", tr("Repeat all items"),
            "player/queueMode/repeatAll", m_btnRepeatAll);
    makeBtn(":/icons/repeat_one.svg", tr("Repeat current item"),
            "player/queueMode/repeatOne", m_btnRepeatOne);
    makeBtn(":/icons/loop_file.svg", tr("Loop file"),
            "player/queueMode/loopFile",  m_btnLoopFile);
    toolbar->addStretch();

    // VIDEO_PLAYER_FIX Batch 5.2 — Save / Load buttons (right-aligned after
    // the stretch). Non-checkable: instantaneous action, not toggle state.
    const QString actionBtnStyle =
        "QToolButton {"
        "  color: rgba(255,255,255,170);"
        "  background: transparent;"
        "  border: 1px solid rgba(255,255,255,30);"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  padding: 2px 6px;"
        "}"
        "QToolButton:hover {"
        "  color: rgba(255,255,255,240);"
        "  background: rgba(255,255,255,14);"
        "}";
    auto* saveBtn = new QToolButton();
    saveBtn->setText(tr("Save"));
    saveBtn->setToolTip(tr("Save queue as .m3u"));
    saveBtn->setFixedHeight(24);
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setFocusPolicy(Qt::NoFocus);
    saveBtn->setStyleSheet(actionBtnStyle);
    toolbar->addWidget(saveBtn);

    auto* loadBtn = new QToolButton();
    loadBtn->setText(tr("Load"));
    loadBtn->setToolTip(tr("Load queue from .m3u"));
    loadBtn->setFixedHeight(24);
    loadBtn->setCursor(Qt::PointingHandCursor);
    loadBtn->setFocusPolicy(Qt::NoFocus);
    loadBtn->setStyleSheet(actionBtnStyle);
    toolbar->addWidget(loadBtn);

    for (QToolButton* b : { m_btnShuffle, m_btnRepeatAll, m_btnRepeatOne }) {
        connect(b, &QToolButton::toggled, this, [this](bool) { persistQueueMode(); });
    }
    // Loop File additionally emits a signal — VideoPlayer relays to sidecar.
    connect(m_btnLoopFile, &QToolButton::toggled, this, [this](bool on) {
        persistQueueMode();
        emit loopFileChanged(on);
    });
    connect(saveBtn, &QToolButton::clicked, this, &PlaylistDrawer::saveRequested);
    connect(loadBtn, &QToolButton::clicked, this, &PlaylistDrawer::loadRequested);

    lay->addLayout(toolbar);

    // Episode list
    m_list = new QListWidget();
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // VIDEO_PLAYER_UI_POLISH Phase 4 2026-04-23 — audit finding #4
    // ("weak hierarchy: current row gets a small play marker, but the
    // filenames are so dense scanning is harder than it should be"):
    // strengthen selected-row contrast (bg 18→42) + 3 px off-white
    // left-border accent + widen item padding 6/4 → 8/10 so the list
    // scans cleanly at a glance. Hover-not-selected bg 10→16 to
    // match the new scale.
    m_list->setStyleSheet(
        "QListWidget {"
        "  background: transparent; border: none; outline: none;"
        "}"
        "QListWidget::item {"
        "  color: rgba(245, 245, 245, 200);"
        "  font-size: 13px;"
        "  padding: 8px 10px;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QListWidget::item:selected {"
        "  background: rgba(255,255,255,42);"
        "  color: rgba(245, 245, 245, 250);"
        "  border-left: 3px solid rgba(214,194,164,200);"
        "  padding-left: 7px;"
        "}"
        "QListWidget::item:hover:!selected {"
        "  background: rgba(255,255,255,16);"
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
        // VIDEO_PLAYER_UI_POLISH Phase 2 2026-04-22 — audit finding #3
        // ("raw release filenames leak into playback UI"): run each
        // entry through the shared episodeLabel helper so drawer rows
        // match the bottom title strip's cleaned format
        // ("Saiki Kusuo no Psi-nan · Episode 4" instead of the raw
        // "[DB]Saiki Kusuo no Psi-nan_-_04_(Dual Audio_10bit_BD1080p_x265)").
        QString stem = player_utils::episodeLabel(paths[i]);
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

// VIDEO_PLAYER_FIX Batch 5.1 — queue mode accessors + persistence.
bool PlaylistDrawer::shuffle()   const { return m_btnShuffle   && m_btnShuffle->isChecked(); }
bool PlaylistDrawer::repeatAll() const { return m_btnRepeatAll && m_btnRepeatAll->isChecked(); }
bool PlaylistDrawer::repeatOne() const { return m_btnRepeatOne && m_btnRepeatOne->isChecked(); }
bool PlaylistDrawer::loopFile()  const { return m_btnLoopFile  && m_btnLoopFile->isChecked(); }

void PlaylistDrawer::persistQueueMode() const
{
    QSettings s("Tankoban", "Tankoban");
    if (m_btnShuffle)   s.setValue("player/queueMode/shuffle",   m_btnShuffle->isChecked());
    if (m_btnRepeatAll) s.setValue("player/queueMode/repeatAll", m_btnRepeatAll->isChecked());
    if (m_btnRepeatOne) s.setValue("player/queueMode/repeatOne", m_btnRepeatOne->isChecked());
    if (m_btnLoopFile)  s.setValue("player/queueMode/loopFile",  m_btnLoopFile->isChecked());
}

void PlaylistDrawer::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
    } else {
        m_anchor = anchor;
        show();
        raise();
        installClickFilter();
    }
}

void PlaylistDrawer::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
}

void PlaylistDrawer::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    // Prevent VideoPlayer's HUD auto-hide while hovering the drawer
    if (parent())
        QMetaObject::invokeMethod(parent(), "showControls");
}

void PlaylistDrawer::wheelEvent(QWheelEvent* event)
{
    // VIDEO_PLAYER_UI_POLISH follow-up 2026-04-23 (hemanth-reported
    // after Phase 4 ship): scrolling the playlist was also changing
    // volume because QListWidget doesn't consume wheel events when
    // it reaches its scroll limit (or when cursor is over non-list
    // regions of the drawer like the toolbar / auto-advance row),
    // so the event bubbled to VideoPlayer::wheelEvent which treats
    // wheel as volume. Accept here so nothing leaks past the drawer.
    // Child widgets (the QListWidget) still get the wheel event first
    // and scroll normally when possible — this only short-circuits
    // the otherwise-propagating residual.
    event->accept();
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

bool PlaylistDrawer::eventFilter(QObject* /*obj*/, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        if (rect().contains(mapFromGlobal(gp)))
            return false;
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        return onAnchor;
    }
    return false;
}
