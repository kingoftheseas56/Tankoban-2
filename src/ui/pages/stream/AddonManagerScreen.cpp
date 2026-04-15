#include "AddonManagerScreen.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QVBoxLayout>

#include "core/stream/addon/AddonRegistry.h"
#include "core/stream/addon/Descriptor.h"
#include "ui/dialogs/AddAddonDialog.h"
#include "AddonDetailPanel.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;

namespace {

class AddonRowWidget : public QWidget
{
public:
    AddonRowWidget(const AddonDescriptor& descriptor, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_descriptor(descriptor)
    {
        setObjectName(QStringLiteral("StreamAddonRow"));

        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(10, 8, 10, 8);
        root->setSpacing(10);

        m_logo = new QLabel(this);
        m_logo->setObjectName(QStringLiteral("StreamAddonLogo"));
        m_logo->setFixedSize(32, 32);
        m_logo->setAlignment(Qt::AlignCenter);
        m_logo->setStyleSheet(
            QStringLiteral("#StreamAddonLogo { background: rgba(255,255,255,0.06);"
                           " border-radius: 4px; color: #9ca3af;"
                           " font-size: 14px; font-weight: 600; }"));
        if (!descriptor.manifest.name.isEmpty()) {
            m_logo->setText(descriptor.manifest.name.left(1).toUpper());
        }
        root->addWidget(m_logo, 0, Qt::AlignTop);

        auto* textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(3);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(6);

        auto* title = new QLabel(descriptor.manifest.name, this);
        title->setStyleSheet(
            QStringLiteral("color: #e5e7eb; font-size: 13px; font-weight: 600;"));
        titleRow->addWidget(title);

        auto* version = new QLabel(QStringLiteral("v") + descriptor.manifest.version, this);
        version->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
        titleRow->addWidget(version);

        if (descriptor.flags.official) {
            auto* badge = new QLabel(QStringLiteral("OFFICIAL"), this);
            badge->setStyleSheet(
                QStringLiteral("background: rgba(255,255,255,0.10); color: #d1d5db;"
                               " font-size: 10px; padding: 1px 6px; border-radius: 8px;"));
            titleRow->addWidget(badge);
        }
        titleRow->addStretch();
        textCol->addLayout(titleRow);

        const QString descriptionText = descriptor.manifest.description.isEmpty()
            ? QStringLiteral("No description")
            : descriptor.manifest.description;
        auto* description = new QLabel(descriptionText, this);
        description->setWordWrap(true);
        description->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
        textCol->addWidget(description);

        root->addLayout(textCol, 1);

        m_enabledToggle = new QCheckBox(QStringLiteral("Enabled"), this);
        m_enabledToggle->setChecked(descriptor.flags.enabled);
        m_enabledToggle->setCursor(Qt::PointingHandCursor);
        m_enabledToggle->setStyleSheet(QStringLiteral("color: #d1d5db; font-size: 11px;"));
        root->addWidget(m_enabledToggle, 0, Qt::AlignTop);
    }

    QLabel* logoLabel() const { return m_logo; }
    QCheckBox* enabledToggle() const { return m_enabledToggle; }
    QString addonId() const { return m_descriptor.manifest.id; }
    QUrl logoUrl() const { return m_descriptor.manifest.logo; }

private:
    AddonDescriptor m_descriptor;
    QLabel* m_logo = nullptr;
    QCheckBox* m_enabledToggle = nullptr;
};

}

AddonManagerScreen::AddonManagerScreen(AddonRegistry* registry, QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_nam(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("streamAddonManager"));
    buildUI();

    if (m_registry) {
        connect(m_registry, &AddonRegistry::addonsChanged,
                this, &AddonManagerScreen::refresh);
    }

    connect(this, &AddonManagerScreen::addAddonRequested, this, [this]() {
        if (!m_registry) {
            return;
        }
        AddAddonDialog dialog(m_registry, this);
        dialog.exec();
        refresh();
    });
}

void AddonManagerScreen::refresh()
{
    const QString previousSelectedId = (m_list->currentRow() >= 0 &&
                                        m_list->currentRow() < m_descriptors.size())
        ? m_descriptors[m_list->currentRow()].manifest.id
        : QString();

    m_list->clear();
    m_descriptors.clear();

    if (!m_registry) {
        m_emptyState->setVisible(true);
        m_list->setVisible(false);
        m_detail->setDescriptor(std::nullopt);
        return;
    }

    m_descriptors = m_registry->list();
    m_emptyState->setVisible(m_descriptors.isEmpty());
    m_list->setVisible(!m_descriptors.isEmpty());

    if (m_descriptors.isEmpty()) {
        m_detail->setDescriptor(std::nullopt);
        return;
    }

    int indexToSelect = 0;
    for (int i = 0; i < m_descriptors.size(); ++i) {
        const AddonDescriptor& addon = m_descriptors[i];

        auto* item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, 72));

        auto* row = new AddonRowWidget(addon, m_list);
        m_list->setItemWidget(item, row);

        const QString addonId = addon.manifest.id;
        QCheckBox* toggle = row->enabledToggle();
        connect(toggle, &QCheckBox::toggled, this,
            [this, addonId, toggle](bool enabled) {
                const bool ok = m_registry->setEnabled(addonId, enabled);
                if (!ok) {
                    QSignalBlocker blocker(toggle);
                    toggle->setChecked(!enabled);
                }
            });

        loadLogoAsync(row->logoUrl(), row->logoLabel());

        if (!previousSelectedId.isEmpty() && addonId == previousSelectedId) {
            indexToSelect = i;
        }
    }

    m_list->setCurrentRow(indexToSelect);
    updateDetailForRow(indexToSelect);
}

void AddonManagerScreen::updateDetailForRow(int row)
{
    if (row < 0 || row >= m_descriptors.size()) {
        m_detail->setDescriptor(std::nullopt);
        return;
    }
    m_detail->setDescriptor(m_descriptors[row]);
}

void AddonManagerScreen::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(10);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("Back"), this);
    m_backBtn->setFixedHeight(28);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    connect(m_backBtn, &QPushButton::clicked, this, &AddonManagerScreen::backRequested);
    header->addWidget(m_backBtn);

    auto* title = new QLabel(QStringLiteral("Addons"), this);
    title->setStyleSheet(
        QStringLiteral("color: #e5e7eb; font-size: 14px; font-weight: 600;"));
    header->addWidget(title);
    header->addStretch();

    m_addAddonBtn = new QPushButton(QStringLiteral("+ Add addon"), this);
    m_addAddonBtn->setFixedHeight(28);
    m_addAddonBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addAddonBtn, &QPushButton::clicked,
            this, &AddonManagerScreen::addAddonRequested);
    header->addWidget(m_addAddonBtn);

    root->addLayout(header);

    m_emptyState = new QLabel(QStringLiteral("No addons installed"), this);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 12px;"));
    m_emptyState->setVisible(false);
    root->addWidget(m_emptyState, 0);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(10);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("StreamAddonList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(6);
    m_list->setMinimumWidth(420);
    m_list->setStyleSheet(QStringLiteral(
        "#StreamAddonList { background: transparent; }"
        "#StreamAddonList::item { border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; margin: 0; }"
        "#StreamAddonList::item:selected {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.20); }"));
    body->addWidget(m_list, 1);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &AddonManagerScreen::updateDetailForRow);

    m_detail = new AddonDetailPanel(m_registry, this);
    body->addWidget(m_detail, 0);

    root->addLayout(body, 1);
}

void AddonManagerScreen::loadLogoAsync(const QUrl& url, QLabel* target)
{
    if (!target) {
        return;
    }
    if (!url.isValid() || url.toString().trimmed().isEmpty()) {
        return;
    }

    const QString key = url.toString();
    if (m_logoCache.contains(key)) {
        target->setPixmap(m_logoCache.value(key));
        target->setText({});
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                 " AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    QPointer<QLabel> guard(target);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll())) {
            return;
        }

        const QPixmap icon = pixmap.scaled(32, 32, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
        m_logoCache.insert(key, icon);
        if (guard) {
            guard->setPixmap(icon);
            guard->setText({});
        }
    });
}
