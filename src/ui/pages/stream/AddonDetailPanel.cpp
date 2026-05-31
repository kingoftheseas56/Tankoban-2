#include "AddonDetailPanel.h"
#include "core/net/NetSeam.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

#include "core/stream/addon/AddonRegistry.h"
#include "core/stream/addon/Manifest.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::ManifestCatalog;
using tankostream::addon::ManifestResource;

namespace {

QWidget* makeCapabilityChip(const QString& text, QWidget* parent)
{
    auto* chip = new QLabel(text, parent);
    chip->setObjectName(QStringLiteral("AddonCapabilityChip"));
    chip->setStyleSheet(QStringLiteral(
        "#AddonCapabilityChip {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 10px;"
        "  color: #d1d5db;"
        "  font-size: 11px;"
        "  padding: 2px 8px;"
        "}"));
    return chip;
}

bool isKnownProtectedConfigurableHost(const QUrl& url)
{
    const QString host = url.host().toLower();
    return host == QStringLiteral("torrentio.strem.fun")
        || host == QStringLiteral("opensubtitles-v3.strem.io");
}

bool canOpenConfigurePage(const AddonDescriptor& descriptor)
{
    return descriptor.manifest.behaviorHints.configurable
        || (descriptor.flags.protectedAddon
            && isKnownProtectedConfigurableHost(descriptor.transportUrl));
}

QUrl configureUrlFor(const AddonDescriptor& descriptor)
{
    if (descriptor.flags.protectedAddon
        && isKnownProtectedConfigurableHost(descriptor.transportUrl)) {
        QUrl root = descriptor.transportUrl;
        root.setPath(QStringLiteral("/configure"));
        root.setQuery(QString());
        root.setFragment(QString());
        return root;
    }

    QString base = descriptor.transportUrl.toString();
    const QString kManifestSuffix = QStringLiteral("/manifest.json");
    if (base.endsWith(kManifestSuffix, Qt::CaseInsensitive)) {
        base.chop(kManifestSuffix.length());
    }
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return QUrl(base + QStringLiteral("/configure"));
}

}

AddonDetailPanel::AddonDetailPanel(AddonRegistry* registry, QWidget* parent)
    : QFrame(parent)
    , m_registry(registry)
    , m_nam(tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-addon-detail")))
{
    setObjectName(QStringLiteral("StreamAddonDetailPanel"));
    setMinimumWidth(340);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "#StreamAddonDetailPanel {"
        "  background: rgba(255,255,255,0.02);"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px;"
        "}"));
    buildUI();
    if (m_registry) {
        connect(m_registry, &AddonRegistry::installSucceeded,
                this, &AddonDetailPanel::onReconfigureInstallSucceeded);
        connect(m_registry, &AddonRegistry::installFailed,
                this, &AddonDetailPanel::onReconfigureInstallFailed);
    }
    setDescriptor(std::nullopt);
}

void AddonDetailPanel::setDescriptor(const std::optional<AddonDescriptor>& descriptor)
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
    m_version->setText(QStringLiteral("v") + d.manifest.version);
    m_transport->setText(d.transportUrl.toString());
    m_desc->setText(d.manifest.description.isEmpty()
                        ? QStringLiteral("No description")
                        : d.manifest.description);

    {
        QSignalBlocker blocker(m_enabled);
        m_enabled->setChecked(d.flags.enabled);
    }

    m_officialBadge->setVisible(d.flags.official);
    m_uninstallBtn->setVisible(!d.flags.protectedAddon);
    m_protectedNote->setVisible(d.flags.protectedAddon);
    // Surface Configure for manifest-declared addons and Reconfigure for
    // protected defaults whose host is known to serve a /configure page.
    if (m_configureBtn) {
        const bool showConfigure = canOpenConfigurePage(d);
        m_configureBtn->setVisible(showConfigure);
        m_configureBtn->setText((d.flags.protectedAddon
                                 && isKnownProtectedConfigurableHost(d.transportUrl))
                                    ? QStringLiteral("Reconfigure")
                                    : QStringLiteral("Configure"));
    }

    m_logo->setPixmap({});
    m_logo->setText(d.manifest.name.isEmpty()
                        ? QString()
                        : d.manifest.name.left(1).toUpper());
    loadLogoAsync(d.manifest.logo);

    refillChips(d);
}

void AddonDetailPanel::onEnabledToggled(bool enabled)
{
    if (!m_current.has_value() || !m_registry) {
        return;
    }
    const bool ok = m_registry->setEnabled(m_current->manifest.id, enabled);
    if (!ok) {
        QSignalBlocker blocker(m_enabled);
        m_enabled->setChecked(!enabled);
    }
}

void AddonDetailPanel::onUninstallClicked()
{
    if (!m_current.has_value() || !m_registry) {
        return;
    }
    if (m_current->flags.protectedAddon) {
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Uninstall addon"),
        QStringLiteral("Uninstall ") + m_current->manifest.name + QStringLiteral("?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    m_registry->uninstall(m_current->manifest.id);
}

void AddonDetailPanel::onConfigureClicked()
{
    if (!m_current.has_value()) return;
    if (!canOpenConfigurePage(*m_current)) return;

    // Stremio addon convention: /configure is an HTML form served by the
    // addon. Protected bare seeds like Torrentio may hide configurability
    // in the seed manifest, so allowlisted hosts use their root /configure.
    const QUrl configureUrl = configureUrlFor(*m_current);
    if (!configureUrl.isValid()) return;

    if (!QDesktopServices::openUrl(configureUrl)) {
        QMessageBox::warning(this,
                             QStringLiteral("Configure addon"),
                             QStringLiteral("Could not open the configure page."));
        return;
    }

    if (!m_current->flags.protectedAddon
        || !isKnownProtectedConfigurableHost(m_current->transportUrl)) {
        return;
    }

    bool ok = false;
    const QString configuredUrl = QInputDialog::getText(
        this,
        QStringLiteral("Reconfigure addon"),
        QStringLiteral("Paste the configured manifest URL from your browser:"),
        QLineEdit::Normal,
        QString(),
        &ok).trimmed();
    if (!ok || configuredUrl.isEmpty() || !m_registry) {
        return;
    }

    const QUrl url(configuredUrl);
    if (!url.isValid() || url.scheme().isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Reconfigure addon"),
                             QStringLiteral("Enter a valid absolute URL (https://...)."));
        return;
    }

    m_pendingReconfigureUrl = url;
    m_registry->installByUrl(url);
}

void AddonDetailPanel::onReconfigureInstallSucceeded(const AddonDescriptor& descriptor)
{
    if (m_pendingReconfigureUrl.isEmpty()) {
        return;
    }
    m_pendingReconfigureUrl = QUrl();

    const QString name = descriptor.manifest.name.isEmpty()
        ? QStringLiteral("Addon")
        : descriptor.manifest.name;
    QMessageBox::information(this,
                             QStringLiteral("Addon reconfigured"),
                             name + QStringLiteral(" was updated."));
}

void AddonDetailPanel::onReconfigureInstallFailed(const QUrl& inputUrl,
                                                  const QString& message)
{
    if (m_pendingReconfigureUrl.isEmpty()) {
        return;
    }
    Q_UNUSED(inputUrl);
    m_pendingReconfigureUrl = QUrl();

    QMessageBox::warning(this,
                         QStringLiteral("Reconfigure addon"),
                         QStringLiteral("Error: ")
                             + (message.isEmpty()
                                    ? QStringLiteral("Install failed")
                                    : message));
}

void AddonDetailPanel::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_placeholder = new QLabel(QStringLiteral("Select an addon to view details"), this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 12px;"));
    root->addWidget(m_placeholder, 1);

    m_contentRoot = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(m_contentRoot);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(10);

    m_logo = new QLabel(m_contentRoot);
    m_logo->setObjectName(QStringLiteral("StreamAddonDetailLogo"));
    m_logo->setFixedSize(64, 64);
    m_logo->setAlignment(Qt::AlignCenter);
    m_logo->setStyleSheet(QStringLiteral(
        "#StreamAddonDetailLogo { background: rgba(255,255,255,0.06);"
        " border-radius: 6px; color: #9ca3af;"
        " font-size: 24px; font-weight: 600; }"));
    headerRow->addWidget(m_logo, 0, Qt::AlignTop);

    auto* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);

    m_name = new QLabel(m_contentRoot);
    m_name->setStyleSheet(
        QStringLiteral("color: #e5e7eb; font-size: 15px; font-weight: 600;"));
    titleRow->addWidget(m_name);

    m_version = new QLabel(m_contentRoot);
    m_version->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
    titleRow->addWidget(m_version);

    m_officialBadge = new QLabel(QStringLiteral("OFFICIAL"), m_contentRoot);
    m_officialBadge->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.10); color: #d1d5db;"
        " font-size: 10px; padding: 1px 6px; border-radius: 8px;"));
    titleRow->addWidget(m_officialBadge);
    titleRow->addStretch();
    titleCol->addLayout(titleRow);

    auto* transportLabel = new QLabel(QStringLiteral("Transport URL"), m_contentRoot);
    transportLabel->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 10px;"));
    titleCol->addWidget(transportLabel);

    m_transport = new QLabel(m_contentRoot);
    m_transport->setWordWrap(true);
    m_transport->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_transport->setStyleSheet(QStringLiteral("color: #d1d5db; font-size: 11px;"));
    titleCol->addWidget(m_transport);

    headerRow->addLayout(titleCol, 1);
    contentLayout->addLayout(headerRow);

    auto* descLabel = new QLabel(QStringLiteral("Description"), m_contentRoot);
    descLabel->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 10px;"));
    contentLayout->addWidget(descLabel);

    m_desc = new QLabel(m_contentRoot);
    m_desc->setWordWrap(true);
    m_desc->setStyleSheet(QStringLiteral("color: #d1d5db; font-size: 11px;"));
    contentLayout->addWidget(m_desc);

    auto* capLabel = new QLabel(QStringLiteral("Capabilities"), m_contentRoot);
    capLabel->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 10px;"));
    contentLayout->addWidget(capLabel);

    m_chipWrap = new QWidget(m_contentRoot);
    m_chipLayout = new QGridLayout(m_chipWrap);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setHorizontalSpacing(6);
    m_chipLayout->setVerticalSpacing(6);
    contentLayout->addWidget(m_chipWrap);

    auto* actionLabel = new QLabel(QStringLiteral("Actions"), m_contentRoot);
    actionLabel->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 10px;"));
    contentLayout->addWidget(actionLabel);

    m_enabled = new QCheckBox(QStringLiteral("Enabled"), m_contentRoot);
    m_enabled->setCursor(Qt::PointingHandCursor);
    m_enabled->setStyleSheet(QStringLiteral("color: #d1d5db; font-size: 12px;"));
    connect(m_enabled, &QCheckBox::toggled,
            this, &AddonDetailPanel::onEnabledToggled);
    contentLayout->addWidget(m_enabled);

    // STREAM_UX_PARITY Phase 5 Batch 5.1 — Configure button. Visible only
    // when the current descriptor's manifest declares
    // behaviorHints.configurable=true (toggled in setDescriptor). Opens
    // `{transportUrl}/configure` in the default browser — Stremio's
    // standard desktop pattern; we deliberately don't host an in-app
    // webview for config forms we don't control.
    m_configureBtn = new QPushButton(QStringLiteral("Configure"), m_contentRoot);
    m_configureBtn->setCursor(Qt::PointingHandCursor);
    m_configureBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: #e0e0e0;"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover { background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.28); }"));
    m_configureBtn->setVisible(false);   // setDescriptor toggles per-addon
    connect(m_configureBtn, &QPushButton::clicked,
            this, &AddonDetailPanel::onConfigureClicked);
    contentLayout->addWidget(m_configureBtn, 0, Qt::AlignLeft);

    // DANGER styling mirrors ContextMenuHelper::addDangerAction at ContextMenuHelper.cpp:66
    m_uninstallBtn = new QPushButton(QStringLiteral("Uninstall"), m_contentRoot);
    m_uninstallBtn->setCursor(Qt::PointingHandCursor);
    m_uninstallBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: #e53935;"
        "  background: rgba(229,57,53,0.08);"
        "  border: 1px solid rgba(229,57,53,0.35);"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover { background: rgba(229,57,53,0.14); }"));
    connect(m_uninstallBtn, &QPushButton::clicked,
            this, &AddonDetailPanel::onUninstallClicked);
    contentLayout->addWidget(m_uninstallBtn, 0, Qt::AlignLeft);

    m_protectedNote = new QLabel(
        QStringLiteral("Protected addon — uninstall disabled"), m_contentRoot);
    m_protectedNote->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
    contentLayout->addWidget(m_protectedNote);

    contentLayout->addStretch();
    root->addWidget(m_contentRoot, 1);
}

void AddonDetailPanel::refillChips(const AddonDescriptor& d)
{
    QLayoutItem* item = nullptr;
    while ((item = m_chipLayout->takeAt(0)) != nullptr) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    QStringList entries;
    for (const QString& type : d.manifest.types) {
        entries.append(QStringLiteral("type:") + type);
    }
    for (const ManifestResource& resource : d.manifest.resources) {
        entries.append(QStringLiteral("resource:") + resource.name);
    }
    for (const ManifestCatalog& catalog : d.manifest.catalogs) {
        entries.append(QStringLiteral("catalog:") + catalog.type +
                       QStringLiteral("/") + catalog.id);
    }

    if (entries.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No declared capabilities"), m_chipWrap);
        none->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
        m_chipLayout->addWidget(none, 0, 0);
        return;
    }

    constexpr int kCols = 2;
    int row = 0;
    int col = 0;
    for (const QString& entry : entries) {
        m_chipLayout->addWidget(makeCapabilityChip(entry, m_chipWrap), row, col);
        if (++col >= kCols) {
            col = 0;
            ++row;
        }
    }
}

void AddonDetailPanel::loadLogoAsync(const QUrl& url)
{
    if (!url.isValid() || url.toString().trimmed().isEmpty()) {
        return;
    }

    const QString key = url.toString();
    if (m_logoCache.contains(key)) {
        m_logo->setPixmap(m_logoCache.value(key));
        m_logo->setText({});
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                 " AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    QPointer<QLabel> guard(m_logo);
    const QString pendingKey = key;
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, pendingKey, guard]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    return;
                }
                QPixmap pixmap;
                if (!pixmap.loadFromData(reply->readAll())) {
                    return;
                }
                const QPixmap icon = pixmap.scaled(64, 64, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                m_logoCache.insert(pendingKey, icon);
                if (guard) {
                    guard->setPixmap(icon);
                    guard->setText({});
                }
            });
}
