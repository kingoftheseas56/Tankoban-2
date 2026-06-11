#pragma once

// DOWNLOADS_OVERHAUL_V2 Task 4 (2026-06-11) — Master-Detail shell rebuild.
// The page is now driven by tankostream::stream::buildDownloadRows so the
// Active / History split is replaced by a single four-section (Failed /
// Active / Queued / Completed) grouped tree with a detail pane stub on the
// right. Public API (class name, constructor signature, injection setters,
// signals) is identical to the pre-T4 page so MainWindow wiring is unchanged.

#include "core/stream/DownloadsCommandModel.h"

#include <QFrame>
#include <QHash>
#include <QPixmap>
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

namespace tankostream::stream { class MetaAggregator; }
namespace tankostream::addon  { struct MetaItem; }

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

    // Poster / meta enrichment (kept from pre-T4 — rewired to serve the tree)
    void savePosterFrom(const QString& imdbId, const QUrl& posterUrl);
    static QString posterCachePath(const QString& imdbId);

    // Injection state
    TorrentClient*                       m_client  = nullptr;
    StreamDownloadIndex*                 m_index   = nullptr;
    tankostream::stream::MetaAggregator* m_meta    = nullptr;
    QNetworkAccessManager*               m_posterNam = nullptr;

    // Enrichment caches (title + poster) — keyed by imdbId
    QHash<QString, QString>  m_titleCache;
    QHash<QString, QPixmap>  m_posterCache;

    // Debounce timer — all signal triggers funnel through here
    QTimer* m_rebuildDebounce = nullptr;

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

    // Detail pane stub (Task 5 replaces)
    QLabel* m_detailPlaceholder = nullptr;

    // Splitter
    QSplitter* m_splitter = nullptr;

    // Selection state
    std::optional<tankostream::stream::DownloadRow> m_selectedRow;
};
