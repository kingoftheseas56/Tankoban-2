#include "AddAddonDialog.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/stream/addon/AddonRegistry.h"
#include "core/stream/addon/AddonTransport.h"
#include "core/stream/addon/Descriptor.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;

AddAddonDialog::AddAddonDialog(AddonRegistry* registry, QWidget* parent)
    : QDialog(parent)
    , m_registry(registry)
{
    setObjectName(QStringLiteral("AddAddonDialog"));
    setWindowTitle(QStringLiteral("Install Addon"));
    setModal(true);
    setFixedSize(500, 300);
    buildUI();

    if (m_registry) {
        connect(m_registry, &AddonRegistry::installSucceeded,
                this, &AddAddonDialog::onInstallSucceeded);
        connect(m_registry, &AddonRegistry::installFailed,
                this, &AddAddonDialog::onInstallFailed);
    }

    // STREAM_UX_PARITY Phase 5 Batch 5.2 — dedicated probe transport so the
    // dialog can introspect manifest.behaviorHints.configurationRequired
    // WITHOUT touching AddonRegistry state. Using the registry's own
    // installByUrl would commit to install before we know if the addon
    // needs configuration; a separate transport lets us peek + branch.
    m_probe = new AddonTransport(this);
    connect(m_probe, &AddonTransport::manifestReady,
            this, &AddAddonDialog::onProbeManifestReady);
    connect(m_probe, &AddonTransport::manifestFailed,
            this, &AddAddonDialog::onProbeManifestFailed);
}

void AddAddonDialog::onInstallClicked()
{
    // STREAM_UX_PARITY Phase 5 Batch 5.2 — dispatch by phase.
    if (m_phase == Phase::AwaitOpenConfigure) {
        // Step: user sees the configure-required message, clicks "Open
        // Configure Page". Launch the addon's /configure URL in the
        // default browser; flip to AwaitConfiguredUrl state so the next
        // click installs with whatever URL they paste back.
        if (m_configureUrl.isValid()) {
            QDesktopServices::openUrl(m_configureUrl);
        }
        enterPhase(Phase::AwaitConfiguredUrl);
        return;
    }

    // Phase::Idle or Phase::AwaitConfiguredUrl — both enter install via
    // probing first. In Idle, this is the normal install. In
    // AwaitConfiguredUrl, the user has completed configure in their
    // browser and pasted the new (configured) manifest URL; we probe it
    // to confirm configurationRequired is no longer set, then install.
    if (!m_registry) {
        showStatus(QStringLiteral("Addon registry not available"), true);
        return;
    }

    const QString raw = m_urlInput->text().trimmed();
    if (raw.isEmpty()) {
        showStatus(QStringLiteral("Manifest URL is required"), true);
        return;
    }

    QUrl url(raw);
    if (!url.isValid() || url.scheme().isEmpty()) {
        showStatus(QStringLiteral("Enter a valid absolute URL (https://...)"), true);
        return;
    }

    startProbe(url);
}

void AddAddonDialog::startProbe(const QUrl& url)
{
    m_pendingUrl = url;
    enterPhase(Phase::Probing);
    showStatus(QStringLiteral("Checking addon manifest..."), false);
    m_probe->fetchManifest(url);
}

void AddAddonDialog::onProbeManifestReady(const AddonDescriptor& descriptor)
{
    if (m_phase != Phase::Probing) return;   // stale callback

    if (descriptor.manifest.behaviorHints.configurationRequired) {
        // Addon requires configuration before it can be used. Compute
        // the /configure URL sibling and enter the configure-open state.
        // Mirrors Batch 5.1's URL-trimming logic in AddonDetailPanel.
        QString base = m_pendingUrl.toString();
        const QString kManifestSuffix = QStringLiteral("/manifest.json");
        if (base.endsWith(kManifestSuffix, Qt::CaseInsensitive)) {
            base.chop(kManifestSuffix.length());
        }
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        m_configureUrl = QUrl(base + QStringLiteral("/configure"));

        enterPhase(Phase::AwaitOpenConfigure);
        showStatus(QStringLiteral(
            "This addon requires configuration. Click \u201COpen Configure Page\u201D "
            "to complete setup in your browser; then paste the configured manifest "
            "URL back into this field."),
            false);
        return;
    }

    // Normal install path — probe confirmed no configuration required.
    // Hand off to AddonRegistry::installByUrl; the registry does its own
    // fetchManifest + validate + persist, so the probe was advisory.
    enterPhase(Phase::Installing);
    showStatus(QStringLiteral("Installing addon..."), false);
    m_registry->installByUrl(m_pendingUrl);
}

void AddAddonDialog::onProbeManifestFailed(const QString& message)
{
    if (m_phase != Phase::Probing) return;   // stale callback

    enterPhase(Phase::Idle);
    showStatus(QStringLiteral("Error: ")
                   + (message.isEmpty()
                          ? QStringLiteral("Could not fetch addon manifest")
                          : message),
               true);
}

void AddAddonDialog::enterPhase(Phase p)
{
    m_phase = p;
    switch (p) {
        case Phase::Idle:
            m_installBtn->setText(QStringLiteral("Install"));
            m_urlInput->setEnabled(true);
            m_installBtn->setEnabled(true);
            m_cancelBtn->setEnabled(true);
            break;
        case Phase::Probing:
            m_installBtn->setText(QStringLiteral("Install"));
            m_urlInput->setEnabled(false);
            m_installBtn->setEnabled(false);
            m_cancelBtn->setEnabled(true);
            break;
        case Phase::AwaitOpenConfigure:
            m_installBtn->setText(QStringLiteral("Open Configure Page"));
            m_urlInput->setEnabled(false);
            m_installBtn->setEnabled(true);
            m_cancelBtn->setEnabled(true);
            break;
        case Phase::AwaitConfiguredUrl:
            m_installBtn->setText(QStringLiteral("Install"));
            m_urlInput->setEnabled(true);
            m_urlInput->clear();
            m_urlInput->setPlaceholderText(
                QStringLiteral("Paste the configured manifest URL from your browser here"));
            m_urlInput->setFocus();
            m_installBtn->setEnabled(true);
            m_cancelBtn->setEnabled(true);
            break;
        case Phase::Installing:
            m_installBtn->setText(QStringLiteral("Install"));
            m_urlInput->setEnabled(false);
            m_installBtn->setEnabled(false);
            m_cancelBtn->setEnabled(true);
            break;
    }
}

void AddAddonDialog::onInstallSucceeded(const AddonDescriptor& descriptor)
{
    // Only consume the signal if THIS dialog initiated the install. Batch
    // 5.2 may fire probes without committing to install (configure flow),
    // so gate on the Installing phase explicitly rather than the old
    // m_pendingUrl.isValid() check.
    if (m_phase != Phase::Installing) return;
    if (!m_pendingUrl.isValid()) return;

    m_pendingUrl = QUrl();

    const QString name = descriptor.manifest.name.isEmpty()
        ? QStringLiteral("Addon")
        : descriptor.manifest.name;
    showStatus(QStringLiteral("Installed: ") + name, false);

    accept();
}

void AddAddonDialog::onInstallFailed(const QUrl& inputUrl, const QString& message)
{
    Q_UNUSED(inputUrl);
    if (m_phase != Phase::Installing) return;
    if (!m_pendingUrl.isValid()) return;

    m_pendingUrl = QUrl();
    enterPhase(Phase::Idle);
    showStatus(QStringLiteral("Error: ") + (message.isEmpty()
                                                ? QStringLiteral("Install failed")
                                                : message),
               true);
}

void AddAddonDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Install Stremio Addon by URL"), this);
    title->setStyleSheet(
        QStringLiteral("color: #e5e7eb; font-size: 14px; font-weight: 600;"));
    root->addWidget(title);

    auto* help = new QLabel(
        QStringLiteral("Paste a manifest URL or base addon URL. Example:\n"
                       "https://opensubtitles-v3.strem.io/manifest.json"),
        this);
    help->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
    help->setWordWrap(true);
    root->addWidget(help);

    m_urlInput = new QLineEdit(this);
    m_urlInput->setObjectName(QStringLiteral("AddAddonUrlInput"));
    m_urlInput->setPlaceholderText(
        QStringLiteral("https://example-addon.strem.io/manifest.json"));
    m_urlInput->setMinimumHeight(32);
    root->addWidget(m_urlInput);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("AddAddonStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
    m_statusLabel->setText(QStringLiteral("Idle"));
    root->addWidget(m_statusLabel);

    root->addStretch();

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelBtn->setFixedHeight(32);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(m_cancelBtn);

    m_installBtn = new QPushButton(QStringLiteral("Install"), this);
    m_installBtn->setFixedHeight(32);
    m_installBtn->setCursor(Qt::PointingHandCursor);
    m_installBtn->setDefault(true);
    connect(m_installBtn, &QPushButton::clicked,
            this, &AddAddonDialog::onInstallClicked);
    buttonRow->addWidget(m_installBtn);

    root->addLayout(buttonRow);
}

void AddAddonDialog::setBusy(bool busy)
{
    m_urlInput->setEnabled(!busy);
    m_installBtn->setEnabled(!busy);
    m_cancelBtn->setEnabled(!busy);
}

void AddAddonDialog::showStatus(const QString& message, bool error)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(error
        ? QStringLiteral("color: #d1d5db; font-size: 11px; font-weight: 600;")
        : QStringLiteral("color: #9ca3af; font-size: 11px;"));
}

void AddAddonDialog::setPrefilledUrl(const QString& url)
{
    // STREAM_UX_PARITY Phase 4 Batch 4.3 — callable between construction
    // and exec() from the search-bar URL-paste flow. User can still edit
    // or clear before clicking Install.
    if (m_urlInput) {
        m_urlInput->setText(url);
        m_urlInput->setCursorPosition(url.length());
    }
}
