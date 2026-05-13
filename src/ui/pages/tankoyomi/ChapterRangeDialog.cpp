#include "ChapterRangeDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <algorithm>
#include <climits>

ChapterRangeDialog::ChapterRangeDialog(const QList<ChapterInfo>& allChapters,
                                        const QSet<QString>& alreadyHandledIds,
                                        QWidget* parent)
    : QDialog(parent), m_all(allChapters), m_handled(alreadyHandledIds)
{
    setObjectName("ChapterRangeDialog");
    setWindowTitle(tr("Download range of chapters"));
    setMinimumWidth(360);

    int lo = INT_MAX, hi = INT_MIN;
    for (const auto& ch : m_all) {
        const int n = int(ch.chapterNumber);
        lo = qMin(lo, n);
        hi = qMax(hi, n);
    }
    if (m_all.isEmpty()) { lo = 1; hi = 1; }

    auto* form = new QFormLayout();
    m_fromSpin = new QSpinBox(this);
    m_fromSpin->setObjectName("ChapterRangeFromSpin");
    m_fromSpin->setRange(lo, hi);
    m_fromSpin->setValue(lo);
    m_toSpin = new QSpinBox(this);
    m_toSpin->setObjectName("ChapterRangeToSpin");
    m_toSpin->setRange(lo, hi);
    m_toSpin->setValue(hi);
    form->addRow(tr("From chapter:"), m_fromSpin);
    form->addRow(tr("To chapter:"),   m_toSpin);

    m_previewText = new QLabel(this);
    m_previewText->setObjectName("ChapterRangePreview");
    m_previewText->setWordWrap(true);

    auto* btnRow = new QHBoxLayout();
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName("ChapterRangeCancelBtn");
    m_dlBtn = new QPushButton(tr("Download"), this);
    m_dlBtn->setObjectName("ChapterRangeDownloadBtn");
    m_dlBtn->setDefault(true);
    btnRow->addStretch();
    btnRow->addWidget(cancel);
    btnRow->addWidget(m_dlBtn);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_previewText);
    root->addLayout(btnRow);

    connect(m_fromSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ChapterRangeDialog::updatePreview);
    connect(m_toSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ChapterRangeDialog::updatePreview);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_dlBtn, &QPushButton::clicked, this, &QDialog::accept);

    updatePreview();
}

void ChapterRangeDialog::updatePreview()
{
    const QList<ChapterInfo> picks = selectedChapters();
    if (picks.isEmpty()) {
        m_previewText->setText(
            tr("No chapters in this range — adjust the From/To values."));
        m_dlBtn->setEnabled(false);
        return;
    }
    int skipped = 0;
    const int from = m_fromSpin->value();
    const int to   = m_toSpin->value();
    for (const auto& ch : m_all) {
        const int n = int(ch.chapterNumber);
        if (n < qMin(from, to) || n > qMax(from, to)) continue;
        if (m_handled.contains(ch.id)) ++skipped;
    }
    QString text = tr("%1 chapters will be downloaded").arg(picks.size());
    if (skipped > 0) text += tr(" (%1 already downloaded, skipped)").arg(skipped);
    m_previewText->setText(text);
    m_dlBtn->setEnabled(true);
}

QList<ChapterInfo> ChapterRangeDialog::selectedChapters() const
{
    const int from = qMin(m_fromSpin->value(), m_toSpin->value());
    const int to   = qMax(m_fromSpin->value(), m_toSpin->value());
    QList<ChapterInfo> out;
    for (const auto& ch : m_all) {
        const int n = int(ch.chapterNumber);
        if (n < from || n > to) continue;
        if (m_handled.contains(ch.id)) continue;
        out.append(ch);
    }
    return out;
}
