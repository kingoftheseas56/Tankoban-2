#pragma once

#include <QDialog>
#include <QSpinBox>

class QueueLimitsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QueueLimitsDialog(QWidget* parent = nullptr);

    int maxDownloads() const;
    int maxUploads() const;
    int maxActive() const;

private:
    QSpinBox* m_dlSpin    = nullptr;
    QSpinBox* m_ulSpin    = nullptr;
    QSpinBox* m_totalSpin = nullptr;
};
