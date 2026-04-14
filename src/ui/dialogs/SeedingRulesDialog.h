#pragma once

#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>

class SeedingRulesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SeedingRulesDialog(const QString& title, float currentRatio, int currentTimeMins,
                                QWidget* parent = nullptr);

    float ratioLimit() const;
    int   seedTimeSecs() const;  // minutes * 60

private:
    QDoubleSpinBox* m_ratioSpin = nullptr;
    QSpinBox*       m_timeSpin  = nullptr;
};
