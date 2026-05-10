#pragma once

#include <QDialog>
#include <QSet>
#include <QString>

#include "core/stream/BulkPackVerifier.h"
#include "core/stream/BulkSourceCollector.h"
#include "core/stream/StreamBulkPlan.h"

class QLabel;
class QListWidget;
class QPushButton;

class StreamBulkPreflightDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StreamBulkPreflightDialog(
        const tankostream::stream::BulkSelectionPlan& plan,
        const tankostream::stream::BulkPackVerificationResult& verifierResult,
        const tankostream::stream::BulkSourceCollectionPayload& sourcePayload,
        const tankostream::stream::BulkPlanResult& planResult,
        const QString& displayLabel,
        const QString& videosRootPath,
        const QString& verificationNote = QString(),
        QWidget* parent = nullptr);

    // STREAM_BULK_DOWNLOAD_V2 Phase 1 — caller reads which itemKeys the user
    // ticked. Returns only enabled+checked rows (skip-if-exists and
    // missing-no-source rows are visible-disabled and never selectable).
    QSet<QString> selectedItemKeys() const;

private:
    void rebuildSelectedSummary();
    void setAllCheckStates(Qt::CheckState state);

    QListWidget* m_episodeList = nullptr;
    QLabel*      m_selectedSummaryLabel = nullptr;
    QPushButton* m_acceptButton = nullptr;
    qint64       m_estimatedBytesSelected = 0;
};
