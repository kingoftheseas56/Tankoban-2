// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 2.3 (Addon Manager UI)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:119
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:120
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:122
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:123
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:124
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:125
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.1_addon_manager_screen.cpp:182
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.1_addon_manager_screen.cpp:207
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.1_addon_manager_screen.cpp:214
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.2_add_addon_dialog.cpp:74
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.2_add_addon_dialog.cpp:116
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/ContextMenuHelper.cpp:61
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/ContextMenuHelper.cpp:64
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/ContextMenuHelper.cpp:70
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/manifest.rs:22
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/manifest.rs:88
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/manifest.rs:99
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/descriptor.rs:8
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/descriptor.rs:33
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 2.3.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <optional>

namespace tankostream::addon {

struct ManifestResource {
    QString name;
};

struct ManifestCatalog {
    QString id;
    QString type;
    QString name;
};

struct AddonManifest {
    QString id;
    QString version;
    QString name;
    QString description;
    QUrl logo;
    QStringList types;
    QList<ManifestResource> resources;
    QList<ManifestCatalog> catalogs;
};

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
    AddonDescriptorFlags flags;
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr) : QObject(parent) {}
    QList<AddonDescriptor> list() const;
    bool setEnabled(const QString& addonId, bool enabled);
    bool uninstall(const QString& addonId);

signals:
    void addonsChanged();
};

} // namespace tankostream::addon

namespace tankostream::ui {

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::ManifestCatalog;
using tankostream::addon::ManifestResource;

static QWidget* makeCapabilityChip(const QString& text, QWidget* parent)
{
    auto* chip = new QLabel(text, parent);
    chip->setObjectName("AddonCapabilityChip");
    chip->setStyleSheet(
        "#AddonCapabilityChip {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 10px;"
        "  color: #d1d5db;"
        "  font-size: 11px;"
        "  padding: 2px 8px;"
        "}");
    return chip;
}

class AddonDetailPanel : public QFrame {
    Q_OBJECT

public:
    explicit AddonDetailPanel(AddonRegistry* registry, QWidget* parent = nullptr)
        : QFrame(parent)
        , m_registry(registry)
    {
        setObjectName("StreamAddonDetailPanel");
        setMinimumWidth(340);
        setFrameShape(QFrame::StyledPanel);
        buildUI();
        setDescriptor(std::nullopt);
    }

    void setDescriptor(const std::optional<AddonDescriptor>& descriptor)
    {
        m_current = descriptor;

        if (!m_current.has_value()) {
            m_placeholder->show();
            m_contentRoot->hide();
            return;
        }

        const AddonDescriptor& d = *m_current;
        m_placeholder->hide();
        m_contentRoot->show();

        m_name->setText(d.manifest.name);
        m_version->setText("v" + d.manifest.version);
        m_transport->setText(d.transportUrl.toString());
        m_desc->setText(d.manifest.description.isEmpty() ? "No description" : d.manifest.description);
        m_enabled->setChecked(d.flags.enabled);
        m_uninstallBtn->setVisible(!d.flags.protectedAddon);
        m_protectedNote->setVisible(d.flags.protectedAddon);
        m_officialBadge->setVisible(d.flags.official);

        refillChips(d);
    }

signals:
    void requestRefresh();

private slots:
    void onEnabledToggled(bool enabled)
    {
        if (!m_current.has_value() || !m_registry)
            return;
        if (!m_registry->setEnabled(m_current->manifest.id, enabled)) {
            // Revert if registry rejects.
            m_enabled->blockSignals(true);
            m_enabled->setChecked(!enabled);
            m_enabled->blockSignals(false);
            return;
        }
        emit requestRefresh();
    }

    void onUninstallClicked()
    {
        if (!m_current.has_value() || !m_registry)
            return;
        if (m_current->flags.protectedAddon)
            return;

        const int yes = QMessageBox::question(
            this,
            "Uninstall addon",
            "Uninstall " + m_current->manifest.name + "?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (yes != QMessageBox::Yes)
            return;

        if (!m_registry->uninstall(m_current->manifest.id))
            return;

        emit requestRefresh();
    }

private:
    void buildUI()
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(10);

        m_placeholder = new QLabel("Select an addon to view details", this);
        m_placeholder->setAlignment(Qt::AlignCenter);
        m_placeholder->setStyleSheet("color: #9ca3af; font-size: 12px;");
        root->addWidget(m_placeholder, 1);

        m_contentRoot = new QWidget(this);
        auto* contentLayout = new QVBoxLayout(m_contentRoot);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(10);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(6);

        m_name = new QLabel(m_contentRoot);
        m_name->setStyleSheet("color: #e5e7eb; font-size: 15px; font-weight: 600;");
        titleRow->addWidget(m_name);

        m_version = new QLabel(m_contentRoot);
        m_version->setStyleSheet("color: #9ca3af; font-size: 11px;");
        titleRow->addWidget(m_version);

        m_officialBadge = new QLabel("OFFICIAL", m_contentRoot);
        m_officialBadge->setStyleSheet(
            "background: rgba(255,255,255,0.10); color: #d1d5db;"
            "font-size: 10px; padding: 1px 6px; border-radius: 8px;");
        titleRow->addWidget(m_officialBadge);
        titleRow->addStretch();
        contentLayout->addLayout(titleRow);

        auto* transportLabel = new QLabel("Transport URL", m_contentRoot);
        transportLabel->setStyleSheet("color: #9ca3af; font-size: 11px;");
        contentLayout->addWidget(transportLabel);

        m_transport = new QLabel(m_contentRoot);
        m_transport->setWordWrap(true);
        m_transport->setStyleSheet("color: #d1d5db; font-size: 11px;");
        contentLayout->addWidget(m_transport);

        auto* descLabel = new QLabel("Description", m_contentRoot);
        descLabel->setStyleSheet("color: #9ca3af; font-size: 11px;");
        contentLayout->addWidget(descLabel);

        m_desc = new QLabel(m_contentRoot);
        m_desc->setWordWrap(true);
        m_desc->setStyleSheet("color: #d1d5db; font-size: 11px;");
        contentLayout->addWidget(m_desc);

        auto* capLabel = new QLabel("Capabilities", m_contentRoot);
        capLabel->setStyleSheet("color: #9ca3af; font-size: 11px;");
        contentLayout->addWidget(capLabel);

        m_chipWrap = new QWidget(m_contentRoot);
        m_chipLayout = new QGridLayout(m_chipWrap);
        m_chipLayout->setContentsMargins(0, 0, 0, 0);
        m_chipLayout->setHorizontalSpacing(6);
        m_chipLayout->setVerticalSpacing(6);
        contentLayout->addWidget(m_chipWrap);

        auto* actionLabel = new QLabel("Actions", m_contentRoot);
        actionLabel->setStyleSheet("color: #9ca3af; font-size: 11px;");
        contentLayout->addWidget(actionLabel);

        m_enabled = new QCheckBox("Enabled", m_contentRoot);
        m_enabled->setStyleSheet("color: #d1d5db; font-size: 12px;");
        connect(m_enabled, &QCheckBox::toggled, this, &AddonDetailPanel::onEnabledToggled);
        contentLayout->addWidget(m_enabled);

        // Use ContextMenuHelper danger visual language: red text action emphasis.
        m_uninstallBtn = new QPushButton("Uninstall", m_contentRoot);
        m_uninstallBtn->setCursor(Qt::PointingHandCursor);
        m_uninstallBtn->setStyleSheet(
            "QPushButton {"
            "  color: #e53935;"
            "  background: rgba(229,57,53,0.08);"
            "  border: 1px solid rgba(229,57,53,0.35);"
            "  border-radius: 6px;"
            "  padding: 5px 10px;"
            "}"
            "QPushButton:hover { background: rgba(229,57,53,0.14); }");
        connect(m_uninstallBtn, &QPushButton::clicked, this, &AddonDetailPanel::onUninstallClicked);
        contentLayout->addWidget(m_uninstallBtn, 0, Qt::AlignLeft);

        m_protectedNote = new QLabel("Protected addon: uninstall disabled", m_contentRoot);
        m_protectedNote->setStyleSheet("color: #9ca3af; font-size: 11px;");
        contentLayout->addWidget(m_protectedNote);

        contentLayout->addStretch();
        root->addWidget(m_contentRoot, 1);
    }

    void refillChips(const AddonDescriptor& d)
    {
        QLayoutItem* item = nullptr;
        while ((item = m_chipLayout->takeAt(0)) != nullptr) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }

        QStringList entries;
        for (const QString& type : d.manifest.types)
            entries.push_back("type:" + type);
        for (const ManifestResource& resource : d.manifest.resources)
            entries.push_back("resource:" + resource.name);
        for (const ManifestCatalog& catalog : d.manifest.catalogs)
            entries.push_back("catalog:" + catalog.type + "/" + catalog.id);

        int row = 0;
        int col = 0;
        constexpr int kCols = 2;
        for (const QString& e : entries) {
            m_chipLayout->addWidget(makeCapabilityChip(e, m_chipWrap), row, col);
            ++col;
            if (col >= kCols) {
                col = 0;
                ++row;
            }
        }

        if (entries.isEmpty()) {
            auto* none = new QLabel("No declared capabilities", m_chipWrap);
            none->setStyleSheet("color: #9ca3af; font-size: 11px;");
            m_chipLayout->addWidget(none, 0, 0);
        }
    }

    AddonRegistry* m_registry = nullptr;
    std::optional<AddonDescriptor> m_current;

    QLabel* m_placeholder = nullptr;
    QWidget* m_contentRoot = nullptr;

    QLabel* m_name = nullptr;
    QLabel* m_version = nullptr;
    QLabel* m_officialBadge = nullptr;
    QLabel* m_transport = nullptr;
    QLabel* m_desc = nullptr;

    QWidget* m_chipWrap = nullptr;
    QGridLayout* m_chipLayout = nullptr;

    QCheckBox* m_enabled = nullptr;
    QPushButton* m_uninstallBtn = nullptr;
    QLabel* m_protectedNote = nullptr;
};

class AddonRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit AddonRowWidget(const AddonDescriptor& descriptor, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_descriptor(descriptor)
    {
        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(10, 8, 10, 8);
        root->setSpacing(8);

        auto* titleCol = new QVBoxLayout();
        titleCol->setContentsMargins(0, 0, 0, 0);
        titleCol->setSpacing(2);

        auto* name = new QLabel(descriptor.manifest.name, this);
        name->setStyleSheet("color: #e5e7eb; font-size: 13px; font-weight: 600;");
        titleCol->addWidget(name);

        auto* ver = new QLabel("v" + descriptor.manifest.version, this);
        ver->setStyleSheet("color: #9ca3af; font-size: 11px;");
        titleCol->addWidget(ver);
        root->addLayout(titleCol, 1);

        auto* enabled = new QCheckBox("Enabled", this);
        enabled->setChecked(descriptor.flags.enabled);
        enabled->setEnabled(false); // edit from detail pane in 2.3.
        root->addWidget(enabled);
    }

    const AddonDescriptor& descriptor() const { return m_descriptor; }

private:
    AddonDescriptor m_descriptor;
};

class AddonManagerScreen : public QWidget {
    Q_OBJECT

public:
    explicit AddonManagerScreen(AddonRegistry* registry, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_registry(registry)
    {
        buildUI();
        connect(m_registry, &AddonRegistry::addonsChanged, this, &AddonManagerScreen::refresh);
    }

    void refresh()
    {
        m_list->clear();
        m_rows.clear();

        const QList<AddonDescriptor> addons = m_registry->list();
        for (const AddonDescriptor& addon : addons) {
            auto* item = new QListWidgetItem(m_list);
            item->setSizeHint(QSize(0, 66));

            auto* row = new AddonRowWidget(addon, m_list);
            m_list->setItemWidget(item, row);
            m_rows.push_back(row);
        }

        if (!addons.isEmpty()) {
            m_list->setCurrentRow(0);
            m_detail->setDescriptor(addons[0]);
        } else {
            m_detail->setDescriptor(std::nullopt);
        }
    }

signals:
    void backRequested();
    void addAddonRequested();

private:
    void buildUI()
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 10, 12, 10);
        root->setSpacing(10);

        auto* header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(8);

        auto* back = new QPushButton(QString::fromUtf8("\u2190 Browse"), this);
        back->setFixedHeight(28);
        connect(back, &QPushButton::clicked, this, &AddonManagerScreen::backRequested);
        header->addWidget(back);

        auto* title = new QLabel("Addons", this);
        title->setStyleSheet("color: #e5e7eb; font-size: 14px; font-weight: 600;");
        header->addWidget(title);
        header->addStretch();

        auto* addBtn = new QPushButton("+ Add addon", this);
        addBtn->setFixedHeight(28);
        connect(addBtn, &QPushButton::clicked, this, &AddonManagerScreen::addAddonRequested);
        header->addWidget(addBtn);

        root->addLayout(header);

        auto* body = new QHBoxLayout();
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(10);

        m_list = new QListWidget(this);
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setSpacing(6);
        m_list->setMinimumWidth(420);
        body->addWidget(m_list, 1);

        m_detail = new AddonDetailPanel(m_registry, this);
        body->addWidget(m_detail, 0);
        root->addLayout(body, 1);

        connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row < 0 || row >= m_rows.size()) {
                m_detail->setDescriptor(std::nullopt);
                return;
            }
            m_detail->setDescriptor(m_rows[row]->descriptor());
        });

        connect(m_detail, &AddonDetailPanel::requestRefresh, this, [this]() {
            refresh();
        });
    }

    AddonRegistry* m_registry = nullptr;
    QListWidget* m_list = nullptr;
    AddonDetailPanel* m_detail = nullptr;
    QList<AddonRowWidget*> m_rows;
};

} // namespace tankostream::ui

// -----------------------------------------------------------------
// Batch 2.3 behavior notes
// -----------------------------------------------------------------
//
// 1) Selection-driven detail pane:
//    - Addon list must be selectable (single-select).
//    - Right pane updates on row change and includes name/version/URL/description.
//
// 2) Capabilities chips:
//    - `types[]`, `resources[]`, `catalogs[]` rendered as read-only chips.
//
// 3) Actions:
//    - Enable/Disable toggles `AddonRegistry::setEnabled(...)`.
//    - Uninstall button uses danger visual language aligned to ContextMenuHelper.
//    - `protected=true` hides uninstall and keeps enable/disable available.
//
// 4) Aggregator implication:
//    - Registry remains source of truth; Phase 3/4 aggregators query enabled addons only.
//
