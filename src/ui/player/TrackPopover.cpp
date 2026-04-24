#include "TrackPopover.h"

#include <cmath>   // Phase 6.2 — std::floor for sample-rate kHz rendering
#include <QApplication>
#include <QFont>   // Phase 6.2 — bold the selected-track list item
#include <QJsonObject>
#include <QLocale> // Phase 6.2 — ISO639 language-code expansion
#include <QMouseEvent>
#include <QWheelEvent>

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
    // VIDEO_PLAYER_UI_POLISH Phase 4 2026-04-23 — audit finding #6
    // ("subtitle delay row is especially rough: the decrement control
    // reads as a tiny unlabeled block, while + and Reset are visually
    // tiny and easy to miss"): bump padding 2/8 → 4/10 and font-size
    // 11 → 12 so the buttons feel like proper tap targets (delay row
    // also grows its button height to 30 px in the row layout below).
    "QPushButton {"
    "  background: rgba(40,40,40,230);"
    "  color: #ccc;"
    "  border: 1px solid rgba(255,255,255,0.1);"
    "  border-radius: 4px;"
    "  padding: 4px 10px;"
    "  font-size: 12px;"
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
    // VIDEO_PLAYER_UI_POLISH Phase 4 2026-04-23 — audit finding #6
    // wanted explicit section separation. Add a "Subtitle Delay"
    // sub-label so the row's purpose is named, not inferred.
    auto* subDelayLabel = new QLabel("Subtitle Delay");
    subDelayLabel->setStyleSheet(LABEL_SS);
    lay->addWidget(subDelayLabel);

    auto* delayRow = new QHBoxLayout;
    delayRow->setSpacing(6);

    m_delayMinus = new QPushButton(QStringLiteral("\u2212"));
    m_delayMinus->setToolTip("Delay subtitles -100 ms");
    m_delayMinus->setFixedSize(36, 30);
    m_delayMinus->setStyleSheet(BTN_SS);
    connect(m_delayMinus, &QPushButton::clicked,
            this, [this]() { emit subDelayAdjusted(-100); });
    delayRow->addWidget(m_delayMinus);

    m_delayLabel = new QLabel("0ms");
    m_delayLabel->setStyleSheet(
        "color: rgba(255,255,255,0.92); font-size: 12px; font-weight: 600; border: none;");
    m_delayLabel->setAlignment(Qt::AlignCenter);
    delayRow->addWidget(m_delayLabel, 1);

    m_delayPlus = new QPushButton("+");
    m_delayPlus->setFixedSize(36, 30);
    m_delayPlus->setStyleSheet(BTN_SS);
    connect(m_delayPlus, &QPushButton::clicked,
            this, [this]() { emit subDelayAdjusted(100); });
    delayRow->addWidget(m_delayPlus);

    m_delayReset = new QPushButton("Reset");
    m_delayReset->setFixedSize(60, 30);
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

    // VIDEO_SUB_POSITION 2026-04-24 — vertical baseline slider. 0..100
    // percent, 100 = bottom (mpv `sub-pos` parity). Independent of the
    // libass MarginV "Margin" slider above (which is a fine-tune in
    // pixels); this one is a coarse percent for lifting subs off the
    // letterbox bars / scoreboards. Persisted globally via QSettings
    // by VideoPlayer; signal subPositionChanged carries int 0..100.
    auto* posRow = new QHBoxLayout;
    posRow->setSpacing(4);
    auto* posLbl = new QLabel("Position");
    posLbl->setStyleSheet(LABEL_SS);
    posLbl->setFixedWidth(42);
    posRow->addWidget(posLbl);

    m_subPosSlider = new QSlider(Qt::Horizontal);
    m_subPosSlider->setRange(0, 100);
    m_subPosSlider->setValue(100);
    m_subPosSlider->setStyleSheet(SLIDER_SS);
    posRow->addWidget(m_subPosSlider, 1);

    m_subPosVal = new QLabel("100");
    m_subPosVal->setStyleSheet(LABEL_SS);
    m_subPosVal->setFixedWidth(24);
    posRow->addWidget(m_subPosVal);
    lay->addLayout(posRow);

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

    // VIDEO_SUB_POSITION 2026-04-24 — sub-position is its own concern,
    // not part of the style payload (subStyleChanged carries 5 fields,
    // none of them are baseline-percent). Direct connection, no debounce
    // — sidecar atomic store is cheap and slider drag is bounded by Qt
    // event coalescing.
    connect(m_subPosSlider, &QSlider::valueChanged, this, [this](int v) {
        m_subPosVal->setText(QString::number(v));
        emit subPositionChanged(v);
    });

    hide();
}

void TrackPopover::setSubPosition(int percent)
{
    if (!m_subPosSlider) return;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    QSignalBlocker b(m_subPosSlider);
    m_subPosSlider->setValue(percent);
    if (m_subPosVal) m_subPosVal->setText(QString::number(percent));
}

int TrackPopover::subPosition() const
{
    return m_subPosSlider ? m_subPosSlider->value() : 100;
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

// PLAYER_UX_FIX Phase 6.2 — IINA-parity helpers.
// Language-code expansion: converts 2/3-letter ISO 639 codes ("en" /
// "eng" / "jpn") to the human-readable language name ("English" /
// "Japanese"). Qt's QLocale accepts ISO codes directly. If the code is
// unknown or empty, we fall back to the raw uppercase code (pre-6.2
// behavior).
static QString expandLangCode(const QString& code)
{
    if (code.isEmpty()) return QString();
    const QLocale loc(code);
    const QString name = QLocale::languageToString(loc.language());
    // QLocale returns "C" when the code isn't recognized — don't display
    // that; fall back to the raw code uppercased.
    if (name.isEmpty() || name == QStringLiteral("C")) {
        return code.toUpper();
    }
    return name;
}

// Channel-count renderer: FFmpeg integer → Dolby-style label.
// 1 → "Mono", 2 → "Stereo", 6 → "5.1", 8 → "7.1", other → "Nch".
static QString describeChannels(int channels)
{
    switch (channels) {
        case 0:  return QString();   // unknown — pre-6.2 sidecar payload
        case 1:  return QStringLiteral("Mono");
        case 2:  return QStringLiteral("Stereo");
        case 6:  return QStringLiteral("5.1");
        case 8:  return QStringLiteral("7.1");
        default: return QStringLiteral("%1ch").arg(channels);
    }
}

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
        // Phase 6.2 fields — tolerated-missing for legacy sidecar payloads.
        const bool isDefault = track.value("default").toBool(false);
        const bool isForced  = track.value("forced").toBool(false);
        const int  channels  = track.value("channels").toInt(0);
        const int  sampleRate = track.value("sample_rate").toInt(0);

        // Build IINA-style inline label:
        //   primary = title || expanded-language || "Track N"
        //   secondary bits (dot-separated): channels, kHz, codec, Default, Forced
        const QString langHuman = expandLangCode(lang);
        QString primary;
        if (!title.isEmpty())
            primary = title;
        else if (!langHuman.isEmpty())
            primary = langHuman;
        else
            primary = QStringLiteral("Track %1").arg(tid);

        QStringList rightBits;
        if (ttype == "audio") {
            const QString chLabel = describeChannels(channels);
            if (!chLabel.isEmpty()) rightBits.append(chLabel);
            if (sampleRate > 0) {
                // Render as kHz with one decimal only when non-integer
                // (common rates 44100/48000/96000 display as 44.1/48/96).
                const double khz = sampleRate / 1000.0;
                rightBits.append(std::floor(khz) == khz
                    ? QStringLiteral("%1kHz").arg(static_cast<int>(khz))
                    : QStringLiteral("%1kHz").arg(khz, 0, 'f', 1));
            }
        }
        if (!codec.isEmpty()) rightBits.append(codec);
        if (isDefault) rightBits.append(QStringLiteral("Default"));
        if (isForced)  rightBits.append(QStringLiteral("Forced"));

        QString label = rightBits.isEmpty()
            ? primary
            : primary + QStringLiteral("   \u00b7 ") + rightBits.join(QStringLiteral(" \u00b7 "));

        if (ttype == "audio") {
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, tid);
            if (tid == currentAudioId) {
                item->setSelected(true);
                // Phase 6.2 — bold the selected track for stronger
                // "this is the active one" affordance. QListWidget's
                // selection highlight is subtle in the Noir palette.
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
            m_audioList->addItem(item);
        } else if (ttype == "subtitle") {
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, tid);
            if (tid == currentSubId && subVisible) {
                item->setSelected(true);
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
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
    m_anchor = anchor;
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

bool TrackPopover::eventFilter(QObject* /*obj*/, QEvent* event)
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

void TrackPopover::wheelEvent(QWheelEvent* event)
{
    // VIDEO_POPOVER_WHEEL 2026-04-24 (hemanth-reported): mirror the
    // PlaylistDrawer 2026-04-23 fix. The audio + sub QListWidgets do not
    // accept wheel events at scroll limits or when the cursor is over the
    // Style sliders / delay row gaps, so the event bubbled to
    // VideoPlayer::wheelEvent which treats wheel as volume — popover
    // scroll changed playback volume simultaneously. Accept here so
    // nothing leaks past the popover; child widgets still get the wheel
    // first and scroll normally when possible.
    event->accept();
}

// ---------------------------------------------------------------
// Internal
// ---------------------------------------------------------------

void TrackPopover::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
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
