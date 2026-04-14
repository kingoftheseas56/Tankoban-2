#include "TrackPopover.h"

#include <QApplication>
#include <QJsonObject>
#include <QMouseEvent>

static const int MAX_VISIBLE_ROWS = 4;
static const int ROW_HEIGHT       = 30;

static const char* TRACK_LIST_SS =
    "QListWidget {"
    "  background: transparent;"
    "  border: none;"
    "  outline: none;"
    "}"
    "QListWidget::item {"
    "  color: rgba(255,255,255,0.92);"
    "  padding: 5px 10px;"
    "  border-radius: 4px;"
    "  font-size: 12px;"
    "}"
    "QListWidget::item:hover {"
    "  background: rgba(255,255,255,0.08);"
    "}"
    "QListWidget::item:selected {"
    "  color: rgba(214,194,164,0.95);"
    "  background: rgba(255,255,255,0.06);"
    "}";

static const char* BTN_SS =
    "QPushButton {"
    "  background: rgba(40,40,40,230);"
    "  color: #ccc;"
    "  border: 1px solid rgba(255,255,255,0.1);"
    "  border-radius: 4px;"
    "  padding: 2px 8px;"
    "  font-size: 11px;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(60,60,60,230);"
    "}";

static const char* SLIDER_SS =
    "QSlider::groove:horizontal {"
    "  height: 4px;"
    "  background: rgba(255,255,255,0.1);"
    "  border-radius: 2px;"
    "}"
    "QSlider::handle:horizontal {"
    "  width: 12px;"
    "  height: 12px;"
    "  margin: -4px 0;"
    "  background: #ccc;"
    "  border-radius: 6px;"
    "}";

static const char* HEADER_SS =
    "color: rgba(214,194,164,0.95);"
    "font-size: 11px;"
    "font-weight: 700;"
    "border: none;";

static const char* LABEL_SS =
    "color: rgba(255,255,255,0.55);"
    "font-size: 10px;"
    "border: none;";

TrackPopover::TrackPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("TrackPopover");
    setStyleSheet(
        "#TrackPopover {"
        "  background: rgba(16,16,16,240);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 8px;"
        "}"
    );
    setMaximumWidth(320);
    setMinimumWidth(200);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(6);

    // --- Audio section ---
    auto* audioHeader = new QLabel("Audio");
    audioHeader->setStyleSheet(HEADER_SS);
    lay->addWidget(audioHeader);

    m_audioList = new QListWidget;
    m_audioList->setStyleSheet(TRACK_LIST_SS);
    m_audioList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_audioList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(m_audioList, &QListWidget::itemClicked,
            this, &TrackPopover::onAudioClicked);
    lay->addWidget(m_audioList);

    // --- Divider ---
    auto* div1 = new QFrame;
    div1->setFrameShape(QFrame::HLine);
    div1->setStyleSheet("color: rgba(255,255,255,0.08);");
    div1->setFixedHeight(1);
    lay->addWidget(div1);

    // --- Subtitle section ---
    auto* subHeader = new QLabel("Subtitle");
    subHeader->setStyleSheet(HEADER_SS);
    lay->addWidget(subHeader);

    m_subList = new QListWidget;
    m_subList->setStyleSheet(TRACK_LIST_SS);
    m_subList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_subList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(m_subList, &QListWidget::itemClicked,
            this, &TrackPopover::onSubClicked);
    lay->addWidget(m_subList);

    // --- Delay row ---
    auto* delayRow = new QHBoxLayout;
    delayRow->setSpacing(4);

    m_delayMinus = new QPushButton(QStringLiteral("\u2212"));
    m_delayMinus->setFixedSize(28, 24);
    m_delayMinus->setStyleSheet(BTN_SS);
    connect(m_delayMinus, &QPushButton::clicked,
            this, [this]() { emit subDelayAdjusted(-100); });
    delayRow->addWidget(m_delayMinus);

    m_delayLabel = new QLabel("0ms");
    m_delayLabel->setStyleSheet(
        "color: rgba(255,255,255,0.92); font-size: 11px; border: none;");
    m_delayLabel->setAlignment(Qt::AlignCenter);
    delayRow->addWidget(m_delayLabel, 1);

    m_delayPlus = new QPushButton("+");
    m_delayPlus->setFixedSize(28, 24);
    m_delayPlus->setStyleSheet(BTN_SS);
    connect(m_delayPlus, &QPushButton::clicked,
            this, [this]() { emit subDelayAdjusted(100); });
    delayRow->addWidget(m_delayPlus);

    m_delayReset = new QPushButton("Reset");
    m_delayReset->setFixedSize(44, 24);
    m_delayReset->setStyleSheet(BTN_SS);
    connect(m_delayReset, &QPushButton::clicked,
            this, [this]() { emit subDelayAdjusted(0); });
    delayRow->addWidget(m_delayReset);

    lay->addLayout(delayRow);

    // --- Divider 2 ---
    auto* div2 = new QFrame;
    div2->setFrameShape(QFrame::HLine);
    div2->setStyleSheet("color: rgba(255,255,255,0.08);");
    div2->setFixedHeight(1);
    lay->addWidget(div2);

    // --- Style section ---
    auto* styleHeader = new QLabel("Style");
    styleHeader->setStyleSheet(HEADER_SS);
    lay->addWidget(styleHeader);

    // Font size row
    auto* fsRow = new QHBoxLayout;
    fsRow->setSpacing(4);
    auto* fsLbl = new QLabel("Size");
    fsLbl->setStyleSheet(LABEL_SS);
    fsLbl->setFixedWidth(42);
    fsRow->addWidget(fsLbl);

    m_fontSizeSlider = new QSlider(Qt::Horizontal);
    m_fontSizeSlider->setRange(16, 48);
    m_fontSizeSlider->setValue(24);
    m_fontSizeSlider->setStyleSheet(SLIDER_SS);
    fsRow->addWidget(m_fontSizeSlider, 1);

    m_fontSizeVal = new QLabel("24");
    m_fontSizeVal->setStyleSheet(LABEL_SS);
    m_fontSizeVal->setFixedWidth(24);
    fsRow->addWidget(m_fontSizeVal);
    lay->addLayout(fsRow);

    // Margin row
    auto* mgRow = new QHBoxLayout;
    mgRow->setSpacing(4);
    auto* mgLbl = new QLabel("Margin");
    mgLbl->setStyleSheet(LABEL_SS);
    mgLbl->setFixedWidth(42);
    mgRow->addWidget(mgLbl);

    m_marginSlider = new QSlider(Qt::Horizontal);
    m_marginSlider->setRange(0, 100);
    m_marginSlider->setValue(40);
    m_marginSlider->setStyleSheet(SLIDER_SS);
    mgRow->addWidget(m_marginSlider, 1);

    m_marginVal = new QLabel("40");
    m_marginVal->setStyleSheet(LABEL_SS);
    m_marginVal->setFixedWidth(24);
    mgRow->addWidget(m_marginVal);
    lay->addLayout(mgRow);

    // Outline checkbox
    m_outlineCb = new QCheckBox("Outline");
    m_outlineCb->setChecked(true);
    m_outlineCb->setStyleSheet(
        "QCheckBox { color: rgba(255,255,255,0.92); font-size: 11px; border: none; }");
    lay->addWidget(m_outlineCb);

    // Font color combo
    auto* fcRow = new QHBoxLayout;
    fcRow->setSpacing(4);
    auto* fcLbl = new QLabel("Color");
    fcLbl->setStyleSheet(LABEL_SS);
    fcLbl->setFixedWidth(42);
    fcRow->addWidget(fcLbl);
    m_fontColorCombo = new QComboBox();
    m_fontColorCombo->addItem("White",  "#ffffff");
    m_fontColorCombo->addItem("Yellow", "#ffff00");
    m_fontColorCombo->addItem("Cyan",   "#00ffff");
    m_fontColorCombo->addItem("Green",  "#00ff00");
    m_fontColorCombo->setStyleSheet(
        "QComboBox { background: rgba(40,40,40,230); color: #ccc;"
        "  font-size: 11px; border: 1px solid rgba(255,255,255,0.1);"
        "  border-radius: 4px; padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: rgb(30,30,30);"
        "  color: #ccc; selection-background-color: rgba(255,255,255,0.1); }");
    fcRow->addWidget(m_fontColorCombo, 1);
    lay->addLayout(fcRow);

    // Background opacity row
    auto* bgRow = new QHBoxLayout;
    bgRow->setSpacing(4);
    auto* bgLbl = new QLabel("BG");
    bgLbl->setStyleSheet(LABEL_SS);
    bgLbl->setFixedWidth(42);
    bgRow->addWidget(bgLbl);
    m_bgOpacitySlider = new QSlider(Qt::Horizontal);
    m_bgOpacitySlider->setRange(0, 255);
    m_bgOpacitySlider->setValue(140);
    m_bgOpacitySlider->setStyleSheet(SLIDER_SS);
    bgRow->addWidget(m_bgOpacitySlider, 1);
    m_bgOpacityVal = new QLabel("140");
    m_bgOpacityVal->setStyleSheet(LABEL_SS);
    m_bgOpacityVal->setFixedWidth(24);
    bgRow->addWidget(m_bgOpacityVal);
    lay->addLayout(bgRow);

    // --- Style debounce timer ---
    m_styleDebounce = new QTimer(this);
    m_styleDebounce->setSingleShot(true);
    m_styleDebounce->setInterval(300);
    connect(m_styleDebounce, &QTimer::timeout,
            this, &TrackPopover::emitStyleChanged);

    connect(m_fontSizeSlider, &QSlider::valueChanged,
            this, &TrackPopover::onStyleWidgetChanged);
    connect(m_marginSlider, &QSlider::valueChanged,
            this, &TrackPopover::onStyleWidgetChanged);
    connect(m_outlineCb, &QCheckBox::toggled,
            this, [this](bool) { onStyleWidgetChanged(); });
    connect(m_fontColorCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { onStyleWidgetChanged(); });
    connect(m_bgOpacitySlider, &QSlider::valueChanged,
            this, &TrackPopover::onStyleWidgetChanged);

    hide();
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

void TrackPopover::populate(const QJsonArray& tracks, int currentAudioId,
                            int currentSubId, bool subVisible)
{
    m_audioList->clear();
    m_subList->clear();

    for (const auto& val : tracks) {
        QJsonObject track = val.toObject();
        int tid          = track.value("id").toVariant().toInt();
        QString ttype    = track.value("type").toString();
        QString lang     = track.value("lang").toString();
        QString title    = track.value("title").toString();
        QString codec    = track.value("codec").toString();

        QStringList labelParts;
        if (!title.isEmpty())
            labelParts.append(title);
        else if (!lang.isEmpty())
            labelParts.append(lang.toUpper());
        else
            labelParts.append(QStringLiteral("Track %1").arg(tid));
        if (!codec.isEmpty())
            labelParts.append(QStringLiteral("(%1)").arg(codec));

        QString label = labelParts.join(' ');

        if (ttype == "audio") {
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, tid);
            if (tid == currentAudioId)
                item->setSelected(true);
            m_audioList->addItem(item);
        } else if (ttype == "subtitle") {
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, tid);
            if (tid == currentSubId && subVisible)
                item->setSelected(true);
            m_subList->addItem(item);
        }
    }

    // Prepend "Off" to subtitle list
    auto* offItem = new QListWidgetItem("Off");
    offItem->setData(Qt::UserRole, 0);
    m_subList->insertItem(0, offItem);
    if (!subVisible || currentSubId <= 0)
        offItem->setSelected(true);

    // Size lists to content (max 4 rows)
    for (auto* lw : {m_audioList, m_subList}) {
        int rows = qMin(lw->count(), MAX_VISIBLE_ROWS);
        lw->setFixedHeight(qMax(rows, 1) * ROW_HEIGHT + 4);
    }
}

void TrackPopover::setDelay(int ms)
{
    if (ms != 0)
        m_delayLabel->setText(QStringLiteral("%1%2ms").arg(ms > 0 ? "+" : "").arg(ms));
    else
        m_delayLabel->setText("0ms");
}

void TrackPopover::setStyle(int fontSize, int margin, bool outline)
{
    m_fontSizeSlider->blockSignals(true);
    m_marginSlider->blockSignals(true);
    m_outlineCb->blockSignals(true);

    m_fontSizeSlider->setValue(fontSize);
    m_marginSlider->setValue(margin);
    m_outlineCb->setChecked(outline);
    m_fontSizeVal->setText(QString::number(fontSize));
    m_marginVal->setText(QString::number(margin));

    m_fontSizeSlider->blockSignals(false);
    m_marginSlider->blockSignals(false);
    m_outlineCb->blockSignals(false);
}

void TrackPopover::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    if (anchor)
        anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

bool TrackPopover::isOpen() const
{
    return isVisible();
}

int TrackPopover::subFontSize() const
{
    return m_fontSizeSlider->value();
}

int TrackPopover::subMargin() const
{
    return m_marginSlider->value();
}

bool TrackPopover::subOutline() const
{
    return m_outlineCb->isChecked();
}

// ---------------------------------------------------------------
// Event filter — click-outside dismiss
// ---------------------------------------------------------------

bool TrackPopover::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        QPoint local = mapFromGlobal(me->globalPosition().toPoint());
        if (!rect().contains(local))
            dismiss();
    }
    return false;
}

void TrackPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void TrackPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}

// ---------------------------------------------------------------
// Internal
// ---------------------------------------------------------------

void TrackPopover::dismiss()
{
    removeClickFilter();
    hide();
}

void TrackPopover::installClickFilter()
{
    if (m_clickFilterInstalled)
        return;
    auto* app = QApplication::instance();
    if (app) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void TrackPopover::removeClickFilter()
{
    if (!m_clickFilterInstalled)
        return;
    auto* app = QApplication::instance();
    if (app)
        app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

void TrackPopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p)
        return;
    QPoint anchorPos = anchor->mapTo(p, anchor->rect().topRight());
    int pw = sizeHint().width();
    int ph = sizeHint().height();
    int x  = qMax(0, anchorPos.x() - pw);
    int y  = qMax(0, anchorPos.y() - ph - 8);
    setGeometry(x, y, pw, ph);
}

void TrackPopover::onAudioClicked(QListWidgetItem* item)
{
    int tid = item->data(Qt::UserRole).toInt();
    emit audioTrackSelected(tid);
    dismiss();
}

void TrackPopover::onSubClicked(QListWidgetItem* item)
{
    int tid = item->data(Qt::UserRole).toInt();
    emit subtitleTrackSelected(tid);
    dismiss();
}

void TrackPopover::onStyleWidgetChanged()
{
    m_fontSizeVal->setText(QString::number(m_fontSizeSlider->value()));
    m_marginVal->setText(QString::number(m_marginSlider->value()));
    m_bgOpacityVal->setText(QString::number(m_bgOpacitySlider->value()));
    m_styleDebounce->start();
}

QString TrackPopover::subFontColor() const
{
    return m_fontColorCombo->currentData().toString();
}

int TrackPopover::subBgOpacity() const
{
    return m_bgOpacitySlider->value();
}

void TrackPopover::emitStyleChanged()
{
    emit subStyleChanged(
        m_fontSizeSlider->value(),
        m_marginSlider->value(),
        m_outlineCb->isChecked(),
        m_fontColorCombo->currentData().toString(),
        m_bgOpacitySlider->value()
    );
}
