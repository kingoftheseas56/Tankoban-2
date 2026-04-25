#include "AudioPopover.h"

#include <cmath>
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStringList>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

// VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — bumped from 6 → 12 per
// Hemanth: a 2-track audio popover should NEVER need a scrollbar.
// Typical files have 1-3 audio tracks; 12 covers 99% of cases without
// making the popover monstrous.
static const int MAX_VISIBLE_ROWS = 12;
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

static const char* HEADER_SS =
    "color: rgba(214,194,164,0.95);"
    "font-size: 11px;"
    "font-weight: 700;"
    "border: none;";

// IINA-parity language-code expansion (ported from TrackPopover.cpp).
static QString expandLangCode(const QString& code)
{
    if (code.isEmpty()) return QString();
    const QLocale loc(code);
    const QString name = QLocale::languageToString(loc.language());
    if (name.isEmpty() || name == QStringLiteral("C")) return code.toUpper();
    return name;
}

static QString describeChannels(int channels)
{
    switch (channels) {
        case 0:  return QString();
        case 1:  return QStringLiteral("Mono");
        case 2:  return QStringLiteral("Stereo");
        case 6:  return QStringLiteral("5.1");
        case 8:  return QStringLiteral("7.1");
        default: return QStringLiteral("%1ch").arg(channels);
    }
}

AudioPopover::AudioPopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("AudioPopover");
    setStyleSheet(
        "#AudioPopover {"
        "  background: rgba(16,16,16,240);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 8px;"
        "}"
    );
    setMaximumWidth(320);
    setMinimumWidth(220);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(6);

    auto* header = new QLabel("Audio");
    header->setStyleSheet(HEADER_SS);
    lay->addWidget(header);

    m_list = new QListWidget;
    m_list->setStyleSheet(TRACK_LIST_SS);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — see
    // SubtitlePopover for the same fix; ScrollBarAlwaysOff prevents the
    // pixel-rounding mismatch from showing a scrollbar on 2-track files.
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::itemClicked, this, &AudioPopover::onItemClicked);
    lay->addWidget(m_list);

    hide();
}

void AudioPopover::populate(const QJsonArray& tracks, int currentAudioId)
{
    m_list->clear();

    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — caller is always
    // responsible for passing audio-only data (m_audioTracks at the chip-
    // click site, or the merged-but-already-audio-filtered slice). The
    // legacy `type != "audio"` filter assumed mixed JSON, which DOESN'T
    // hold for the chip-click path where m_audioTracks entries lack the
    // "type" discriminator (it gets stripped at extraction time). Removing
    // the filter unifies both entry points around audio-only input.
    for (const auto& val : tracks) {
        QJsonObject track = val.toObject();
        const int     tid        = track.value("id").toVariant().toInt();
        const QString lang       = track.value("lang").toString();
        const QString title      = track.value("title").toString();
        const QString codec      = track.value("codec").toString();
        const bool    isDefault  = track.value("default").toBool(false);
        const bool    isForced   = track.value("forced").toBool(false);
        const int     channels   = track.value("channels").toInt(0);
        const int     sampleRate = track.value("sample_rate").toInt(0);

        const QString langHuman = expandLangCode(lang);
        QString primary;
        if (!title.isEmpty())          primary = title;
        else if (!langHuman.isEmpty()) primary = langHuman;
        else                           primary = QStringLiteral("Track %1").arg(tid);

        QStringList rightBits;
        const QString chLabel = describeChannels(channels);
        if (!chLabel.isEmpty()) rightBits.append(chLabel);
        if (sampleRate > 0) {
            const double khz = sampleRate / 1000.0;
            rightBits.append(std::floor(khz) == khz
                ? QStringLiteral("%1kHz").arg(static_cast<int>(khz))
                : QStringLiteral("%1kHz").arg(khz, 0, 'f', 1));
        }
        if (!codec.isEmpty()) rightBits.append(codec);
        if (isDefault) rightBits.append(QStringLiteral("Default"));
        if (isForced)  rightBits.append(QStringLiteral("Forced"));

        QString label = rightBits.isEmpty()
            ? primary
            : primary + QStringLiteral("   · ") + rightBits.join(QStringLiteral(" · "));

        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, tid);
        if (tid == currentAudioId) {
            item->setSelected(true);
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_list->addItem(item);
    }

    const int rows = qMin(m_list->count(), MAX_VISIBLE_ROWS);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — sizeHintForRow
    // for actual rendered row height (see SubtitlePopover.cpp for full
    // rationale). Hardcoded ROW_HEIGHT=30 underestimated the rendered
    // height; padding to 8px instead of 4 absorbs frame slack.
    int actualRowH = ROW_HEIGHT;
    if (m_list->count() > 0) {
        const int hint = m_list->sizeHintForRow(0);
        if (hint > 0) actualRowH = hint;
    }
    const int listFixedH = qMax(rows, 1) * actualRowH + 8;
    m_list->setFixedHeight(listFixedH);
    qInfo() << "[AudioPopover] populate listH=" << listFixedH
            << "rows=" << rows << "count=" << m_list->count()
            << "actualRowH=" << actualRowH;
}

void AudioPopover::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    if (anchor) anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

void AudioPopover::onItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    const int tid = item->data(Qt::UserRole).toInt();
    emit audioTrackSelected(tid);
    dismiss();
}

bool AudioPopover::eventFilter(QObject* /*obj*/, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        if (rect().contains(mapFromGlobal(gp))) return false;
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        return onAnchor;
    }
    return false;
}

void AudioPopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void AudioPopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}

void AudioPopover::wheelEvent(QWheelEvent* event)
{
    // Mirror PlaylistDrawer 2026-04-23 + TrackPopover 2026-04-24 fix:
    // accept here so wheel doesn't bubble to VideoPlayer (which treats
    // wheel as volume).
    event->accept();
}

void AudioPopover::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — diagnostic.
    qInfo() << "[AudioPopover] resizeEvent old=" << event->oldSize()
            << "new=" << event->size()
            << "list.size=" << (m_list ? m_list->size() : QSize());
}

void AudioPopover::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — emit hoverChanged(false)
    // on dismiss so VideoPlayer's hover handler can restart the HUD auto-
    // hide timer with a fresh 3s window. Without this, HUD lifecycle
    // could lag the popover lifecycle (HUD stays visible "forever" until
    // next mouse-move triggers showControls).
    emit hoverChanged(false);
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 — see SubtitlePopover::dismiss
    // for full rationale; emit dismissed so VideoPlayer can drive the
    // anchor chip's :checked state in lockstep with popover visibility.
    emit dismissed();
}

void AudioPopover::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void AudioPopover::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

void AudioPopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) return;

    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — explicit height calc.
    // adjustSize + sizeHint round-trip on a hidden popover was returning
    // too-small heights (sometimes only 1 row visible for 2-track files);
    // computing height directly from the known children bypasses Qt's
    // layout-resolve timing entirely. The list's setFixedHeight was set
    // in populate; we just sum padding + header + spacing + list.
    adjustSize();
    int pw = qMax(width(), minimumWidth());
    if (pw < 220) pw = 220;
    if (pw > 320) pw = 320;
    const int paddingV = 10 + 10;  // root layout: setContentsMargins(10, 10, 10, 10)
    const int spacingV = 6;        // single inter-widget gap (header + list)
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — mirror
    // populate's sizeHintForRow approach so the popover height matches
    // the list's actual rendered content (not the underestimated
    // ROW_HEIGHT constant).
    const int rows = m_list ? qMin(m_list->count(), MAX_VISIBLE_ROWS) : 0;
    int actualRowH = ROW_HEIGHT;
    if (m_list && m_list->count() > 0) {
        const int hint = m_list->sizeHintForRow(0);
        if (hint > 0) actualRowH = hint;
    }
    const int listH = qMax(rows, 1) * actualRowH + 8;
    const int ph = paddingV + 18 /*header est*/ + spacingV + listH;

    const QPoint anchorPos = anchor->mapTo(p, anchor->rect().topRight());
    const int x = qMax(0, anchorPos.x() - pw);
    const int y = qMax(0, anchorPos.y() - ph - 8);
    setGeometry(x, y, pw, ph);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — diagnostic.
    qInfo() << "[AudioPopover] anchorAbove ph=" << ph
            << "listH=" << listH
            << "popover.size=" << size()
            << "list.size=" << (m_list ? m_list->size() : QSize())
            << "parent=" << p->size()
            << "anchorY=" << anchorPos.y();
}
