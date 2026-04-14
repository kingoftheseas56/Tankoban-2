#pragma once

#include <QDialog>
#include <QSpinBox>

class SpeedLimitDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpeedLimitDialog(const QString& title, int currentDlKBs, int currentUlKBs,
                              QWidget* parent = nullptr);

    int dlLimitBps() const;  // KB/s * 1024
    int ulLimitBps() const;

private:
    QSpinBox* m_dlSpin = nullptr;
    QSpinBox* m_ulSpin = nullptr;
};
