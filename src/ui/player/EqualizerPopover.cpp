#include "EqualizerPopover.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMouseEvent>
#include <QSettings>
#include <QVBoxLayout>

constexpr int EqualizerPopover::BAND_FREQS[];

// PLAYER_UX_FIX Phase 6.3 — built-in preset gains (dB per band, 10 bands
// at 31/62/125/250/500/1k/2k/4k/8k/16k Hz). Matches industry-standard
// graphic-EQ starting points; users tweak from these or save customs.
namespace {
struct EqPreset {
    const char* name;
    int gains[10];
};
constexpr EqPreset BUILTIN_PRESETS[] = {
    { "Flat",          {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 } },
    { "Rock",          { +5, +3, -2, -5, -3, -2, +2, +5, +6, +6 } },
    { "Pop",           { -1, +1, +3, +4, +4, +2,  0, -1, -1, -1 } },
    { "Jazz",          { +3, +2, +1, +2, -2, -2,  0, +1, +2, +3 } },
    { "Classical",     { +3, +2, +1,  0,  0,  0, -2, -2, -2, -3 } },
    { "Bass Boost",    { +8, +7, +5, +3, +1,  0,  0,  0,  0,  0 } },
    { "Treble Boost",  {  0,  0,  0,  0, +1, +2, +3, +5, +6, +7 } },
    { "Vocal Boost",   { -3, -2, -1, +2, +4, +5, +4, +2, -1, -2 } },
};
constexpr int BUILTIN_COUNT = sizeof(BUILTIN_PRESETS) / sizeof(BUILTIN_PRESETS[0]);
}  // namespace

EqualizerPopover::EqualizerPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("EqualizerPopover");
    setFixedWidth(340);

    setStyleSheet(
        "QFrame#EqualizerPopover {"
        "  background: rgba(16, 16, 16, 240);"
        "  border: 1px solid rgba(255, 255, 255, 31);"
        "  border-radius: 8px;"
        "}"
    );

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(4);

    // VIDEO_PLAYER_UI_POLISH Phase 4 2026-04-23 — audit finding #7
    // ("EQ popover reads more like a developer widget than an intentional
    // audio surface; sliders and frequency labels are tiny"): raise the
    // section header 10 → 13 px, companion band-slider + label sizes
    // below also grow so the panel has breathing room.
    auto* header = new QLabel("Equalizer");
    header->setStyleSheet(
        "color: rgba(214,194,164,240); font-size: 13px; font-weight: 700; border: none;"
    );
    lay->addWidget(header);

    // PLAYER_UX_FIX Phase 6.3 — preset combo + save button.
    // Row sits above the band sliders; matches QMPlay2 + IINA's EQ
    // profile-picker pattern. Custom profiles persist under QSettings
    // "eq/profiles/<name>" and append to this combo's model after the
    // built-ins. The "Save as…" button prompts for a name and stores
    // the current slider state.
    auto* presetRow = new QHBoxLayout();
    presetRow->setSpacing(6);

    m_presetCombo = new QComboBox();
    m_presetCombo->setStyleSheet(
        "QComboBox { background: rgba(40,40,40,230); color: rgba(220,220,220,240);"
        "  border: 1px solid rgba(255,255,255,30); border-radius: 4px;"
        "  padding: 3px 8px; font-size: 10px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: rgb(30,30,30);"
        "  color: rgba(220,220,220,240); selection-background-color: rgba(214,194,164,80); }"
    );
    populatePresetCombo();
    // activated (user picked) vs currentIndexChanged (any change) — we
    // want to react only to user picks, not programmatic refreshes.
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int) { applyPreset(m_presetCombo->currentText()); });
    presetRow->addWidget(m_presetCombo, 1);

    m_saveBtn = new QPushButton("Save as\u2026");   // "…" U+2026
    m_saveBtn->setStyleSheet(
        "QPushButton { background: rgba(40,40,40,230); color: #ccc;"
        "  border: 1px solid rgba(255,255,255,0.1); border-radius: 4px;"
        "  padding: 3px 10px; font-size: 10px; }"
        "QPushButton:hover { background: rgba(60,60,60,230); }"
    );
    connect(m_saveBtn, &QPushButton::clicked, this,
            &EqualizerPopover::saveCurrentAsPreset);
    presetRow->addWidget(m_saveBtn);

    lay->addLayout(presetRow);

    // Band sliders (vertical, side by side)
    auto* bandsRow = new QHBoxLayout();
    bandsRow->setSpacing(2);

    for (int i = 0; i < BAND_COUNT; ++i) {
        auto* col = new QVBoxLayout();
        col->setSpacing(2);
        col->setAlignment(Qt::AlignCenter);

        // Phase 4: val label alpha 120→220, font 8→10 px, width 28→32.
        m_valLabels[i] = new QLabel("0");
        m_valLabels[i]->setStyleSheet(
            "color: rgba(255,255,255,220); font-size: 10px; font-weight: 500; border: none;");
        m_valLabels[i]->setAlignment(Qt::AlignCenter);
        m_valLabels[i]->setFixedWidth(32);
        col->addWidget(m_valLabels[i]);

        // Phase 4: groove width 4→6, handle 10×10→14×14, slider height
        // 100→120 for taller gesture.
        m_sliders[i] = new QSlider(Qt::Vertical);
        m_sliders[i]->setRange(-12, 12);  // dB
        m_sliders[i]->setValue(0);
        m_sliders[i]->setFixedHeight(120);
        m_sliders[i]->setStyleSheet(
            "QSlider::groove:vertical {"
            "  width: 6px; background: rgba(255,255,255,35); border-radius: 3px;"
            "}"
            "QSlider::handle:vertical {"
            "  height: 14px; width: 14px; margin: 0 -4px;"
            "  background: #ddd; border-radius: 7px;"
            "}"
            "QSlider::handle:vertical:hover {"
            "  background: #fff;"
            "}"
        );
        col->addWidget(m_sliders[i], 0, Qt::AlignCenter);

        // Phase 4: freq label alpha 100→200, font 8→10 px, width 28→32.
        auto* freqLabel = new QLabel(BAND_LABELS[i]);
        freqLabel->setStyleSheet(
            "color: rgba(255,255,255,200); font-size: 10px; border: none;");
        freqLabel->setAlignment(Qt::AlignCenter);
        freqLabel->setFixedWidth(32);
        col->addWidget(freqLabel);

        bandsRow->addLayout(col);

        connect(m_sliders[i], &QSlider::valueChanged, this, &EqualizerPopover::onSliderChanged);
    }
    lay->addLayout(bandsRow);

    // Reset button
    auto* resetBtn = new QPushButton("Reset");
    resetBtn->setStyleSheet(
        "QPushButton { background: rgba(40,40,40,230); color: #ccc;"
        "  border: 1px solid rgba(255,255,255,0.1); border-radius: 4px;"
        "  padding: 4px 12px; font-size: 10px; }"
        "QPushButton:hover { background: rgba(60,60,60,230); }"
    );
    connect(resetBtn, &QPushButton::clicked, this, &EqualizerPopover::resetAll);
    lay->addWidget(resetBtn, 0, Qt::AlignCenter);

    // Batch 4.3 — Dynamic Range Compression toggle.
    // "Loud movie at low volume keeps dialogue audible" — sidecar-side
    // feed-forward compressor (threshold -12 dB, ratio 3:1, attack 10 ms,
    // release 100 ms) lights up when this is checked. Off by default.
    // Styled in the existing grayscale aesthetic (per feedback_no_color_no_emoji).
    m_drcCheck = new QCheckBox("Dynamic Range Compression");
    m_drcCheck->setStyleSheet(
        "QCheckBox { color: rgba(214,194,164,240); font-size: 10px; border: none; padding-top: 6px; }"
        "QCheckBox::indicator { width: 12px; height: 12px; }"
        "QCheckBox::indicator:unchecked {"
        "  background: rgba(40,40,40,230);"
        "  border: 1px solid rgba(255,255,255,0.2); border-radius: 2px;"
        "}"
        "QCheckBox::indicator:checked {"
        "  background: rgba(214,194,164,240);"
        "  border: 1px solid rgba(214,194,164,240); border-radius: 2px;"
        "}"
    );
    m_drcCheck->setChecked(false);
    connect(m_drcCheck, &QCheckBox::toggled, this, &EqualizerPopover::drcToggled);
    lay->addWidget(m_drcCheck, 0, Qt::AlignCenter);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        emit eqChanged(filterString());
    });

    hide();
}

void EqualizerPopover::onSliderChanged()
{
    for (int i = 0; i < BAND_COUNT; ++i)
        m_valLabels[i]->setText(QString::number(m_sliders[i]->value()));
    // Phase 6.3 — suppress per-band debounce restart during programmatic
    // preset application; applyPreset emits eqChanged once after all 10
    // bands settle to avoid 10x filter-chain rebuild on the sidecar.
    if (!m_applyingPreset) {
        m_debounce.start();
    }
}

void EqualizerPopover::populatePresetCombo()
{
    // PLAYER_UX_FIX Phase 6.3 — rebuild the combo's model with built-ins
    // followed by user-saved profiles. Called once on construction and
    // again after saveCurrentAsPreset persists a new one.
    const QString prior = m_presetCombo
        ? m_presetCombo->currentText()
        : QString();
    m_presetCombo->blockSignals(true);
    m_presetCombo->clear();

    // Built-ins first — "Flat" is the synthetic default when all bands
    // are zero. A separator between built-ins and user profiles helps
    // readability but isn't strictly required.
    for (const auto& p : BUILTIN_PRESETS) {
        m_presetCombo->addItem(QString::fromLatin1(p.name));
    }

    // Load user profiles from QSettings "eq/profiles" (QVariantMap of
    // <name> → QString gains "g0,g1,...,g9"). Append to combo.
    QSettings s("Tankoban", "Tankoban");
    s.beginGroup("eq/profiles");
    const QStringList userNames = s.childKeys();
    s.endGroup();
    if (!userNames.isEmpty()) {
        m_presetCombo->insertSeparator(m_presetCombo->count());
        for (const QString& name : userNames) {
            m_presetCombo->addItem(name);
        }
    }

    // Preserve prior selection if still present.
    if (!prior.isEmpty()) {
        const int idx = m_presetCombo->findText(prior);
        if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
    }
    m_presetCombo->blockSignals(false);
}

void EqualizerPopover::applyPreset(const QString& name)
{
    if (name.isEmpty()) return;

    // Resolve: built-in? user profile?
    int gains[BAND_COUNT] = {};
    bool resolved = false;

    for (const auto& p : BUILTIN_PRESETS) {
        if (name == QString::fromLatin1(p.name)) {
            for (int i = 0; i < BAND_COUNT; ++i) gains[i] = p.gains[i];
            resolved = true;
            break;
        }
    }

    if (!resolved) {
        QSettings s("Tankoban", "Tankoban");
        const QString key = QStringLiteral("eq/profiles/") + name;
        if (s.contains(key)) {
            const QStringList parts = s.value(key).toString().split(',');
            if (parts.size() == BAND_COUNT) {
                for (int i = 0; i < BAND_COUNT; ++i) {
                    gains[i] = qBound(-12, parts[i].toInt(), 12);
                }
                resolved = true;
            }
        }
    }

    if (!resolved) return;

    // Apply all 10 bands with the apply-preset guard active so each
    // per-band onSliderChanged skips the debounce. Emit eqChanged once
    // at the end so the sidecar rebuilds its audio filter exactly once.
    m_applyingPreset = true;
    for (int i = 0; i < BAND_COUNT; ++i) {
        m_sliders[i]->setValue(gains[i]);
    }
    m_applyingPreset = false;
    emit eqChanged(filterString());
}

void EqualizerPopover::saveCurrentAsPreset()
{
    // Prompt for name; blank or cancelled → no-op. Names that collide
    // with a built-in are rejected (prevents overriding "Flat" etc.).
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Save EQ Profile"),
        QStringLiteral("Profile name:"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    for (const auto& p : BUILTIN_PRESETS) {
        if (name == QString::fromLatin1(p.name)) return;   // reserved
    }

    // Serialize gains as a simple comma-separated int list — simpler
    // than QVariantList for QSettings round-trip on Windows.
    QStringList parts;
    for (int i = 0; i < BAND_COUNT; ++i) {
        parts.append(QString::number(m_sliders[i]->value()));
    }

    QSettings s("Tankoban", "Tankoban");
    s.setValue(QStringLiteral("eq/profiles/") + name, parts.join(','));

    // Refresh combo + select the just-saved profile.
    populatePresetCombo();
    const int idx = m_presetCombo->findText(name);
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

void EqualizerPopover::resetAll()
{
    for (int i = 0; i < BAND_COUNT; ++i)
        m_sliders[i]->setValue(0);
}

QString EqualizerPopover::filterString() const
{
    QStringList parts;
    for (int i = 0; i < BAND_COUNT; ++i) {
        int gain = m_sliders[i]->value();
        if (gain == 0) continue;
        // octave bandwidth for each band
        parts << QString("equalizer=f=%1:t=o:w=1:g=%2")
                    .arg(BAND_FREQS[i]).arg(gain);
    }
    return parts.join(",");
}

bool EqualizerPopover::isActive() const
{
    for (int i = 0; i < BAND_COUNT; ++i)
        if (m_sliders[i]->value() != 0) return true;
    return false;
}

void EqualizerPopover::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    if (anchor) anchorAbove(anchor);
    show();
    raise();
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void EqualizerPopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) return;
    QPoint pos = anchor->mapTo(p, anchor->rect().topRight());
    int pw = sizeHint().width();
    int ph = sizeHint().height();
    setGeometry(qMax(0, pos.x() - pw), qMax(0, pos.y() - ph - 8), pw, ph);
}

void EqualizerPopover::dismiss()
{
    if (m_clickFilterInstalled) {
        if (auto* app = QApplication::instance())
            app->removeEventFilter(this);
        m_clickFilterInstalled = false;
    }
    hide();
    m_anchor.clear();
}

bool EqualizerPopover::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        if (rect().contains(mapFromGlobal(gp)))
            return false;
        // Outside popover — if the press is on the anchor chip, swallow
        // (return true) after dismiss so the chip's clicked() signal
        // doesn't immediately re-toggle us open.
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        return onAnchor;
    }
    return false;
}

void EqualizerPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void EqualizerPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}
