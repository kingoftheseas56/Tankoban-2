#include "OrganisePage.h"
#include "CategoryEditorPage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

OrganisePage::OrganisePage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("organise");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    m_homePage = new QWidget(this);
    m_homePage->setObjectName("OrganiseHomePage");
    auto* homeLayout = new QVBoxLayout(m_homePage);
    homeLayout->setContentsMargins(28, 24, 28, 28);
    homeLayout->setSpacing(20);

    auto* header = new QHBoxLayout();
    auto* backButton = new QPushButton("Back", m_homePage);
    backButton->setObjectName("OrganiseBackButton");
    connect(backButton, &QPushButton::clicked, this, &OrganisePage::backToVideosRequested);
    header->addWidget(backButton, 0, Qt::AlignLeft);

    auto* title = new QLabel("Organise", m_homePage);
    title->setObjectName("OrganiseTitle");
    header->addWidget(title, 1);
    homeLayout->addLayout(header);

    auto* grid = new QGridLayout();
    grid->setSpacing(14);
    int row = 0;
    int col = 0;
    for (const auto& info : videoCategoryInfos()) {
        auto* button = new QPushButton(QString::fromUtf8(info.label), m_homePage);
        button->setObjectName("OrganiseCategoryButton");
        button->setMinimumHeight(92);
        connect(button, &QPushButton::clicked, this, [this, category = info.category]() {
            openEditor(category);
        });
        grid->addWidget(button, row, col);
        if (++col == 2) {
            col = 0;
            ++row;
        }
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    homeLayout->addLayout(grid);
    homeLayout->addStretch(1);

    m_editor = new CategoryEditorPage(m_bridge, this);
    connect(m_editor, &CategoryEditorPage::backRequested, this, [this]() {
        m_stack->setCurrentWidget(m_homePage);
    });
    connect(m_editor, &CategoryEditorPage::assignmentsChanged, this, &OrganisePage::assignmentsChanged);

    m_stack->addWidget(m_homePage);
    m_stack->addWidget(m_editor);

    setStyleSheet(
        "QWidget#OrganiseHomePage { background: transparent; }"
        "QLabel#OrganiseTitle { color: #f2f2f2; font-size: 24px; font-weight: 600; }"
        "QPushButton#OrganiseBackButton, QPushButton#OrganiseCategoryButton { background: rgba(255,255,255,0.07); color: #eee; border: 1px solid rgba(255,255,255,0.14); border-radius: 8px; }"
        "QPushButton#OrganiseBackButton { padding: 7px 12px; }"
        "QPushButton#OrganiseCategoryButton { font-size: 18px; font-weight: 600; text-align: left; padding-left: 22px; }"
        "QPushButton#OrganiseBackButton:hover, QPushButton#OrganiseCategoryButton:hover { background: rgba(255,255,255,0.14); }");
}

void OrganisePage::setShows(const QList<ShowInfo>& shows)
{
    m_shows = shows;
    m_editor->setShows(m_shows);
}

void OrganisePage::activate()
{
    m_stack->setCurrentWidget(m_homePage);
}

void OrganisePage::openEditor(VideoCategory category)
{
    m_editor->setShows(m_shows);
    m_editor->setCategory(category);
    m_stack->setCurrentWidget(m_editor);
}
