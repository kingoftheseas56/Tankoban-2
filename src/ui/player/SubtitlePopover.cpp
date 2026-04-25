#include "SubtitlePopover.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QStringList>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

namespace {

constexpr const char* kOffKey = "off";

// VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — bumped from 6 → 12 per
// Hemanth: a 3-track subtitle popover should NEVER need a scrollbar.
// Typical files have 1-5 sub tracks; 12 covers 99% of cases without
// making the popover monstrous.
const int MAX_VISIBLE_ROWS = 12;
const int ROW_HEIGHT       = 30;

const char* HEADER_SS =
    "color: rgba(214,194,164,0.95);"
    "font-size: 11px;"
    "font-weight: 700;"
    "border: none;";

const char* TRACK_LIST_SS =
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

const char* BTN_SS =
    "QPushButton {"
    "  background: rgba(40,40,40,230);"
    "  color: #ccc;"
    "  border: 1px solid rgba(255,255,255,0.10);"
    "  border-radius: 4px;"
    "  padding: 6px 10px;"
    "  font-size: 12px;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(60,60,60,230);"
    "}";

QString expandLangCode(const QString& code)
{
    if (code.isEmpty()) return QString();
    const QLocale loc(code);
    const QString name = QLocale::languageToString(loc.language());
    if (name.isEmpty() || name == QStringLiteral("C")) return code.toUpper();
    return name;
}

} // namespace

SubtitlePopover::SubtitlePopover(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("SubtitlePopover");
    setStyleSheet(
        "#SubtitlePopover {"
        "  background: rgba(16,16,16,240);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 8px;"
        "}"
    );
    setMaximumWidth(360);
    setMinimumWidth(260);

    buildUI();
    hide();
}

void SubtitlePopover::buildUI()
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(6);

    m_titleLabel = new QLabel("Subtitles");
    m_titleLabel->setStyleSheet(HEADER_SS);
    lay->addWidget(m_titleLabel);

    m_choiceList = new QListWidget;
    m_choiceList->setStyleSheet(TRACK_LIST_SS);
    m_choiceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — Hemanth re-
    // reported the scrollbar bug after the prior wake's fix landed on
    // a fresh exe (Scenario B: incomplete fix). Root cause: refreshList's
    // setFixedHeight used a hardcoded ROW_HEIGHT=30 constant that
    // underestimated the actual rendered row height (QSS padding 5px*2 +
    // 12pt font + Qt list-item internal margin yields ~32-34px in
    // practice; 4 rows*32 = 128 > setFixedHeight(126) → scrollbar
    // appears). Forcing ScrollBarAlwaysOff prevents the symptom regardless
    // of pixel-rounding mismatch; refreshList now uses sizeHintForRow()
    // for accurate measurement so the list IS tall enough for content.
    m_choiceList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_choiceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_choiceList->setUniformItemSizes(true);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — dropped the prior
    // setMinimumHeight(160) floor (5.3 rows). It was a hedge against
    // empty-list collapse but conflicted with the per-populate
    // setFixedHeight in refreshList — Qt's resolve order left some
    // configurations clipped (Hemanth's 3-track screenshot showed ~2.5
    // rows visible with a scrollbar despite content fitting in 96px).
    // refreshList's setFixedHeight is the authoritative size now.
    connect(m_choiceList, &QListWidget::itemClicked,
            this, &SubtitlePopover::onChoiceClicked);
    lay->addWidget(m_choiceList);

    m_loadFileBtn = new QPushButton(tr("Load from file..."));
    m_loadFileBtn->setStyleSheet(BTN_SS);
    m_loadFileBtn->setCursor(Qt::PointingHandCursor);
    m_loadFileBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_loadFileBtn, &QPushButton::clicked,
            this, &SubtitlePopover::onLoadFileClicked);
    lay->addWidget(m_loadFileBtn);
}

void SubtitlePopover::setSidecar(SidecarProcess* sidecar)
{
    if (m_sidecar == sidecar) return;
    if (m_sidecar) disconnect(m_sidecar, nullptr, this, nullptr);
    m_sidecar = sidecar;
    if (m_sidecar) {
        connect(m_sidecar, &SidecarProcess::subtitleTracksListed,
                this, &SubtitlePopover::onEmbeddedTracksListed);
        // Seed immediately — sidecar may already have fired tracks_changed
        // before the popover was constructed.
        onEmbeddedTracksListed(m_sidecar->listSubtitleTracks(),
                               m_sidecar->activeSubtitleIndex());
    }
}

void SubtitlePopover::setExternalTracks(
    const QList<tankostream::addon::SubtitleTrack>& tracks,
    const QHash<QString, QString>& originByTrackKey)
{
    m_addonTracks = tracks;
    m_addonOriginsByKey = originByTrackKey;
    rebuildChoices();
    refreshList();
}

void SubtitlePopover::setEmbeddedTracksFromJson(const QJsonArray& tracks,
                                                int currentSubId, bool subVisible)
{
    m_embeddedJson      = tracks;
    m_currentEmbeddedId = currentSubId;
    m_subVisible        = subVisible;
    rebuildChoices();
    refreshList();
}

void SubtitlePopover::onEmbeddedTracksListed(
    const QList<SubtitleTrackInfo>& tracks, int activeIndex)
{
    m_embeddedTracks      = tracks;
    m_activeEmbeddedIndex = activeIndex;
    rebuildChoices();
    refreshList();
}

void SubtitlePopover::onChoiceClicked(QListWidgetItem* item)
{
    if (!item) return;
    const QString key = item->data(Qt::UserRole).toString();
    for (const Choice& c : m_choices) {
        if (c.key == key) {
            applyChoice(c);
            m_activeChoiceKey = key;
            // Embedded path also fires the signal so VideoPlayer can
            // update m_activeSubId, saveShowPrefs, persist preferred
            // language, and toast the change.
            if (c.kind == ChoiceKind::Off) {
                emit embeddedSubtitleSelected(0);
            } else if (c.kind == ChoiceKind::Embedded) {
                emit embeddedSubtitleSelected(c.embeddedId);
            }
            dismiss();
            return;
        }
    }
}

void SubtitlePopover::onLoadFileClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Load Subtitle"),
        QString(),
        tr("Subtitles (*.srt *.ass *.ssa *.sub *.vtt)"));
    if (path.isEmpty()) return;

    m_customFilePath = path;
    rebuildChoices();

    const QString key = QStringLiteral("file:%1").arg(path);
    for (const Choice& c : m_choices) {
        if (c.key == key) {
            applyChoice(c);
            m_activeChoiceKey = key;
            break;
        }
    }
    refreshList();
}

void SubtitlePopover::applyChoice(const Choice& c)
{
    if (!m_sidecar) return;

    switch (c.kind) {
    case ChoiceKind::Off:
        // Visibility-only Off — VideoPlayer's slot for embeddedSubtitleSelected(0)
        // routes through setSubtitleOff which handles sendSetSubVisibility.
        // No direct sidecar call here to avoid double-dispatch.
        break;
    case ChoiceKind::Embedded:
        // Same reasoning — VideoPlayer's slot calls sendSetTracks("", idStr).
        break;
    case ChoiceKind::Addon:
    case ChoiceKind::LocalFile:
        m_sidecar->sendSetSubtitleUrl(c.url, 0, 0);
        break;
    }
}

void SubtitlePopover::rebuildChoices()
{
    m_choices.clear();

    Choice off;
    off.kind  = ChoiceKind::Off;
    off.key   = QString::fromLatin1(kOffKey);
    off.title = tr("Off");
    m_choices.push_back(off);

    // Embedded — prefer JSON path (richer; from sidecar's tracks_changed
    // event via VideoPlayer) when present; fall back to SubtitleMenu's
    // SubtitleTrackInfo cache.
    if (!m_embeddedJson.isEmpty()) {
        // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — symmetric with
        // AudioPopover: caller passes subtitle-only JSON; the prior
        // `type != "subtitle"` filter was dead defense-in-depth that
        // would drop entries lacking the discriminator (the chip-click
        // path doesn't stamp it).
        for (const auto& val : m_embeddedJson) {
            const QJsonObject t = val.toObject();
            const int     tid    = t.value("id").toVariant().toInt();
            const QString lang   = t.value("lang").toString();
            const QString title  = t.value("title").toString();

            Choice c;
            c.kind          = ChoiceKind::Embedded;
            c.embeddedIndex = tid;
            c.embeddedId    = tid;
            c.key           = QStringLiteral("emb:%1").arg(tid);
            const QString langHuman = expandLangCode(lang);
            if (!title.isEmpty())          c.title = title;
            else if (!langHuman.isEmpty()) c.title = langHuman;
            else                           c.title = QStringLiteral("Track %1").arg(tid);
            c.language = lang.trimmed();
            m_choices.push_back(c);
        }
    } else {
        for (const SubtitleTrackInfo& e : m_embeddedTracks) {
            Choice c;
            c.kind          = ChoiceKind::Embedded;
            c.embeddedIndex = e.index;
            c.embeddedId    = e.index;
            c.key           = QStringLiteral("emb:%1").arg(e.index);
            c.title         = embeddedDisplayLabel(e);
            c.language      = e.lang.trimmed();
            m_choices.push_back(c);
        }
    }

    for (const auto& a : m_addonTracks) {
        Choice c;
        c.kind = ChoiceKind::Addon;
        c.url  = a.url;
        c.key  = QStringLiteral("addon:%1").arg(a.url.toString(QUrl::FullyEncoded));
        c.title    = addonDisplayLabel(a);
        c.language = a.lang.trimmed();
        c.addonId  = m_addonOriginsByKey.value(normalizeAddonKey(a));
        m_choices.push_back(c);
    }

    if (!m_customFilePath.isEmpty()) {
        Choice c;
        c.kind = ChoiceKind::LocalFile;
        c.url  = QUrl::fromLocalFile(m_customFilePath);
        c.key  = QStringLiteral("file:%1").arg(m_customFilePath);
        c.title    = tr("Local file");
        c.language = m_customFilePath.section('/', -1);
        m_choices.push_back(c);
    }

    // Reconcile active marker with sidecar/JSON truth on initial open.
    if (m_activeChoiceKey.isEmpty()) {
        if (!m_embeddedJson.isEmpty() && m_currentEmbeddedId > 0 && m_subVisible) {
            m_activeChoiceKey = QStringLiteral("emb:%1").arg(m_currentEmbeddedId);
        } else if (m_activeEmbeddedIndex >= 0) {
            m_activeChoiceKey = QStringLiteral("emb:%1").arg(m_activeEmbeddedIndex);
        } else {
            m_activeChoiceKey = QString::fromLatin1(kOffKey);
        }
    }
}

void SubtitlePopover::refreshList()
{
    if (!m_choiceList) return;

    m_choiceList->blockSignals(true);
    m_choiceList->clear();

    int activeRow = -1;
    for (int i = 0; i < m_choices.size(); ++i) {
        const Choice& c = m_choices[i];
        QString label = c.title;
        if (!c.language.isEmpty() && c.kind != ChoiceKind::Off
            && c.kind != ChoiceKind::LocalFile) {
            label += QStringLiteral("  (%1)").arg(c.language.toUpper());
        }
        if (!c.addonId.isEmpty()) {
            label += QStringLiteral("  — %1").arg(c.addonId);
        }

        auto* item = new QListWidgetItem(label, m_choiceList);
        item->setData(Qt::UserRole, c.key);

        if (c.key == m_activeChoiceKey) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            activeRow = i;
        }

        m_choiceList->addItem(item);
    }

    if (activeRow >= 0) m_choiceList->setCurrentRow(activeRow);
    m_choiceList->blockSignals(false);

    const int rows = qMin(m_choiceList->count(), MAX_VISIBLE_ROWS);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — use Qt's
    // own row-height measurement instead of the hardcoded ROW_HEIGHT
    // constant (which underestimates the rendered height once QSS
    // padding + font metrics + Qt internal margins are accounted for).
    // sizeHintForRow returns the actual rendered height of the item
    // post-stylesheet. Falls back to ROW_HEIGHT if no items (defensive).
    int actualRowH = ROW_HEIGHT;
    if (m_choiceList->count() > 0) {
        const int hint = m_choiceList->sizeHintForRow(0);
        if (hint > 0) actualRowH = hint;
    }
    // Generous 8px padding instead of 6 to absorb any frame-width or
    // viewport-margin slack we don't see at this layer.
    const int listFixedH = qMax(rows, 1) * actualRowH + 8;
    m_choiceList->setFixedHeight(listFixedH);
    qInfo() << "[SubtitlePopover] refreshList listH=" << listFixedH
            << "rows=" << rows << "count=" << m_choiceList->count()
            << "actualRowH=" << actualRowH;
}

void SubtitlePopover::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    rebuildChoices();
    refreshList();
    if (anchor) anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

bool SubtitlePopover::eventFilter(QObject* /*obj*/, QEvent* event)
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

void SubtitlePopover::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    emit hoverChanged(true);
}

void SubtitlePopover::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    emit hoverChanged(false);
}

void SubtitlePopover::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void SubtitlePopover::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — diagnostic
    // qInfo. Captures any post-show resize that shrinks the popover
    // after our explicit setGeometry in anchorAbove.
    qInfo() << "[SubtitlePopover] resizeEvent old=" << event->oldSize()
            << "new=" << event->size()
            << "list.size=" << (m_choiceList ? m_choiceList->size() : QSize());
}

void SubtitlePopover::dismiss()
{
    removeClickFilter();
    hide();
    m_anchor.clear();
    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — see AudioPopover::dismiss
    // for rationale; mirror the hoverChanged(false) emit so the HUD auto-
    // hide timer restarts with a fresh 3s window post-dismiss.
    emit hoverChanged(false);
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 — chip uncheck propagation.
    // dismiss() is the single chokepoint for popover-internal close
    // paths (item click → onChoiceClicked → dismiss; click outside →
    // eventFilter → dismiss). VideoPlayer connects this signal to
    // m_subtitleChip->setChecked(false). dismissOtherPopovers() bypasses
    // dismiss() by calling hide() directly but already manually unchecks
    // the chip there, so coverage is complete.
    emit dismissed();
}

void SubtitlePopover::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void SubtitlePopover::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) app->removeEventFilter(this);
    m_clickFilterInstalled = false;
}

void SubtitlePopover::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) {
        adjustSize();
        return;
    }

    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — bypass Qt's hint-
    // resolution timing by computing height explicitly from the three
    // child widgets we know about (header / list / load-button) plus
    // root-layout margins + spacings. The list height is authoritative
    // because refreshList just called setFixedHeight on it. Width
    // continues to use sizeHint via adjustSize since the content-driven
    // width path doesn't have the same ordering risk.
    adjustSize();
    const int pw = qMax(width(), minimumWidth());
    const int paddingV = 10 + 10;  // root layout: setContentsMargins(10, 10, 10, 10)
    const int spacingV = 6 * 2;    // 2 inter-widget gaps in 3-widget VBox
    const int headerH = m_titleLabel ? m_titleLabel->sizeHint().height() : 18;
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — mirror
    // refreshList's sizeHintForRow approach so the popover height
    // matches the list's actual rendered content (not the underestimated
    // ROW_HEIGHT constant).
    const int rows = m_choiceList ? qMin(m_choiceList->count(), MAX_VISIBLE_ROWS) : 0;
    int actualRowH = ROW_HEIGHT;
    if (m_choiceList && m_choiceList->count() > 0) {
        const int hint = m_choiceList->sizeHintForRow(0);
        if (hint > 0) actualRowH = hint;
    }
    const int listH = qMax(rows, 1) * actualRowH + 8;
    const int footerH = m_loadFileBtn ? m_loadFileBtn->sizeHint().height() : 32;
    const int ph = paddingV + headerH + spacingV + listH + footerH;

    int x;
    int y;
    if (anchor) {
        const QPoint anchorPos = anchor->mapTo(p, anchor->rect().topRight());
        x = qMax(0, anchorPos.x() - pw);
        y = qMax(0, anchorPos.y() - ph - 8);
    } else {
        x = qMax(0, (p->width() - pw) / 2);
        y = qMax(0, (p->height() - ph) / 2);
    }
    setGeometry(x, y, pw, ph);
    // VIDEO_HUD_MINIMALIST 1.x bug-fix re-poke 2026-04-25 — diagnostic
    // qInfo. Logs computed ph + each component + actual size() the
    // setGeometry call lands at + the inner QListWidget's actual size.
    qInfo() << "[SubtitlePopover] anchorAbove ph=" << ph
            << "listH=" << listH << "headerH=" << headerH
            << "footerH=" << footerH
            << "popover.size=" << size()
            << "list.size=" << (m_choiceList ? m_choiceList->size() : QSize())
            << "parent=" << p->size()
            << "anchorY=" << (anchor ? anchor->mapTo(p, anchor->rect().topRight()).y() : -1);
}

QString SubtitlePopover::normalizeAddonKey(const tankostream::addon::SubtitleTrack& t)
{
    return t.id.trimmed().toLower()
         + QLatin1Char('|')
         + t.lang.trimmed().toLower()
         + QLatin1Char('|')
         + t.url.toString(QUrl::FullyEncoded).toLower();
}

QString SubtitlePopover::addonDisplayLabel(const tankostream::addon::SubtitleTrack& t)
{
    const QString label = t.label.trimmed();
    if (!label.isEmpty()) return label;
    const QString lang = t.lang.trimmed();
    if (!lang.isEmpty()) return lang.toUpper();
    return SubtitlePopover::tr("Addon subtitle");
}

QString SubtitlePopover::embeddedDisplayLabel(const SubtitleTrackInfo& t)
{
    const QString title = t.title.trimmed();
    if (!title.isEmpty()) return title;
    const QString lang = t.lang.trimmed();
    if (!lang.isEmpty()) return lang.toUpper();
    return SubtitlePopover::tr("Embedded track %1").arg(t.index + 1);
}
