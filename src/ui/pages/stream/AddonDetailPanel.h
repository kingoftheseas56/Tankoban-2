#pragma once

#include <QFrame>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <QUrl>

#include <optional>

#include "core/stream/addon/Descriptor.h"

class QCheckBox;
class QGridLayout;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QWidget;

namespace tankostream::addon {
class AddonRegistry;
}

class AddonDetailPanel : public QFrame
{
    Q_OBJECT

public:
    explicit AddonDetailPanel(tankostream::addon::AddonRegistry* registry,
                              QWidget* parent = nullptr);

    void setDescriptor(const std::optional<tankostream::addon::AddonDescriptor>& descriptor);

private slots:
    void onEnabledToggled(bool enabled);
    void onUninstallClicked();
    // STREAM_UX_PARITY Phase 5 Batch 5.1 — opens `{transportUrl}/configure`
    // in the default browser via QDesktopServices. Button is visible only
    // when manifest.behaviorHints.configurable is true.
    void onConfigureClicked();

private:
    void buildUI();
    void refillChips(const tankostream::addon::AddonDescriptor& descriptor);
    void loadLogoAsync(const QUrl& url);

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    std::optional<tankostream::addon::AddonDescriptor> m_current;

    QLabel* m_placeholder = nullptr;
    QWidget* m_contentRoot = nullptr;

    QLabel* m_logo = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_version = nullptr;
    QLabel* m_officialBadge = nullptr;
    QLabel* m_transport = nullptr;
    QLabel* m_desc = nullptr;

    QWidget* m_chipWrap = nullptr;
    QGridLayout* m_chipLayout = nullptr;

    QCheckBox* m_enabled = nullptr;
    QPushButton* m_uninstallBtn = nullptr;
    // STREAM_UX_PARITY Phase 5 Batch 5.1 — Configure button, only visible
    // when the current descriptor's manifest.behaviorHints.configurable is
    // true. Grayscale styling per feedback_no_color_no_emoji.
    QPushButton* m_configureBtn = nullptr;
    QLabel* m_protectedNote = nullptr;

    QHash<QString, QPixmap> m_logoCache;
};
