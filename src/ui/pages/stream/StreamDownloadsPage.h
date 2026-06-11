#pragma once

// DOWNLOADS_OVERHAUL_V2 Task 4 (2026-06-11) — Master-Detail shell rebuild.
// Task 5 (2026-06-11) — m_detailPlaceholder replaced by DownloadDetailPane.
// Task 7 (2026-06-11) — Top strip wired: live totals, Pause All / Resume All /
//   Clear Done, max-active knob.
// The page is now driven by tankostream::stream::buildDownloadRows so the
// Active / History split is replaced by a single four-section (Failed /
// Active / Queued / Completed) grouped tree with a real detail pane on the
// right. Public API (class name, constructor signature, injection setters,
// signals) is identical to the pre-T4 page so MainWindow wiring is unchanged.

#include "core/stream/DownloadsCommandModel.h"

#include <QFrame>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QString>
#include <optional>

class QComboBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QNetworkAccessManager;
class QUrl;
class TorrentClient;
class StreamDownloadIndex;
class DownloadDetailPane;

namespace tankostream::stream { class MetaAggregator; }
namespace tankostream::addon  { struct MetaItem; }
namespace tankoban::queue     { class TransferQueue; }

class StreamDownloadsPage : public QFrame
{
    Q_OBJECT
public:
    explicit StreamDownloadsPage(QWidget* parent = nullptr);
    ~StreamDownloadsPage() override = default;

    // Injection points — same-pointer guard + disconnect pattern mirrors
    // StreamLibraryLayout / pre-T4 version. MainWindow wiring unchanged.
    void setTorrentClient(TorrentClient* client);
    void setStreamDownloadIndex(StreamDownloadIndex* index);
    void setMetaAggregator(tankostream::stream::MetaAggregator* agg);

signals:
    void backRequested();
    void playLocalFileRequested(const QString& canonicalPath,
                                const QString& imdbId,
                                const QString& showTitle,
                                int season,
                                int episode);
    // DOWNLOADS_OVERHAUL_V2 T6 — emitted by the retry intent handler after
    // cleaning up the failed transfer. StreamPage re-runs the auto source pick.
    void retryEpisodeRequested(const QString& imdbId, int season, int episode);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void rebuild();
    void onMetaItemReady(const tankostream::addon::MetaItem& item);

private:
    // Build constants
    static constexpr qint64 kCompletedTrimMs = 30LL * 24 * 60 * 60 * 1000;

    void buildUi();
    void updateTotals();

    // Tree helpers
    QString currentSelectionKey() const;
    void    restoreSelection(const QString& key);

    // Row helpers
    QString displayShowTitle(const QString& imdbId) const;
    QString statusText(const tankostream::stream::DownloadRow& r) const;

    // DOWNLOADS_OVERHAUL_V2 T6 — re-resolve the row's CURRENT state by key
    // before acting on an intent — the pane's snapshot can be a debounce stale
    // (T5 review I4). Returns nullopt when the episode no longer exists.
    std::optional<tankostream::stream::DownloadRow>
    freshRowFor(const tankostream::stream::DownloadRow& stale) const;

    // Poster / meta enrichment (kept from pre-T4 — rewired to serve the tree)
    void savePosterFrom(const QString& imdbId, const QUrl& posterUrl);
    static QString posterCachePath(const QString& imdbId);

    // Injection state
    TorrentClient*                       m_client  = nullptr;
    StreamDownloadIndex*                 m_index   = nullptr;
    tankostream::stream::MetaAggregator* m_meta    = nullptr;
    QNetworkAccessManager*               m_posterNam = nullptr;
    // Tracks the currently connected TransferQueue so we can disconnect it on
    // client re-set (the queue is a separate QObject from TorrentClient).
    QPointer<tankoban::queue::TransferQueue> m_connectedQueue;

    // Enrichment caches (title + poster) — keyed by imdbId
    QHash<QString, QString>  m_titleCache;
    QHash<QString, QPixmap>  m_posterCache;
    // Guards against per-rebuild refetch of dead ids: negative results are not
    // cached by MetaAggregator, so without this set every rebuild would re-fire
    // fetchMetaItem for any id that never resolves.
    QSet<QString>            m_metaRequested;

    // Debounce timer — all signal triggers funnel through here
    QTimer* m_rebuildDebounce = nullptr;

    // Top-strip live-speed timer — runs only while page is visible (1 Hz).
    // Kept separate from the rebuild debounce so speed updates don't cause full
    // tree rebuilds; updateTotals() is cheap (no tree churn).
    QTimer* m_totalsTimer = nullptr;

    // "Clear Done" hidden-row keys — loaded from QSettings ("downloads/
    // clearedDoneKeys", stored as QStringList). Key shape:
    // "imdbId|season|episode|addedAt" (addedAt disambiguates re-downloads of
    // the same episode). A click captures the keys of rows that are Completed
    // RIGHT NOW; rows still in flight at click time keep their key out of the
    // set and stay visible when they finish (T7 review I1 — the old addedAt
    // watermark hid in-flight completions). Display-only: the
    // StreamDownloadIndex is untouched.
    QSet<QString> m_clearedDoneKeys;

    // Topbar
    QPushButton* m_backBtn    = nullptr;
    QLabel*      m_titleLabel = nullptr;

    // Top strip (Task 7 wires these; disabled for now)
    QLabel*    m_totalsLabel   = nullptr;
    QPushButton* m_pauseAllBtn  = nullptr;
    QPushButton* m_resumeAllBtn = nullptr;
    QPushButton* m_clearDoneBtn = nullptr;
    QComboBox*   m_maxActiveCombo = nullptr;

    // Master tree
    QTreeWidget* m_tree = nullptr;

    // Detail pane (Task 5 — replaces stub)
    DownloadDetailPane* m_detailPane = nullptr;

    // Splitter
    QSplitter* m_splitter = nullptr;

    // Selection state
    std::optional<tankostream::stream::DownloadRow> m_selectedRow;
};
