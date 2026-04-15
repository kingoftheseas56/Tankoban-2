#include "SubtitleMenu.h"

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr const char* kOffKey = "off";
}

SubtitleMenu::SubtitleMenu(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("SubtitleMenu"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    setStyleSheet(QStringLiteral(
        "#SubtitleMenu {"
        "  background: rgb(18,18,18);"
        "  color: rgb(220,220,220);"
        "  border: 1px solid rgba(255,255,255,30);"
        "  border-radius: 6px;"
        "}"
        "#SubtitleMenu QLabel { color: rgb(220,220,220); }"
        "#SubtitleMenu QListWidget {"
        "  background: rgb(24,24,24);"
        "  color: rgb(220,220,220);"
        "  border: 1px solid rgba(255,255,255,30);"
        "  border-radius: 4px;"
        "  outline: none;"
        "}"
        "#SubtitleMenu QListWidget::item { padding: 6px 10px; }"
        "#SubtitleMenu QListWidget::item:selected {"
        "  background: rgba(255,255,255,0.10);"
        "  color: rgb(255,255,255);"
        "}"
        "#SubtitleMenu QPushButton {"
        "  background: rgb(32,32,32);"
        "  color: rgb(220,220,220);"
        "  border: 1px solid rgba(255,255,255,40);"
        "  border-radius: 4px;"
        "  padding: 6px 10px;"
        "}"
        "#SubtitleMenu QPushButton:hover { background: rgb(40,40,40); }"
    ));

    buildUI();
    hide();
}

void SubtitleMenu::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    m_titleLabel = new QLabel(tr("Subtitles"), this);
    QFont f = m_titleLabel->font();
    f.setBold(true);
    m_titleLabel->setFont(f);
    root->addWidget(m_titleLabel);

    m_choiceList = new QListWidget(this);
    m_choiceList->setUniformItemSizes(true);
    m_choiceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_choiceList->setMinimumWidth(320);
    m_choiceList->setMinimumHeight(200);
    root->addWidget(m_choiceList, 1);
    connect(m_choiceList, &QListWidget::itemClicked, this, &SubtitleMenu::onChoiceClicked);

    m_loadFileBtn = new QPushButton(tr("Load from file..."), this);
    root->addWidget(m_loadFileBtn);
    connect(m_loadFileBtn, &QPushButton::clicked, this, &SubtitleMenu::onLoadFileClicked);

    // Delay slider — ±5000 ms, 50 ms steps.
    auto* delayRow = new QHBoxLayout();
    delayRow->setSpacing(8);
    auto* delayLabel = new QLabel(tr("Delay"), this);
    delayLabel->setMinimumWidth(60);
    delayRow->addWidget(delayLabel);
    m_delaySlider = new QSlider(Qt::Horizontal, this);
    m_delaySlider->setRange(-5000, 5000);
    m_delaySlider->setSingleStep(50);
    m_delaySlider->setPageStep(250);
    m_delaySlider->setValue(0);
    delayRow->addWidget(m_delaySlider, 1);
    m_delayValue = new QLabel(QStringLiteral("0 ms"), this);
    m_delayValue->setMinimumWidth(70);
    m_delayValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    delayRow->addWidget(m_delayValue);
    root->addLayout(delayRow);
    connect(m_delaySlider, &QSlider::valueChanged, this, [this](int ms) {
        m_delayMs = ms;
        m_delayValue->setText(QStringLiteral("%1 ms").arg(ms));
        if (m_sidecar) m_sidecar->sendSetSubtitleDelayMs(ms);
    });

    // Offset slider — vertical pixel offset, -120..+200.
    auto* offsetRow = new QHBoxLayout();
    offsetRow->setSpacing(8);
    auto* offsetLabel = new QLabel(tr("Offset"), this);
    offsetLabel->setMinimumWidth(60);
    offsetRow->addWidget(offsetLabel);
    m_offsetSlider = new QSlider(Qt::Horizontal, this);
    m_offsetSlider->setRange(-120, 200);
    m_offsetSlider->setSingleStep(2);
    m_offsetSlider->setPageStep(10);
    m_offsetSlider->setValue(0);
    offsetRow->addWidget(m_offsetSlider, 1);
    m_offsetValue = new QLabel(QStringLiteral("0 px"), this);
    m_offsetValue->setMinimumWidth(70);
    m_offsetValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    offsetRow->addWidget(m_offsetValue);
    root->addLayout(offsetRow);
    connect(m_offsetSlider, &QSlider::valueChanged, this, [this](int px) {
        m_offsetPx = px;
        m_offsetValue->setText(QStringLiteral("%1 px").arg(px));
        if (m_sidecar) m_sidecar->sendSetSubtitlePixelOffset(px);
    });

    // Size slider — percent, 50..200 (scale 0.50..2.00).
    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(8);
    auto* sizeLabel = new QLabel(tr("Size"), this);
    sizeLabel->setMinimumWidth(60);
    sizeRow->addWidget(sizeLabel);
    m_sizeSlider = new QSlider(Qt::Horizontal, this);
    m_sizeSlider->setRange(50, 200);
    m_sizeSlider->setSingleStep(5);
    m_sizeSlider->setPageStep(10);
    m_sizeSlider->setValue(100);
    sizeRow->addWidget(m_sizeSlider, 1);
    m_sizeValue = new QLabel(QStringLiteral("100%"), this);
    m_sizeValue->setMinimumWidth(70);
    m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeRow->addWidget(m_sizeValue);
    root->addLayout(sizeRow);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int pct) {
        m_sizeScale = pct / 100.0;
        m_sizeValue->setText(QStringLiteral("%1%").arg(pct));
        if (m_sidecar) m_sidecar->sendSetSubtitleSize(m_sizeScale);
    });
}

void SubtitleMenu::setSidecar(SidecarProcess* sidecar)
{
    if (m_sidecar == sidecar) return;
    if (m_sidecar) {
        disconnect(m_sidecar, nullptr, this, nullptr);
    }
    m_sidecar = sidecar;
    if (m_sidecar) {
        connect(m_sidecar, &SidecarProcess::subtitleTracksListed,
                this, &SubtitleMenu::onEmbeddedTracksListed);
        // Seed immediately from the cache — sidecar may already have fired
        // tracks_changed before the menu was constructed.
        onEmbeddedTracksListed(m_sidecar->listSubtitleTracks(),
                               m_sidecar->activeSubtitleIndex());
    }
}

void SubtitleMenu::setExternalTracks(const QList<tankostream::addon::SubtitleTrack>& tracks,
                                     const QHash<QString, QString>& originByTrackKey)
{
    m_addonTracks = tracks;
    m_addonOriginsByKey = originByTrackKey;
    rebuildChoices();
    refreshList();
}

void SubtitleMenu::toggle(QWidget* anchor)
{
    if (isVisible()) {
        dismiss();
        return;
    }
    m_anchor = anchor;
    rebuildChoices();
    refreshList();
    anchorAbove(anchor);
    show();
    raise();
    installClickFilter();
}

bool SubtitleMenu::isOpen() const
{
    return isVisible();
}

void SubtitleMenu::onEmbeddedTracksListed(const QList<SubtitleTrackInfo>& tracks,
                                          int activeIndex)
{
    m_embeddedTracks = tracks;
    m_activeEmbeddedIndex = activeIndex;
    rebuildChoices();
    refreshList();
}

void SubtitleMenu::onChoiceClicked(QListWidgetItem* item)
{
    if (!item) return;
    const QString key = item->data(Qt::UserRole).toString();
    for (const Choice& c : m_choices) {
        if (c.key == key) {
            applyChoice(c);
            m_activeChoiceKey = key;
            refreshList();
            return;
        }
    }
}

void SubtitleMenu::onLoadFileClicked()
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

void SubtitleMenu::applyChoice(const Choice& c)
{
    if (!m_sidecar) return;

    switch (c.kind) {
    case ChoiceKind::Off:
        m_sidecar->sendSetSubtitleTrack(-1);
        break;
    case ChoiceKind::Embedded:
        m_sidecar->sendSetSubtitleTrack(c.embeddedIndex);
        break;
    case ChoiceKind::Addon:
    case ChoiceKind::LocalFile:
        m_sidecar->sendSetSubtitleUrl(c.url, m_offsetPx, m_delayMs);
        break;
    }
}

void SubtitleMenu::rebuildChoices()
{
    m_choices.clear();

    Choice off;
    off.kind  = ChoiceKind::Off;
    off.key   = QString::fromLatin1(kOffKey);
    off.title = tr("Off");
    m_choices.push_back(off);

    for (const SubtitleTrackInfo& e : m_embeddedTracks) {
        Choice c;
        c.kind = ChoiceKind::Embedded;
        c.embeddedIndex = e.index;
        c.key = QStringLiteral("emb:%1").arg(e.index);
        c.title = embeddedDisplayLabel(e);
        c.language = e.lang.trimmed();
        m_choices.push_back(c);
    }

    for (const auto& a : m_addonTracks) {
        Choice c;
        c.kind = ChoiceKind::Addon;
        c.url = a.url;
        c.key = QStringLiteral("addon:%1").arg(a.url.toString(QUrl::FullyEncoded));
        c.title = addonDisplayLabel(a);
        c.language = a.lang.trimmed();
        c.addonId = m_addonOriginsByKey.value(normalizeAddonKey(a));
        m_choices.push_back(c);
    }

    if (!m_customFilePath.isEmpty()) {
        Choice c;
        c.kind = ChoiceKind::LocalFile;
        c.url = QUrl::fromLocalFile(m_customFilePath);
        c.key = QStringLiteral("file:%1").arg(m_customFilePath);
        c.title = tr("Local file");
        c.language = m_customFilePath.section('/', -1);
        m_choices.push_back(c);
    }

    // Reconcile the "active" marker with sidecar truth when menu has no
    // prior explicit selection (e.g. initial open).
    if (m_activeChoiceKey.isEmpty()) {
        if (m_activeEmbeddedIndex >= 0) {
            m_activeChoiceKey = QStringLiteral("emb:%1").arg(m_activeEmbeddedIndex);
        } else {
            m_activeChoiceKey = QString::fromLatin1(kOffKey);
        }
    }
}

void SubtitleMenu::refreshList()
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
        m_choiceList->addItem(item);

        if (c.key == m_activeChoiceKey) activeRow = i;
    }

    if (activeRow >= 0) {
        m_choiceList->setCurrentRow(activeRow);
    }
    m_choiceList->blockSignals(false);
}

void SubtitleMenu::dismiss()
{
    hide();
    removeClickFilter();
    m_anchor.clear();
}

void SubtitleMenu::anchorAbove(QWidget* anchor)
{
    QWidget* p = parentWidget();
    if (!p) {
        adjustSize();
        return;
    }
    adjustSize();
    const int pw = sizeHint().width();
    const int ph = sizeHint().height();
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
}

void SubtitleMenu::installClickFilter()
{
    if (m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
        m_clickFilterInstalled = true;
    }
}

void SubtitleMenu::removeClickFilter()
{
    if (!m_clickFilterInstalled) return;
    if (auto* app = QApplication::instance()) {
        app->removeEventFilter(this);
    }
    m_clickFilterInstalled = false;
}

bool SubtitleMenu::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        const QRect r(mapToGlobal(QPoint(0, 0)), size());
        if (r.contains(gp)) {
            return QFrame::eventFilter(obj, event);
        }
        // Outside the menu — if the press is on the anchor chip, swallow
        // after dismiss so the chip's clicked() signal doesn't re-open
        // the menu in the same event cycle.
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(gp);
        dismiss();
        if (onAnchor) return true;
    }
    return QFrame::eventFilter(obj, event);
}

QString SubtitleMenu::normalizeAddonKey(const tankostream::addon::SubtitleTrack& t)
{
    return t.id.trimmed().toLower()
         + QLatin1Char('|')
         + t.lang.trimmed().toLower()
         + QLatin1Char('|')
         + t.url.toString(QUrl::FullyEncoded).toLower();
}

QString SubtitleMenu::addonDisplayLabel(const tankostream::addon::SubtitleTrack& t)
{
    const QString label = t.label.trimmed();
    if (!label.isEmpty()) return label;
    const QString lang = t.lang.trimmed();
    if (!lang.isEmpty()) return lang.toUpper();
    return SubtitleMenu::tr("Addon subtitle");
}

QString SubtitleMenu::embeddedDisplayLabel(const SubtitleTrackInfo& t)
{
    const QString title = t.title.trimmed();
    if (!title.isEmpty()) return title;
    const QString lang = t.lang.trimmed();
    if (!lang.isEmpty()) return lang.toUpper();
    return SubtitleMenu::tr("Embedded track %1").arg(t.index + 1);
}
