// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — SeasonCheckoutPanel implementation.
// Pack-first Season Checkout dialog (spec §3.2). T9 wires this into StreamPage.

#include "SeasonCheckoutPanel.h"
#include "core/TorrentResult.h"  // for humanSize()

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QDialogButtonBox>
#include <algorithm>

// ── Styling ──────────────────────────────────────────────────────────────────

static const QString BG_DIALOG   = QStringLiteral("rgba(12, 12, 12, 0.97)");
static const QString BORDER_SOFT = QStringLiteral("rgba(255,255,255,0.08)");
static const QString CLR_DIM     = QStringLiteral("#666");
static const QString CLR_MUTED   = QStringLiteral("#999");
static const QString CLR_NORMAL  = QStringLiteral("#eee");
static const QString CLR_SECTION = QStringLiteral("#aaa");

static const QColor  QCLR_OWNED(90, 90, 90);      // greyed-out rows
static const QColor  QCLR_COVERED(200, 200, 200);  // covered-by-pack rows
static const QColor  QCLR_GAP(238, 238, 238);      // gap rows

static QString sectionLabelStyle()
{
    return QStringLiteral(
        "QLabel { color: %1; font-size: 11px; text-transform: uppercase; "
        "letter-spacing: 1px; padding-top: 6px; }").arg(CLR_SECTION);
}

static QString listStyle()
{
    return QStringLiteral(
        "QListWidget { background: rgba(255,255,255,0.04); border: 1px solid %1; "
        "border-radius: 6px; color: %2; font-size: 13px; outline: none; }"
        "QListWidget::item { padding: 6px 10px; }"
        "QListWidget::item:selected { background: rgba(255,255,255,0.10); }"
    ).arg(BORDER_SOFT, CLR_NORMAL);
}

static QString footerLabelStyle()
{
    return QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(CLR_MUTED);
}

static QString queueBtnStyle()
{
    return QStringLiteral(
        "QPushButton { background: rgba(255,255,255,0.10); border: 1px solid rgba(255,255,255,0.18); "
        "border-radius: 6px; color: #eee; font-size: 13px; padding: 7px 20px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.16); }"
        "QPushButton:disabled { color: #555; border-color: rgba(255,255,255,0.06); }");
}

namespace tankostream::stream {

// ── Constructor ───────────────────────────────────────────────────────────────

SeasonCheckoutPanel::SeasonCheckoutPanel(const QString& imdbId,
                                         const QString& showTitle,
                                         int season,
                                         const QList<int>& episodes,
                                         const QSet<int>& owned,
                                         const QList<int>& preselected,
                                         QWidget* parent)
    : QDialog(parent)
    , m_imdbId(imdbId)
    , m_showTitle(showTitle)
    , m_season(season)
    , m_owned(owned)
{
    // Build m_wanted: preselected subset (or all episodes) minus owned, sorted.
    const QList<int>& source = preselected.isEmpty() ? episodes : preselected;
    for (int ep : source) {
        if (!owned.contains(ep))
            m_wanted.append(ep);
    }
    std::sort(m_wanted.begin(), m_wanted.end());

    // Window title
    const bool isSubset = !preselected.isEmpty();
    const QString titlePrefix = isSubset
        ? QStringLiteral("Download Selected")
        : QStringLiteral("Download Season %1").arg(season);
    setWindowTitle(QStringLiteral("%1 — %2").arg(titlePrefix, showTitle));

    setModal(true);
    setMinimumSize(560, 640);
    resize(560, 640);
    setStyleSheet(QStringLiteral(
        "SeasonCheckoutPanel { background: %1; border: 1px solid %2; border-radius: 10px; }"
    ).arg(BG_DIALOG, BORDER_SOFT));

    // ── Root layout ──────────────────────────────────────────────────────────
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(8);

    // Header label
    m_headerLabel = new QLabel(windowTitle(), this);
    m_headerLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #eee; font-size: 15px; font-weight: bold; }"));
    root->addWidget(m_headerLabel);

    // Pack section
    m_packSectionLabel = new QLabel(QStringLiteral("Season packs"), this);
    m_packSectionLabel->setStyleSheet(sectionLabelStyle());
    root->addWidget(m_packSectionLabel);

    m_packList = new QListWidget(this);
    m_packList->setStyleSheet(listStyle());
    m_packList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packList->setMinimumHeight(140);
    m_packList->setMaximumHeight(220);
    root->addWidget(m_packList);

    // Searching placeholder
    auto* placeholder = new QListWidgetItem(QStringLiteral("Searching packs..."), m_packList);
    placeholder->setFlags(Qt::NoItemFlags);
    placeholder->setForeground(QColor(CLR_DIM));

    // Plan section
    m_planSectionLabel = new QLabel(QStringLiteral("Plan"), this);
    m_planSectionLabel->setStyleSheet(sectionLabelStyle());
    root->addWidget(m_planSectionLabel);

    m_planList = new QListWidget(this);
    m_planList->setStyleSheet(listStyle());
    m_planList->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(m_planList, 1);  // stretch

    // Initial plan rows (all shown as gaps before packs arrive)
    rebuildPlanRows();

    // Footer + Queue all
    auto* footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 4, 0, 0);

    m_footerLabel = new QLabel(this);
    m_footerLabel->setStyleSheet(footerLabelStyle());
    footerRow->addWidget(m_footerLabel, 1);

    m_queueBtn = new QPushButton(QStringLiteral("Queue all"), this);
    m_queueBtn->setStyleSheet(queueBtnStyle());
    m_queueBtn->setEnabled(false);  // disabled until search settles
    footerRow->addWidget(m_queueBtn);

    root->addLayout(footerRow);

    updateFooter();

    // Wire pack selection → plan rebuild
    connect(m_packList, &QListWidget::currentRowChanged,
            this, [this](int) { rebuildPlanRows(); updateFooter(); });

    // Wire Queue all
    connect(m_queueBtn, &QPushButton::clicked, this, [this]() {
        const tankoban::stream::theatre::EnrichedPack* pack = selectedCoveringPack();

        CheckoutPlan plan;
        plan.usePack = (pack != nullptr);

        if (pack) {
            plan.packMagnet = pack->raw.magnetUri;
            plan.packTitle  = pack->raw.title;
            // A covering pack covers all wanted episodes by definition; gapEpisodes left empty.
        } else {
            // No covering pack: all wanted episodes need individual sources.
            plan.gapEpisodes = m_wanted;
        }

        emit queueAllRequested(plan);
        accept();
    });
}

// ── setPackCandidates ─────────────────────────────────────────────────────────

void SeasonCheckoutPanel::setPackCandidates(
    const QList<tankoban::stream::theatre::EnrichedPack>& packs)
{
    m_searchSettled = true;

    // Sanity filter: must have a non-empty magnet URI.
    m_packs.clear();
    for (const auto& p : packs) {
        if (!p.raw.magnetUri.trimmed().isEmpty())
            m_packs.append(p);
    }

    // Sort best-first by combinedScore descending (stable: equal-score order preserved).
    std::stable_sort(m_packs.begin(), m_packs.end(),
                     [](const tankoban::stream::theatre::EnrichedPack& a,
                        const tankoban::stream::theatre::EnrichedPack& b) {
                         return a.combinedScore > b.combinedScore;
                     });

    // Truncate to 5 so row index == pack index always (sentinel sits at row m_packs.size()).
    if (m_packs.size() > 5)
        m_packs = m_packs.mid(0, 5);

    m_packList->clear();

    // Render candidates (at most 5 after truncation above).
    const int limit = m_packs.size();
    for (int i = 0; i < limit; ++i) {
        const auto& p = m_packs.at(i);
        const bool covers = packCoversSeason(p);

        const QString line1 = QStringLiteral("%1  *  %2  ^%3")
            .arg(p.raw.title,
                 humanSize(p.raw.sizeBytes),
                 QString::number(p.raw.seeders));
        const QString line2 = covers
            ? QStringLiteral("  covers this season")
            : QStringLiteral("  partial/unknown coverage");
        const QString display = line1 + QStringLiteral("\n") + line2;

        auto* item = new QListWidgetItem(display, m_packList);
        item->setForeground(covers ? QCLR_GAP : QColor(CLR_MUTED));
    }

    // "No pack" sentinel row — sits at row index m_packs.size() (always valid after truncation).
    auto* noPack = new QListWidgetItem(QStringLiteral("No pack -- per-episode only"), m_packList);
    noPack->setForeground(QColor(CLR_MUTED));

    // Default selection: first COVERING pack, or the no-pack sentinel.
    // Deviation from plan's "first candidate": preselecting a non-covering pack would render
    // a confusing all-gaps plan — best COVERING pack or no-pack sentinel.
    int defaultRow = m_packs.size();  // no-pack sentinel index
    for (int i = 0; i < m_packs.size(); ++i) {
        if (packCoversSeason(m_packs.at(i))) {
            defaultRow = i;
            break;
        }
    }
    m_packList->setCurrentRow(defaultRow);

    m_queueBtn->setEnabled(true);
    rebuildPlanRows();
    updateFooter();
}

// ── setSearchFailed ───────────────────────────────────────────────────────────

void SeasonCheckoutPanel::setSearchFailed(const QString& message)
{
    m_searchSettled = true;
    m_packs.clear();

    m_packList->clear();
    auto* item = new QListWidgetItem(message.isEmpty()
        ? QStringLiteral("Pack search unavailable")
        : message, m_packList);
    item->setFlags(Qt::NoItemFlags);
    item->setForeground(QColor(CLR_DIM));

    // Degrade gracefully: plan = all gaps.
    m_queueBtn->setEnabled(true);
    rebuildPlanRows();
    updateFooter();
}

// ── packCoversSeason ──────────────────────────────────────────────────────────

bool SeasonCheckoutPanel::packCoversSeason(
    const tankoban::stream::theatre::EnrichedPack& p) const
{
    return p.classification.isCompleteSeries
           || p.classification.detectedSeasons.contains(m_season);
}

// ── selectedCoveringPack ──────────────────────────────────────────────────────

const tankoban::stream::theatre::EnrichedPack*
SeasonCheckoutPanel::selectedCoveringPack() const
{
    const int selRow = m_packList->currentRow();
    if (selRow < 0 || selRow >= m_packs.size())
        return nullptr;
    const auto& p = m_packs.at(selRow);
    return packCoversSeason(p) ? &p : nullptr;
}

// ── rebuildPlanRows ───────────────────────────────────────────────────────────

void SeasonCheckoutPanel::rebuildPlanRows()
{
    m_planList->clear();

    // Determine if a covering pack is currently selected.
    const bool hasCoveringPack = (selectedCoveringPack() != nullptr);

    // Wanted episodes (not owned).
    for (int ep : m_wanted) {
        QString text;
        QColor  color;
        if (hasCoveringPack) {
            text  = QStringLiteral("E%1  --  covered by pack").arg(ep, 2, 10, QChar('0'));
            color = QCLR_COVERED;
        } else {
            text  = QStringLiteral("E%1  --  best single source (auto-picked at queue time)")
                        .arg(ep, 2, 10, QChar('0'));
            color = QCLR_GAP;
        }
        auto* item = new QListWidgetItem(text, m_planList);
        item->setFlags(Qt::NoItemFlags);  // display-only
        item->setForeground(color);
        // per-row source override deferred — spec sanctions auto-pick for gaps
    }

    // Owned episodes — greyed, display-only, excluded from plan.
    QList<int> ownedSorted = m_owned.values();
    std::sort(ownedSorted.begin(), ownedSorted.end());
    for (int ep : ownedSorted) {
        const QString text = QStringLiteral("E%1  --  already have")
            .arg(ep, 2, 10, QChar('0'));
        auto* item = new QListWidgetItem(text, m_planList);
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QCLR_OWNED);
    }
}

// ── updateFooter ──────────────────────────────────────────────────────────────

void SeasonCheckoutPanel::updateFooter()
{
    if (m_wanted.isEmpty()) {
        m_footerLabel->setText(
            QStringLiteral("Nothing to download -- all episodes already in library"));
        m_queueBtn->setEnabled(false);
        return;
    }

    // Determine if a covering pack is selected.
    const tankoban::stream::theatre::EnrichedPack* coveringPack = selectedCoveringPack();
    const bool hasCoveringPack = (coveringPack != nullptr);
    const qint64 packSizeBytes = hasCoveringPack ? coveringPack->raw.sizeBytes : 0;

    const int totalEps  = m_wanted.size();
    const int gapCount  = hasCoveringPack ? 0 : totalEps;

    QString footerText = QStringLiteral("%1 episode%2")
        .arg(totalEps)
        .arg(totalEps == 1 ? QString{} : QStringLiteral("s"));

    if (hasCoveringPack) {
        footerText += QStringLiteral("  *  ~%1").arg(humanSize(packSizeBytes));
    } else {
        footerText += QStringLiteral("  *  ~%1 singles").arg(gapCount);
    }

    m_footerLabel->setText(footerText);

    // Queue all is enabled once search has settled (or degraded).
    if (m_searchSettled)
        m_queueBtn->setEnabled(true);
}

}  // namespace tankostream::stream
