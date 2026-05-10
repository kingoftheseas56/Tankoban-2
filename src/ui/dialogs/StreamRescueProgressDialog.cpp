// STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 6 — see header + spec §9.6.

#include "StreamRescueProgressDialog.h"

#include "core/stream/StreamRescueScanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

StreamRescueProgressDialog::StreamRescueProgressDialog(StreamRescueScanner* scanner,
                                                       QWidget* parent)
    : QDialog(parent), m_scanner(scanner)
{
    setObjectName(QStringLiteral("StreamRescueProgressDialog"));
    setWindowTitle(tr("Migrating downloaded shows to Stream library"));
    setModal(true);
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);

    auto* heading = new QLabel(tr("Scanning your Videos folders for previously "
                                  "downloaded shows..."), this);
    heading->setWordWrap(true);
    root->addWidget(heading);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    root->addWidget(m_bar);

    m_status = new QLabel(tr("Starting..."), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    btnRow->addWidget(m_cancelBtn);
    root->addLayout(btnRow);

    if (m_scanner) {
        connect(m_scanner, &StreamRescueScanner::progressUpdate,
                this, &StreamRescueProgressDialog::onProgress);
        connect(m_scanner, &StreamRescueScanner::complete, this,
                [this](const StreamRescueScanner::Stats& s) {
                    onComplete(s.showsMatched, s.showsUnmatched,
                               s.showsAmbiguous, s.episodesRegistered);
                });
        connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
            if (m_scanner) m_scanner->cancel();
            close();
        });
    }
}

void StreamRescueProgressDialog::onProgress(int current, int total,
                                            const QString& showName)
{
    if (m_bar && total > 0) {
        m_bar->setRange(0, total);
        m_bar->setValue(current);
    }
    if (m_status) {
        m_status->setText(tr("Processing %1 (%2 of %3)")
                              .arg(showName).arg(current).arg(total));
    }
}

void StreamRescueProgressDialog::onComplete(int matched, int unmatched,
                                            int ambiguous, int episodes)
{
    if (m_status) {
        m_status->setText(tr("Done. Added %1 shows and %2 episodes to Stream library. "
                             "%3 shows could not be matched to Cinemeta and remain in Videos.")
                          .arg(matched).arg(episodes).arg(unmatched + ambiguous));
    }
    if (m_cancelBtn)
        m_cancelBtn->setText(tr("Close"));
    if (m_bar)
        m_bar->setValue(m_bar->maximum());
    if (m_cancelBtn) {
        disconnect(m_cancelBtn, nullptr, nullptr, nullptr);
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::accept);
    }
}
