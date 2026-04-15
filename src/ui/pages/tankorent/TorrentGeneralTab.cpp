#include "TorrentGeneralTab.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "core/TorrentResult.h"  // humanSize()

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>

namespace {
const char* kLabelStyle = "color: #ccc; font-size: 12px;";
const char* kValueStyle = "color: #eee; font-size: 12px;";
const char* kMonoStyle  = "color: #eee; font-size: 12px; font-family: Consolas, monospace;";

QString formatSecsFromNow(const QDateTime& dt)
{
    if (!dt.isValid()) return QStringLiteral("—");
    const qint64 s = QDateTime::currentDateTime().secsTo(dt);
    if (s <= 0) return QStringLiteral("now");
    if (s < 60) return QStringLiteral("%1s").arg(s);
    if (s < 3600) return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
}
} // namespace

TorrentGeneralTab::TorrentGeneralTab(TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_client(client)
{
    buildUI();
}

void TorrentGeneralTab::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto makeValue = [](const QString& style) {
        auto* l = new QLabel;
        l->setStyleSheet(style);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        return l;
    };

    auto addRow = [this, form](const QString& labelText, QLabel* valueWidget) {
        auto* key = new QLabel(labelText);
        key->setStyleSheet(kLabelStyle);
        form->addRow(key, valueWidget);
    };

    m_name           = makeValue(kValueStyle);
    m_size           = makeValue(kValueStyle);
    m_pieces         = makeValue(kValueStyle);
    m_pieceSize      = makeValue(kValueStyle);
    m_created        = makeValue(kValueStyle);
    m_createdBy      = makeValue(kValueStyle);
    m_comment        = makeValue(kValueStyle);
    m_infoHashLabel  = makeValue(kMonoStyle);
    m_savePath       = makeValue(kValueStyle);
    m_currentTracker = makeValue(kValueStyle);
    m_availability   = makeValue(kValueStyle);
    m_shareRatio     = makeValue(kValueStyle);
    m_nextReannounce = makeValue(kValueStyle);

    addRow("Name",             m_name);
    addRow("Size",             m_size);
    addRow("Pieces",           m_pieces);
    addRow("Piece size",       m_pieceSize);
    addRow("Created",          m_created);
    addRow("Created by",       m_createdBy);
    addRow("Comment",          m_comment);
    addRow("Info hash",        m_infoHashLabel);
    addRow("Save path",        m_savePath);
    addRow("Current tracker",  m_currentTracker);
    addRow("Availability",     m_availability);
    addRow("Share ratio",      m_shareRatio);
    addRow("Next reannounce",  m_nextReannounce);

    root->addLayout(form);

    auto* openFolderBtn = new QPushButton("Open save folder");
    openFolderBtn->setCursor(Qt::PointingHandCursor);
    openFolderBtn->setFixedHeight(28);
    connect(openFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString path = m_savePath->text();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    root->addWidget(openFolderBtn, 0, Qt::AlignLeft);
    root->addStretch();
}

void TorrentGeneralTab::setInfoHash(const QString& infoHash)
{
    m_infoHash = infoHash;
    m_staticPopulated = false;
    refresh();
}

void TorrentGeneralTab::refresh()
{
    if (m_infoHash.isEmpty() || !m_client) return;
    const TorrentDetails d = m_client->engine()->torrentDetails(m_infoHash);

    if (!m_staticPopulated) {
        m_name->setText(d.name.isEmpty() ? QStringLiteral("—") : d.name);
        m_size->setText(d.totalSize > 0 ? humanSize(d.totalSize) : QStringLiteral("—"));
        m_pieces->setText(d.pieceCount > 0 ? QString::number(d.pieceCount) : QStringLiteral("—"));
        m_pieceSize->setText(d.pieceSize > 0 ? humanSize(d.pieceSize) : QStringLiteral("—"));
        m_created->setText(d.created.isValid()
            ? d.created.toString("yyyy-MM-dd HH:mm")
            : QStringLiteral("—"));
        m_createdBy->setText(d.createdBy.isEmpty() ? QStringLiteral("—") : d.createdBy);
        m_comment->setText(d.comment.isEmpty() ? QStringLiteral("—") : d.comment);
        m_infoHashLabel->setText(d.infoHash);
        m_savePath->setText(d.savePath);
        // Name/Hash/Size/Pieces/Creator/Comment are static — don't repopulate.
        if (!d.name.isEmpty() && d.pieceCount > 0)
            m_staticPopulated = true;
    }

    // Dynamic fields
    m_currentTracker->setText(d.currentTracker.isEmpty() ? QStringLiteral("—") : d.currentTracker);
    m_availability->setText(QString::number(d.availability, 'f', 3));
    m_shareRatio->setText(d.shareRatio > 0.f
        ? QString::number(d.shareRatio, 'f', 3)
        : QStringLiteral("0.000"));
    m_nextReannounce->setText(formatSecsFromNow(d.nextReannounce));
}
