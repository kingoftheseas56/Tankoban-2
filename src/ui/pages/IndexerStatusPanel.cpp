#include "IndexerStatusPanel.h"

#include "core/TorrentIndexer.h"
#include "core/indexers/NyaaIndexer.h"
#include "core/indexers/PirateBayIndexer.h"
#include "core/indexers/YtsIndexer.h"
#include "core/indexers/EztvIndexer.h"
#include "core/indexers/ExtTorrentsIndexer.h"
#include "core/indexers/TorrentsCsvIndexer.h"
#include "core/indexers/X1337xIndexer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <QDateTime>
#include <QHash>

// ── Local credentials sub-dialog ────────────────────────────────────────────

namespace {

class CredentialsDialog : public QDialog
{
public:
    CredentialsDialog(TorrentIndexer* indexer, QWidget* parent)
        : QDialog(parent), m_indexer(indexer)
    {
        setWindowTitle(QStringLiteral("Credentials — %1").arg(indexer->displayName()));
        setMinimumWidth(420);
        setStyleSheet(QStringLiteral(
            "CredentialsDialog { background: rgba(12,12,12,0.98); border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; }"
            "QLabel { color: #ccc; font-size: 12px; }"
            "QLineEdit { background: rgba(0,0,0,0.35); border: 1px solid rgba(255,255,255,0.1); "
                        "border-radius: 4px; padding: 4px 6px; color: #eee; }"
        ));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 14, 16, 14);
        root->setSpacing(10);

        auto* title = new QLabel(QStringLiteral("Credentials for %1").arg(indexer->displayName()));
        title->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: bold; color: #eee;"));
        root->addWidget(title);

        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);

        for (const QString& key : indexer->credentialKeys()) {
            auto* edit = new QLineEdit(indexer->credential(key));
            edit->setPlaceholderText(QStringLiteral("(unset — default used)"));
            m_edits.insert(key, edit);
            form->addRow(key, edit);
        }
        root->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setCursor(Qt::PointingHandCursor);
        buttons->button(QDialogButtonBox::Cancel)->setCursor(Qt::PointingHandCursor);
        root->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            for (auto it = m_edits.begin(); it != m_edits.end(); ++it)
                m_indexer->setCredential(it.key(), it.value()->text());
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private:
    TorrentIndexer*          m_indexer = nullptr;
    QHash<QString, QLineEdit*> m_edits;
};

} // namespace

// ── Panel ───────────────────────────────────────────────────────────────────

IndexerStatusPanel::IndexerStatusPanel(QNetworkAccessManager* nam, QWidget* parent)
    : QDialog(parent), m_nam(nam)
{
    setWindowTitle("Sources");
    setMinimumSize(880, 460);
    resize(1000, 520);
    setStyleSheet(QStringLiteral(
        "IndexerStatusPanel { background: rgba(12,12,12,0.98); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; }"
    ));

    // Sentinel indexers — one per id, lifetime tied to the dialog. Their
    // constructors run loadPersistedHealth() so state reflects QSettings
    // on open; refresh() reconstructs to pick up mid-session search writes.
    m_sentinels.append(new NyaaIndexer(nam, this));
    m_sentinels.append(new PirateBayIndexer(nam, this));
    m_sentinels.append(new YtsIndexer(nam, this));
    m_sentinels.append(new EztvIndexer(nam, this));
    m_sentinels.append(new ExtTorrentsIndexer(nam, this));
    m_sentinels.append(new TorrentsCsvIndexer(nam, this));
    m_sentinels.append(new X1337xIndexer(nam, this));

    buildUI();
    populateTable();
}

IndexerStatusPanel::~IndexerStatusPanel() = default;

void IndexerStatusPanel::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* title = new QLabel("Sources");
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #eee;"));
    root->addWidget(title);

    auto* hint = new QLabel(
        "Enable or disable indexers and manage credentials. "
        "Health state refreshes after each search.");
    hint->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_table = new QTableWidget(0, 7, this);
    m_table->setObjectName("IndexersTable");
    m_table->setHorizontalHeaderLabels(
        { "Name", "Health", "Last Success", "Last Error", "Response", "Enabled", "Credentials" });
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setFocusPolicy(Qt::NoFocus);

    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);       // Name
    hdr->setSectionResizeMode(1, QHeaderView::Interactive);   // Health
    hdr->setSectionResizeMode(2, QHeaderView::Interactive);   // Last Success
    hdr->setSectionResizeMode(3, QHeaderView::Stretch);       // Last Error
    hdr->setSectionResizeMode(4, QHeaderView::Interactive);   // Response
    hdr->setSectionResizeMode(5, QHeaderView::Interactive);   // Enabled
    hdr->setSectionResizeMode(6, QHeaderView::Interactive);   // Credentials
    hdr->resizeSection(1, 130);
    hdr->resizeSection(2, 110);
    hdr->resizeSection(4, 80);
    hdr->resizeSection(5, 70);
    hdr->resizeSection(6, 110);

    m_table->setStyleSheet(QStringLiteral(
        "#IndexersTable { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); "
            "border-radius: 6px; color: #eee; font-size: 12px; }"
        "#IndexersTable::item { padding: 4px 8px; }"
        "#IndexersTable QHeaderView::section { background: #1a1a1a; color: #888; border: none; "
            "border-right: 1px solid #222; border-bottom: 1px solid #222; "
            "padding: 4px 8px; font-size: 11px; }"
    ));

    root->addWidget(m_table, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();

    auto* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setFixedHeight(28);
    connect(refreshBtn, &QPushButton::clicked, this, &IndexerStatusPanel::refresh);
    btnRow->addWidget(refreshBtn);

    auto* closeBtn = new QPushButton("Close");
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedHeight(28);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);

    root->addLayout(btnRow);
}

void IndexerStatusPanel::refresh()
{
    for (TorrentIndexer* idx : m_sentinels)
        idx->reloadPersistedState();
    populateTable();
}

void IndexerStatusPanel::populateTable()
{
    m_table->setRowCount(m_sentinels.size());

    QSettings settings;
    for (int i = 0; i < m_sentinels.size(); ++i) {
        TorrentIndexer* idx = m_sentinels[i];
        const QString id = idx->id();

        // Col 0: Name
        m_table->setItem(i, 0, new QTableWidgetItem(idx->displayName()));

        // Col 1: Health (text only — grayscale rim via cell styling handled by QSS)
        m_table->setItem(i, 1, new QTableWidgetItem(healthLabel(idx->health())));

        // Col 2: Last Success (relative)
        m_table->setItem(i, 2, new QTableWidgetItem(formatRelativeTime(idx->lastSuccess())));

        // Col 3: Last Error
        QString err = idx->lastError();
        if (err.isEmpty()) err = QStringLiteral("—");
        auto* errItem = new QTableWidgetItem(err);
        errItem->setToolTip(err);
        m_table->setItem(i, 3, errItem);

        // Col 4: Response Time
        m_table->setItem(i, 4, new QTableWidgetItem(formatResponseMs(idx->lastResponseMs())));

        // Col 5: Enabled (checkbox in cell widget)
        auto* enableCell = new QWidget;
        auto* enableLay  = new QHBoxLayout(enableCell);
        enableLay->setContentsMargins(0, 0, 0, 0);
        enableLay->setAlignment(Qt::AlignCenter);
        auto* enableBox = new QCheckBox;
        const QString enabledKey = QStringLiteral("tankorent/indexers/%1/enabled").arg(id);
        enableBox->setChecked(settings.value(enabledKey, true).toBool());
        connect(enableBox, &QCheckBox::toggled, this, [this, id](bool on) {
            onEnabledToggled(id, on);
        });
        enableLay->addWidget(enableBox);
        m_table->setCellWidget(i, 5, enableCell);

        // Col 6: Credentials (Configure button if required, "—" otherwise)
        if (idx->requiresCredentials()) {
            auto* cfgCell = new QWidget;
            auto* cfgLay  = new QHBoxLayout(cfgCell);
            cfgLay->setContentsMargins(4, 0, 4, 0);
            auto* cfgBtn = new QPushButton("Configure");
            cfgBtn->setCursor(Qt::PointingHandCursor);
            cfgBtn->setFixedHeight(24);
            cfgBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.14); "
                    "color: #ddd; padding: 2px 8px; border-radius: 4px; font-size: 11px; }"
                "QPushButton:hover { background: rgba(255,255,255,0.12); }"
            ));
            connect(cfgBtn, &QPushButton::clicked, this, [this, idx]() {
                onConfigureCredentials(idx);
            });
            cfgLay->addWidget(cfgBtn);
            m_table->setCellWidget(i, 6, cfgCell);
        } else {
            auto* dashItem = new QTableWidgetItem(QStringLiteral("—"));
            dashItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 6, dashItem);
        }
    }
}

void IndexerStatusPanel::onConfigureCredentials(TorrentIndexer* indexer)
{
    CredentialsDialog dlg(indexer, this);
    if (dlg.exec() == QDialog::Accepted) {
        emit configurationChanged();
        populateTable();
    }
}

void IndexerStatusPanel::onEnabledToggled(const QString& indexerId, bool enabled)
{
    QSettings().setValue(
        QStringLiteral("tankorent/indexers/%1/enabled").arg(indexerId), enabled);
    emit configurationChanged();
}

QString IndexerStatusPanel::healthLabel(IndexerHealth h)
{
    switch (h) {
    case IndexerHealth::Unknown:           return QStringLiteral("Unknown");
    case IndexerHealth::Ok:                return QStringLiteral("Ok");
    case IndexerHealth::Disabled:          return QStringLiteral("Disabled");
    case IndexerHealth::AuthRequired:      return QStringLiteral("Auth required");
    case IndexerHealth::CloudflareBlocked: return QStringLiteral("Cloudflare");
    case IndexerHealth::RateLimited:       return QStringLiteral("Rate limited");
    case IndexerHealth::Unreachable:       return QStringLiteral("Unreachable");
    }
    return QStringLiteral("Unknown");
}

QString IndexerStatusPanel::formatRelativeTime(const QDateTime& dt)
{
    if (!dt.isValid())
        return QStringLiteral("never");

    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)   return QStringLiteral("just now");
    if (secs < 3600) return QStringLiteral("%1 min ago").arg(secs / 60);
    if (secs < 86400) return QStringLiteral("%1 h ago").arg(secs / 3600);
    return QStringLiteral("%1 d ago").arg(secs / 86400);
}

QString IndexerStatusPanel::formatResponseMs(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("—");
    if (ms < 1000) return QStringLiteral("%1 ms").arg(ms);
    return QStringLiteral("%1.%2 s").arg(ms / 1000).arg((ms % 1000) / 100);
}
