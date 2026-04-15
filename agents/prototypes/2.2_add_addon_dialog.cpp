// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 2.2 (Addon Manager UI)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:111
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:114
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:115
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.1_addon_manager_screen.cpp:231
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.1_addon_manager_screen.cpp:255
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/2.1_addon_manager_screen.cpp:258
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:150
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:204
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:205
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/dialogs/AddMangaDialog.cpp:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/dialogs/AddMangaDialog.cpp:185
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/dialogs/AddTorrentDialog.cpp:48
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:129
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 2.2.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QObject>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace tankostream::addon {

struct AddonManifest {
    QString id;
    QString version;
    QString name;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
};

// Surface borrowed from Batch 1.3 prototype.
class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr) : QObject(parent) {}
    QList<AddonDescriptor> list() const;
    void installByUrl(const QUrl& transportUrlInput);

signals:
    void addonsChanged();
    void installSucceeded(const AddonDescriptor& descriptor);
    void installFailed(const QUrl& inputUrl, const QString& message);
};

} // namespace tankostream::addon

namespace tankostream::ui {

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;

class AddAddonDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddAddonDialog(AddonRegistry* registry, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_registry(registry)
    {
        setWindowTitle("Install Addon");
        setFixedSize(500, 300); // Batch 2.2 size target.
        buildUI();

        connect(m_registry, &AddonRegistry::installSucceeded,
                this, &AddAddonDialog::onInstallSucceeded);
        connect(m_registry, &AddonRegistry::installFailed,
                this, &AddAddonDialog::onInstallFailed);
    }

private slots:
    void onInstallClicked()
    {
        if (!m_registry)
            return;

        const QString raw = m_urlInput->text().trimmed();
        if (raw.isEmpty()) {
            showError("Manifest URL is required");
            return;
        }

        QUrl url(raw);
        if (!url.isValid() || url.scheme().isEmpty()) {
            showError("Enter a valid absolute URL");
            return;
        }

        m_pendingUrl = url;
        setBusy(true);
        m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
        m_statusLabel->setText("Installing addon...");

        // Registry normalizes base URLs and manifest URLs.
        m_registry->installByUrl(url);
    }

    void onInstallSucceeded(const AddonDescriptor& descriptor)
    {
        // Guard against stale signals from older dialogs.
        if (!m_pendingUrl.isValid())
            return;

        m_statusLabel->setStyleSheet("font-size: 11px; color: rgb(34,197,94);");
        m_statusLabel->setText("Installed: " + descriptor.manifest.name);
        m_pendingUrl = QUrl();

        // Success contract for 2.2: close + refresh list.
        // AddonManagerScreen listens to registry->addonsChanged and/or dialog Accepted.
        accept();
    }

    void onInstallFailed(const QUrl& inputUrl, const QString& message)
    {
        if (!m_pendingUrl.isValid())
            return;
        if (inputUrl.isValid() && inputUrl != m_pendingUrl)
            return;

        setBusy(false);
        showError(message.isEmpty() ? QString("Install failed") : message);
        m_pendingUrl = QUrl();
    }

private:
    void buildUI()
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 12, 16, 12);
        root->setSpacing(10);

        auto* title = new QLabel("Install Stremio Addon by URL", this);
        title->setStyleSheet("font-size: 14px; font-weight: bold; color: #eee;");
        root->addWidget(title);

        auto* help = new QLabel(
            "Paste a manifest URL or base addon URL. Example:\n"
            "https://opensubtitles-v3.strem.io/manifest.json",
            this);
        help->setStyleSheet("font-size: 11px; color: #9ca3af;");
        help->setWordWrap(true);
        root->addWidget(help);

        m_urlInput = new QLineEdit(this);
        m_urlInput->setPlaceholderText("https://example-addon.strem.io/manifest.json");
        m_urlInput->setMinimumHeight(32);
        root->addWidget(m_urlInput);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
        m_statusLabel->setText("Idle");
        root->addWidget(m_statusLabel);

        root->addStretch();

        auto* buttonRow = new QHBoxLayout();
        buttonRow->addStretch();

        m_cancelBtn = new QPushButton("Cancel", this);
        m_cancelBtn->setFixedHeight(32);
        m_cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        buttonRow->addWidget(m_cancelBtn);

        m_installBtn = new QPushButton("Install", this);
        m_installBtn->setFixedHeight(32);
        m_installBtn->setCursor(Qt::PointingHandCursor);
        m_installBtn->setDefault(true);
        connect(m_installBtn, &QPushButton::clicked, this, &AddAddonDialog::onInstallClicked);
        buttonRow->addWidget(m_installBtn);

        root->addLayout(buttonRow);
    }

    void setBusy(bool busy)
    {
        m_urlInput->setEnabled(!busy);
        m_installBtn->setEnabled(!busy);
        m_cancelBtn->setEnabled(!busy);
    }

    void showError(const QString& message)
    {
        m_statusLabel->setStyleSheet("font-size: 11px; color: rgb(220,50,50);");
        m_statusLabel->setText("Error: " + message);
    }

    AddonRegistry* m_registry = nullptr;
    QUrl m_pendingUrl;

    QLineEdit* m_urlInput = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_installBtn = nullptr;
};

} // namespace tankostream::ui

// -----------------------------------------------------------------
// AddonManagerScreen integration sketch (Batch 2.2 scope only)
// -----------------------------------------------------------------
//
// In AddonManagerScreen constructor:
//   connect(this, &AddonManagerScreen::addAddonRequested, this, [this]() {
//       AddAddonDialog dialog(m_registry, this);
//       if (dialog.exec() == QDialog::Accepted) {
//           // Registry already emitted addonsChanged; explicit refresh keeps UX immediate.
//           refresh();
//       }
//   });
//
// Notes:
// 1) Keep AddonRegistry as single source of truth for persistence + validation.
// 2) Keep all user-visible failures inline in dialog status label (no transient toasts).
// 3) Dialog can accept both base URL and /manifest.json URL; registry normalizes.
//
