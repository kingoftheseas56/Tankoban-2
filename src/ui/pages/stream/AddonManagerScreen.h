#pragma once

#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QUrl>
#include <QWidget>

#include "core/stream/addon/Descriptor.h"

class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QPushButton;

class AddonDetailPanel;

namespace tankostream::addon {
class AddonRegistry;
}

class AddonManagerScreen : public QWidget
{
    Q_OBJECT

public:
    explicit AddonManagerScreen(tankostream::addon::AddonRegistry* registry,
                                QWidget* parent = nullptr);

    void refresh();

signals:
    void backRequested();
    void addAddonRequested();

private:
    void buildUI();
    void loadLogoAsync(const QUrl& url, QLabel* target);
    void updateDetailForRow(int row);

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    QNetworkAccessManager* m_nam = nullptr;

    QPushButton* m_backBtn = nullptr;
    QPushButton* m_addAddonBtn = nullptr;
    QLabel* m_emptyState = nullptr;
    QListWidget* m_list = nullptr;
    AddonDetailPanel* m_detail = nullptr;

    QList<tankostream::addon::AddonDescriptor> m_descriptors;
    QHash<QString, QPixmap> m_logoCache;
};
