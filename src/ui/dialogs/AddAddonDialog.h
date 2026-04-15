#pragma once

#include <QDialog>
#include <QUrl>

#include "core/stream/addon/Descriptor.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace tankostream::addon {
class AddonRegistry;
class AddonTransport;
}

class AddAddonDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddAddonDialog(tankostream::addon::AddonRegistry* registry,
                            QWidget* parent = nullptr);

    // STREAM_UX_PARITY Phase 4 Batch 4.3 — pre-fill the URL field when the
    // dialog is opened from the search-bar URL-paste flow. Safe to call
    // between construction and exec(); the user can still edit or clear
    // before clicking Install.
    void setPrefilledUrl(const QString& url);

private slots:
    void onInstallClicked();
    void onInstallSucceeded(const tankostream::addon::AddonDescriptor& descriptor);
    void onInstallFailed(const QUrl& inputUrl, const QString& message);

    // STREAM_UX_PARITY Phase 5 Batch 5.2 — configurationRequired install flow.
    // The probe transport fetches the manifest only; based on
    // manifest.behaviorHints.configurationRequired we branch:
    //   false → proceed with normal AddonRegistry::installByUrl flow.
    //   true  → enter Configure flow: button → "Open Configure Page" →
    //           QDesktopServices::openUrl → user pastes new URL → install
    //           with that URL.
    void onProbeManifestReady(const tankostream::addon::AddonDescriptor& descriptor);
    void onProbeManifestFailed(const QString& message);

private:
    void buildUI();
    void setBusy(bool busy);
    void showStatus(const QString& message, bool error);

    // Configure-flow state machine (Batch 5.2).
    enum class Phase {
        Idle,                  // user types URL, button = "Install"
        Probing,               // probe inflight
        AwaitOpenConfigure,    // probe said configRequired; button = "Open Configure Page"
        AwaitConfiguredUrl,    // user clicked Open; button = "Install" (expects new URL)
        Installing             // registry->installByUrl() in flight
    };

    void enterPhase(Phase p);
    void startProbe(const QUrl& url);

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    tankostream::addon::AddonTransport* m_probe = nullptr;   // Batch 5.2 — manifest probe
    QUrl m_pendingUrl;
    QUrl m_configureUrl;   // Batch 5.2 — /configure page URL
    Phase m_phase = Phase::Idle;

    QLineEdit* m_urlInput = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_installBtn = nullptr;
};
