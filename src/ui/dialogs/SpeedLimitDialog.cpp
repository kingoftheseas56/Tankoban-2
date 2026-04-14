#include "SpeedLimitDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

static const QString GLASS_BG = QStringLiteral("rgba(12, 12, 12, 0.95)");
static const QString BORDER   = QStringLiteral("rgba(255,255,255,0.08)");

SpeedLimitDialog::SpeedLimitDialog(const QString& title, int currentDlKBs, int currentUlKBs,
                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumWidth(340);
    setStyleSheet(QStringLiteral(
        "SpeedLimitDialog { background: %1; border: 1px solid %2; border-radius: 10px; }"
    ).arg(GLASS_BG, BORDER));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* hint = new QLabel("0 = Unlimited");
    hint->setStyleSheet("color: #888; font-size: 11px;");
    root->addWidget(hint);

    auto* form = new QFormLayout;
    form->setSpacing(10);

    m_dlSpin = new QSpinBox;
    m_dlSpin->setRange(0, 999999);
    m_dlSpin->setSuffix(" KB/s");
    m_dlSpin->setValue(currentDlKBs);
    m_dlSpin->setFixedHeight(28);
    form->addRow("Download limit:", m_dlSpin);

    m_ulSpin = new QSpinBox;
    m_ulSpin->setRange(0, 999999);
    m_ulSpin->setSuffix(" KB/s");
    m_ulSpin->setValue(currentUlKBs);
    m_ulSpin->setFixedHeight(28);
    form->addRow("Upload limit:", m_ulSpin);

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

int SpeedLimitDialog::dlLimitBps() const { return m_dlSpin->value() * 1024; }
int SpeedLimitDialog::ulLimitBps() const { return m_ulSpin->value() * 1024; }
