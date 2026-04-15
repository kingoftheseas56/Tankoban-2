// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 2.1 (Addon Manager UI)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:111
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.h:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:60
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:127
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamSearchWidget.cpp:48
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:105
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:181
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/installed_addons_with_filters.rs:35
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 2.1.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QObject>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QHash>
#include <QUrl>
#include <QString>
#include <QStringList>

namespace tankostream::addon {

// -----------------------------------------------------------------
// Minimal type/API slice from Batch 1.3 for standalone prototype.
// -----------------------------------------------------------------

struct AddonManifest {
    QString id;
    QString version;
    QString name;
    QString description;
    QUrl logo;
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

signals:
    void addonsChanged();
};

} // namespace tankostream::addon

namespace tankostream::ui {

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;

// -----------------------------------------------------------------
// One row in the addon list (logo + name/version + badge + desc + toggle)
// -----------------------------------------------------------------

class AddonRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit AddonRowWidget(const AddonDescriptor& descriptor, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_descriptor(descriptor)
    {
        setObjectName("StreamAddonRow");

        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(10, 8, 10, 8);
        root->setSpacing(10);

        m_logo = new QLabel(this);
        m_logo->setFixedSize(32, 32);
        m_logo->setObjectName("StreamAddonLogo");
        m_logo->setStyleSheet(
            "#StreamAddonLogo { background: rgba(255,255,255,0.06); border-radius: 4px; }");
        m_logo->setAlignment(Qt::AlignCenter);
        root->addWidget(m_logo, 0, Qt::AlignTop);

        auto* textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(3);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(6);

        m_title = new QLabel(descriptor.manifest.name, this);
        m_title->setStyleSheet("color: #e5e7eb; font-size: 13px; font-weight: 600;");
        titleRow->addWidget(m_title);

        m_version = new QLabel("v" + descriptor.manifest.version, this);
        m_version->setStyleSheet("color: #9ca3af; font-size: 11px;");
        titleRow->addWidget(m_version);

        if (descriptor.flags.official) {
            m_officialBadge = new QLabel("OFFICIAL", this);
            m_officialBadge->setStyleSheet(
                "background: rgba(255,255,255,0.10); color: #d1d5db;"
                "font-size: 10px; padding: 1px 6px; border-radius: 8px;");
            titleRow->addWidget(m_officialBadge);
        }
        titleRow->addStretch();
        textCol->addLayout(titleRow);

        m_description = new QLabel(
            descriptor.manifest.description.isEmpty()
                ? QString("No description")
                : descriptor.manifest.description,
            this);
        m_description->setWordWrap(true);
        m_description->setStyleSheet("color: #9ca3af; font-size: 11px;");
        textCol->addWidget(m_description);

        root->addLayout(textCol, 1);

        auto* rightCol = new QVBoxLayout();
        rightCol->setContentsMargins(0, 0, 0, 0);
        rightCol->setSpacing(2);
        rightCol->setAlignment(Qt::AlignTop);

        m_enabledToggle = new QCheckBox("Enabled", this);
        m_enabledToggle->setChecked(descriptor.flags.enabled);
        m_enabledToggle->setCursor(Qt::PointingHandCursor);
        m_enabledToggle->setStyleSheet("color: #d1d5db; font-size: 11px;");
        rightCol->addWidget(m_enabledToggle);

        root->addLayout(rightCol, 0);
    }

    QLabel* logoLabel() const { return m_logo; }
    QCheckBox* enabledToggle() const { return m_enabledToggle; }
    QString addonId() const { return m_descriptor.manifest.id; }
    QUrl logoUrl() const { return m_descriptor.manifest.logo; }

private:
    AddonDescriptor m_descriptor;
    QLabel* m_logo = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_version = nullptr;
    QLabel* m_officialBadge = nullptr;
    QLabel* m_description = nullptr;
    QCheckBox* m_enabledToggle = nullptr;
};

// -----------------------------------------------------------------
// Batch 2.1 main screen
// -----------------------------------------------------------------

class AddonManagerScreen : public QWidget {
    Q_OBJECT

public:
    explicit AddonManagerScreen(AddonRegistry* registry, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_registry(registry)
        , m_nam(new QNetworkAccessManager(this))
    {
        setObjectName("streamAddonManager");
        buildUI();

        connect(m_registry, &AddonRegistry::addonsChanged, this, &AddonManagerScreen::refresh);
    }

    void refresh()
    {
        m_list->clear();
        m_rowByAddonId.clear();

        const QList<AddonDescriptor> addons = m_registry->list();
        m_emptyState->setVisible(addons.isEmpty());
        m_list->setVisible(!addons.isEmpty());

        for (const AddonDescriptor& addon : addons) {
            auto* item = new QListWidgetItem(m_list);
            item->setSizeHint(QSize(0, 72));

            auto* row = new AddonRowWidget(addon, m_list);
            m_list->setItemWidget(item, row);
            m_rowByAddonId.insert(addon.manifest.id, row);

            connect(row->enabledToggle(), &QCheckBox::toggled, this,
                [this, addonId = addon.manifest.id](bool enabled) {
                    const bool ok = m_registry->setEnabled(addonId, enabled);
                    if (!ok) {
                        auto it = m_rowByAddonId.find(addonId);
                        if (it != m_rowByAddonId.end())
                            it.value()->enabledToggle()->setChecked(!enabled);
                    }
                });

            // Async 32x32 logo load (memoized by URL string).
            loadLogoAsync(addon.manifest.logo, row->logoLabel());
        }
    }

signals:
    void backRequested();
    void addAddonRequested(); // batch 2.2 entry point

private:
    void buildUI()
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 10, 12, 10);
        root->setSpacing(10);

        auto* header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(8);

        m_backBtn = new QPushButton(QString::fromUtf8("\u2190 Browse"), this);
        m_backBtn->setFixedHeight(28);
        m_backBtn->setCursor(Qt::PointingHandCursor);
        connect(m_backBtn, &QPushButton::clicked, this, &AddonManagerScreen::backRequested);
        header->addWidget(m_backBtn);

        auto* title = new QLabel("Addons", this);
        title->setStyleSheet("color: #e5e7eb; font-size: 14px; font-weight: 600;");
        header->addWidget(title);
        header->addStretch();

        m_addAddonBtn = new QPushButton("+ Add addon", this);
        m_addAddonBtn->setFixedHeight(28);
        m_addAddonBtn->setCursor(Qt::PointingHandCursor);
        connect(m_addAddonBtn, &QPushButton::clicked, this, &AddonManagerScreen::addAddonRequested);
        header->addWidget(m_addAddonBtn);

        root->addLayout(header);

        m_emptyState = new QLabel("No addons installed", this);
        m_emptyState->setAlignment(Qt::AlignCenter);
        m_emptyState->setStyleSheet("color: #9ca3af; font-size: 12px;");
        root->addWidget(m_emptyState, 1);

        m_list = new QListWidget(this);
        m_list->setObjectName("StreamAddonList");
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setSpacing(6);
        m_list->setStyleSheet(
            "#StreamAddonList { background: transparent; }"
            "#StreamAddonList::item { border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 8px; margin: 0; }");
        root->addWidget(m_list, 1);
    }

    void loadLogoAsync(const QUrl& url, QLabel* target)
    {
        if (!target)
            return;

        if (!url.isValid() || url.toString().trimmed().isEmpty()) {
            target->setText("A");
            return;
        }

        const QString key = url.toString();
        if (m_logoCache.contains(key)) {
            target->setPixmap(m_logoCache.value(key));
            return;
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
        req.setTransferTimeout(10000);

        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, target]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
                return;

            QPixmap pix;
            if (!pix.loadFromData(reply->readAll()))
                return;

            QPixmap icon = pix.scaled(32, 32, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            m_logoCache.insert(key, icon);
            if (target)
                target->setPixmap(icon);
        });
    }

    AddonRegistry* m_registry = nullptr;
    QNetworkAccessManager* m_nam = nullptr;

    QPushButton* m_backBtn = nullptr;
    QPushButton* m_addAddonBtn = nullptr;
    QLabel* m_emptyState = nullptr;
    QListWidget* m_list = nullptr;

    QHash<QString, AddonRowWidget*> m_rowByAddonId;
    QHash<QString, QPixmap> m_logoCache;
};

} // namespace tankostream::ui

// -----------------------------------------------------------------
// StreamPage integration sketch (Batch 2.1 scope only)
// -----------------------------------------------------------------
//
// 1) Add members in StreamPage.h:
//      QPushButton* m_addonsBtn = nullptr;       // 28x28 gear in browse header
//      AddonManagerScreen* m_addonManager = nullptr;
//      AddonRegistry* m_addonRegistry = nullptr; // from Phase 1
//
// 2) In StreamPage::buildUI():
//      m_addonManager = new AddonManagerScreen(m_addonRegistry, this);
//      m_mainStack->addWidget(m_addonManager);   // index 3: addons
//      connect(m_addonManager, &AddonManagerScreen::backRequested,
//              this, &StreamPage::showBrowse);
//
// 3) In StreamPage::buildSearchBar() (or browse top row):
//      m_addonsBtn = new QPushButton(QString::fromUtf8("\u2699"), m_searchBarFrame);
//      m_addonsBtn->setFixedSize(28, 28);
//      m_addonsBtn->setCursor(Qt::PointingHandCursor);
//      layout->addWidget(m_addonsBtn);
//      connect(m_addonsBtn, &QPushButton::clicked, this, [this]() {
//          if (m_addonManager) m_addonManager->refresh();
//          m_mainStack->setCurrentIndex(3);
//      });
//
// 4) Existing showBrowse() continues to set index 0 and is the single back target.
//
