#include "SeedingRulesDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

static const QString GLASS_BG = QStringLiteral("rgba(12, 12, 12, 0.95)");
static const QString BORDER   = QStringLiteral("rgba(255,255,255,0.08)");

SeedingRulesDialog::SeedingRulesDialog(const QString& title, float currentRatio, int currentTimeMins,
                                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumWidth(340);
    setStyleSheet(QStringLiteral(
        "SeedingRulesDialog { background: %1; border: 1px solid %2; border-radius: 10px; }"
    ).arg(GLASS_BG, BORDER));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* hint = new QLabel("0 = Disabled (no limit)");
    hint->setStyleSheet("color: #888; font-size: 11px;");
    root->addWidget(hint);

    auto* form = new QFormLayout;
    form->setSpacing(10);

    m_ratioSpin = new QDoubleSpinBox;
    m_ratioSpin->setRange(0.0, 100.0);
    m_ratioSpin->setSingleStep(0.1);
    m_ratioSpin->setDecimals(1);
    m_ratioSpin->setValue(static_cast<double>(currentRatio));
    m_ratioSpin->setFixedHeight(28);
    form->addRow("Ratio limit:", m_ratioSpin);

    m_timeSpin = new QSpinBox;
    m_timeSpin->setRange(0, 99999);
    m_timeSpin->setSuffix(" min");
    m_timeSpin->setValue(currentTimeMins);
    m_timeSpin->setFixedHeight(28);
    form->addRow("Seed time limit:", m_timeSpin);

    root->addLayout(form);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* okBtn = new QPushButton("OK");
    okBtn->setFixedHeight(28);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(okBtn);
    auto* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setFixedHeight(28);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);
}

float SeedingRulesDialog::ratioLimit() const { return static_cast<float>(m_ratioSpin->value()); }
int   SeedingRulesDialog::seedTimeSecs() const { return m_timeSpin->value() * 60; }
