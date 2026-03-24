#include "RootFoldersOverlay.h"
#include "core/CoreBridge.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDir>

static const QMap<QString, QString> DOMAIN_LABELS = {
    {"comics",     "Comics"},
    {"books",      "Books"},
    {"videos",     "Videos"},
    {"audiobooks", "Audiobooks"},
};

RootFoldersOverlay::RootFoldersOverlay(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("root_folders_overlay");
    setAttribute(Qt::WA_StyledBackground, true);

    auto* outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignCenter);

    // Card
    m_card = new QFrame(this);
    m_card->setObjectName("RootFoldersCard");
    m_card->setFixedWidth(440);
    m_card->setMaximumHeight(420);
    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    // Title row
    auto* titleRow = new QHBoxLayout();
    m_title = new QLabel("Root Folders", m_card);
    m_title->setObjectName("RootFoldersTitle");

    auto* closeBtn = new QPushButton(QChar(0x00D7), m_card); // ×
    closeBtn->setFixedSize(26, 26);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setObjectName("RootFoldersCloseBtn");
    connect(closeBtn, &QPushButton::clicked, this, &RootFoldersOverlay::closeRequested);

    titleRow->addWidget(m_title);
    titleRow->addStretch();
    titleRow->addWidget(closeBtn);
    cardLayout->addLayout(titleRow);

    // Scrollable body
    auto* scroll = new QScrollArea(m_card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("RootFoldersScroll");
    auto* body = new QWidget();
    m_bodyLayout = new QVBoxLayout(body);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(8);
    scroll->setWidget(body);
    cardLayout->addWidget(scroll, 1);

    // Add button
    m_addBtn = new QPushButton("+ Add folder", m_card);
    m_addBtn->setFixedHeight(30);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setObjectName("RootFoldersAddBtn");
    connect(m_addBtn, &QPushButton::clicked, this, &RootFoldersOverlay::onAdd);
    cardLayout->addWidget(m_addBtn);

    outer->addWidget(m_card);
}

void RootFoldersOverlay::refresh(const QString& domain)
{
    m_domain = domain;
    clearBody();

    QString label = DOMAIN_LABELS.value(domain, "Unknown");
    m_title->setText(label + " Root Folders");
    m_addBtn->setVisible(true);

    QStringList roots = m_bridge->rootFolders(domain);
    if (roots.isEmpty()) {
        auto* empty = new QLabel("No folders added yet");
        empty->setObjectName("RootFoldersNote");
        empty->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 12px; padding: 12px 0;");
        m_bodyLayout->addWidget(empty);
    } else {
        for (const auto& path : roots)
            m_bodyLayout->addWidget(folderRow(path));
    }
    m_bodyLayout->addStretch();
}

void RootFoldersOverlay::clearBody()
{
    while (m_bodyLayout->count()) {
        auto* item = m_bodyLayout->takeAt(0);
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

QWidget* RootFoldersOverlay::folderRow(const QString& path)
{
    auto* row = new QWidget();
    row->setObjectName("RootFoldersRow");
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(4, 2, 4, 2);
    h->setSpacing(8);

    // Shorten display path
    QString display = path;
    if (display.length() > 50) {
        QDir dir(path);
        display = ".../" + dir.dirName();
    }

    auto* lbl = new QLabel(display, row);
    lbl->setToolTip(path);
    lbl->setObjectName("RootFoldersPathLabel");
    lbl->setStyleSheet("color: rgba(255,255,255,0.86); font-size: 11px;");

    auto* rmBtn = new QPushButton(QChar(0x2212), row); // −
    rmBtn->setFixedSize(22, 22);
    rmBtn->setCursor(Qt::PointingHandCursor);
    rmBtn->setToolTip("Remove this folder");
    rmBtn->setObjectName("RootFoldersRemoveBtn");
    rmBtn->setStyleSheet(
        "QPushButton { background: rgba(255,80,80,0.10); border: 1px solid rgba(255,80,80,0.18);"
        "  border-radius: 11px; color: #ff6b6b; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(255,80,80,0.22); border-color: rgba(255,80,80,0.35); }"
    );

    connect(rmBtn, &QPushButton::clicked, this, [this, path]() {
        onRemove(path);
    });

    h->addWidget(lbl, 1);
    h->addWidget(rmBtn);
    return row;
}

void RootFoldersOverlay::onAdd()
{
    if (m_domain.isEmpty()) return;

    QString label = DOMAIN_LABELS.value(m_domain, "").toLower();
    QString folder = QFileDialog::getExistingDirectory(
        this, "Add " + label + " root folder"
    );
    if (folder.isEmpty()) return;

    m_bridge->addRootFolder(m_domain, folder);
    emit foldersChanged();
    refresh(m_domain);
}

void RootFoldersOverlay::onRemove(const QString& path)
{
    if (m_domain.isEmpty()) return;

    m_bridge->removeRootFolder(m_domain, path);
    emit foldersChanged();
    refresh(m_domain);
}

void RootFoldersOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void RootFoldersOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_card->geometry().contains(event->pos())) {
        emit closeRequested();
        return;
    }
    QWidget::mousePressEvent(event);
}
