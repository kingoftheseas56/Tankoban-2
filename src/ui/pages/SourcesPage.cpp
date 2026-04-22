#include "SourcesPage.h"
#include "TankorentPage.h"
#include "TankoyomiPage.h"
#include "TankoLibraryPage.h"
#include "core/CoreBridge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>

// Stack indices
static constexpr int IDX_LAUNCHER     = 0;
static constexpr int IDX_TANKORENT    = 1;
static constexpr int IDX_TANKOYOMI    = 2;
static constexpr int IDX_TANKOLIBRARY = 3;

// ── AppTile helper ──────────────────────────────────────────────────────────
static QPushButton* createAppTile(const QString& title, const QString& subtitle, QWidget* parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    btn->setMinimumSize(220, 140);
    btn->setMaximumSize(320, 200);
    btn->setStyleSheet(
        "QPushButton { background: rgba(20,20,24,0.7); border: 1px solid #333; border-radius: 12px; }"
        "QPushButton:hover { border-color: #c7a76b; }"
    );

    auto *layout = new QVBoxLayout(btn);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignCenter);

    auto *titleLbl = new QLabel(title);
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: bold; background: transparent; border: none;");
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(titleLbl);

    auto *subLbl = new QLabel(subtitle);
    subLbl->setAlignment(Qt::AlignCenter);
    subLbl->setStyleSheet("font-size: 12px; color: #888; background: transparent; border: none;");
    subLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(subLbl);

    return btn;
}

// ── Constructor ─────────────────────────────────────────────────────────────
SourcesPage::SourcesPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_bridge(bridge), m_client(client)
{
    setObjectName("sources");
    buildUI();
}

// ── UI ──────────────────────────────────────────────────────────────────────
void SourcesPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Back bar (hidden on launcher) ──
    m_backBar = new QFrame;
    auto *backLayout = new QHBoxLayout(m_backBar);
    backLayout->setContentsMargins(8, 4, 8, 4);
    backLayout->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("\u2190  Back"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedHeight(28);
    m_backBtn->setStyleSheet(
        "QPushButton { color: #c7a76b; background: transparent; border: none; font-size: 13px; padding: 2px 8px; }"
        "QPushButton:hover { text-decoration: underline; }"
    );
    connect(m_backBtn, &QPushButton::clicked, this, &SourcesPage::navigateHome);
    backLayout->addWidget(m_backBtn);

    m_backTitle = new QLabel;
    m_backTitle->setStyleSheet("font-weight: bold; font-size: 13px; border: none; background: transparent;");
    backLayout->addWidget(m_backTitle);
    backLayout->addStretch(1);

    m_backBar->setStyleSheet(
        "QFrame { background: rgba(20,20,24,0.7); border-bottom: 1px solid #333; }"
    );
    m_backBar->setVisible(false);
    root->addWidget(m_backBar);

    // ── Stacked widget ──
    m_stack = new QStackedWidget;
    root->addWidget(m_stack, 1);

    // Index 0: Launcher
    auto *launcher = new QWidget;
    auto *launcherOuter = new QVBoxLayout(launcher);
    launcherOuter->setContentsMargins(0, 0, 0, 0);

    auto *tileRow = new QHBoxLayout;
    tileRow->setSpacing(32);

    m_tankorentTile = createAppTile("Tankorent", "Torrent search & download", launcher);
    connect(m_tankorentTile, &QPushButton::clicked, this, [this]() { navigateTo(IDX_TANKORENT); });
    tileRow->addWidget(m_tankorentTile);

    m_tankoyomiTile = createAppTile("Tankoyomi", "Manga & comics", launcher);
    connect(m_tankoyomiTile, &QPushButton::clicked, this, [this]() { navigateTo(IDX_TANKOYOMI); });
    tileRow->addWidget(m_tankoyomiTile);

    m_tankolibraryTile = createAppTile("Tankolibrary", "Books & ebooks", launcher);
    connect(m_tankolibraryTile, &QPushButton::clicked, this, [this]() { navigateTo(IDX_TANKOLIBRARY); });
    tileRow->addWidget(m_tankolibraryTile);

    launcherOuter->addLayout(tileRow);
    launcherOuter->setAlignment(tileRow, Qt::AlignCenter);
    m_stack->addWidget(launcher);

    // Index 1: Tankorent
    m_tankorentPage = new TankorentPage(m_bridge, m_client);
    m_stack->addWidget(m_tankorentPage);

    // Index 2: Tankoyomi
    m_tankoyomiPage = new TankoyomiPage(m_bridge);
    m_stack->addWidget(m_tankoyomiPage);

    // Index 3: Tankolibrary
    m_tankolibraryPage = new TankoLibraryPage(m_bridge);
    m_stack->addWidget(m_tankolibraryPage);

    m_stack->setCurrentIndex(IDX_LAUNCHER);
}

// ── Navigation ──────────────────────────────────────────────────────────────
void SourcesPage::navigateTo(int index)
{
    int old = m_stack->currentIndex();
    if (old == index)
        return;

    m_stack->setCurrentIndex(index);

    if (index == IDX_LAUNCHER) {
        m_backBar->setVisible(false);
    } else {
        QString titles[] = { QString(), "Tankorent", "Tankoyomi", "Tankolibrary" };
        m_backTitle->setText(titles[index]);
        m_backBar->setVisible(true);
    }
}

void SourcesPage::navigateHome()
{
    navigateTo(IDX_LAUNCHER);
}

// ── Page lifecycle ──────────────────────────────────────────────────────────
void SourcesPage::activate()
{
    // Nothing yet — sub-pages will hook in here
}

void SourcesPage::deactivate()
{
    // Nothing yet — sub-pages will hook in here
}
