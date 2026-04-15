#include "StreamSourceList.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include "StreamSourceCard.h"

namespace tankostream::stream {

StreamSourceList::StreamSourceList(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StreamSourceList"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#StreamSourceList { background: transparent; }"
        "#StreamSourceListScroll { background: transparent; border: none; }"
        "#StreamSourceListScroll > QWidget > QWidget { background: transparent; }"
        "#StreamSourceListStatus { color: #9ca3af; font-size: 12px; }"
        "#StreamSourceListStatusError { color: #f3a6a6; font-size: 12px; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 0; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15);"
        " border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.25); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    buildUI();
    setPlaceholder(tr("Select an item to see sources"));
}

void StreamSourceList::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Phase 2 Batch 2.4 — auto-launch toast banner, pinned at the top of
    // the pane above the scroll area. Hidden by default; StreamPage arms
    // it when a saved-source match + timestamp gate succeed.
    m_autoLaunchToast = new QFrame(this);
    m_autoLaunchToast->setObjectName(QStringLiteral("StreamAutoLaunchToast"));
    m_autoLaunchToast->setStyleSheet(QStringLiteral(
        "#StreamAutoLaunchToast {"
        "  background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; }"
        "#StreamAutoLaunchPickDifferent {"
        "  background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 4px; color: #e0e0e0;"
        "  font-size: 11px; padding: 3px 10px; }"
        "#StreamAutoLaunchPickDifferent:hover {"
        "  background: rgba(255,255,255,0.18); }"));
    auto* toastLayout = new QHBoxLayout(m_autoLaunchToast);
    toastLayout->setContentsMargins(10, 6, 10, 6);
    toastLayout->setSpacing(8);

    m_autoLaunchLabel = new QLabel(m_autoLaunchToast);
    m_autoLaunchLabel->setStyleSheet(
        "color: #d1d5db; font-size: 12px;");
    m_autoLaunchLabel->setWordWrap(true);
    toastLayout->addWidget(m_autoLaunchLabel, 1);

    auto* pickDifferentBtn = new QPushButton(tr("Pick different"), m_autoLaunchToast);
    pickDifferentBtn->setObjectName(QStringLiteral("StreamAutoLaunchPickDifferent"));
    pickDifferentBtn->setCursor(Qt::PointingHandCursor);
    connect(pickDifferentBtn, &QPushButton::clicked, this,
            &StreamSourceList::autoLaunchCancelRequested);
    toastLayout->addWidget(pickDifferentBtn, 0);

    m_autoLaunchToast->hide();
    root->addWidget(m_autoLaunchToast);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("StreamSourceListScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_cardsContainer = new QWidget(m_scroll);
    m_cardsContainer->setObjectName(QStringLiteral("StreamSourceListContainer"));
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(2, 2, 8, 2);
    m_cardsLayout->setSpacing(8);
    m_cardsLayout->addStretch(1);   // sentinel bottom stretch; cards insert above

    m_scroll->setWidget(m_cardsContainer);

    root->addWidget(m_scroll, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("StreamSourceListStatus"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setContentsMargins(8, 16, 8, 16);
    root->addWidget(m_statusLabel);
}

void StreamSourceList::showAutoLaunchToast(const QString& label)
{
    if (!m_autoLaunchToast) return;
    if (m_autoLaunchLabel) m_autoLaunchLabel->setText(label);
    m_autoLaunchToast->show();
}

void StreamSourceList::hideAutoLaunchToast()
{
    if (!m_autoLaunchToast) return;
    m_autoLaunchToast->hide();
}

void StreamSourceList::clearCards()
{
    for (StreamSourceCard* card : m_cards) {
        m_cardsLayout->removeWidget(card);
        card->deleteLater();
    }
    m_cards.clear();
}

void StreamSourceList::showStatus(const QString& message, bool emphasizeError)
{
    // Inline color override beats objectName-swap-then-repolish — the latter
    // requires a parent restyle pass that's flaky in Qt6's stylesheet engine
    // when called mid-widget-lifecycle. Color comes straight from the same
    // palette the parent stylesheet defines.
    m_statusLabel->setStyleSheet(emphasizeError
        ? QStringLiteral("color: #f3a6a6; font-size: 12px;")
        : QStringLiteral("color: #9ca3af; font-size: 12px;"));

    m_statusLabel->setText(message);
    m_statusLabel->setVisible(!message.isEmpty());
    m_scroll->setVisible(message.isEmpty());
}

void StreamSourceList::setPlaceholder(const QString& message)
{
    clearCards();
    showStatus(message);
}

void StreamSourceList::setLoading()
{
    clearCards();
    showStatus(tr("Loading sources..."));
}

void StreamSourceList::setEmpty()
{
    clearCards();
    showStatus(tr("No sources found. Try enabling another stream addon."));
}

void StreamSourceList::setError(const QString& message)
{
    clearCards();
    showStatus(message.isEmpty() ? tr("Failed to fetch sources.") : message,
               /*emphasizeError=*/true);
}

void StreamSourceList::setSources(const QList<StreamPickerChoice>& choices,
                                   const QString&                   savedChoiceKey)
{
    clearCards();

    if (choices.isEmpty()) {
        setEmpty();
        return;
    }

    // Hide status panel; show the scroll area.
    showStatus(QString());

    // Insert each card BEFORE the bottom stretch sentinel.
    for (const StreamPickerChoice& choice : choices) {
        auto* card = new StreamSourceCard(choice, m_cardsContainer);
        if (!savedChoiceKey.isEmpty()
         && pickerChoiceKey(choice) == savedChoiceKey) {
            card->setSelected(true);
        }
        connect(card, &StreamSourceCard::clicked,
                this, &StreamSourceList::sourceActivated);

        const int insertPos = qMax(0, m_cardsLayout->count() - 1);
        m_cardsLayout->insertWidget(insertPos, card);
        m_cards.push_back(card);
    }

    m_scroll->verticalScrollBar()->setValue(0);
}

}
