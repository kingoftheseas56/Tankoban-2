#pragma once
// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — pack-first Season Checkout (spec §3.2).
// Modal dialog. The caller (StreamPage, T9) feeds pack candidates as the search
// resolves; the panel renders the plan (pack coverage + per-episode gap rows +
// owned-greyed) and emits ONE CheckoutPlan on "Queue all". Nothing downloads
// until that click.
#include "core/stream/UnifiedPackSearchEngine.h"
#include <QDialog>
#include <QList>
#include <QSet>

class QLabel;
class QListWidget;
class QPushButton;

namespace tankostream::stream {

struct CheckoutPlan {
    bool       usePack    = false;
    QString    packMagnet;
    QString    packTitle;
    QList<int> gapEpisodes;   // per-episode auto-pick downloads (T9 dispatches)
};

class SeasonCheckoutPanel : public QDialog {
    Q_OBJECT
public:
    // episodes: all episode numbers of the season; owned: already downloaded
    // (greyed + excluded); preselected: non-empty = Download Selected subset.
    SeasonCheckoutPanel(const QString& imdbId, const QString& showTitle, int season,
                        const QList<int>& episodes, const QSet<int>& owned,
                        const QList<int>& preselected, QWidget* parent = nullptr);

    void setPackCandidates(const QList<tankoban::stream::theatre::EnrichedPack>& packs);
    void setSearchFailed(const QString& message);   // degrade to all-gap mode

signals:
    // Emitted BEFORE accept(); the receiver must not delete the dialog synchronously
    // — use deleteLater() or WA_DeleteOnClose instead.
    void queueAllRequested(const tankostream::stream::CheckoutPlan& plan);

private:
    void rebuildPlanRows();
    void updateFooter();
    bool packCoversSeason(const tankoban::stream::theatre::EnrichedPack& p) const;
    // Returns the selected pack iff selRow ∈ [0, m_packs.size()) AND packCoversSeason;
    // returns nullptr otherwise (no-pack sentinel or non-covering selection).
    const tankoban::stream::theatre::EnrichedPack* selectedCoveringPack() const;

    QString  m_imdbId;
    QString  m_showTitle;
    int      m_season      = 0;
    QList<int> m_wanted;    // episodes - owned, sorted; respects preselected subset
    QSet<int>  m_owned;

    QList<tankoban::stream::theatre::EnrichedPack> m_packs;  // filtered + sorted candidates
    bool     m_searchSettled = false;

    // UI widgets
    QLabel*       m_headerLabel   = nullptr;
    QLabel*       m_packSectionLabel = nullptr;
    QListWidget*  m_packList      = nullptr;
    QLabel*       m_planSectionLabel = nullptr;
    QListWidget*  m_planList      = nullptr;
    QLabel*       m_footerLabel   = nullptr;
    QPushButton*  m_queueBtn      = nullptr;
};

}  // namespace tankostream::stream
