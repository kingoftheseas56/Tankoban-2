#include "StreamPage.h"

#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/BulkPackVerifier.h"
#include "core/stream/BulkSourceCollector.h"
#include "core/stream/StreamBulkPlan.h"
#include "core/stream/UnifiedPackSearchEngine.h"
#include "core/stream/addon/AddonRegistry.h"
#include "ui/pages/stream/AddonManagerScreen.h"
// THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) — StreamServerEngine +
// StreamPlayerController are no longer constructed or referenced by
// StreamPage; Theatre is download-only. Includes removed (files still on
// disk pending Phase 2 deletion).
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/AutoSourcePicker.h"
#include "core/stream/StreamLibrary.h"
#include "core/torrent/TorrentEngine.h"
#include "stream/StreamLibraryLayout.h"
#include "stream/StreamSearchWidget.h"
#include "stream/StreamDetailView.h"
#include "stream/StreamSourceChoice.h"
#include "stream/StreamSourceList.h"
#include "core/stream/StreamAggregator.h"
#include "core/stream/SubtitlesAggregator.h"
#include "stream/StreamContinueStrip.h"
#include "stream/StreamHomeBoard.h"
#include "stream/CatalogBrowseScreen.h"
#include "stream/TheatreDownloadPanel.h"
#include "core/stream/StreamProgress.h"
#include "core/torrent/TorrentClient.h"

#include <QNetworkAccessManager>
#include "ui/pages/TileStrip.h"
#include "ui/pages/TileCard.h"

#include "ui/player/VideoPlayer.h"
// THEATRE_STREAMING_RESTORE P1 (2026-06-09) — Stremio stream-server engine +
// restored player controller. Episode "watch" click streams the auto-picked
// source; explicit download actions stay on libtorrent.
#include "core/stream/stremio/StreamServerEngine.h"
#include "ui/pages/stream/StreamPlayerController.h"
#include "ui/player/IPlayerBackend.h"
#include "ui/dialogs/AddAddonDialog.h"
#include "ui/dialogs/AddTorrentDialog.h"
#include "core/stream/addon/StreamSource.h"

#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEasingCurve>
#include <QFrame>
#include <QEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QInputDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QParallelAnimationGroup>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStackedLayout>
#include <QUrl>
#include <QDateTime>
#include <QDesktopServices>
#include <QThread>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>
#include <QVariant>

#include <functional>
#include <memory>

namespace {

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- construct a LayerEntry for
// the Stream page. All emit sites call makeStreamLayer so the pageId is
// always "stream" and the struct fields are assembled consistently.
// Field names are consumed by restoreLayer.
QString streamLayerLabelFor(const tankoban::ui::LayerEntry& e)
{
    if (e.kind == QLatin1String("browse"))        return QStringLiteral("Theatre Home");
    if (e.kind == QLatin1String("catalogBrowse")) return e.stateBlob.value(QStringLiteral("catalogTitle")).toString();
    if (e.kind == QLatin1String("detail"))        return e.stateBlob.value(QStringLiteral("detailImdbId")).toString();
    if (e.kind == QLatin1String("addonManager"))  return QStringLiteral("Addons");
    if (e.kind == QLatin1String("search"))        return QStringLiteral("Search");
    return QStringLiteral("Theatre");
}

tankoban::ui::LayerEntry makeStreamLayer(const QString& kind, const QJsonObject& blob = {})
{
    tankoban::ui::LayerEntry e{QStringLiteral("stream"), kind, QString(), blob};
    e.label = streamLayerLabelFor(e);
    return e;
}

int episodeNumberFromBulkItemKey(const QString& itemKey)
{
    static const QRegularExpression pattern(QStringLiteral("[Ss]\\d{1,2}[Ee](\\d{1,3})"));
    const QRegularExpressionMatch match = pattern.match(itemKey);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int seasonNumberFromBulkItemKey(const QString& itemKey)
{
    static const QRegularExpression pattern(QStringLiteral("[Ss](\\d{1,2})[Ee]\\d{1,3}"));
    const QRegularExpressionMatch match = pattern.match(itemKey);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

StreamBulkItemState streamBulkItemStateFromJsonString(const QString& state)
{
    if (state == QLatin1String("Downloading")) return StreamBulkItemState::Downloading;
    if (state == QLatin1String("Publishing")) return StreamBulkItemState::Publishing;
    if (state == QLatin1String("Published")) return StreamBulkItemState::Published;
    if (state == QLatin1String("MissingSource")) return StreamBulkItemState::MissingSource;
    if (state == QLatin1String("MetadataFailed")) return StreamBulkItemState::MetadataFailed;
    if (state == QLatin1String("PublishFailed")) return StreamBulkItemState::PublishFailed;
    if (state == QLatin1String("Failed")) return StreamBulkItemState::Failed;
    if (state == QLatin1String("Completed")) return StreamBulkItemState::Completed;
    if (state == QLatin1String("Cancelled")) return StreamBulkItemState::Cancelled;
    if (state == QLatin1String("Orphaned")) return StreamBulkItemState::Orphaned;
    return StreamBulkItemState::Pending;
}

StreamBulkGroupRecord streamBulkGroupRecordFromJson(const QString& groupId,
                                                    const QJsonObject& obj)
{
    StreamBulkGroupRecord group;
    group.groupId = obj.value(QStringLiteral("groupId")).toString(groupId);
    group.groupKind = obj.value(QStringLiteral("groupKind")).toString(QStringLiteral("streamSeason"));
    group.label = obj.value(QStringLiteral("label")).toString(group.groupId);
    const QJsonObject sourceIds = obj.value(QStringLiteral("sourceIds")).toObject();
    group.sourceSeriesId = sourceIds.value(QStringLiteral("seriesId")).toString();
    group.sourceSeason = sourceIds.value(QStringLiteral("season")).toInt(-1);
    group.destinationRoot = obj.value(QStringLiteral("destinationRoot")).toString();
    group.stagingPath = obj.value(QStringLiteral("stagingPath")).toString();
    group.retryGeneration = obj.value(QStringLiteral("retryGeneration")).toInt(0);
    group.createdAtMs = obj.value(QStringLiteral("createdAtMs")).toVariant().toLongLong();
    group.updatedAtMs = obj.value(QStringLiteral("updatedAtMs")).toVariant().toLongLong();

    const QJsonObject canonicalNames = obj.value(QStringLiteral("canonicalNames")).toObject();
    for (auto it = canonicalNames.begin(); it != canonicalNames.end(); ++it)
        group.canonicalNames.insert(it.key(), it.value().toString());

    const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
    for (const auto& value : items) {
        const QJsonObject itemObj = value.toObject();
        StreamBulkGroupItem item;
        item.itemKey = itemObj.value(QStringLiteral("itemKey")).toString();
        item.destinationKey = itemObj.value(QStringLiteral("destinationKey")).toString();
        item.infoHash = itemObj.value(QStringLiteral("infoHash")).toString();
        item.fileIndex = itemObj.value(QStringLiteral("fileIndex")).toInt(-1);
        item.canonicalFilename = itemObj.value(QStringLiteral("canonicalFilename")).toString();
        item.itemState = streamBulkItemStateFromJsonString(
            itemObj.value(QStringLiteral("itemState")).toString());
        item.lastError = itemObj.value(QStringLiteral("lastError")).toString();
        group.items.push_back(item);
    }
    return group;
}

constexpr int kTheatrePaneExitDx = 24;
constexpr int kTheatrePaneEntryDx = 32;
constexpr int kTheatrePaneAnimMs = 180;
const char* kTheatrePaneAnimationName = "TheatrePaneAnimation";
const char* kTheatrePaneBasePosProperty = "theatrePaneBasePos";

QGraphicsOpacityEffect* ensureTheatrePaneOpacity(QWidget* widget)
{
    if (!widget)
        return nullptr;
    if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect()))
        return effect;

    auto* effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(1.0);
    widget->setGraphicsEffect(effect);
    return effect;
}

void stopTheatrePaneAnimation(QWidget* widget)
{
    if (!widget)
        return;
    if (auto* animation = widget->findChild<QParallelAnimationGroup*>(
            QString::fromLatin1(kTheatrePaneAnimationName))) {
        animation->stop();
        animation->deleteLater();
    }
}

void slideOutToRight(QWidget* widget)
{
    if (!widget || !widget->isVisible())
        return;

    stopTheatrePaneAnimation(widget);
    const QPoint basePos =
        widget->property(kTheatrePaneBasePosProperty).isValid()
            ? widget->property(kTheatrePaneBasePosProperty).toPoint()
            : widget->pos();
    widget->setProperty(kTheatrePaneBasePosProperty, basePos);
    const QPoint startPos = widget->pos();
    auto* opacity = ensureTheatrePaneOpacity(widget);
    if (!opacity)
        return;

    auto* group = new QParallelAnimationGroup(widget);
    group->setObjectName(QString::fromLatin1(kTheatrePaneAnimationName));

    auto* posAnim = new QPropertyAnimation(widget, "pos", group);
    posAnim->setDuration(kTheatrePaneAnimMs);
    posAnim->setEasingCurve(QEasingCurve::InCubic);
    posAnim->setStartValue(startPos);
    posAnim->setEndValue(startPos + QPoint(kTheatrePaneExitDx, 0));

    auto* fadeAnim = new QPropertyAnimation(opacity, "opacity", group);
    fadeAnim->setDuration(kTheatrePaneAnimMs);
    fadeAnim->setEasingCurve(QEasingCurve::InCubic);
    fadeAnim->setStartValue(opacity->opacity());
    fadeAnim->setEndValue(0.0);

    QObject::connect(group, &QParallelAnimationGroup::finished, widget,
                     [widget, opacity, basePos]() {
                         widget->hide();
                         widget->move(basePos);
                         opacity->setOpacity(1.0);
                     });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void slideInFromRight(QWidget* widget)
{
    if (!widget)
        return;

    stopTheatrePaneAnimation(widget);
    const QPoint endPos =
        widget->property(kTheatrePaneBasePosProperty).isValid()
            ? widget->property(kTheatrePaneBasePosProperty).toPoint()
            : widget->pos();
    widget->setProperty(kTheatrePaneBasePosProperty, endPos);
    const QPoint startPos = endPos + QPoint(kTheatrePaneEntryDx, 0);
    auto* opacity = ensureTheatrePaneOpacity(widget);
    if (!opacity)
        return;

    widget->move(startPos);
    opacity->setOpacity(0.0);
    widget->show();
    widget->raise();

    auto* group = new QParallelAnimationGroup(widget);
    group->setObjectName(QString::fromLatin1(kTheatrePaneAnimationName));

    auto* posAnim = new QPropertyAnimation(widget, "pos", group);
    posAnim->setDuration(kTheatrePaneAnimMs);
    posAnim->setEasingCurve(QEasingCurve::OutCubic);
    posAnim->setStartValue(startPos);
    posAnim->setEndValue(endPos);

    auto* fadeAnim = new QPropertyAnimation(opacity, "opacity", group);
    fadeAnim->setDuration(kTheatrePaneAnimMs);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);

    QObject::connect(group, &QParallelAnimationGroup::finished, widget,
                     [widget, opacity, endPos]() {
                         widget->move(endPos);
                         opacity->setOpacity(1.0);
                     });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace

StreamPage::StreamPage(CoreBridge* bridge, TorrentClient* torrentClient,
                       QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_torrentClient(torrentClient)
    , m_torrentEngine(torrentClient ? torrentClient->engine() : nullptr)
{
    setObjectName("stream");
    setWindowTitle(tr("Theatre"));

    // Core services
    m_addonRegistry = new tankostream::addon::AddonRegistry(bridge->dataDir(), nullptr, this);
    m_streamAggregator = new tankostream::stream::StreamAggregator(m_addonRegistry, this);
    m_metaAggregator = new tankostream::stream::MetaAggregator(m_addonRegistry, this);
    m_subtitlesAggregator = new tankostream::stream::SubtitlesAggregator(m_addonRegistry, this);

    // Phase 4 Batch 4.1 â€” drive the search-bar spinner off the aggregator's
    // catalog signals. StreamSearchWidget is the primary result consumer;
    // these connects only toggle the busy UI state. Lambdas capture `this`
    // safely â€” StreamPage owns m_metaAggregator so lifetimes are coupled.
    connect(m_metaAggregator, &tankostream::stream::MetaAggregator::catalogResults,
            this, [this](const QList<tankostream::addon::MetaItemPreview>&) {
                setSearchBusy(false);
            });
    connect(m_metaAggregator, &tankostream::stream::MetaAggregator::catalogError,
            this, [this](const QString&) {
                setSearchBusy(false);
            });

    // Batch 5.3 â€” route subtitle aggregator results to the VideoPlayer's
    // SubtitlePopover (merged subtitle UI; previously a separate
    // SubtitleMenu drawer pre-VIDEO_HUD_MINIMALIST 2026-04-25). Player
    // is created by MainWindow and reachable via findChild; connection
    // is persistent for the StreamPage lifetime.
    connect(m_subtitlesAggregator, &tankostream::stream::SubtitlesAggregator::subtitlesReady,
        this, [this](const QList<tankostream::addon::SubtitleTrack>& tracks,
                     const QHash<QString, QString>& originByTrackKey) {
            auto* mainWin = window();
            if (!mainWin) return;
            auto* player = mainWin->findChild<VideoPlayer*>();
            if (!player) return;
            player->setExternalSubtitleTracks(tracks, originByTrackKey);
        });
    m_library   = new StreamLibrary(&bridge->store(), this);

    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) â€” Theatre is download-only.
    // The Stremio stream-server subprocess (StreamServerEngine) and the
    // streaming controller (StreamPlayerController) are no longer created,
    // so stremio-runtime.exe never spawns and the "Resolving metadata" hang
    // is structurally impossible. Play now means: owned -> play local file;
    // not-owned -> open the download flow (see beginPlayOrDownload). The four
    // controller-signal handler slots and their stall wiring are removed.

    if (m_torrentClient) {
        connect(m_torrentClient, &TorrentClient::streamBulkRetrySourcePickRequested,
                this, &StreamPage::retryBulkSeasonDownload);
    }

    buildUI();
}

void StreamPage::activate()
{
    if (m_homeBoard)
        m_homeBoard->refresh();
    if (m_libraryLayout)
        m_libraryLayout->refresh();
}

namespace {
inline bool replyOkStream(QJsonObject& reply, QJsonObject fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it)
        reply.insert(it.key(), it.value());
    return true;
}
inline bool replyErrStream(QJsonObject& reply, const char* code, const QString& msg)
{
    reply["type"]    = QStringLiteral("error");
    reply["code"]    = QString::fromLatin1(code);
    reply["message"] = msg;
    return true;
}
}  // namespace

bool StreamPage::dispatchDevCommand(const QString& cmd,
                                    const QJsonObject& payload,
                                    QJsonObject& reply)
{
    // v1.6 Phase D.4 (2026-05-19) — library-side bridge. Pre-existing
    // stream_* commands route through MainWindow's handleDevCommand
    // directly (devOpenDetail / devGetSources / devDirectDownload). This
    // method only handles the cross-mode library_* surface. Stream has no
    // disk-scan or sort/density combos so those branches return
    // NOT_APPLICABLE rather than silently no-op'ing.
    if (!cmd.startsWith(QLatin1String("library_")))
        return false;

    if (cmd == QLatin1String("library_get_section"))
        return replyOkStream(reply, {{"section", devLibrarySection()}});
    if (cmd == QLatin1String("library_get_continue_reading"))
        return replyOkStream(reply,
            {{"cr_strip", devLibrarySection().value("cr_strip").toObject()}});
    if (cmd == QLatin1String("library_get_recently_added"))
        return replyOkStream(reply,
            {{"recently_added", devLibrarySection().value("recently_added").toObject()}});
    if (cmd == QLatin1String("library_get_search_state")) {
        return replyOkStream(reply, {
            {"query", m_searchInput ? m_searchInput->text() : QString()},
            {"search_state", devLibrarySection().value("search_state").toObject()}
        });
    }
    if (cmd == QLatin1String("library_get_scan_state"))
        return replyErrStream(reply, "NOT_APPLICABLE",
            "stream is network-driven; no on-disk scan state");
    if (cmd == QLatin1String("library_trigger_scan"))
        return replyErrStream(reply, "NOT_APPLICABLE",
            "stream has no disk scan to trigger");
    if (cmd == QLatin1String("library_get_sort"))
        return replyErrStream(reply, "NOT_APPLICABLE",
            "stream has no library sort combo");
    if (cmd == QLatin1String("library_set_sort"))
        return replyErrStream(reply, "NOT_APPLICABLE",
            "stream has no library sort combo");
    if (cmd == QLatin1String("library_set_density"))
        return replyErrStream(reply, "NOT_APPLICABLE",
            "stream has no density slider");
    if (cmd == QLatin1String("library_set_search_query")) {
        if (!m_searchInput)
            return replyErrStream(reply, "INTERNAL", "search input not constructed");
        const QString q = payload.value("query").toString();
        m_searchInput->setText(q);
        return replyOkStream(reply, {{"query", q}});
    }
    if (cmd == QLatin1String("library_get_active_layer"))
        return replyOkStream(reply, {{"layer",
            devLibrarySection().value("active_layer").toString()}});
    if (cmd == QLatin1String("library_reset_mode")) {
        resetToRoot();
        return replyOkStream(reply, {{"reset", true},
            {"layer", devLibrarySection().value("active_layer").toString()}});
    }
    if (cmd == QLatin1String("library_get_selected_items"))
        return replyOkStream(reply, {{"selection", QJsonArray{}}});
    return false;
}

QJsonObject StreamPage::devLibrarySection() const
{
    QJsonObject sec;
    QJsonObject cr;
    cr["visible"] = m_continueStrip && m_continueStrip->isVisible();
    cr["count"]   = 0;  // StreamContinueStrip count not exposed; smoke
                        // surfaces visibility via the visible flag.
    sec["cr_strip"] = cr;

    QJsonObject ra;
    ra["count"]   = m_library ? static_cast<int>(m_library->getAll().size()) : 0;
    ra["visible"] = m_library && !m_library->getAll().isEmpty();
    sec["recently_added"] = ra;

    QJsonObject ss;
    ss["query"]      = m_searchInput ? m_searchInput->text() : QString();
    ss["overlayVisible"] = m_searchWidget && m_searchWidget->isVisible();
    sec["search_state"] = ss;

    sec["scan_state"]   = QJsonValue::Null;  // stream is network-driven
    sec["root_folders"] = QJsonArray{};      // no on-disk roots
    sec["sort_key"]     = QJsonValue::Null;
    sec["density"]      = -1;
    sec["selection"]    = QJsonArray{};

    QString layer = QStringLiteral("browse");
    const int idx = m_mainStack ? m_mainStack->currentIndex() : -1;
    if (m_searchWidget && m_searchWidget->isVisible())
        layer = QStringLiteral("search");
    else if (idx == 1) layer = QStringLiteral("detail");
    else if (idx == 2) layer = QStringLiteral("player");
    else if (idx == 3) layer = QStringLiteral("addons");
    else if (idx == 4) layer = QStringLiteral("catalog");
    sec["active_layer"] = layer;
    return sec;
}

// STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) â€” fan out the download
// index to both consumers: StreamLibrary (so remove() also evicts per-
// episode rows) and StreamLibraryLayout (so tile DOWNLOADED chips render +
// re-evaluate on entriesChanged). Wired by MainWindow after both
// m_streamPage and m_streamDownloadIndex are constructed.
void StreamPage::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_streamDownloadIndex = idx;
    if (m_library)
        m_library->setStreamDownloadIndex(idx);
    if (m_libraryLayout) {
        m_libraryLayout->setStreamDownloadIndex(idx);
        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — wire TorrentClient alongside
        // so the DOWNLOADING chip can reflect in-flight cohort state.
        m_libraryLayout->setTorrentClient(m_torrentClient);
    }
    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” also wire the detail
    // view so per-row "on disk" markers + click-branch + alt-stream menu can
    // resolve. m_detailView may be null on early invocation (buildUI hasn't
    // run yet); MainWindow re-fires after buildPageStack so this lands.
    if (m_detailView)
        m_detailView->setStreamDownloadIndex(idx);
    if (m_theatreDownloadPanel)
        m_theatreDownloadPanel->setStreamDownloadIndex(idx);
}

QJsonObject StreamPage::devSnapshot() const
{
    QJsonObject snap;
    QString layer = QStringLiteral("unknown");
    const int idx = m_mainStack ? m_mainStack->currentIndex() : -1;
    if (m_searchWidget && m_searchWidget->isVisible())
        layer = QStringLiteral("search");
    else if (idx == 0)
        layer = QStringLiteral("browse");
    else if (idx == 1)
        layer = QStringLiteral("detail");
    else if (idx == 2)
        layer = QStringLiteral("player");
    else if (idx == 3)
        layer = QStringLiteral("addons");
    else if (idx == 4)
        layer = QStringLiteral("catalog");

    snap[QStringLiteral("layer")] = layer;
    snap[QStringLiteral("searchQuery")] = m_searchInput ? m_searchInput->text() : QString();
    snap[QStringLiteral("searchBusy")] = m_searchBusy && m_searchBusy->isVisible();

    QJsonArray nav;
    for (const NavEntry& entry : m_navStack) {
        QJsonObject o;
        switch (entry.kind) {
        case NavEntry::Kind::Browse:        o[QStringLiteral("kind")] = QStringLiteral("browse"); break;
        case NavEntry::Kind::CatalogBrowse: o[QStringLiteral("kind")] = QStringLiteral("catalogBrowse"); break;
        case NavEntry::Kind::Detail:        o[QStringLiteral("kind")] = QStringLiteral("detail"); break;
        case NavEntry::Kind::AddonManager:  o[QStringLiteral("kind")] = QStringLiteral("addonManager"); break;
        case NavEntry::Kind::Search:        o[QStringLiteral("kind")] = QStringLiteral("search"); break;
        }
        o[QStringLiteral("detailImdbId")] = entry.detailImdbId;
        o[QStringLiteral("searchQuery")] = entry.searchQuery;
        nav.append(o);
    }
    snap[QStringLiteral("navStack")] = nav;

    if (m_detailView)
        snap[QStringLiteral("detail")] = m_detailView->devSnapshot();
    snap[QStringLiteral("library")] = devLibrarySection();
    return snap;
}

QJsonObject StreamPage::devSearch(const QString& query,
                                  const QString& typeFilter,
                                  int timeoutMs)
{
    QJsonObject out;
    if (!m_metaAggregator || query.trimmed().isEmpty()) {
        out[QStringLiteral("status")] = QStringLiteral("bad_request");
        return out;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QList<tankostream::addon::MetaItemPreview> results;
    QString error;
    bool timedOut = false;
    connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    m_metaAggregator->searchByTitle(
        query.trimmed(), typeFilter,
        [&](const QList<tankostream::addon::MetaItemPreview>& found,
            const QString& err) {
            results = found;
            error = err;
            loop.quit();
        });
    timer.start(qBound(1000, timeoutMs, 15000));
    loop.exec();

    QJsonArray arr;
    for (const auto& item : results) {
        if (!typeFilter.isEmpty() && item.type != typeFilter)
            continue;
        QJsonObject o;
        o[QStringLiteral("imdb")] = item.id;
        o[QStringLiteral("name")] = item.name;
        o[QStringLiteral("year")] = item.releaseInfo;
        o[QStringLiteral("type")] = item.type;
        o[QStringLiteral("poster")] = item.poster.toString();
        arr.append(o);
    }

    out[QStringLiteral("status")] = timedOut ? QStringLiteral("timeout")
                                             : QStringLiteral("ok");
    out[QStringLiteral("timedOut")] = timedOut;
    out[QStringLiteral("error")] = error;
    out[QStringLiteral("results")] = arr;
    return out;
}

QJsonObject StreamPage::devDispatchEpisode(const QString& imdbId, int season, int episode)
{
    return devDispatchEpisodes(imdbId, season, QList<int>{episode});
}

QJsonObject StreamPage::devDispatchSeason(const QString& imdbId, int season)
{
    return devDispatchEpisodes(imdbId, season, QList<int>{});
}

// ─── v1.3 stream-side bridge expansion ──────────────────────────────────────
// Agent 4 attribution, 2026-05-19. Skeleton stubs land first to pre-allocate
// dispatcher namespace + verify the build green; bodies filled by parallel
// Agent 4 subordinate sessions (A4S1/A4S2/A4S3).

QJsonObject StreamPage::devOpenDetail(const QString& imdbId)
{
    // A4S1: mirror the user-visible search-result single-click path. The
    // (QString) overload of showDetail is the exact same slot wired to
    // StreamLibraryLayout::showClicked / StreamSearchWidget::metaActivated
    // (via lambda) -- it pushes a Detail NavEntry, emits enteredLayer, and
    // is idempotent against repeat-clicks on the same imdbId.
    QJsonObject out;
    if (imdbId.isEmpty()) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("imdbId is empty");
        return out;
    }
    if (!m_detailView || !m_mainStack) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("StreamPage not fully constructed (m_detailView or m_mainStack null)");
        return out;
    }

    showDetail(imdbId);

    out[QStringLiteral("status")]      = QStringLiteral("ok");
    out[QStringLiteral("imdbId")]      = imdbId;
    out[QStringLiteral("currentImdb")] = m_detailView->currentImdb();
    // StreamPage m_mainStack layout: 0=browse, 1=detail, 2=player, 3=addons.
    // After showDetail() the stack should land on index 1; report whatever
    // it actually settled at so the caller can verify the navigation took.
    out[QStringLiteral("layer")]       = m_mainStack->currentIndex();
    return out;
}

QJsonObject StreamPage::devGetSources()
{
    // A4S2 (2026-05-19). Snapshot the active detail view's source-card pane
    // as JSON for the v1.3 dev-control bridge. Mirrors the user-visible
    // StreamSourceList without mutating it; the StreamSourceList descendant
    // is reached via findChild (same pattern A4S3's devDirectDownload uses).
    QJsonObject out;

    if (!m_detailView || !m_mainStack
     || m_detailView->currentImdb().isEmpty()) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("no detail view active");
        return out;
    }

    auto* sourceList =
        m_detailView->findChild<tankostream::stream::StreamSourceList*>();
    if (!sourceList) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("no detail view active");
        return out;
    }

    if (sourceList->isLoading()) {
        out[QStringLiteral("status")]  = QStringLiteral("pending");
        out[QStringLiteral("message")] = QStringLiteral("source collection in progress");
        return out;
    }

    const auto choices = sourceList->snapshotChoices();

    QJsonArray sources;
    int idx = 0;
    for (const tankostream::stream::StreamPickerChoice& choice : choices) {
        QJsonObject row;
        row[QStringLiteral("index")] = idx++;

        // Coarse kind classification — drives the smoke's "pick the tankorent
        // card" / "pick a premium debrid card" selectors. No canonical enum
        // exists; this is a heuristic over StreamPickerChoice fields:
        //   tankorent  — addonId/addonName carries "tankorent" (the in-app
        //                Tankorent-as-Source surface from TANKORENT_STREAM_INTEGRATION)
        //   addon-bulk — magnet-source rows from a torrent-aggregator addon
        //                (Torrentio et al; the bulk of the panel for series)
        //   premium    — direct streams (HTTP/URL) — usually debrid-resolved
        //                or premium hosted (RealDebrid, etc.)
        //   other      — anything we didn't classify above (YouTube, etc.)
        QString kind = QStringLiteral("other");
        if (choice.addonId.contains(QStringLiteral("tankorent"), Qt::CaseInsensitive)
         || choice.addonName.contains(QStringLiteral("tankorent"), Qt::CaseInsensitive)) {
            kind = QStringLiteral("tankorent");
        } else if (choice.sourceKind == QLatin1String("magnet")
                || !choice.magnetUri.isEmpty()) {
            kind = QStringLiteral("addon-bulk");
        } else if (choice.isDirect
                || choice.sourceKind == QLatin1String("http")
                || choice.sourceKind == QLatin1String("url")) {
            kind = QStringLiteral("premium");
        }
        row[QStringLiteral("kind")] = kind;

        row[QStringLiteral("addonName")]  = choice.addonName;
        row[QStringLiteral("name")]       = choice.displayTitle;
        row[QStringLiteral("magnetUri")]  = !choice.magnetUri.isEmpty();
        row[QStringLiteral("quality")]    = choice.displayQuality;
        row[QStringLiteral("peers")]      = choice.seeders;
        row[QStringLiteral("sizeBytes")]  = static_cast<qint64>(choice.sizeBytes);

        // Bonus context the smoke benefits from when filtering pack-type
        // results (TANKORENT_CINEMETA_PACK_MAPPING Phase 2 targets packs).
        if (!choice.packType.isEmpty())
            row[QStringLiteral("packType")] = choice.packType;
        if (!choice.packLabel.isEmpty())
            row[QStringLiteral("packLabel")] = choice.packLabel;

        sources.append(row);
    }

    out[QStringLiteral("status")]        = QStringLiteral("ok");
    out[QStringLiteral("detailImdb")]    = m_detailView->currentImdb();
    out[QStringLiteral("currentSeason")] = m_detailView->currentSeason();
    out[QStringLiteral("sources")]       = sources;
    return out;
}

QJsonObject StreamPage::devDirectDownload(int sourceIndex)
{
    // A4S3 (2026-05-19). Programmatic trigger for the directDownloadRequested
    // chain: StreamSourceList -> StreamDetailView -> StreamPage::onDirectDownloadRequested
    // -> TorrentClient::startDownload (Phase 2 substrate from
    // TANKORENT_CINEMETA_PACK_MAPPING Tasks 7-10). The bridge needs no
    // accessor on StreamDetailView -- the StreamSourceList lives as a
    // descendant of m_detailView and findChild reaches it via its objectName.
    QJsonObject out;

    if (!m_detailView || !m_mainStack) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("no detail view active");
        return out;
    }

    auto* sourceList =
        m_detailView->findChild<tankostream::stream::StreamSourceList*>();
    if (!sourceList) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("no detail view active");
        return out;
    }

    const int count = sourceList->sourceCardCount();
    if (count <= 0) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("no source cards loaded (panel empty or still loading)");
        return out;
    }
    if (sourceIndex < 0 || sourceIndex >= count) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] =
            QStringLiteral("sourceIndex %1 out of range (0..%2)")
                .arg(sourceIndex)
                .arg(count - 1);
        return out;
    }

    QString addonName;
    QString displayName;
    bool    hasMagnet = false;
    const bool ok = sourceList->triggerDirectDownloadAt(
        sourceIndex, &addonName, &displayName, &hasMagnet);
    if (!ok) {
        out[QStringLiteral("status")]  = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("triggerDirectDownloadAt failed for sourceIndex %1").arg(sourceIndex);
        return out;
    }

    out[QStringLiteral("status")]      = QStringLiteral("ok");
    out[QStringLiteral("sourceIndex")] = sourceIndex;
    out[QStringLiteral("magnetUri")]   = hasMagnet;
    out[QStringLiteral("addonName")]   = addonName;
    out[QStringLiteral("displayName")] = displayName;
    return out;
}

// â”€â”€â”€ UI construction â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void StreamPage::buildUI()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    buildSearchBar();

    m_mainStack = new QStackedWidget(this);

    buildBrowseLayer();
    m_mainStack->addWidget(m_browseLayer);   // index 0: browse

    // Detail layer
    m_detailView = new StreamDetailView(m_bridge, m_metaAggregator, m_library, this);
    m_mainStack->addWidget(m_detailView); // index 1: detail

    // THEATRE_STREAMING_RESTORE P1 (2026-06-09) — construct the Stremio
    // stream-server engine + restored player controller. The "watch" click
    // (finishAutoDownloadPick with forStream=true) streams the auto-picked
    // source; explicit download actions stay on libtorrent. Cache dir mirrors
    // the pre-deletion path (dataDir/stream_server_cache). Core-first: the 4
    // signals wire to open-player / overlay / error / cleanup; session-lifecycle
    // polish (stall, buffered-range, playback-window) is deferred to P1.x.
    if (m_bridge && !m_streamEngine) {
        const QString cacheDir = m_bridge->dataDir() + "/stream_server_cache";
        m_streamEngine = new StreamServerEngine(cacheDir, this);
        m_streamEngine->start();
        m_streamEngine->cleanupOrphans();
        m_streamEngine->startPeriodicCleanup();

        m_playerController = new StreamPlayerController(m_bridge, m_streamEngine, this);
        connect(m_playerController, &StreamPlayerController::bufferUpdate,
                this, &StreamPage::onBufferUpdate);
        connect(m_playerController, &StreamPlayerController::readyToPlay,
                this, &StreamPage::onReadyToPlay);
        connect(m_playerController, &StreamPlayerController::streamFailed,
                this, &StreamPage::onStreamFailed);
        connect(m_playerController, &StreamPlayerController::streamStopped,
                this, &StreamPage::onStreamStopped);
    }

    // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) â€” wire TorrentClient
    // through to the detail view so Remove-from-Library can detect active
    // bulk groups for the show and gate the destructive action behind a
    // confirmation dialog. Spec Â§10.10.
    if (m_torrentClient)
        m_detailView->setTorrentClient(m_torrentClient);

    m_detailRightPaneStack = m_detailView->rightPaneStack();
    m_detailSourcesPanel = m_detailView->sourcesPanel();
    m_unifiedPackSearchEngine =
        new tankoban::stream::theatre::UnifiedPackSearchEngine(m_streamAggregator, this);
    if (m_detailRightPaneStack) {
        m_theatreDownloadPanel =
            new tankoban::stream::theatre::TheatreDownloadPanel(m_detailRightPaneStack);
        m_theatreDownloadPanel->setSearchEngine(m_unifiedPackSearchEngine);
        m_theatreDownloadPanel->setStreamDownloadIndex(m_streamDownloadIndex);
        m_theatreDownloadPanel->setTorrentClient(m_torrentClient);
        m_theatreDownloadPanel->hide();

        if (auto* rightStack =
                qobject_cast<QStackedLayout*>(m_detailRightPaneStack->layout())) {
            rightStack->addWidget(m_theatreDownloadPanel);
        }
    }

    connect(m_detailView, &StreamDetailView::backRequested, this, &StreamPage::goBack);
    connect(m_detailView, &StreamDetailView::playRequested, this, &StreamPage::onPlayRequested);
    connect(m_detailView, &StreamDetailView::sourceActivated,
            this, &StreamPage::onSourceActivated);
    connect(m_detailView, &StreamDetailView::addToTankorentRequested,
            this, &StreamPage::onAddToTankorentRequested);
    connect(m_detailView, &StreamDetailView::bulkDownloadRequested,
            this, &StreamPage::triggerBulkSeasonDownload);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — direct-dispatch from episode/season/
    // selected-set download signals. Bypasses the preflight dialog.
    connect(m_detailView, &StreamDetailView::seasonDownloadRequested,
            this, &StreamPage::onSeasonDownloadRequested);
    connect(m_detailView, &StreamDetailView::selectedEpisodesDownloadRequested,
            this, &StreamPage::onSelectedEpisodesDownloadRequested);
    connect(m_detailView, &StreamDetailView::singleEpisodeDownloadRequested,
            this, &StreamPage::onSingleEpisodeDownloadRequested);
    connect(m_detailView, &StreamDetailView::theatreDownloadRequested,
            this, [this](const QString& imdbId,
                         const QString& showName,
                         int season,
                         const QString& mediaType,
                         const QMap<int, int>& knownEpisodeCounts) {
                if (!m_theatreDownloadPanel)
                    return;
                const QString showYear = m_detailView ? m_detailView->currentYear() : QString();
                m_theatreDownloadPanel->openFor(imdbId, showName, showYear,
                                                season, mediaType, knownEpisodeCounts);
                if (m_detailSourcesPanel)
                    slideOutToRight(m_detailSourcesPanel);
                slideInFromRight(m_theatreDownloadPanel);
            });
    connect(m_detailView, &StreamDetailView::theatreTopSeededDownloadRequested,
            this, &StreamPage::onTheatreTopSeededDownloadRequested);
    connect(m_detailView, &StreamDetailView::directDownloadRequested,
            this, &StreamPage::onDirectDownloadRequested);
    // THEATRE_DOWNLOAD_OVERHAUL stale-panel-on-show-change fix 2026-05-17 -
    // when StreamDetailView transitions to a different show, dismiss the
    // TheatreDownloadPanel via the imperative helper. v2: simplified to match
    // dismissRequested pattern + delegates to dismissTheatreDownloadPanelIfOpen
    // which is also called at every other navigation transition site.
    connect(m_detailView, &StreamDetailView::entryContextChanging,
            this, [this]() { dismissTheatreDownloadPanelIfOpen(); });
    if (m_theatreDownloadPanel) {
        connect(m_theatreDownloadPanel,
                &tankoban::stream::theatre::TheatreDownloadPanel::dismissRequested,
                this, [this]() {
                    if (m_theatreDownloadPanel)
                        slideOutToRight(m_theatreDownloadPanel);
                    if (m_detailSourcesPanel)
                        slideInFromRight(m_detailSourcesPanel);
                });
        connect(m_theatreDownloadPanel,
                &tankoban::stream::theatre::TheatreDownloadPanel::downloadRequested,
                this, [this](const QString& imdbId,
                             int season,
                             const QString& magnetUri,
                             const QString& infoHash,
                             const AddTorrentConfig& config,
                             const QList<int>& selectedEpisodes,
                             const QMap<int, int>& fileIndexByEpisode,
                             const QString& packTitle) {
                    using namespace tankostream::stream;

                    if (!m_torrentClient || !m_bridge || !m_detailView
                        || imdbId.isEmpty() || infoHash.isEmpty()) {
                        return;
                    }

                    AddTorrentConfig dispatchConfig = config;
                    dispatchConfig.streamGroupId.clear();
                    dispatchConfig.imdbId = imdbId;
                    dispatchConfig.season = season;
                    // F9 fix 2026-05-19: pass the magnet URI through so startDownload can
                    // self-defend when the indexer pre-filled infoHash and resolveMetadata was
                    // skipped. Empty magnetUri is fine — startDownload will warn+abort cleanly
                    // rather than writing a zombie record.
                    dispatchConfig.magnetUri = magnetUri;

                    if (season > 0 && !selectedEpisodes.isEmpty()) {
                        const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
                        if (roots.isEmpty() || roots.first().isEmpty())
                            return;

                        BulkPlanInput input;
                        input.seriesId = imdbId;
                        input.seriesTitle = m_detailView->currentTitle();
                        if (input.seriesTitle.isEmpty())
                            input.seriesTitle = imdbId;
                        input.seriesYear = m_detailView->currentYear();
                        input.seasonNumber = season;
                        input.videosRootPath = roots.first();

                        const QList<StreamEpisode> allEpisodes =
                            m_detailView->episodesForSeason(season);
                        QSet<int> selectedSet;
                        for (int ep : selectedEpisodes)
                            selectedSet.insert(ep);
                        for (const StreamEpisode& episode : allEpisodes) {
                            if (!selectedSet.contains(episode.episode))
                                continue;
                            BulkPlanEpisodeInput row;
                            row.season = season;
                            row.episode = episode.episode;
                            row.title = episode.title;
                            row.extensionHint = QStringLiteral("mkv");
                            input.episodes.push_back(row);
                        }
                        if (input.episodes.isEmpty())
                            return;

                        const BulkPlanResult plan = buildBulkPlan(input, [](const QString& path) {
                            return QFileInfo::exists(path);
                        });
                        QHash<QString, BulkPlanItem> planByKey;
                        for (const BulkPlanItem& item : plan.items)
                            planByKey.insert(item.itemKey, item);

                        StreamBulkGroupRecord group;
                        const qint64 now = QDateTime::currentMSecsSinceEpoch();
                        group.groupId = QStringLiteral("stream:%1:s%2:%3")
                            .arg(input.seriesId)
                            .arg(input.seasonNumber, 2, 10, QChar('0'))
                            .arg(now);
                        group.groupKind = QStringLiteral("streamSeason");
                        group.label = QStringLiteral("%1 - Season %2")
                            .arg(input.seriesTitle)
                            .arg(input.seasonNumber);
                        group.sourceSeriesId = input.seriesId;
                        group.sourceSeason = input.seasonNumber;
                        group.destinationRoot = input.videosRootPath;
                        group.createdAtMs = now;
                        group.updatedAtMs = now;

                        for (const BulkPlanEpisodeInput& episode : input.episodes) {
                            const QString itemKey = makeItemKey(
                                input.seriesId, input.seasonNumber, episode.episode);
                            const BulkPlanItem planItem = planByKey.value(itemKey);
                            if (planItem.itemKey.isEmpty())
                                return;
                            if (!fileIndexByEpisode.contains(episode.episode))
                                return;

                            StreamBulkGroupItem item;
                            item.itemKey = planItem.itemKey;
                            item.destinationKey = planItem.destinationKey;
                            item.canonicalFilename = planItem.canonicalFilename;
                            item.infoHash = infoHash;
                            item.fileIndex = fileIndexByEpisode.value(episode.episode);
                            item.itemState = StreamBulkItemState::Pending;
                            group.items.push_back(item);
                        }
                        if (group.items.isEmpty())
                            return;

                        m_torrentClient->upsertStreamBulkGroup(group);
                    }

                    Q_UNUSED(magnetUri);
                    Q_UNUSED(packTitle);
                    m_torrentClient->startDownload(infoHash, dispatchConfig);
                    if (m_detailView)
                        m_detailView->autoAddToLibrary();
                    if (m_theatreDownloadPanel) {
                        m_theatreDownloadPanel->reset();
                        slideOutToRight(m_theatreDownloadPanel);
                    }
                    if (m_detailSourcesPanel)
                        slideInFromRight(m_detailSourcesPanel);
                });
    }
    // THEATRE_DOWNLOAD_ONLY P1.1 (2026-05-29) â€” trailers KEPT. The original
    // path streamed an ad-hoc httpSource through StreamPlayerController, which
    // the download-only pivot removed. Trailers don't need the stream-server:
    // a trailer is a direct video URL the player can open. Re-wired to open
    // the URL straight in the floating VideoPlayer (found via
    // window()->findChild<VideoPlayer*>, the same lookup pattern used at the
    // subtitle-route + next-episode-overlay sites) â€” no StreamServerEngine /
    // StreamPlayerController involved. We deliberately do NOT route through
    // playLocalFileFromStreamRequested: MainWindow's slot for that signal
    // guards on QFileInfo::exists() and would silently drop a remote URL.
    //
    // NOTE: trailers apparently never actually played before â€” the typical
    // trailer URL is a YouTube link ffmpeg can't open directly. Actually
    // FIXING trailer playback (URL resolution) is an explicit separate
    // follow-up, NOT this task; this wiring just preserves the direct-open
    // path so the feature isn't erased. If the URL is directly playable the
    // player plays it; if not, that's the follow-up.
    connect(m_detailView, &StreamDetailView::trailerDirectPlayRequested,
            this, [this](const QUrl& trailerUrl) {
                if (!trailerUrl.isValid()) return;
                auto* mainWin = window();
                if (!mainWin) return;
                auto* player = mainWin->findChild<VideoPlayer*>();
                if (!player) return;
                const QString title = m_detailView ? m_detailView->currentTitle()
                                                    : QString();
                // imdbId/title from the current detail view; season/episode 0.
                // Pass the trailer URL as the file/source. Default playlist +
                // index 0 + startPositionSec 0.0; displayTitle = show title.
                player->openFile(trailerUrl.toString(), {}, 0, 0.0, title);
            });

    // Phase 2 Batch 2.4 â€” Pick-different button in the toast; aborts the
    // auto-launch timer and leaves the picker open so the user can pick a
    // different source manually.
    connect(m_detailView, &StreamDetailView::autoLaunchCancelRequested,
            this, &StreamPage::cancelAutoLaunch);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” downloaded-episode
    // click. Detail view fires this AHEAD of playRequested when the
    // StreamDownloadIndex hit AND the file exists on disk; falls through to
    // playRequested otherwise. Spec Â§6.2.
    connect(m_detailView, &StreamDetailView::playLocalFileFromStreamRequested,
            this, &StreamPage::onDetailPlayLocalFileFromStream);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” right-click â†’ "Show
    // alternate streams". Same flow as a default click on an undownloaded
    // episode: flip right pane to loading + emit the existing playRequested
    // so onPlayRequested runs the source-pick aggregator. Spec Â§6.3.
    connect(m_detailView, &StreamDetailView::alternateStreamRequested,
            this, [this](int season, int episode) {
                if (!m_detailView) return;
                m_detailView->setStreamSourcesLoading();
                onPlayRequested(m_detailView->currentImdb(),
                                QStringLiteral("series"), season, episode);
            });

    // Auto-launch timer â€” single-shot, 2s window (enough for the user to
    // notice the "Resuming with last-used source" toast and cancel).
    m_autoLaunchTimer = new QTimer(this);
    m_autoLaunchTimer->setSingleShot(true);
    m_autoLaunchTimer->setInterval(2000);
    connect(m_autoLaunchTimer, &QTimer::timeout,
            this, &StreamPage::onAutoLaunchFire);

    // Wire library grid single+double-click â†’ show detail. Explicit lambda
    // because showDetail is overloaded â€” pointer-to-member would be ambiguous
    // between the (QString) and (MetaItemPreview) overloads.
    connect(m_libraryLayout, &StreamLibraryLayout::showClicked, this,
        [this](const QString& imdbId) { showDetail(imdbId); });

    // Player layer â€” buffer overlay only (VideoPlayer handles its own controls)
    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” promoted to member
    // so onNextEpisodeCancel can restore m_nextEpisodeOverlay's parent
    // after the mid-playback reparent to VideoPlayer.
    m_playerLayer = new QWidget(this);
    auto* playerLayer = m_playerLayer;
    auto* playerLayerLayout = new QVBoxLayout(playerLayer);
    playerLayerLayout->setContentsMargins(0, 0, 0, 0);
    playerLayerLayout->setAlignment(Qt::AlignCenter);

    m_bufferOverlay = new QWidget(playerLayer);
    auto* bufLayout = new QVBoxLayout(m_bufferOverlay);
    bufLayout->setAlignment(Qt::AlignCenter);
    bufLayout->setSpacing(12);

    m_bufferLabel = new QLabel("Connecting...", m_bufferOverlay);
    m_bufferLabel->setObjectName("StreamBufferOverlay");
    m_bufferLabel->setAlignment(Qt::AlignCenter);
    m_bufferLabel->setStyleSheet(
        "#StreamBufferOverlay { background: rgba(0,0,0,180); color: white;"
        "  font-size: 15px; padding: 12px 24px; border-radius: 8px; }");
    bufLayout->addWidget(m_bufferLabel, 0, Qt::AlignCenter);

    m_bufferCancelBtn = new QPushButton("Cancel", m_bufferOverlay);
    m_bufferCancelBtn->setFixedSize(100, 30);
    m_bufferCancelBtn->setCursor(Qt::PointingHandCursor);
    m_bufferCancelBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.15); border: none;"
        "  border-radius: 6px; color: rgba(255,255,255,0.7); font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.25); }");
    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) â€” there is no streaming
    // controller to stop anymore; the buffer overlay is no longer shown
    // during the download-only flow. Cancel just hides the overlay
    // defensively (the widget is retained as dead UI referenced by the
    // unreachable next-episode overlay path).
    connect(m_bufferCancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_bufferOverlay) m_bufferOverlay->hide();
    });
    bufLayout->addWidget(m_bufferCancelBtn, 0, Qt::AlignCenter);

    playerLayerLayout->addWidget(m_bufferOverlay);

    // Phase 2 Batch 2.5 â€” next-episode overlay lives on the same player
    // layer as the buffer overlay. Hidden by default; becomes visible after
    // the user closes a series episode whose playback crossed 95% AND a
    // next-episode pre-fetch completed with a matched source. 10s countdown
    // auto-plays; Play Now skips the countdown; Cancel returns to browse.
    m_nextEpisodeOverlay = new QFrame(playerLayer);
    m_nextEpisodeOverlay->setObjectName(QStringLiteral("StreamNextEpisodeOverlay"));
    m_nextEpisodeOverlay->setStyleSheet(QStringLiteral(
        "#StreamNextEpisodeOverlay {"
        "  background: rgba(0,0,0,200); border-radius: 10px; padding: 20px; }"
        "#StreamNextEpisodeTitle {"
        "  color: #f0f0f0; font-size: 15px; font-weight: 600; }"
        "#StreamNextEpisodeCountdown {"
        "  color: rgba(255,255,255,0.75); font-size: 12px; }"
        "#StreamNextEpisodePlayNow, #StreamNextEpisodeCancel {"
        "  background: rgba(255,255,255,0.14); border: 1px solid rgba(255,255,255,0.22);"
        "  border-radius: 6px; color: #e8e8e8; font-size: 12px; padding: 6px 18px; }"
        "#StreamNextEpisodePlayNow:hover, #StreamNextEpisodeCancel:hover {"
        "  background: rgba(255,255,255,0.22); }"));
    m_nextEpisodeOverlay->setFixedWidth(420);
    auto* nextLayout = new QVBoxLayout(m_nextEpisodeOverlay);
    nextLayout->setContentsMargins(20, 16, 20, 16);
    nextLayout->setSpacing(8);
    nextLayout->setAlignment(Qt::AlignCenter);

    m_nextEpisodeTitleLabel = new QLabel(m_nextEpisodeOverlay);
    m_nextEpisodeTitleLabel->setObjectName(QStringLiteral("StreamNextEpisodeTitle"));
    m_nextEpisodeTitleLabel->setAlignment(Qt::AlignCenter);
    m_nextEpisodeTitleLabel->setWordWrap(true);
    nextLayout->addWidget(m_nextEpisodeTitleLabel);

    m_nextEpisodeCountdownLabel = new QLabel(m_nextEpisodeOverlay);
    m_nextEpisodeCountdownLabel->setObjectName(QStringLiteral("StreamNextEpisodeCountdown"));
    m_nextEpisodeCountdownLabel->setAlignment(Qt::AlignCenter);
    nextLayout->addWidget(m_nextEpisodeCountdownLabel);

    auto* nextBtnRow = new QHBoxLayout();
    nextBtnRow->setSpacing(10);
    nextBtnRow->setAlignment(Qt::AlignCenter);

    m_nextEpisodePlayNowBtn = new QPushButton(tr("Play now"), m_nextEpisodeOverlay);
    m_nextEpisodePlayNowBtn->setObjectName(QStringLiteral("StreamNextEpisodePlayNow"));
    m_nextEpisodePlayNowBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextEpisodePlayNowBtn, &QPushButton::clicked,
            this, &StreamPage::onNextEpisodePlayNow);
    nextBtnRow->addWidget(m_nextEpisodePlayNowBtn);

    m_nextEpisodeCancelBtn = new QPushButton(tr("Cancel"), m_nextEpisodeOverlay);
    m_nextEpisodeCancelBtn->setObjectName(QStringLiteral("StreamNextEpisodeCancel"));
    m_nextEpisodeCancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextEpisodeCancelBtn, &QPushButton::clicked,
            this, &StreamPage::onNextEpisodeCancel);
    nextBtnRow->addWidget(m_nextEpisodeCancelBtn);

    nextLayout->addLayout(nextBtnRow);

    m_nextEpisodeOverlay->hide();
    playerLayerLayout->addWidget(m_nextEpisodeOverlay, 0, Qt::AlignCenter);

    // Countdown timer â€” 1s tick. Decrements m_nextEpisodeCountdownSec; fires
    // Play Now at zero.
    m_nextEpisodeCountdownTimer = new QTimer(this);
    m_nextEpisodeCountdownTimer->setInterval(1000);
    connect(m_nextEpisodeCountdownTimer, &QTimer::timeout,
            this, &StreamPage::onNextEpisodeCountdownTick);

    m_mainStack->addWidget(playerLayer); // index 2: player

    // Addon manager layer (Phase 2 Batch 2.1)
    m_addonManager = new AddonManagerScreen(m_addonRegistry, this);
    m_mainStack->addWidget(m_addonManager); // index 3: addons

    connect(m_addonManager, &AddonManagerScreen::backRequested,
            this, &StreamPage::goBack);
    // addAddonRequested stays unwired until Batch 2.2 ships AddAddonDialog.

    // Catalog browse layer (Phase 3 Batch 3.3)
    m_catalogBrowse = new tankostream::stream::CatalogBrowseScreen(m_addonRegistry, this);
    m_mainStack->addWidget(m_catalogBrowse); // index 4: catalog browse

    connect(m_catalogBrowse, &tankostream::stream::CatalogBrowseScreen::backRequested,
            this, &StreamPage::goBack);
    connect(m_catalogBrowse, &tankostream::stream::CatalogBrowseScreen::metaActivated, this,
        [this](const tankostream::addon::MetaItemPreview& preview) {
            showDetail(preview);
        });
    // Stream library UX rework 2026-04-15 â€” StreamHomeBoard no longer emits
    // browseCatalogRequested (featured rows deleted). Users reach the
    // catalog browser via the Catalog button in the search bar instead.

    rootLayout->addWidget(m_mainStack, 1);

    m_mainStack->setCurrentIndex(0);
}

void StreamPage::buildSearchBar()
{
    m_searchBarFrame = new QFrame(this);
    m_searchBarFrame->setObjectName("streamSearchBar");

    // Margins are 0 left/right because the search bar now lives inside
    // m_scrollLayout (20,0,20,20), which already provides the page-edge
    // inset. Top=20 preserves the original breathing room from the page top.
    auto* layout = new QHBoxLayout(m_searchBarFrame);
    layout->setContentsMargins(0, 20, 0, 0);
    layout->setSpacing(8);

    m_searchInput = new QLineEdit(m_searchBarFrame);
    m_searchInput->setPlaceholderText("Search movies & TV shows...");
    // 36px input height to match VideosPage.cpp:119 search bar. Was 32.
    m_searchInput->setFixedHeight(36);
    m_searchInput->setObjectName("StreamSearchInput");
    m_searchInput->setClearButtonEnabled(true);
    layout->addWidget(m_searchInput, 1);

    // Phase 4 Batch 4.1 â€” indeterminate spinner. Busy mode QProgressBar
    // (range 0..0) animates natively; 16x16 sits between input + Search btn.
    auto* busy = new QProgressBar(m_searchBarFrame);
    busy->setRange(0, 0);
    busy->setTextVisible(false);
    busy->setFixedSize(16, 16);
    busy->setObjectName("StreamSearchBusy");
    busy->setStyleSheet(
        "#StreamSearchBusy { background: transparent; border: none; }"
        "#StreamSearchBusy::chunk { background: rgba(255,255,255,0.5); }");
    busy->hide();
    layout->addWidget(busy);
    m_searchBusy = busy;

    // THEATRE_POLISH 2026-05-22 â€” Search button is now a magnifying-glass
    // SVG icon only (no text). Saves horizontal space next to the topbar
    // search input and matches the iconographic style of the gear button.
    m_searchBtn = new QPushButton(m_searchBarFrame);
    m_searchBtn->setFixedHeight(36);
    m_searchBtn->setFixedWidth(36);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setObjectName("StreamSearchBtn");
    m_searchBtn->setIcon(QIcon(QStringLiteral(":/icons/search.svg")));
    m_searchBtn->setIconSize(QSize(18, 18));
    m_searchBtn->setToolTip("Search");
    layout->addWidget(m_searchBtn);

    m_addonsBtn = new QPushButton("Addons", m_searchBarFrame);
    m_addonsBtn->setFixedHeight(36);
    m_addonsBtn->setCursor(Qt::PointingHandCursor);
    m_addonsBtn->setObjectName("StreamAddonsBtn");
    m_addonsBtn->setToolTip("Manage installed addons");
    layout->addWidget(m_addonsBtn);

    // Stream library UX rework 2026-04-15 â€” Catalog button replaces the
    // deleted home-board featured rows. Opens the existing
    // CatalogBrowseScreen; the screen's own addon/catalog combos handle
    // selection (persists across sessions).
    m_catalogBtn = new QPushButton("Catalog", m_searchBarFrame);
    m_catalogBtn->setFixedHeight(36);
    m_catalogBtn->setCursor(Qt::PointingHandCursor);
    m_catalogBtn->setObjectName("StreamCatalogBtn");
    m_catalogBtn->setToolTip("Browse all addon catalogs");
    layout->addWidget(m_catalogBtn);

    // THEATRE_CLEANUP F2 (2026-05-22) — gear icon at the right edge of the
    // Theatre topbar. Opens a small QMenu containing the Clear Library
    // action (red text). Click → menu → action triggers
    // onClearLibraryRequested which runs the two-step confirmation
    // (warning modal + type-"clear" input modal) before invoking
    // m_library->clear().
    //
    // THEATRE_POLISH 2026-05-22 — drop the button's default dark
    // backdrop (transparent + no border, hover-tint on roll-over) so the
    // gear icon reads as just-an-icon next to the boxy Addons/Catalog
    // text buttons. Single-action menu (the "Danger zone" section header
    // was visual clutter for one action) with a tighter padding box and
    // red action text since destructive is the only thing in here.
    m_settingsBtn = new QPushButton(QStringLiteral("⚙"), m_searchBarFrame);
    m_settingsBtn->setFixedHeight(36);
    m_settingsBtn->setFixedWidth(36);
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setObjectName("StreamSettingsBtn");
    m_settingsBtn->setToolTip("Theatre settings");
    m_settingsBtn->setStyleSheet(
        "QPushButton#StreamSettingsBtn {"
        "  background: transparent;"
        "  border: none;"
        "  color: #cccccc;"
        "  font-size: 16px;"
        "}"
        "QPushButton#StreamSettingsBtn:hover {"
        "  background: rgba(255,255,255,0.06);"
        "  border-radius: 4px;"
        "}"
        "QPushButton#StreamSettingsBtn::menu-indicator { image: none; width: 0; }");
    auto* settingsMenu = new QMenu(m_settingsBtn);
    auto* clearLibraryAction = settingsMenu->addAction(tr("Clear Library"));
    settingsMenu->setStyleSheet(
        "QMenu {"
        "  background: #1f1f1f;"
        "  border: 1px solid #3a3a3a;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  padding: 6px 14px;"
        "  color: #e85050;"
        "}"
        "QMenu::item:selected {"
        "  background: #3a2020;"
        "  color: #ff6464;"
        "}");
    m_settingsBtn->setMenu(settingsMenu);
    connect(clearLibraryAction, &QAction::triggered,
            this, &StreamPage::onClearLibraryRequested);
    layout->addWidget(m_settingsBtn);

    connect(m_searchInput, &QLineEdit::returnPressed,
            this, &StreamPage::onSearchSubmit);
    connect(m_searchBtn, &QPushButton::clicked,
            this, &StreamPage::onSearchSubmit);
    connect(m_addonsBtn, &QPushButton::clicked,
            this, &StreamPage::showAddonManager);
    connect(m_catalogBtn, &QPushButton::clicked,
            this, &StreamPage::onCatalogBtnClicked);

    // Live-search debounce REMOVED 2026-04-25 per Hemanth: even at 800 ms
    // (raised from 300 â†’ 800 ms on 2026-04-20 for the same reason) the
    // search fired during natural mid-typing pauses while Hemanth was
    // still composing his query. Search now only fires on Enter / Search
    // button / history-row click. textChanged is still wired so paste-kind
    // detection + history-dropdown show/hide-on-empty stay live; it just
    // no longer arms a deferred search.
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &StreamPage::onSearchTextChanged);

    // Phase 4 Batch 4.2 â€” search history. Load persisted list, build
    // dropdown frame, install event filter on input for focus tracking.
    loadSearchHistory();
    buildSearchHistoryDropdown();
    m_searchInput->installEventFilter(this);
}

void StreamPage::buildBrowseLayer()
{
    m_browseLayer = new QWidget(this);
    auto* layerLayout = new QVBoxLayout(m_browseLayer);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    layerLayout->setSpacing(0);

    m_browseScroll = new QScrollArea(m_browseLayer);
    m_browseScroll->setFrameShape(QFrame::NoFrame);
    m_browseScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browseScroll->setWidgetResizable(true);

    m_scrollHome = new QWidget();
    m_scrollLayout = new QVBoxLayout(m_scrollHome);
    m_scrollLayout->setContentsMargins(20, 0, 20, 20);
    m_scrollLayout->setSpacing(24);

    m_scrollLayout->addWidget(m_searchBarFrame);

    // Home board: continue-watching strip + N catalog rows (Phase 3 Batch 3.2).
    // Phase 2 Batch 2.2 â€” m_metaAggregator plumbed through so the
    // StreamContinueStrip can resolve next-unwatched episodes for series
    // whose most-recent progress entry is finished.
    m_homeBoard = new tankostream::stream::StreamHomeBoard(
        m_bridge, m_library, m_addonRegistry, m_metaAggregator, m_scrollHome);
    m_scrollLayout->addWidget(m_homeBoard);

    m_continueStrip = m_homeBoard->continueStrip();
    connect(m_continueStrip, &StreamContinueStrip::playRequested, this,
        [this](const QString& imdbId, int season, int episode) {
            QString mediaType = (season == 0 && episode == 0) ? "movie" : "series";
            // Continue-strip clicks used to open a modal dialog with sources;
            // under the inline-source-pane UX we have to navigate to the
            // detail view so the right-pane source list is visible when the
            // streams populate. Without this, clicking continue-watching
            // fires onPlayRequested â†’ loads streams â†’ populates m_detailView
            // which is not currently shown â†’ user sees nothing.
            if (m_detailView && m_detailView->currentImdb() != imdbId) {
                // THEATRE_DOWNLOAD_OVERHAUL stale-panel fix v2 2026-05-17 -
                // dismiss panel on show transition (currentImdb != imdbId).
                dismissTheatreDownloadPanelIfOpen();
                m_detailView->showEntry(imdbId, season, episode);
            }
            m_mainStack->setCurrentIndex(1);
            // For movies, showEntry's movie-branch already emits playRequested
            // back into us â€” skip to avoid double-loading streams. For series,
            // showEntry waits on episode-click, so we explicitly fire with the
            // specific episode context from the continue-strip click.
            if (mediaType != QLatin1String("movie")) {
                onPlayRequested(imdbId, mediaType, season, episode);
            }
        });

    // Stream library UX rework 2026-04-15 â€” StreamHomeBoard no longer emits
    // metaActivated (featured-row tile clicks deleted). The only entry
    // points into showDetail for non-library titles are now:
    //   - StreamSearchWidget::metaActivated (search results)
    //   - CatalogBrowseScreen::metaActivated (explicit catalog browse)
    //   - StreamLibraryLayout / Continue strip for library-resident titles.

    m_homeBoard->refresh();

    // Library grid
    m_libraryLayout = new StreamLibraryLayout(m_bridge, m_library, m_scrollHome);
    m_libraryLayout->refresh();
    m_scrollLayout->addWidget(m_libraryLayout, 1);

    connect(m_library, &StreamLibrary::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);

    m_browseScroll->setWidget(m_scrollHome);
    layerLayout->addWidget(m_browseScroll, 1);

    // Search results overlay
    m_searchWidget = new StreamSearchWidget(m_metaAggregator, m_library, m_browseLayer);
    m_searchWidget->hide();
    layerLayout->addWidget(m_searchWidget, 1);

    connect(m_searchWidget, &StreamSearchWidget::backRequested, this, &StreamPage::goBack);
    connect(m_searchWidget, &StreamSearchWidget::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);
    // Phase 1 Batch 1.2 â€” search-tile click opens the detail view with the
    // result's MetaItemPreview. Previously the click toggled library add/
    // remove; that moved into the detail view's Add-to-Library button.
    connect(m_searchWidget, &StreamSearchWidget::metaActivated, this,
        [this](const tankostream::addon::MetaItemPreview& preview) {
            showDetail(preview);
        });
}

// â”€â”€â”€ Search â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void StreamPage::onSearchSubmit()
{
    QString query = m_searchInput->text().trimmed();
    if (query.isEmpty())
        return;

    hideSearchHistoryDropdown();

    // Phase 4 Batch 4.3 â€” route URL-paste kinds to their action instead
    // of the search path. PasteKind is refreshed on every textChanged,
    // so by the time Enter / Search button fires it reflects the full
    // current input. Fresh re-detect here as a defensive sync in case
    // textChanged was skipped (clipboard paste + immediate Enter).
    const PasteKind kind = detectPasteKind(query);
    if (kind != PasteKind::None) {
        handlePasteAction(kind, query);
        return;
    }

    pushSearchHistory(query);

    // STREAM_NAV_BACK_STACK 2026-05-06 â€” push a Search entry onto the
    // nav stack so Back from a Detail view (entered via search-result
    // click) returns to Search. Re-submitting from an existing Search
    // overwrites the current entry's query (no duplicate stack layer)
    // so the depth stays clean.
    if (!m_navStack.isEmpty() && m_navStack.top().kind == NavEntry::Kind::Search) {
        m_navStack.top().searchQuery = query;
    } else {
        // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) — record the Search layer.
        if (!m_inLayerRestore) {
            QJsonObject blob;
            blob[QStringLiteral("searchQuery")] = query;
            emit enteredLayer(makeStreamLayer(QStringLiteral("search"), blob));
        }
        NavEntry e;
        e.kind = NavEntry::Kind::Search;
        e.searchQuery = query;
        m_navStack.push(e);
    }

    showSearchResults();   // visual transition (hide browseScroll, show searchWidget)
    setSearchBusy(true);
    m_searchWidget->search(query);
}

// Phase 4 Batch 4.1 â€” search input observer + spinner handlers.
//
// Live-search debounce was removed 2026-04-25 per Hemanth. This handler now
// only updates UI affordances tied to the input's current text â€” it never
// kicks off a search. Search execution is gated entirely on Enter, the
// Search button, or a history-row click (all routed through onSearchSubmit).

void StreamPage::onSearchTextChanged(const QString& text)
{
    const QString trimmed = text.trimmed();

    // Phase 4 Batch 4.3 â€” re-detect paste kind on every keystroke. The
    // Search button label reflects the intended action; Enter uses it.
    const PasteKind newKind = detectPasteKind(trimmed);
    if (newKind != m_pasteKind) {
        m_pasteKind = newKind;
        applyPasteKindToSearchButton(m_pasteKind);
    }

    if (trimmed.isEmpty()) {
        // Clearing restores the home/browse layer + offers the history
        // dropdown if the input still has focus (Batch 4.2).
        setSearchBusy(false);
        if (m_searchWidget && m_searchWidget->isVisible()) {
            showBrowse();
        }
        if (m_searchInput && m_searchInput->hasFocus()) {
            showSearchHistoryDropdown();
        }
        return;
    }

    // Any non-empty text hides the history dropdown (Batch 4.2) â€” the user
    // is typing a new query, not browsing history.
    hideSearchHistoryDropdown();
}

void StreamPage::setSearchBusy(bool busy)
{
    if (!m_searchBusy) return;
    m_searchBusy->setVisible(busy);
}

// â”€â”€â”€ Phase 4 Batch 4.3 â€” URL / magnet paste handling â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

StreamPage::PasteKind StreamPage::detectPasteKind(const QString& input) const
{
    const QString s = input.trimmed();
    if (s.isEmpty()) return PasteKind::None;

    // Magnet URI â€” regex anchored per RFC 2056 style: magnet:?xt=urn:btih:HASH
    static const QRegularExpression kMagnetRe(
        QStringLiteral("^magnet:\\?xt=urn:btih:[A-Za-z0-9]{32,}"),
        QRegularExpression::CaseInsensitiveOption);
    if (kMagnetRe.match(s).hasMatch()) return PasteKind::Magnet;

    // Stremio addon deep link.
    if (s.startsWith(QStringLiteral("stremio://"), Qt::CaseInsensitive))
        return PasteKind::AddonManifest;

    // HTTP / HTTPS â€” branch further by path shape.
    const QUrl url = QUrl::fromUserInput(s);
    if (!url.isValid() || url.scheme().isEmpty()) return PasteKind::None;
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        return PasteKind::None;

    const QString path = url.path().toLower();

    // Addon manifest convention: URL ends in /manifest.json. Stremio
    // documents this explicitly for published addons.
    if (path.endsWith(QStringLiteral("/manifest.json")))
        return PasteKind::AddonManifest;

    // Direct-video: common container extensions. Matches the TODO's
    // enumeration (mp4, mkv, m3u8) plus a few common siblings.
    static const QStringList kVideoExts = {
        QStringLiteral(".mp4"), QStringLiteral(".m4v"),
        QStringLiteral(".mkv"), QStringLiteral(".webm"),
        QStringLiteral(".mov"), QStringLiteral(".avi"),
        QStringLiteral(".m3u8"), QStringLiteral(".ts"),
    };
    for (const QString& ext : kVideoExts) {
        if (path.endsWith(ext)) return PasteKind::DirectVideo;
    }

    // Plain HTTP without a recognized path shape â€” ambiguous between
    // "arbitrary search query that happens to look like a URL" and
    // "an unknown addon / media endpoint". Per the TODO's "only if the
    // full value is parseable as a URL â€” regex guarded" â€” we stay
    // conservative and treat unrecognized HTTP as a text search.
    return PasteKind::None;
}

void StreamPage::applyPasteKindToSearchButton(PasteKind kind)
{
    if (!m_searchBtn) return;
    switch (kind) {
        case PasteKind::Magnet:
        case PasteKind::DirectVideo:
            m_searchBtn->setText(tr("Play this stream"));
            break;
        case PasteKind::AddonManifest:
            m_searchBtn->setText(tr("Install this addon"));
            break;
        case PasteKind::None:
        default:
            m_searchBtn->setText(tr("Search"));
            break;
    }
}

void StreamPage::handlePasteAction(PasteKind kind, const QString& input)
{
    const QString s = input.trimmed();
    if (s.isEmpty()) return;

    switch (kind) {
        case PasteKind::AddonManifest: {
            // Pre-fill the URL into AddAddonDialog and exec(). Reusing the
            // dialog's existing install flow â€” install signals still fire
            // through AddonRegistry as in the regular "Addons â†’ Add" path.
            if (!m_addonRegistry) return;
            AddAddonDialog dlg(m_addonRegistry, this);
            dlg.setPrefilledUrl(s);
            dlg.exec();
            // Clear the search field so the button label resets; also
            // clears the paste-kind state via the textChanged chain.
            if (m_searchInput) m_searchInput->clear();
            return;
        }
        case PasteKind::Magnet:
        case PasteKind::DirectVideo: {
            // THEATRE_DOWNLOAD_ONLY P1.1 (2026-05-29) â€” ad-hoc paste-to-play
            // dropped. This built a synthetic Stream and streamed it through
            // the now-removed StreamPlayerController. Both forms are explicit
            // non-goals of the download-only pivot: a pasted magnet is an
            // un-indexed ad-hoc download with no series/episode identity, and
            // direct-URL playback is dropped (spec Â§4). Clear the field so the
            // button label resets, then no-op.
            if (m_searchInput) m_searchInput->clear();
            return;
        }
        case PasteKind::None:
        default:
            return;
    }
}

// â”€â”€â”€ Phase 4 Batch 4.2 â€” search history â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void StreamPage::loadSearchHistory()
{
    QSettings s;
    m_searchHistory = s.value(QStringLiteral("stream/searchHistory")).toStringList();
    // Defensive clamp in case old settings data exceeds the cap.
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
}

void StreamPage::saveSearchHistory()
{
    QSettings s;
    s.setValue(QStringLiteral("stream/searchHistory"), m_searchHistory);
}

void StreamPage::pushSearchHistory(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;
    // Dedup: remove any prior exact-match so the new one takes the top slot.
    m_searchHistory.removeAll(q);
    m_searchHistory.prepend(q);
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
    saveSearchHistory();
}

void StreamPage::removeSearchHistoryEntry(const QString& query)
{
    m_searchHistory.removeAll(query);
    saveSearchHistory();
    // If the dropdown is visible, rebuild its rows on the fly.
    if (m_searchHistoryDropdown && m_searchHistoryDropdown->isVisible()) {
        showSearchHistoryDropdown();   // rebuilds via the same code path
    }
}

void StreamPage::clearSearchHistory()
{
    if (m_searchHistory.isEmpty()) return;
    m_searchHistory.clear();
    saveSearchHistory();
    // Empty history â†’ dropdown self-hides on next show, so just hide now.
    hideSearchHistoryDropdown();
}

void StreamPage::buildSearchHistoryDropdown()
{
    m_searchHistoryDropdown = new QFrame(this);
    m_searchHistoryDropdown->setObjectName("StreamSearchHistory");
    m_searchHistoryDropdown->setStyleSheet(
        "#StreamSearchHistory { background: #1b1b1b;"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px; }");
    m_searchHistoryDropdown->hide();

    auto* outer = new QVBoxLayout(m_searchHistoryDropdown);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(0);

    m_searchHistoryList = new QWidget(m_searchHistoryDropdown);
    auto* listLayout = new QVBoxLayout(m_searchHistoryList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    outer->addWidget(m_searchHistoryList);

    // Delayed-hide timer â€” gives click-on-row events time to register
    // before focus-out hides the dropdown.
    m_searchHistoryHideTimer = new QTimer(this);
    m_searchHistoryHideTimer->setSingleShot(true);
    m_searchHistoryHideTimer->setInterval(150);
    connect(m_searchHistoryHideTimer, &QTimer::timeout, this, [this]() {
        if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
    });
}

void StreamPage::positionSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchInput) return;
    // Map the input's top-left in StreamPage coords, then position the
    // dropdown directly below. Width matches input; height is content-driven
    // with a reasonable max (10 rows Ã— ~28px â‰ˆ 280 + padding).
    const QPoint topLeft =
        m_searchInput->mapTo(this, QPoint(0, m_searchInput->height() + 2));
    m_searchHistoryDropdown->setGeometry(
        topLeft.x(), topLeft.y(), m_searchInput->width(),
        m_searchHistoryDropdown->sizeHint().height());
}

void StreamPage::showSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchHistoryList) return;
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();

    // Clear old rows.
    auto* layout = qobject_cast<QVBoxLayout*>(m_searchHistoryList->layout());
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (m_searchHistory.isEmpty()) {
        // No history yet â€” hide instead of showing an empty frame.
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(m_searchHistory.size(), kMaxSearchHistory);
    const char* kRowBtnStyle =
        "QPushButton { background: transparent; color: #d0d0d0; border: none;"
        "  text-align: left; padding: 6px 10px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); }";
    const char* kRemoveBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.45);"
        "  border: none; font-size: 14px; padding: 0 10px; }"
        "QPushButton:hover { color: #fff; }";
    const char* kClearAllBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.55);"
        "  border: none; text-align: left; padding: 6px 10px;"
        "  font-size: 11px; font-weight: 500; letter-spacing: 0.4px; }"
        "QPushButton:hover { color: #fff; }";

    for (int i = 0; i < rows; ++i) {
        const QString q = m_searchHistory.at(i);

        auto* row = new QWidget(m_searchHistoryList);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* queryBtn = new QPushButton(q, row);
        queryBtn->setCursor(Qt::PointingHandCursor);
        queryBtn->setStyleSheet(kRowBtnStyle);
        queryBtn->setFocusPolicy(Qt::NoFocus);
        connect(queryBtn, &QPushButton::clicked, this, [this, q]() {
            // Fill the input + fire the search immediately (Enter-equivalent
            // path). onSearchSubmit pushes to history (no-op on dedup) and
            // hides the dropdown.
            if (m_searchInput) {
                m_searchInput->setText(q);
            }
            onSearchSubmit();
        });
        rowLayout->addWidget(queryBtn, 1);

        auto* removeBtn = new QPushButton(QStringLiteral("\u00D7"), row);   // Ã—
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setStyleSheet(kRemoveBtnStyle);
        removeBtn->setFocusPolicy(Qt::NoFocus);
        removeBtn->setToolTip(tr("Remove from history"));
        connect(removeBtn, &QPushButton::clicked, this, [this, q]() {
            removeSearchHistoryEntry(q);
        });
        rowLayout->addWidget(removeBtn);

        layout->addWidget(row);
    }

    // Footer: "Ã— Clear search history" wipes the entire list. Single visible
    // affordance per Hemanth's 2026-04-25 ask ("there isn't an option to
    // clear search history"). Per-entry Ã— on each row above is preserved.
    // Hairline divider above so the footer reads as a separate action,
    // not an additional history item.
    auto* divider = new QFrame(m_searchHistoryList);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        "QFrame { border: none; background: rgba(255,255,255,0.08);"
        "  max-height: 1px; min-height: 1px; }");
    layout->addWidget(divider);

    auto* clearAllBtn = new QPushButton(
        QStringLiteral("\u00D7  Clear search history"), m_searchHistoryList);
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    clearAllBtn->setStyleSheet(kClearAllBtnStyle);
    clearAllBtn->setFocusPolicy(Qt::NoFocus);
    connect(clearAllBtn, &QPushButton::clicked,
            this, &StreamPage::clearSearchHistory);
    layout->addWidget(clearAllBtn);

    m_searchHistoryDropdown->adjustSize();
    positionSearchHistoryDropdown();
    m_searchHistoryDropdown->show();
    m_searchHistoryDropdown->raise();
}

void StreamPage::hideSearchHistoryDropdown()
{
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();
    if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
}

bool StreamPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_searchInput) {
        if (event->type() == QEvent::FocusIn) {
            // Show history only if input is empty â€” otherwise we'd obscure
            // the user's live typing with irrelevant past queries.
            if (m_searchInput->text().trimmed().isEmpty()) {
                showSearchHistoryDropdown();
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Delayed hide â€” if focus moved to a dropdown row's button,
            // the click handler runs first via the 150ms timer.
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” depth-first back navigation.
// Pop one entry off m_navStack; re-show the previous entry without
// pushing. Stack-bottom (Library) is a no-op re-show â€” currently
// stays on Library; future enhancement could propagate up to
// MainWindow if cross-mode back is wanted.
void StreamPage::goBack()
{
    // THEATRE_DOWNLOAD_OVERHAUL stale-panel fix v2 2026-05-17 - dismiss panel
    // when user exits the current detail view via Back so it does not
    // persist across the navigation transition (panel is StreamPage-owned
    // so detail-view teardown alone does not affect it).
    dismissTheatreDownloadPanelIfOpen();
    if (!m_inLayerRestore) emit exitedLayer();
    if (m_navStack.size() <= 1) {
        // STREAM_CATALOG_BACK_NAVSTACK_SEED_FIX 2026-05-06 â€” Hemanth-reported
        // "the back button besides catalog is not working now": when user
        // opens Tankoban â†’ Stream tab â†’ clicks Catalog without ever pushing
        // Browse first (the ctor doesn't seed it; only show*() slots push),
        // navStack ends up [CatalogBrowse] only. Original guard only caught
        // the empty-stack case, so size=1 with non-Browse top was a no-op
        // (re-showed the same view, looked like Back was broken). Fix:
        // unconditional normalize to Browse on top-of-stack escape â€” clears
        // whatever's there and seeds Browse fresh.
        m_navStack.clear();
        NavEntry e;
        e.kind = NavEntry::Kind::Browse;
        m_navStack.push(e);
        showEntryRaw(m_navStack.top());
        return;
    }
    m_navStack.pop();
    showEntryRaw(m_navStack.top());
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” restore the search overlay visual
// state without re-running the network fetch. m_searchWidget caches its
// query + results widgets across hide/show cycles, so a simple
// hide-browse + show-search is sufficient. Used by showEntryRaw on
// Kind::Search restore; onSearchSubmit calls it explicitly before
// invoking m_searchWidget->search() to actually run a fresh query.
void StreamPage::showSearchResults()
{
    m_mainStack->setCurrentIndex(0);
    m_browseScroll->hide();
    m_searchWidget->show();
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” internal view-swap helper. Does
// the visual transition implied by NavEntry::kind WITHOUT pushing onto
// m_navStack. show* slots push then call this; goBack pops then calls
// this; the player-exit restore path calls this directly.
void StreamPage::showEntryRaw(const NavEntry& entry)
{
    using Kind = NavEntry::Kind;
    switch (entry.kind) {
    case Kind::Browse:
        cancelBulkSeasonDownload();
        m_mainStack->setCurrentIndex(0);
        m_searchWidget->hide();
        m_browseScroll->show();
        // Phase 4 Batch 4.1 â€” defensive spinner reset when navigating
        // away from search (covers the "user hit Back / Esc while a
        // search was in flight" case â€” the later catalogResults lands
        // but the UI is already elsewhere).
        setSearchBusy(false);
        // Phase 4 Batch 4.2 â€” defensive history-dropdown dismissal.
        hideSearchHistoryDropdown();
        // 2026-04-15 â€” cancel any pending seek-pre-gate retry. User
        // navigated away; no more launchPlayer should fire.
        m_session.seekRetry.reset();
        // Invalidate any in-flight play context so a late streamsReady
        // arrival followed by an accidental card-click can't dispatch
        // playback for the title the user just backed away from.
        m_session.pending.valid = false;
        cancelAutoLaunch();
        // Phase 2 Batch 2.5 â€” if a next-episode overlay was pending,
        // clear it; user explicitly navigated away.
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        if (m_homeBoard)
            m_homeBoard->refresh();
        if (m_libraryLayout)
            m_libraryLayout->refresh();
        break;

    case Kind::CatalogBrowse:
        if (!m_catalogBrowse) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        m_catalogBrowse->open(entry.catalogAddonId,
                              entry.catalogType,
                              entry.catalogId);
        m_mainStack->setCurrentIndex(4);
        break;

    case Kind::Detail:
        if (!m_detailView) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        // THEATRE_DOWNLOAD_OVERHAUL stale-panel fix v2 2026-05-17 - dismiss
        // panel on restoreLayer Detail-case so a prior show's pack list does
        // not carry across nav-history restoration.
        dismissTheatreDownloadPanelIfOpen();
        if (entry.detailHasPreview) {
            m_detailView->showEntry(entry.detailPreview.id,
                                    entry.detailPreselectSeason,
                                    entry.detailPreselectEpisode,
                                    entry.detailPreview);
        } else {
            m_detailView->showEntry(entry.detailImdbId);
        }
        m_mainStack->setCurrentIndex(1);
        break;

    case Kind::AddonManager:
        if (!m_addonManager) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        m_addonManager->refresh();
        m_mainStack->setCurrentIndex(3);
        break;

    case Kind::Search:
        showSearchResults();
        break;
    }
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” restore pre-player view if snapshot
// was captured at launch; otherwise fall back to library (legacy
// default). Called from onStreamStopped UserEnd, the defensive 3s
// post-close timer, and onNextEpisodeCancel case (a).
void StreamPage::restorePlayerExitView()
{
    if (m_beforePlayerEntry.has_value()) {
        const NavEntry entry = *m_beforePlayerEntry;
        m_beforePlayerEntry.reset();
        showEntryRaw(entry);
        return;
    }
    // System-initiated fallback when no pre-player snapshot exists.
    // Pass emitNav=false so the global Back/Forward stack doesn't
    // record a spurious Browse entry the user didn't navigate to.
    showBrowse(/*emitNav=*/false);
}

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- restore target layer without
// emitting enteredLayer/exitedLayer (called by MainWindow::onLayerRestoreRequested
// when the controller fires layerRestoreRequested for pageId="stream"). Uses
// QScopedValueRollback to suppress the enteredLayer guard on every show* path
// reached via showEntryRaw during the restore.
void StreamPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    QScopedValueRollback<bool> rollback(m_inLayerRestore, true);
    NavEntry entry;
    using Kind = NavEntry::Kind;
    const QString kind = target.kind;
    const QJsonObject blob = target.stateBlob;
    if (kind == "browse") {
        entry.kind = Kind::Browse;
    } else if (kind == "catalogBrowse") {
        if (!m_catalogBrowse) return;
        entry.kind = Kind::CatalogBrowse;
        entry.catalogAddonId = blob.value("catalogAddonId").toString();
        entry.catalogType    = blob.value("catalogType").toString();
        entry.catalogId      = blob.value("catalogId").toString();
        entry.catalogTitle   = blob.value("catalogTitle").toString();
    } else if (kind == "detail") {
        if (!m_detailView) return;
        const QString imdb = blob.value("detailImdbId").toString();
        if (imdb.isEmpty()) return;
        entry.kind = Kind::Detail;
        entry.detailImdbId = imdb;
        entry.detailHasPreview = false;
        entry.detailPreselectSeason  = blob.value("detailPreselectSeason").toInt(-1);
        entry.detailPreselectEpisode = blob.value("detailPreselectEpisode").toInt(-1);
    } else if (kind == "addonManager") {
        if (!m_addonManager) return;
        entry.kind = Kind::AddonManager;
    } else if (kind == "search") {
        entry.kind = Kind::Search;
        entry.searchQuery = blob.value("searchQuery").toString();
    } else {
        return;
    }
    showEntryRaw(entry);
}

void StreamPage::resetToRoot()
{
    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — public forwarder
    // invoked by MainWindow::resetActivePageToRoot when the user clicks
    // the Theatre topbar pill while already on this page. emitNav=true so
    // the pill-reset records a NavHistory entry (consistent with cross-mode
    // pill clicks); Phase 1+ may refine to a stack-clearing semantic.
    showBrowse(/*emitNav=*/true);
}

void StreamPage::showBrowse(bool emitNav)
{
    // THEATRE_DOWNLOAD_OVERHAUL stale-panel fix v2 2026-05-17 - dismiss panel
    // when returning to the library home (any showBrowse path) so the panel
    // never persists outside of a detail-view context.
    dismissTheatreDownloadPanelIfOpen();
    // STREAM_NAV_BACK_STACK 2026-05-06 â€” Library is the stack bottom.
    // showBrowse explicitly resets the stack to a clean Browse-only
    // state. Called on Stream-mode entry, on legitimate library-home
    // navigation (e.g. nav-bar Library button), and as the legacy
    // fallback when no pre-player snapshot exists.
    // GLOBAL_NAV_HISTORY Task 14 — emit on USER-initiated Library-home
    // transitions (default emitNav=true). System-initiated callers
    // (restorePlayerExitView fallback, etc.) pass emitNav=false so the
    // global stack doesn't record a spurious nav the user didn't make.
    if (emitNav) {
        if (!m_inLayerRestore)
            emit enteredLayer(makeStreamLayer(QStringLiteral("browse")));
    }
    m_navStack.clear();
    NavEntry e;
    e.kind = NavEntry::Kind::Browse;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showAddonManager()
{
    if (!m_inLayerRestore)
        emit enteredLayer(makeStreamLayer(QStringLiteral("addonManager")));
    NavEntry e;
    e.kind = NavEntry::Kind::AddonManager;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showCatalogBrowse(const QString& addonId, const QString& type,
                                   const QString& catalogId, const QString& title)
{
    if (!m_inLayerRestore) {
        QJsonObject blob;
        blob[QStringLiteral("catalogAddonId")] = addonId;
        blob[QStringLiteral("catalogType")]    = type;
        blob[QStringLiteral("catalogId")]      = catalogId;
        blob[QStringLiteral("catalogTitle")]   = title;
        emit enteredLayer(makeStreamLayer(QStringLiteral("catalogBrowse"), blob));
    }
    NavEntry e;
    e.kind = NavEntry::Kind::CatalogBrowse;
    e.catalogAddonId = addonId;
    e.catalogType = type;
    e.catalogId = catalogId;
    e.catalogTitle = title;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::onCatalogBtnClicked()
{
    // Stream library UX rework 2026-04-15 â€” open catalog browse with no
    // preselection. CatalogBrowseScreen::open with empty args calls
    // selectAddon/selectCatalog with empty strings â€” the for-loop lookups
    // find no match, so the combos stay at index 0 (first addon, first
    // catalog) after rebuildSelectors populates them. reload() then kicks
    // a fetch for that default pair. On subsequent clicks, the screen
    // persists combo state via its own mechanisms so the user returns to
    // whatever they had selected.
    showCatalogBrowse(QString(), QString(), QString(), QString());
}

void StreamPage::onClearLibraryRequested()
{
    // THEATRE_CLEANUP F2 (2026-05-22) — gear-menu Clear Library handler.
    // Per Hemanth Q3 2026-05-22 ~2:50pm IST: two-step confirmation. First
    // a warning modal; if Yes, a type-"clear" input modal. Only on
    // verbatim match does the destructive op fire.
    if (!m_library) return;

    // Step 1: warning modal. List the consequences explicitly so the
    // user can back out before the second prompt.
    const auto warnResult = QMessageBox::warning(
        this,
        tr("Clear Theatre Library?"),
        tr("This will permanently delete your entire Theatre library:\n\n"
           "  • Every added show + movie\n"
           "  • Every downloaded video file on disk\n"
           "  • Every in-flight download (cancelled + files removed)\n"
           "  • Per-episode download tracking\n\n"
           "Your Comics + Books libraries, Tankorent search history, "
           "indexer settings, and user preferences are NOT affected.\n\n"
           "Continue?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (warnResult != QMessageBox::Yes) return;

    // Step 2: type-"clear" confirmation. Case-insensitive verbatim match.
    bool ok = false;
    const QString typed = QInputDialog::getText(
        this,
        tr("Confirm Clear Library"),
        tr("Type 'clear' (lowercase) to confirm:"),
        QLineEdit::Normal,
        QString(),
        &ok);
    if (!ok) return;
    if (typed.trimmed().toLower() != QLatin1String("clear")) {
        QMessageBox::information(
            this,
            tr("Clear Library cancelled"),
            tr("You did not type 'clear' — nothing was deleted."));
        return;
    }

    // Both gates passed. Fire the destructive op. StreamLibrary::clear()
    // handles the full cascade (entries + download-index eviction + every
    // stream-bulk cohort cancelled with deleteFiles=true). Returns the
    // count of entries cleared so we can surface a confirmation toast.
    const int cleared = m_library->clear();

    QMessageBox::information(
        this,
        tr("Theatre Library cleared"),
        cleared == 0
            ? tr("Your library was already empty — nothing to delete.")
            : tr("Cleared %1 show/movie %2 + cancelled active downloads + "
                 "deleted downloaded files. Your Theatre library is now "
                 "empty.").arg(cleared).arg(cleared == 1 ? tr("entry") : tr("entries")));
}

void StreamPage::showDetail(const QString& imdbId)
{
    // Idempotent: the library/home-board wires BOTH tileSingleClicked and
    // tileDoubleClicked to this slot, and Qt delivers BOTH for a double-click
    // gesture (first mousePress â†’ single-click, then the second press becomes
    // a dedicated double-click event). The second call would otherwise reset
    // the detail state + re-fire the meta fetch just as the first call's
    // response lands AND would push a duplicate Detail entry onto m_navStack.
    if (m_mainStack->currentIndex() == 1 && m_detailView
        && m_detailView->currentImdb() == imdbId) {
        return;
    }
    if (!m_inLayerRestore) {
        QJsonObject blob;
        blob[QStringLiteral("detailImdbId")]           = imdbId;
        blob[QStringLiteral("detailPreselectSeason")]  = -1;
        blob[QStringLiteral("detailPreselectEpisode")] = -1;
        emit enteredLayer(makeStreamLayer(QStringLiteral("detail"), blob));
    }
    NavEntry e;
    e.kind = NavEntry::Kind::Detail;
    e.detailImdbId = imdbId;
    e.detailHasPreview = false;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showDetail(const tankostream::addon::MetaItemPreview& preview,
                            int preselectSeason,
                            int preselectEpisode)
{
    // Phase 1 Batch 1.1: catalog/home/search tile path. Same idempotency
    // guard as the imdbId overload â€” repeat clicks on the same tile don't
    // re-fire state resets. The preview is passed through as `previewHint`
    // so the detail view paints title/year/poster/description from the
    // tile's metadata without needing the title to be in the library.
    if (preview.id.isEmpty()) return;
    if (m_mainStack->currentIndex() == 1 && m_detailView
        && m_detailView->currentImdb() == preview.id) {
        return;
    }
    if (!m_inLayerRestore) {
        QJsonObject blob;
        blob[QStringLiteral("detailImdbId")]           = preview.id;
        blob[QStringLiteral("detailPreselectSeason")]  = preselectSeason;
        blob[QStringLiteral("detailPreselectEpisode")] = preselectEpisode;
        emit enteredLayer(makeStreamLayer(QStringLiteral("detail"), blob));
    }
    NavEntry e;
    e.kind = NavEntry::Kind::Detail;
    e.detailImdbId = preview.id;
    e.detailHasPreview = true;
    e.detailPreview = preview;
    e.detailPreselectSeason = preselectSeason;
    e.detailPreselectEpisode = preselectEpisode;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::onPlayRequested(const QString& imdbId, const QString& mediaType,
                                  int season, int episode)
{
    // THEATRE_DOWNLOAD_SIMPLIFY (2026-05-29): a SERIES episode click is a
    // download/play intent → funnel to download-or-play-local (owned → play
    // from disk; otherwise silent auto-pick best 1080p + download). No source
    // picker. MOVIES fall through to the existing source-load below: movie
    // detail-open auto-fires playRequested(movie,0,0), so auto-downloading
    // here would wrongly download on mere open — the movie's own Download
    // button handles movie downloads.
    if (mediaType != QLatin1String("movie")) {
        qInfo().noquote() << "[auto-dl] series click -> beginPlayOrDownload imdb="
                          << imdbId << "s" << season << "e" << episode;
        beginPlayOrDownload(imdbId, mediaType, season, episode, nullptr);
        return;
    }

    // Build episode key for choice persistence
    QString epKey = (mediaType == "movie")
        ? StreamProgress::movieKey(imdbId)
        : StreamProgress::episodeKey(imdbId, season, episode);

    // Stash this play context for onSourceActivated to consume when the
    // user clicks a card. Replaces what the dialog used to keep alive
    // between exec() and accept(). 'valid' guards against late streamsReady
    // arrivals after the user backed out of the detail view. Phase 1 Batch
    // 1.3 â€” beginSession stamps the new generation + clears prior session
    // state (stopping the countdown timer + disconnecting aggregators) in
    // one boundary, so a detail re-entry mid-prefetch doesn't leak stale
    // async closures into the new session.
    beginSession(epKey,
                 PendingPlay{imdbId, mediaType, season, episode, epKey, true},
                 QStringLiteral("onPlayRequested"));

    // Check if we have a saved choice for this episode â€” if so, build the
    // picker-key shape so the StreamSourceList can highlight the matching
    // card on populate. (Auto-launch on saved choice lands in Batch 2.4; for
    // now the highlight is a one-click visual hint.)
    QJsonObject savedChoice = StreamChoices::loadChoice(epKey);
    QString savedChoiceKey;
    if (!savedChoice.isEmpty()) {
        const QString sourceKind = savedChoice.value("sourceKind").toString();
        const QString addonId    = savedChoice.value("addonId").toString();
        const QString hashOrUrl  = (sourceKind == QLatin1String("magnet"))
            ? savedChoice.value("infoHash").toString().toLower()
            : savedChoice.value("directUrl").toString();
        const int fileIndex = savedChoice.value("fileIndex").toInt(-1);
        savedChoiceKey = addonId + QLatin1Char('|')
                       + sourceKind + QLatin1Char('|')
                       + hashOrUrl + QLatin1Char('|')
                       + QString::number(fileIndex);
    }

    // Phase 2 Batch 2.3 â€” if per-episode is empty and this is a series, fall
    // through to the series-level bingeGroup match. Resolved inside the
    // streamsReady lambda because we need the incoming stream list to find
    // the matching card.
    QString seriesBingeGroup;
    qint64  seriesUpdatedAt = 0;
    if (savedChoiceKey.isEmpty()
        && mediaType == QLatin1String("series")) {
        const QJsonObject seriesChoice = StreamChoices::loadSeriesChoice(imdbId);
        seriesBingeGroup = seriesChoice.value("bingeGroup").toString();
        seriesUpdatedAt  = seriesChoice.value("updatedAt").toInteger(0);
    }

    // Phase 2 Batch 2.4 â€” auto-launch eligibility: either the per-episode
    // saved choice OR the per-series saved choice must be within 10 minutes
    // of the last-watched stamp. The streamsReady lambda uses this to decide
    // whether to fire the resume toast + arm m_autoLaunchTimer.
    constexpr qint64 kAutoLaunchWindowMs = 10LL * 60LL * 1000LL;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 episodeUpdatedAt = savedChoice.value("updatedAt").toInteger(0);
    const bool   perEpisodeRecent  = episodeUpdatedAt > 0
                                       && (nowMs - episodeUpdatedAt) < kAutoLaunchWindowMs;
    const bool   perSeriesRecent   = seriesUpdatedAt > 0
                                       && (nowMs - seriesUpdatedAt) < kAutoLaunchWindowMs;
    const bool   autoLaunchEligible = perEpisodeRecent || perSeriesRecent;

    // Reset any in-flight auto-launch before we kick off the new resolve â€”
    // a second onPlayRequested (user clicks another episode mid-countdown)
    // must replace, not stack.
    cancelAutoLaunch();

    // Fetch streams via StreamAggregator, then push into the right pane of
    // StreamDetailView. No modal â€” the cards live inside the detail view
    // and the user clicks one to play (handled by onSourceActivated).
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
               this, nullptr);

    // DOWNLOAD BUG 2026-06-02 — same correlation token as the auto-download
    // path. The play one-shot is armed BEFORE load() runs, so the token is
    // held in a shared_ptr captured by value and filled from the load() return
    // below. The lambda discards any emit whose token != the current load
    // generation (a late streamsReady from a superseded play/source-load).
    auto playToken = std::make_shared<quint64>(0);

    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
        [this, savedChoiceKey, seriesBingeGroup, autoLaunchEligible, playToken](
            const QList<tankostream::addon::Stream>& streams,
            const QHash<QString, QString>& addonsById) {
            if (m_streamAggregator->currentLoadToken() != *playToken)
                return;
            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);

            const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);

            // Resolve the highlight key + retain a pointer to the matched
            // choice for Batch 2.4's auto-launch path. Per-episode wins;
            // fall through to series-level bingeGroup match.
            QString highlightKey = savedChoiceKey;
            const tankostream::stream::StreamPickerChoice* matchedChoice = nullptr;
            if (!highlightKey.isEmpty()) {
                for (const auto& c : choices) {
                    if (tankostream::stream::pickerChoiceKey(c) == highlightKey) {
                        matchedChoice = &c;
                        break;
                    }
                }
            }
            if (!matchedChoice && !seriesBingeGroup.isEmpty()) {
                for (const auto& c : choices) {
                    if (c.stream.behaviorHints.bingeGroup == seriesBingeGroup) {
                        highlightKey   = tankostream::stream::pickerChoiceKey(c);
                        matchedChoice  = &c;
                        break;
                    }
                }
            }

            if (m_detailView) {
                m_detailView->setStreamSources(choices, highlightKey);
            }

            // Phase 2 Batch 2.4 â€” auto-launch DISABLED 2026-04-16 per
            // Hemanth UX call (Phase 1 telemetry session, post One Piece
            // pack regression). The 2-second countdown was too aggressive â€”
            // entering Sources view would fire playback before the user
            // could meaningfully pick a different source. Manual source
            // selection (user clicks a card) still works via the existing
            // setStreamSources path above. The 10-minute eligibility gate +
            // m_autoLaunchTimer infrastructure are preserved in case future
            // UX iteration wants a longer countdown variant; one-line
            // re-enable point is the `if (false &&` guard below â€” flip to
            // restore (with a kAutoLaunchCountdownMs bump in the timer
            // setInterval at the top of buildUI before re-enabling).
            //
            // Suppressed variables keep clean shape for the re-enable diff:
            (void)matchedChoice;
            (void)autoLaunchEligible;
            if (false && matchedChoice && autoLaunchEligible && m_detailView) {
                m_autoLaunchChoice = *matchedChoice;
                m_detailView->showAutoLaunchToast(
                    tr("Resuming with last-used source..."));
                if (m_autoLaunchTimer) m_autoLaunchTimer->start();
            }
        });

    tankostream::stream::StreamLoadRequest req;
    req.type = (mediaType == "movie") ? QStringLiteral("movie") : QStringLiteral("series");
    // THEATRE_ANIME_CATALOG — anime series resolve to a Kitsu id; Torrentio
    // serves their streams via "kitsu:<id>:<absoluteEpisode>" (no season).
    // Falls back to the standard imdb:season:episode id for everything else.
    const int kitsuId = (mediaType != QLatin1String("movie") && m_metaAggregator)
                            ? m_metaAggregator->kitsuIdForSeries(imdbId)
                            : -1;
    if (mediaType == "movie") {
        req.id = imdbId;
    } else if (kitsuId > 0) {
        req.id = QStringLiteral("kitsu:%1:%2").arg(kitsuId).arg(qMax(1, episode));
    } else {
        req.id = imdbId + QLatin1Char(':') + QString::number(qMax(1, season))
                        + QLatin1Char(':') + QString::number(qMax(1, episode));
    }
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
        [this](const QString& addonId, const QString& message) {
            const QString shown = addonId.isEmpty()
                ? message
                : QStringLiteral("[%1] %2").arg(addonId, message);
            if (m_detailView) {
                m_detailView->setStreamSourcesError(
                    QStringLiteral("Failed to fetch sources: ") + shown);
            }
        });

    // DOWNLOAD BUG 2026-06-02 — fill the correlation token captured by the
    // one-shot above so it can discard a stale emit from a superseded load().
    *playToken = m_streamAggregator->load(req);
}

// Phase 2 Batch 2.4 â€” auto-launch orchestration.

void StreamPage::onAutoLaunchFire()
{
    if (m_detailView) m_detailView->hideAutoLaunchToast();
    if (!m_autoLaunchChoice.has_value()) return;
    const auto choice = *m_autoLaunchChoice;
    m_autoLaunchChoice.reset();
    // Same entry point user-click takes â€” keeps persistence + handoff
    // behavior identical between manual and auto-launch flows.
    onSourceActivated(choice);
}

void StreamPage::cancelAutoLaunch()
{
    if (m_autoLaunchTimer) m_autoLaunchTimer->stop();
    m_autoLaunchChoice.reset();
    if (m_detailView) m_detailView->hideAutoLaunchToast();
}

// Phase 2 Batch 2.5 â€” next-episode pre-fetch + overlay orchestration.

void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
                                           int currentSeason, int currentEpisode)
{
    // Skip movies â€” no next episode concept.
    if (currentSeason <= 0 || currentEpisode <= 0) return;
    if (!m_metaAggregator || !m_streamAggregator) return;

    // Use the cached series meta (populated when the user opened detail view
    // for this series). fetchSeriesMeta emits synchronously on cache hit.
    // Re-wire once per fetch to avoid leaking stale connections.
    disconnect(m_metaAggregator,
               &tankostream::stream::MetaAggregator::seriesMetaReady,
               this, nullptr);
    connect(m_metaAggregator,
            &tankostream::stream::MetaAggregator::seriesMetaReady,
            this,
        [this, imdbId, currentSeason, currentEpisode](
            const QString& resolvedImdb,
            const QMap<int, QList<tankostream::stream::StreamEpisode>>& seasons) {
            if (resolvedImdb != imdbId) return;
            // One-shot: disconnect after resolving for this imdb.
            disconnect(m_metaAggregator,
                       &tankostream::stream::MetaAggregator::seriesMetaReady,
                       this, nullptr);

            // Flatten + sort.
            QList<QPair<int, int>> episodesInOrder;
            for (auto it = seasons.constBegin(); it != seasons.constEnd(); ++it) {
                for (const auto& ep : it.value()) {
                    episodesInOrder.append({it.key(), ep.episode});
                }
            }
            std::sort(episodesInOrder.begin(), episodesInOrder.end());

            // Build a synthetic allProgress that marks the current episode as
            // finished, so nextUnwatchedEpisode correctly advances past it
            // even though we're only at 95% (the real "finished" stamp
            // arrives at user-close or at 90% boundary, but we want the
            // prefetch to fire NOW).
            QJsonObject allProgress = m_bridge->allProgress("stream");
            const QString curKey = StreamProgress::episodeKey(
                imdbId, currentSeason, currentEpisode);
            QJsonObject curState = allProgress.value(curKey).toObject();
            curState["finished"] = true;
            allProgress[curKey] = curState;
            StreamProgress::invalidateNextUnwatchedCache(imdbId);

            const QPair<int, int> next =
                StreamProgress::nextUnwatchedEpisode(imdbId, episodesInOrder, allProgress);
            if (next.first <= 0 || next.second <= 0) return;  // series finished

            // Stash the prefetch context and fire a stream load.
            NextEpisodePrefetch prefetch;
            prefetch.imdbId   = imdbId;
            prefetch.season   = next.first;
            prefetch.episode  = next.second;
            prefetch.epKey    = StreamProgress::episodeKey(imdbId, next.first, next.second);
            m_session.nextPrefetch    = prefetch;

            // Reuse m_streamAggregator â€” the current episode's streamsReady
            // has already fired (we're at 95%), so nothing in-flight. Its
            // load() resets internal state cleanly.
            disconnect(m_streamAggregator,
                       &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);
            // DOWNLOAD BUG 2026-06-02 — same correlation token guard as the
            // play/auto-download paths. The prefetch one-shot is armed before
            // its load(), so the token rides in a shared_ptr filled from the
            // load() return below; a stale emit from a superseded load() is
            // discarded instead of feeding the wrong show into the prefetch.
            auto prefetchToken = std::make_shared<quint64>(0);
            connect(m_streamAggregator,
                    &tankostream::stream::StreamAggregator::streamsReady, this,
                [this, prefetchToken](const QList<tankostream::addon::Stream>& streams,
                       const QHash<QString, QString>& addonsById) {
                    if (m_streamAggregator->currentLoadToken() != *prefetchToken)
                        return;
                    disconnect(m_streamAggregator,
                               &tankostream::stream::StreamAggregator::streamsReady,
                               this, nullptr);
                    onNextEpisodePrefetchStreams(streams, addonsById);
                });

            tankostream::stream::StreamLoadRequest req;
            req.type = QStringLiteral("series");
            req.id   = imdbId + QLatin1Char(':') + QString::number(qMax(1, next.first))
                              + QLatin1Char(':') + QString::number(qMax(1, next.second));
            *prefetchToken = m_streamAggregator->load(req);
        });

    m_metaAggregator->fetchSeriesMeta(imdbId);
}

void StreamPage::onNextEpisodePrefetchStreams(
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>& addonsById)
{
    if (!m_session.nextPrefetch.has_value()) return;

    const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);
    if (choices.isEmpty()) return;

    // Match priority: per-episode saved choice > per-series bingeGroup.
    // We only fire auto-play for the next episode when one of these matches;
    // the overlay won't show otherwise.
    const QJsonObject epChoice = StreamChoices::loadChoice(m_session.nextPrefetch->epKey);
    QString epChoiceKey;
    if (!epChoice.isEmpty()) {
        const QString sourceKind = epChoice.value("sourceKind").toString();
        const QString addonId    = epChoice.value("addonId").toString();
        const QString hashOrUrl  = (sourceKind == QLatin1String("magnet"))
            ? epChoice.value("infoHash").toString().toLower()
            : epChoice.value("directUrl").toString();
        const int fileIndex = epChoice.value("fileIndex").toInt(-1);
        epChoiceKey = addonId + QLatin1Char('|') + sourceKind + QLatin1Char('|')
                    + hashOrUrl + QLatin1Char('|') + QString::number(fileIndex);
    }

    const QJsonObject seriesChoice = StreamChoices::loadSeriesChoice(m_session.nextPrefetch->imdbId);
    const QString seriesBingeGroup = seriesChoice.value("bingeGroup").toString();

    for (const auto& c : choices) {
        const bool epMatch = !epChoiceKey.isEmpty()
                           && tankostream::stream::pickerChoiceKey(c) == epChoiceKey;
        const bool seriesMatch = !seriesBingeGroup.isEmpty()
                              && c.stream.behaviorHints.bingeGroup == seriesBingeGroup;
        if (epMatch || seriesMatch) {
            m_session.nextPrefetch->matchedChoice = c;
            break;
        }
    }
    m_session.nextPrefetch->streamsLoaded = true;

    // Phase 2 Batch 2.6 â€” Shift+N path: if the user fired the shortcut
    // while prefetch was still resolving, auto-play the moment a match
    // lands (no overlay, no countdown). No match â†’ silent no-op per
    // TODO "No-op if no next episode".
    if (m_session.nextShortcutPending) {
        m_session.nextShortcutPending = false;
        if (m_session.nextPrefetch->matchedChoice.has_value()) {
            onNextEpisodePlayNow();
        }
        return;
    }

    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” if we reached a
    // matched choice during normal playback (not the Shift+N shortcut),
    // surface the overlay NOW as a floating layer over the still-playing
    // video. Stremio / Netflix UX: user sees "Up next" popup during the
    // last ~30-60s of the episode (or earlier, depending on how long the
    // async prefetch took), can Play Now to skip credits, let the 10s
    // countdown expire to auto-advance, or Cancel to finish the episode
    // naturally. Previous behavior only showed the overlay on
    // closeRequested â€” too late for binge-watch ergonomics.
    if (m_session.nextPrefetch->matchedChoice.has_value()) {
        showNextEpisodeOverlayInPlayer();
    }
}

void StreamPage::showNextEpisodeOverlay()
{
    if (!m_nextEpisodeOverlay || !m_session.nextPrefetch.has_value()) return;

    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” defensive reparent
    // back to m_playerLayer in case the mid-playback path earlier in
    // this session parented the overlay onto VideoPlayer. The legacy
    // close-path fires with the player hidden, so a widget parented to
    // a hidden VideoPlayer would be invisible on show(). Re-adding to
    // the layout restores the centered geometry the close-path UX
    // expects.
    if (m_playerLayer && m_nextEpisodeOverlay->parent() != m_playerLayer) {
        m_nextEpisodeOverlay->setParent(m_playerLayer);
        if (auto* lay = qobject_cast<QVBoxLayout*>(m_playerLayer->layout())) {
            lay->addWidget(m_nextEpisodeOverlay, 0, Qt::AlignCenter);
        }
    }

    const QString seriesName = m_library
        ? m_library->get(m_session.nextPrefetch->imdbId).name
        : QString();
    const QString label = seriesName.isEmpty()
        ? QStringLiteral("S%1E%2")
              .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
              .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'))
        : seriesName + QStringLiteral(" \u00B7 ")
              + QStringLiteral("S%1E%2")
                    .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
                    .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'));

    if (m_nextEpisodeTitleLabel) {
        m_nextEpisodeTitleLabel->setText(tr("Up next: ") + label);
    }

    m_nextEpisodeCountdownSec = 10;
    if (m_nextEpisodeCountdownLabel) {
        m_nextEpisodeCountdownLabel->setText(
            tr("Playing in %1s...").arg(m_nextEpisodeCountdownSec));
    }

    m_bufferOverlay->hide();
    m_nextEpisodeOverlay->show();
    m_mainStack->setCurrentIndex(2);

    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->start();
}

void StreamPage::hideNextEpisodeOverlay()
{
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    if (m_nextEpisodeOverlay) m_nextEpisodeOverlay->hide();
}

// STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” mid-playback sibling of
// showNextEpisodeOverlay. Reparents the same overlay widget onto the
// floating VideoPlayer (found via mainWin->findChild<VideoPlayer*>, same
// pattern as the progressUpdated-lambda setup at line ~1743) so the
// overlay paints OVER still-playing video rather than waiting for
// closeRequested to swap the player away first. Position: bottom-right
// of the player's rect with 40px horizontal + 80px vertical margins â€”
// matches Stremio/Netflix binge-overlay placement (doesn't block content
// center where the climax of a scene usually plays). Countdown starts
// fresh at 10s each invocation â€” safe to re-enter if called twice
// before dismiss (mutates text in place via setText).
void StreamPage::showNextEpisodeOverlayInPlayer()
{
    if (!m_nextEpisodeOverlay || !m_session.nextPrefetch.has_value()) return;

    auto* mainWin = window();
    if (!mainWin) return;
    auto* player = mainWin->findChild<VideoPlayer*>();
    if (!player || !player->isVisible()) {
        // Defensive: player not visible (session may have torn down
        // between prefetch start + stream resolve). Let the close-path
        // overlay handle it via the existing closeRequested branch.
        return;
    }

    m_nextEpisodeOverlay->setParent(player);
    constexpr int kRightMarginPx  = 40;
    constexpr int kBottomMarginPx = 80;
    m_nextEpisodeOverlay->move(
        player->width()  - m_nextEpisodeOverlay->width()  - kRightMarginPx,
        player->height() - m_nextEpisodeOverlay->height() - kBottomMarginPx);

    // Text identical to showNextEpisodeOverlay â€” same "Up next: SeriesName Â· SxxExx"
    // + "Playing in Ns..." format. Kept duplicated here rather than
    // refactored into a helper so the code path stays readable and each
    // overlay-surface gets to independently tune its own text / layout
    // (e.g., in-player overlay may want a more subtle font in future).
    const QString seriesName = m_library
        ? m_library->get(m_session.nextPrefetch->imdbId).name
        : QString();
    const QString label = seriesName.isEmpty()
        ? QStringLiteral("S%1E%2")
              .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
              .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'))
        : seriesName + QStringLiteral(" Â· ")
              + QStringLiteral("S%1E%2")
                    .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
                    .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'));

    if (m_nextEpisodeTitleLabel) {
        m_nextEpisodeTitleLabel->setText(tr("Up next: ") + label);
    }

    m_nextEpisodeCountdownSec = 10;
    if (m_nextEpisodeCountdownLabel) {
        m_nextEpisodeCountdownLabel->setText(
            tr("Playing in %1s...").arg(m_nextEpisodeCountdownSec));
    }

    // Do NOT hide m_bufferOverlay or switch m_mainStack â€” the player
    // window is already visible (floating above the stack's current
    // page). We only show our overlay on top.
    m_nextEpisodeOverlay->show();
    m_nextEpisodeOverlay->raise();

    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->start();
}

void StreamPage::onNextEpisodeCountdownTick()
{
    --m_nextEpisodeCountdownSec;
    if (m_nextEpisodeCountdownSec <= 0) {
        onNextEpisodePlayNow();
        return;
    }
    if (m_nextEpisodeCountdownLabel) {
        m_nextEpisodeCountdownLabel->setText(
            tr("Playing in %1s...").arg(m_nextEpisodeCountdownSec));
    }
}

void StreamPage::onNextEpisodePlayNow()
{
    if (!m_session.nextPrefetch.has_value() || !m_session.nextPrefetch->matchedChoice.has_value()) {
        // Defensive: no prefetch available. Treat as cancel.
        onNextEpisodeCancel();
        return;
    }

    const auto choice = *m_session.nextPrefetch->matchedChoice;
    const auto prefetchCopy = *m_session.nextPrefetch;

    hideNextEpisodeOverlay();

    // Populate m_session.pending so onSourceActivated knows which episode
    // this is â€” same shape onPlayRequested would have produced, minus the
    // streams fan-out round-trip (we already have the matched choice).
    // Batch 1.3 â€” route through beginSession so the new episode's session
    // gets a fresh generation + prior session's async closures are aborted
    // at the boundary instead of firing against the new epKey.
    PendingPlay p;
    p.imdbId    = prefetchCopy.imdbId;
    p.mediaType = QStringLiteral("series");
    p.season    = prefetchCopy.season;
    p.episode   = prefetchCopy.episode;
    p.epKey     = prefetchCopy.epKey;
    p.valid     = true;
    beginSession(p.epKey, p, QStringLiteral("nextEpisodePlayNow"));

    // Reset prefetch + near-end so the next episode's playback can re-
    // prefetch when it approaches its own end.
    resetNextEpisodePrefetch();

    // Drive through the canonical source-activation path â€” same persistence,
    // subtitle fan-out, and player handoff the user-click flow uses.
    onSourceActivated(choice);
}

void StreamPage::onNextEpisodeCancel()
{
    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” two contexts now call
    // Cancel:
    //   (a) Legacy close-path: overlay appeared AFTER closeRequested; the
    //       stream was already stopped; Cancel should return to browse.
    //   (b) Mid-playback path: overlay appeared DURING last ~30-60s of
    //       playback (new Stremio-parity path); the stream is still
    //       running; Cancel should ONLY dismiss the overlay and let the
    //       user finish watching.
    // Discriminator: if the player is visible, we're in case (b).
    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) â€” the streaming controller is
    // gone; the next-episode overlay is unreachable in download-only Theatre
    // (it was only armed during streamed playback). The player-visible check
    // alone is the correct discriminator now. Path retained as inert code.
    auto* mainWin = window();
    auto* player = mainWin ? mainWin->findChild<VideoPlayer*>() : nullptr;
    const bool midPlayback = player && player->isVisible();

    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();

    if (midPlayback) {
        // Case (b): reparent overlay back to m_playerLayer so if the
        // close-path later tries to showNextEpisodeOverlay for a future
        // session, the widget tree is consistent. resetNextEpisodePrefetch
        // above already cleared matchedChoice so overlayEligible will be
        // false on close for this session anyway â€” this is belt-and-
        // braces cleanup against future session state. Widget is hidden
        // (via hideNextEpisodeOverlay above) so layout recalc is cheap.
        if (m_nextEpisodeOverlay && m_playerLayer) {
            m_nextEpisodeOverlay->setParent(m_playerLayer);
            m_nextEpisodeOverlay->hide();
        }
        return;  // player keeps playing
    }
    // Case (a): player has already closed; restore the pre-player view
    // (STREAM_NAV_BACK_STACK 2026-05-06) so the user lands on their
    // originating Detail / Catalog / Search page instead of library.
    restorePlayerExitView();
}

void StreamPage::resetNextEpisodePrefetch()
{
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    m_session.nextPrefetch.reset();
    m_session.nearEndCrossed = false;
    m_session.nextShortcutPending = false;
    // Drop any in-flight prefetch connections from MetaAggregator /
    // StreamAggregator â€” we reset them on each prefetch start anyway, but
    // safer to clear when fully canceling so stale lambdas don't accumulate.
    if (m_metaAggregator) {
        disconnect(m_metaAggregator,
                   &tankostream::stream::MetaAggregator::seriesMetaReady,
                   this, nullptr);
    }
    if (m_streamAggregator) {
        disconnect(m_streamAggregator,
                   &tankostream::stream::StreamAggregator::streamsReady,
                   this, nullptr);
    }
}

// STREAM_LIFECYCLE_FIX Phase 1 Batches 1.1 + 1.2 + 1.3 â€” PlaybackSession
// foundation + full migration. 1.1 introduced the struct + generation
// counter + beginSession/resetSession API. 1.2 migrated `_currentEpKey` +
// m_pendingPlay + m_lastDeadlineUpdateMs. 1.3 migrated m_session.nextPrefetch +
// m_session.nearEndCrossed + m_session.nextShortcutPending + m_seekRetryState (raw QObject*
// identity-token pattern replaced with generation-check â€” first real
// consumer of currentGeneration()/isCurrentGeneration()). 1.3 also fleshed
// SeekRetryState, added reason param + wrap-guard + begin-log on
// beginSession, and wired beginSession into the 4 session-start sites
// (trailer paste, magnet paste, onPlayRequested, onNextEpisodePlayNow).
// Phase 1 CLOSED at 1.3. Prototype credit:
// agents/prototypes/stream_lifecycle/Batch1.1_PlaybackSession_struct_API.cpp
// (Agent 7, Codex) â€” shape adopted as-is modulo file-style conventions.

quint64 StreamPage::currentGeneration() const
{
    return m_session.generation;
}

bool StreamPage::isCurrentGeneration(quint64 gen) const
{
    return gen != 0 && gen == m_session.generation;
}

quint64 StreamPage::beginSession(const QString& epKey, const PendingPlay& pending,
                                 const QString& reason)
{
    resetSession(reason.isEmpty()
                     ? QStringLiteral("beginSession")
                     : QStringLiteral("beginSession:%1").arg(reason));

    // Defensive wrap guard (prototype shape). quint64 doesn't wrap in
    // practical lifetime, but if m_nextGeneration ever landed at 0 we'd
    // stamp m_session.generation = 0 which is the "no-session" sentinel â€”
    // breaking isValid() / isCurrentGeneration() contract silently.
    if (m_nextGeneration == 0) m_nextGeneration = 1;

    m_session.generation = m_nextGeneration++;
    m_session.epKey      = epKey;
    m_session.pending    = pending;

    qInfo().noquote() << QStringLiteral("[stream-session] begin: gen=%1 epKey=%2")
                             .arg(m_session.generation).arg(epKey);

    return m_session.generation;
}

void StreamPage::resetSession(const QString& reason)
{
    // Same teardown shape as resetNextEpisodePrefetch (countdown timer stop +
    // prefetch aggregator disconnect) plus a full state clear. Kept as a pure
    // boundary: no showBrowse(), no signal emits, no player touches â€” callers
    // decide what UI follows. Matches audit advisory #1's "single boundary"
    // shape so every scattered inline reset can funnel here.
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    if (m_metaAggregator) {
        disconnect(m_metaAggregator,
                   &tankostream::stream::MetaAggregator::seriesMetaReady,
                   this, nullptr);
    }
    if (m_streamAggregator) {
        disconnect(m_streamAggregator,
                   &tankostream::stream::StreamAggregator::streamsReady,
                   this, nullptr);
    }
    m_session = PlaybackSession{};
    qInfo().noquote() << QStringLiteral("[stream-session] reset: reason=%1")
                             .arg(reason.isEmpty() ? QStringLiteral("unspecified")
                                                    : reason);
}

void StreamPage::onStreamNextEpisodeShortcut()
{
    // STREAM_LIFECYCLE_FIX Phase 4 Batch 4.1 â€” audit P2-3 close. Pre-4.1 guard
    // used `m_session.pending.valid` which onSourceActivated clears before
    // playback starts (pending is consumed as the session installs) â€” making
    // Shift+N a silent no-op during actual playback. Correct identity signals:
    //   (a) m_session.isValid() â€” active session (generation != 0 AND
    //       epKey non-empty). Holds true from beginSession through
    //       resetSession/next beginSession, spans the entire playback.
    //   (b) pending.mediaType == "series" â€” filter out movies / trailers /
    //       adhoc URLs where "next episode" has no meaning.
    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) â€” the old (c) m_playerController
    // ->isActive() sanity check is dropped with the streaming controller. The
    // next-episode prefetch was only ever armed during streamed playback
    // (from the now-removed onReadyToPlay), and this shortcut's wiring lived in
    // that same removed slot â€” so this handler is unreachable in download-only
    // Theatre. Retained as inert code; the session-identity guards below keep
    // it harmless if ever re-entered.
    if (!m_session.isValid()) return;
    if (m_session.pending.mediaType != QStringLiteral("series")) return;
    if (m_session.pending.imdbId.isEmpty()) return;  // defensive; isValid implies epKey non-empty but imdbId is a separate field

    // Already resolved (user crossed 95% earlier in this playback). Skip
    // the countdown and play immediately.
    if (m_session.nextPrefetch.has_value()
        && m_session.nextPrefetch->matchedChoice.has_value())
    {
        onNextEpisodePlayNow();
        return;
    }

    // Prefetch already in flight from near-end trigger â€” mark shortcut
    // pending so onNextEpisodePrefetchStreams auto-plays once match lands.
    if (m_session.nextPrefetch.has_value() && !m_session.nextPrefetch->streamsLoaded) {
        m_session.nextShortcutPending = true;
        return;
    }

    // Cold path â€” no prefetch yet. Kick one off for the current episode
    // and arm the shortcut-pending flag. startNextEpisodePrefetch reuses
    // the series-meta â†’ next-unwatched resolve path from Batch 2.5;
    // if the series has no next unwatched episode, matchedChoice never
    // lands and the shortcut falls through to no-op silently.
    m_session.nextShortcutPending = true;
    startNextEpisodePrefetch(m_session.pending.imdbId,
                             m_session.pending.season,
                             m_session.pending.episode);
}

void StreamPage::onAddToTankorentRequested(const tankostream::stream::StreamPickerChoice& choice)
{
    // Defensive guard. The card-side menu suppresses the action for
    // non-magnet sources (StreamSourceCard::contextMenuEvent), so a
    // signal-arrival here with a bad payload would only happen if a
    // future code path emits the signal directly without going through
    // the card. Treat as no-op rather than crash.
    if (choice.sourceKind != QLatin1String("magnet")
     || choice.magnetUri.isEmpty()) {
        qWarning() << "StreamPage::onAddToTankorentRequested: refusing non-magnet"
                   << "sourceKind=" << choice.sourceKind
                   << "magnetUri.isEmpty=" << choice.magnetUri.isEmpty();
        return;
    }

    // Display name. STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 repurposed
    // displayTitle to carry the release name (was: addon name). It is
    // now the right primary identifier here â€” no longer needs the
    // displayFilename fallback (field removed). Session-pending fallback
    // synthesizes a "<imdbId> S<NN>E<NN>" identifier when extractReleaseName
    // bottomed out at "(unnamed release)" but we have episode context.
    QString displayName = choice.displayTitle;
    if ((displayName.isEmpty() || displayName == QLatin1String("(unnamed release)"))
     && m_session.pending.valid
     && m_session.pending.mediaType == QLatin1String("series")
     && m_session.pending.season > 0
     && m_session.pending.episode > 0) {
        displayName = QStringLiteral("%1 S%2E%3")
                          .arg(m_session.pending.imdbId)
                          .arg(m_session.pending.season, 2, 10, QChar('0'))
                          .arg(m_session.pending.episode, 2, 10, QChar('0'));
    }

    emit addToTankorentRequested(choice.magnetUri, displayName);
}

void StreamPage::triggerBulkSeasonDownload(int season)
{
    using namespace tankostream::stream;

    cancelBulkSeasonDownload();

    if (!m_detailView || !m_bridge || !m_torrentClient)
        return;
    if (m_detailView->currentType() != QLatin1String("series"))
        return;

    const QList<StreamEpisode> episodes = m_detailView->episodesForSeason(season);
    if (m_detailView->currentImdb().isEmpty() || season <= 0 || episodes.isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("No episodes available for season download"));
        return;
    }

    const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
    if (roots.isEmpty() || roots.first().isEmpty()) {
        m_detailView->setStreamSourcesError(tr("Videos library root is not configured"));
        return;
    }

    // DOWNLOAD_AUTO_LIBRARY_ADD 2026-05-13 — Hemanth report "adding an
    // episode to download isn't automatically adding the show to the
    // library". The existing auto-add hook fires from the progressUpdated
    // lambda (StreamPage.cpp:2988) only on the FIRST successful playback
    // tick — i.e., download-without-play didn't pin the show. Stremio
    // behavior pins on any user gesture that implies intent to consume
    // (play OR download). Fire here too. autoAddToLibrary is idempotent
    // (m_library->has() short-circuit + add-only, never toggles off).
    m_detailView->autoAddToLibrary();

    m_bulkInput = BulkPlanInput{};
    m_bulkInput.seriesId = m_detailView->currentImdb();
    m_bulkInput.seriesTitle = m_detailView->currentTitle();
    if (m_bulkInput.seriesTitle.isEmpty())
        m_bulkInput.seriesTitle = m_bulkInput.seriesId;
    m_bulkInput.seriesYear = m_detailView->currentYear();
    m_bulkInput.seasonNumber = season;
    m_bulkInput.videosRootPath = roots.first();
    for (const StreamEpisode& episode : episodes) {
        BulkPlanEpisodeInput ep;
        ep.season = season;
        ep.episode = episode.episode;
        ep.title = episode.title;
        ep.extensionHint = QStringLiteral("mkv");
        m_bulkInput.episodes.push_back(ep);
    }

    m_bulkPlanResult = buildBulkPlan(m_bulkInput, [](const QString& path) {
        return QFileInfo::exists(path);
    });
    m_bulkSourcePayload = BulkSourceCollectionPayload{};
    m_bulkVerificationNote.clear();

    m_bulkProgressDialog = new QDialog(this);
    m_bulkProgressDialog->setWindowTitle(tr("Preparing stream download"));
    m_bulkProgressDialog->setModal(false);
    auto* layout = new QVBoxLayout(m_bulkProgressDialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    m_bulkProgressLabel = new QLabel(tr("Resolving sources..."), m_bulkProgressDialog);
    m_bulkProgressLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_bulkProgressLabel);
    m_bulkProgressBar = new QProgressBar(m_bulkProgressDialog);
    m_bulkProgressBar->setRange(0, 0);
    layout->addWidget(m_bulkProgressBar);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_bulkProgressDialog);
    connect(buttons, &QDialogButtonBox::rejected, this, &StreamPage::cancelBulkSeasonDownload);
    layout->addWidget(buttons);
    connect(m_bulkProgressDialog, &QDialog::rejected,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkProgressDialog->show();

    m_bulkSourceCollector = new BulkSourceCollector(m_addonRegistry, this);
    connect(m_bulkSourceCollector, &BulkSourceCollector::progressTick,
            this, [this](int resolved, int total) {
                if (m_bulkProgressLabel) {
                    m_bulkProgressLabel->setText(
                        tr("Resolving sources... %1/%2").arg(resolved).arg(total));
                }
            });
    connect(m_bulkSourceCollector, &BulkSourceCollector::collectionComplete,
            this, &StreamPage::onBulkSourcesCollected);
    connect(m_bulkSourceCollector, &BulkSourceCollector::cancelled,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkSourceCollector->begin(m_bulkInput);
}

// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — three new slots wired from StreamDetailView's
// seasonDownloadRequested / selectedEpisodesDownloadRequested / singleEpisodeDownloadRequested
// signals. All three funnel into triggerBulkSelectedEpisodes which builds the episode
// list then runs the normal BulkSourceCollector path.

void StreamPage::onSeasonDownloadRequested(int season)
{
    if (!m_detailView) return;
    triggerBulkSelectedEpisodes(m_detailView->currentImdb(), season, QList<int>{});
}

void StreamPage::onSelectedEpisodesDownloadRequested(int season, const QList<int>& episodes)
{
    if (!m_detailView) return;
    triggerBulkSelectedEpisodes(m_detailView->currentImdb(), season, episodes);
}

void StreamPage::onSingleEpisodeDownloadRequested(int season, int episode)
{
    if (!m_detailView) return;
    startAutoDownload(m_detailView->currentImdb(), QStringLiteral("series"), season, episode);
}

// THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - movie auto-dispatch fast
// path. Picking already done emitter-side in StreamDetailView (top-seeded
// magnet selected from m_lastChoices, which is pre-sorted by buildPickerChoices
// per StreamSourceChoice.h:62-65). This slot is trivial dispatch glue:
// resolveMetadata fallback if infoHash didn't come through, then a direct
// TorrentClient::startDownload with the movie-appropriate config (season=0,
// no streamGroupId, original layout). Bypasses TheatreDownloadPanel entirely.
void StreamPage::onTheatreTopSeededDownloadRequested(const QString& imdbId,
                                                      const QString& showName,
                                                      const QString& infoHash,
                                                      const QString& magnetUri)
{
    Q_UNUSED(showName);
    if (!m_torrentClient || imdbId.isEmpty()) return;

    QString hash = infoHash;
    if (hash.isEmpty() && !magnetUri.isEmpty()) {
        hash = m_torrentClient->resolveMetadata(magnetUri);
    }
    if (hash.isEmpty()) {
        qWarning() << "StreamPage::onTheatreTopSeededDownloadRequested:"
                   << "no dispatchable hash for" << imdbId << "- aborting";
        return;
    }

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value("videos");
    config.contentLayout   = QStringLiteral("original");
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = imdbId;
    config.season          = 0;
    // F9 fix 2026-05-19: pass magnet URI so startDownload can self-defend.
    config.magnetUri       = magnetUri;

    m_torrentClient->startDownload(hash, config);
    if (m_detailView && m_detailView->currentImdb() == imdbId) {
        m_detailView->autoAddToLibrary();
        m_detailView->showAutoLaunchToast(
            tr("Movie added - downloading top-seeded source"));
    }
}

void StreamPage::dismissTheatreDownloadPanelIfOpen()
{
    // THEATRE_DOWNLOAD_OVERHAUL stale-panel-on-show-change fix v2 2026-05-17 -
    // imperative helper called at every StreamPage navigation transition
    // site (4 showEntry call sites + goBack + showBrowse + the
    // entryContextChanging signal slot). Idempotent + safe when panel is
    // already hidden. Combines:
    //   (a) panel->reset() - clears m_packs / m_filteredPacks / m_tileChecked /
    //       m_pendingMetadataHash / m_realFiles / derive-scope cache + resets
    //       internal QStackedWidget to PackList state. Without reset, a later
    //       openFor() on a different show could flash stale data on first
    //       paint before the new search returns.
    //   (b) slideOutToRight(panel) - the visible dismiss animation (only fires
    //       if panel is visible per slideOutToRight's internal isVisible guard).
    //   (c) slideInFromRight(sources) - bring the Sources sidebar back into
    //       the right-pane slot to replace the dismissed panel.
    //
    // Called instead of the prior signal-based slot's logic which proved
    // insufficient against Hemanth's 2026-05-17 smoke (stale panel persisted
    // across show-view transitions even after MOC regen + relaunch).
    if (!m_theatreDownloadPanel) return;
    m_theatreDownloadPanel->reset();
    slideOutToRight(m_theatreDownloadPanel);
    if (m_detailSourcesPanel)
        slideInFromRight(m_detailSourcesPanel);
}

void StreamPage::onDirectDownloadRequested(const tankostream::stream::StreamPickerChoice& choice)
{
    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - dispatch a specific
    // right-clicked Sources-panel stream into the Theatre library via
    // TorrentClient::startDownload directly. Companion to the auto-pick
    // top-seeded fast-path; here the user has chosen WHICH stream they want.
    if (!m_torrentClient) return;
    if (choice.sourceKind != QLatin1String("magnet")) {
        // THEATRE_DOWNLOAD_ONLY (2026-05-29) â€” non-magnet sources can't be
        // downloaded (no torrent to hand TorrentClient). Pre-fix this was a
        // silent early-return, so picking a non-torrent source looked like a
        // dead click. Surface a visible message via the in-page toast the
        // detail view already owns (same surface as the movie-download
        // confirmation toast) so the no-op isn't confusing.
        if (m_detailView)
            m_detailView->showAutoLaunchToast(
                tr("This source can't be downloaded - pick a torrent source."));
        else
            qWarning() << "StreamPage::onDirectDownloadRequested:"
                       << "non-magnet source picked - nothing to download";
        return;
    }
    if (choice.infoHash.isEmpty() && choice.magnetUri.isEmpty()) return;

    QString hash = choice.infoHash;
    if (hash.isEmpty() && !choice.magnetUri.isEmpty()) {
        hash = m_torrentClient->resolveMetadata(choice.magnetUri);
    }
    if (hash.isEmpty()) {
        qWarning() << "StreamPage::onDirectDownloadRequested:"
                   << "no dispatchable hash - aborting";
        return;
    }

    // C1 fix (code-quality review 2026-05-17): Sources panel populates for
    // series episodes too (StreamDetailView.cpp:1163-1164), not only movies.
    // Branch on currentType() so series-episode right-click + Download
    // registers with the correct season identity (else download routes as
    // season=0 movie and the episode chip never lights up on completion).
    // I1 fix: empty imdbId silently breaks downstream registration - guard.
    if (!m_detailView) {
        qWarning() << "StreamPage::onDirectDownloadRequested:"
                   << "no detail view context - aborting";
        return;
    }
    const QString imdbId = m_detailView->currentImdb();
    if (imdbId.isEmpty()) {
        qWarning() << "StreamPage::onDirectDownloadRequested:"
                   << "empty imdbId from detail view - aborting";
        return;
    }
    const QString type = m_detailView->currentType();
    const int season = (type == QLatin1String("series"))
        ? m_detailView->currentSeason()
        : 0;

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value("videos");
    config.contentLayout   = QStringLiteral("original");
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = imdbId;
    config.season          = season;
    // F9 fix 2026-05-19: pass magnet URI so startDownload can self-defend.
    config.magnetUri       = choice.magnetUri;

    m_torrentClient->startDownload(hash, config);
}

// THEATRE_DOWNLOAD_SIMPLIFY P1.T2 (2026-05-29) — silent auto-download entry.
// Mirrors onPlayRequested's one-shot streamsReady idiom (StreamPage.cpp ~2308-
// 2400): disconnect any prior handler, connect a fresh self-disconnecting
// lambda, build the Torrentio request id, then load(). The handler runs the
// auto-pick + startDownload in finishAutoDownloadPick().
void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaType,
                                   int season, int episode, bool forStream)
{
    if (!m_streamAggregator || !m_torrentClient || imdbId.isEmpty())
        return;

    // DOWNLOAD BUG 2026-06-02 — in-flight dedup. Rapid identical Download
    // clicks (the logs show 2-6 startAutoDownload within seconds) used to
    // re-arm the shared streamsReady one-shot and call load() again, which
    // reset() mid-flight and let a late stale emit deliver the wrong show's
    // streams. If the SAME request is already in flight, ignore the re-click.
    if (m_pendingAuto.active
        && m_pendingAuto.imdbId == imdbId
        && m_pendingAuto.season == season
        && m_pendingAuto.episode == episode
        && m_pendingAuto.forStream == forStream) {
        // THEATRE_STREAMING_RESTORE P1 (2026-06-09) — dedup must also match the
        // watch-vs-download intent: a Download right after a Watch (or vice
        // versa) on the same episode is a DIFFERENT request, not a re-click
        // (Codex review finding). Only an identical-intent re-click is dropped.
        qInfo().noquote() << "[auto-dl] dedup: ignoring re-click for in-flight imdb="
                          << imdbId << "s" << season << "e" << episode
                          << "stream=" << (forStream ? "y" : "n");
        return;
    }

    m_pendingAuto = PendingAutoDownload{};
    m_pendingAuto.active         = true;
    m_pendingAuto.imdbId         = imdbId;
    m_pendingAuto.mediaType      = mediaType;
    m_pendingAuto.season         = season;
    m_pendingAuto.episode        = episode;
    m_pendingAuto.runtimeMinutes = 0;  // unknown -> AutoSourcePicker skips size guardrail
    // Capture the show title now (the detail view shows the requested show at
    // click time) so the picker's show-identity gate survives any later
    // navigation while the async source fetch is in flight.
    m_pendingAuto.showTitle      = m_detailView ? m_detailView->currentTitle() : QString();
    m_pendingAuto.forStream      = forStream;  // THEATRE_STREAMING_RESTORE P1 — watch vs download intent
    qInfo().noquote() << "[auto-dl] startAutoDownload imdb=" << imdbId
                      << "type=" << mediaType << "s" << season << "e" << episode;

    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
        [this](const QList<tankostream::addon::Stream>& streams,
               const QHash<QString, QString>& addonsById) {
            // DOWNLOAD BUG 2026-06-02 — ignore a stale emit from a superseded
            // load(). Without this, a late streamsReady from an EARLIER request
            // fires this one-shot carrying the WRONG show's streams; the picker
            // show-gate then rejects them all -> "No 1080p source found".
            if (m_streamAggregator->currentLoadToken() != m_pendingAuto.token)
                return;
            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);
            finishAutoDownloadPick(streams, addonsById);
        });

    tankostream::stream::StreamLoadRequest req;
    req.type = (mediaType == QLatin1String("movie")) ? QStringLiteral("movie") : QStringLiteral("series");
    const int kitsuId = (mediaType != QLatin1String("movie") && m_metaAggregator)
                            ? m_metaAggregator->kitsuIdForSeries(imdbId)
                            : -1;
    if (mediaType == QLatin1String("movie")) {
        req.id = imdbId;
    } else if (kitsuId > 0) {
        req.id = QStringLiteral("kitsu:%1:%2").arg(kitsuId).arg(qMax(1, episode));
    } else {
        req.id = imdbId + QLatin1Char(':') + QString::number(qMax(1, season))
                        + QLatin1Char(':') + QString::number(qMax(1, episode));
    }
    qInfo().noquote() << "[auto-dl] req.id=" << req.id
                      << "showTitle=" << m_pendingAuto.showTitle;
    // DOWNLOAD BUG 2026-06-06 — a single addon failing must NOT abort the
    // auto-download or alarm the user. The Amatsu anime gateway's
    // tt->AniList->Nyaa lookup routinely trips the 10s transport timeout, but
    // StreamAggregator counts EVERY dispatched addon and still fires a FINAL
    // streamsReady with the surviving addons' streams (completeOne() emits once
    // m_pendingResponses hits 0, regardless of per-addon failures). The one-shot
    // streamsReady handler above then runs finishAutoDownloadPick(), which either
    // starts the download from Torrentio's result OR surfaces a genuine
    // "No 1080p source found" when the final result set is empty. Clearing
    // m_pendingAuto.active here was the bug: a late Amatsu timeout cancelled a
    // download Torrentio had already sourced, so "nothing about Theatre downloads
    // works" on every anime title. We now only LOG the per-addon error; the
    // terminal pick (or its empty-result branch) owns all user-facing outcomes.
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
        [](const QString& addonId, const QString& message) {
            qInfo().noquote() << "[auto-dl] addon source error (non-fatal):"
                              << (addonId.isEmpty() ? QStringLiteral("(unknown)") : addonId)
                              << message;
        });

    // DOWNLOAD BUG 2026-06-02 — capture the generation token load() stamped so
    // the one-shot above can discard a late emit from a superseded load().
    // Set AFTER m_pendingAuto fields are populated.
    m_pendingAuto.token = m_streamAggregator->load(req);
}

// One-shot streamsReady handler for an active auto-download. Converts the
// Torrentio results into AutoSourcePicker candidates, picks the best 1080p,
// resolves the infoHash, and starts the download stamped theatre:<imdbId>.
void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>& streams,
                                        const QHash<QString, QString>& addonsById)
{
    if (!m_pendingAuto.active) return;
    const PendingAutoDownload ctx = m_pendingAuto;
    m_pendingAuto.active = false;  // consume

    const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);
    qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                      << "active=" << (m_pendingAuto.active ? "y" : "n");

    // DOWNLOAD BUG 2026-06-03 — build the show-identity match text from the RAW
    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
    // put a stats badge ("[2176 seeders | 1.34 GB | NyaaSi]") in displayTitle,
    // which lacks the show name and made the gate reject every real One Piece result
    // ("No 1080p source found"). The raw blob carries the filename wherever the
    // addon stashed it (name / description / fileNameHint / parsedFilename), so
    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
    // title (its job in the picker is unchanged).
    auto identityBlob = [](const tankostream::stream::StreamPickerChoice& ch) {
        return QStringList{
            ch.displayTitle,
            ch.stream.name,
            ch.stream.description,
            ch.stream.source.fileNameHint,
            ch.fileNameHint,
            ch.stream.behaviorHints.filename,
            ch.stream.behaviorHints.other
                .value(QStringLiteral("parsedFilename")).toString(),
        }.join(QLatin1Char('\n'));
    };

    QList<tankostream::stream::SourceCandidate> cands;
    cands.reserve(choices.size());
    for (const auto& c : choices) {
        tankostream::stream::SourceCandidate sc;
        sc.title       = c.displayTitle;
        sc.matchText    = identityBlob(c);
        sc.seeders     = c.seeders;
        sc.sizeBytes   = c.sizeBytes;
        sc.qualitySort = c.qualitySort;
        cands.append(sc);
    }

    // DOWNLOAD BUG 2026-06-02/06-03 — diagnostic: dump every candidate with
    // whether it passes the show-identity gate against the RAW identity blob
    // (what pick() now uses) vs the old displayTitle. If the pick still fails,
    // this shows conclusively whether real One Piece titles are being wrongly
    // rejected (gate too strict / blob missing the name) vs the choices being
    // the wrong show entirely (correlation broken — wrong streams reached here).
    for (const auto& ch : choices) {
        const QString blob = identityBlob(ch);
        qInfo().noquote() << "[auto-dl] cand q=" << ch.qualitySort << "seed=" << ch.seeders
                          << "gateBlob=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
                                            blob, ctx.showTitle)
                          << "gateOld=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
                                            ch.displayTitle, ctx.showTitle)
                          << "title=" << ch.displayTitle.left(60)
                          << "| blob=" << blob.simplified().left(100);
    }

    const std::optional<int> picked =
        tankostream::stream::AutoSourcePicker::pick(cands, ctx.showTitle, ctx.runtimeMinutes);
    qInfo().noquote() << "[auto-dl] picked="
                      << (picked.has_value() ? QString::number(*picked) : QStringLiteral("NONE"))
                      << "of" << cands.size();
    if (!picked.has_value()) {
        // No acceptable 1080p source. (P1.T4 refines this into a tile state;
        // for now surface it in the sources panel.)
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("No 1080p source found"));
        return;
    }

    const tankostream::stream::StreamPickerChoice& chosen = choices.at(*picked);

    // THEATRE_STREAMING_RESTORE P1 (2026-06-09) — "watch" intent streams the
    // auto-picked source via the Stremio stream-server (instant play, no full
    // download). The explicit download actions (single/season/selected/direct)
    // route here with forStream=false and fall through to the libtorrent
    // startDownload path below — download is kept (Hemanth 2026-06-09 hybrid).
    if (ctx.forStream) {
        if (!m_playerController) {
            if (m_detailView)
                m_detailView->setStreamSourcesError(tr("Streaming engine not ready"));
            return;
        }
        m_pendingStreamTitle = ctx.showTitle;
        if (m_pendingStreamTitle.isEmpty() && m_detailView)
            m_pendingStreamTitle = m_detailView->currentTitle();
        if (m_bufferOverlay)
            m_bufferOverlay->show();
        qInfo().noquote() << "[stream] startStream imdb=" << ctx.imdbId
                          << "type=" << ctx.mediaType
                          << "s" << ctx.season << "e" << ctx.episode
                          << "title=" << m_pendingStreamTitle;
        m_playerController->startStream(ctx.imdbId, ctx.mediaType,
                                        ctx.season, ctx.episode, chosen.stream);
        return;
    }

    QString hash = chosen.infoHash;
    if (hash.isEmpty() && !chosen.magnetUri.isEmpty())
        hash = m_torrentClient->resolveMetadata(chosen.magnetUri);
    if (hash.isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("Could not resolve source"));
        return;
    }

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value(QStringLiteral("videos"));
    config.contentLayout   = QStringLiteral("original");
    // Empty: lets TorrentClient's onMetadataReady/onPieceFinished/onTorrentFinished
    // drive per-episode pending→progress→complete into StreamDownloadIndex (that
    // path is gated on an EMPTY streamGroupId). Tankorent-page separation is done
    // via the imdbId filter (P2.T5), not via this group id.
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = ctx.imdbId;
    config.season          = (ctx.mediaType == QLatin1String("movie")) ? 0 : ctx.season;
    config.magnetUri       = chosen.magnetUri;

    qInfo().noquote() << "[auto-dl] startDownload hash=" << hash.left(12)
                      << "imdb=" << ctx.imdbId << "season=" << config.season;
    m_torrentClient->startDownload(hash, config);
}

// ── THEATRE_STREAMING_RESTORE P1 (2026-06-09) — StreamPlayerController handlers ──
// Core-first subset: open-player / overlay / error / cleanup. The full
// session-lifecycle machinery from the original (stall detect/recover, buffered-
// range overlay on the seek slider, playback-window deadline retargeting,
// next-episode prefetch) is deferred to P1.x polish.

void StreamPage::onBufferUpdate(const QString& statusText, double /*percent*/)
{
    // Surface buffering status in the sources panel for now (no dedicated
    // overlay-text wiring yet). m_bufferOverlay is shown on stream start and
    // hidden in onReadyToPlay / onStreamFailed / onStreamStopped.
    if (m_detailView && !statusText.isEmpty())
        m_detailView->setStreamSourcesError(statusText);
}

void StreamPage::onReadyToPlay(const QString& httpUrl)
{
    if (m_bufferOverlay)
        m_bufferOverlay->hide();
    if (httpUrl.isEmpty())
        return;

    auto* mainWin = window();
    if (!mainWin) return;
    auto* player = mainWin->findChild<VideoPlayer*>();
    if (!player) return;

    // The Stremio stream-server serves the torrent's video file over local
    // HTTP; the sidecar opens that URL through the same openFile path as a
    // local file. setStreamMode(true) before openFile marks this a stream
    // session; PersistenceMode::None keeps a stream open out of the Videos
    // store. On player close → stopStream (engine tears down the session) and
    // restore the Videos-mode persistence defaults.
    player->setPersistenceMode(VideoPlayer::PersistenceMode::None);
    player->setStreamMode(true);

    disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
    connect(player, &VideoPlayer::closeRequested, this, [this, player]() {
        if (m_playerController)
            m_playerController->stopStream();
        player->setStreamMode(false);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);
    });

    player->openFile(httpUrl, {}, 0, 0.0, m_pendingStreamTitle);

    // THEATRE_STREAMING_RESTORE P1 (2026-06-09) — present the floating player
    // overlay. openFile() only starts playback state; the show/raise/focus +
    // geometry that PRESENT the overlay live in MainWindow::openVideoPlayer-
    // WithOptions for the local-file path (Codex review P0). Mirror that
    // presentation here for the stream-URL path so Watch actually shows the
    // player. (Back-button nav-enable + stream-domain progress save are P1.x.)
    if (auto* parent = player->parentWidget())
        player->setGeometry(parent->rect());
    player->show();
    player->raise();
    player->setFocus();
}

void StreamPage::onStreamFailed(const QString& message)
{
    if (m_bufferOverlay)
        m_bufferOverlay->hide();
    if (m_detailView)
        m_detailView->setStreamSourcesError(tr("Stream failed: %1").arg(message));
}

void StreamPage::onStreamStopped()
{
    if (m_bufferOverlay)
        m_bufferOverlay->hide();
}

// triggerBulkSelectedEpisodes — shared entry point for the three direct-dispatch
// slots. When episodeFilter is non-empty only those episode numbers are included
// in the m_bulkInput; empty filter = whole season (identical to the legacy
// triggerBulkSeasonDownload path). Uses the same BulkSourceCollector orchestration.
void StreamPage::triggerBulkSelectedEpisodes(const QString& imdbId, int season,
                                              const QList<int>& episodeFilter)
{
    using namespace tankostream::stream;

    cancelBulkSeasonDownload();

    if (!m_detailView || !m_bridge || !m_torrentClient)
        return;
    if (m_detailView->currentType() != QLatin1String("series"))
        return;

    const QList<StreamEpisode> allEpisodes = m_detailView->episodesForSeason(season);
    if (imdbId.isEmpty() || season <= 0 || allEpisodes.isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("No episodes available for season download"));
        return;
    }

    const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
    if (roots.isEmpty() || roots.first().isEmpty()) {
        m_detailView->setStreamSourcesError(tr("Videos library root is not configured"));
        return;
    }

    // DOWNLOAD_AUTO_LIBRARY_ADD 2026-05-13 — see triggerBulkSeasonDownload
    // for rationale. Same call here covers the per-row [↓] action-icon
    // single-episode dispatch path + the "Download Selected (N)" + the
    // P3-shipped onSeasonDownloadRequested signal. autoAddToLibrary is
    // idempotent.
    m_detailView->autoAddToLibrary();

    m_bulkInput = BulkPlanInput{};
    m_bulkInput.seriesId = imdbId;
    m_bulkInput.seriesTitle = m_detailView->currentTitle();
    if (m_bulkInput.seriesTitle.isEmpty())
        m_bulkInput.seriesTitle = imdbId;
    m_bulkInput.seriesYear = m_detailView->currentYear();
    m_bulkInput.seasonNumber = season;
    m_bulkInput.videosRootPath = roots.first();

    // Build the episode list — apply filter if non-empty
    QSet<int> filterSet;
    for (int ep : episodeFilter)
        filterSet.insert(ep);

    for (const StreamEpisode& episode : allEpisodes) {
        if (!filterSet.isEmpty() && !filterSet.contains(episode.episode))
            continue;
        BulkPlanEpisodeInput ep;
        ep.season = season;
        ep.episode = episode.episode;
        ep.title = episode.title;
        ep.extensionHint = QStringLiteral("mkv");
        m_bulkInput.episodes.push_back(ep);
    }

    if (m_bulkInput.episodes.isEmpty()) {
        m_detailView->setStreamSourcesError(tr("No matching episodes for download"));
        return;
    }

    m_bulkPlanResult = buildBulkPlan(m_bulkInput, [](const QString& path) {
        return QFileInfo::exists(path);
    });
    m_bulkSourcePayload = BulkSourceCollectionPayload{};
    m_bulkVerificationNote.clear();

    m_bulkProgressDialog = new QDialog(this);
    m_bulkProgressDialog->setWindowTitle(tr("Preparing stream download"));
    m_bulkProgressDialog->setModal(false);
    auto* layout = new QVBoxLayout(m_bulkProgressDialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    m_bulkProgressLabel = new QLabel(tr("Resolving sources..."), m_bulkProgressDialog);
    m_bulkProgressLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_bulkProgressLabel);
    m_bulkProgressBar = new QProgressBar(m_bulkProgressDialog);
    m_bulkProgressBar->setRange(0, 0);
    layout->addWidget(m_bulkProgressBar);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_bulkProgressDialog);
    connect(buttons, &QDialogButtonBox::rejected, this, &StreamPage::cancelBulkSeasonDownload);
    layout->addWidget(buttons);
    connect(m_bulkProgressDialog, &QDialog::rejected,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkProgressDialog->show();

    m_bulkSourceCollector = new BulkSourceCollector(m_addonRegistry, this);
    connect(m_bulkSourceCollector, &BulkSourceCollector::progressTick,
            this, [this](int resolved, int total) {
                if (m_bulkProgressLabel) {
                    m_bulkProgressLabel->setText(
                        tr("Resolving sources... %1/%2").arg(resolved).arg(total));
                }
            });
    connect(m_bulkSourceCollector, &BulkSourceCollector::collectionComplete,
            this, &StreamPage::onBulkSourcesCollected);
    connect(m_bulkSourceCollector, &BulkSourceCollector::cancelled,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkSourceCollector->begin(m_bulkInput);
}

QJsonObject StreamPage::devDispatchEpisodes(const QString& imdbId, int season,
                                            const QList<int>& episodeFilter)
{
    using namespace tankostream::stream;

    auto fail = [](const QString& message) {
        QJsonObject out;
        out[QStringLiteral("status")] = QStringLiteral("error");
        out[QStringLiteral("message")] = message;
        return out;
    };

    if (!m_torrentClient || !m_bridge || !m_addonRegistry || !m_metaAggregator)
        return fail(QStringLiteral("stream services not initialized"));
    if (imdbId.isEmpty() || season <= 0)
        return fail(QStringLiteral("imdbId and positive season are required"));

    QMap<int, QList<StreamEpisode>> seasons;
    if (m_detailView && m_detailView->currentImdb() == imdbId) {
        const QList<StreamEpisode> active = m_detailView->episodesForSeason(season);
        if (!active.isEmpty())
            seasons.insert(season, active);
    }

    if (seasons.value(season).isEmpty()) {
        QEventLoop metaLoop;
        QTimer metaTimer;
        metaTimer.setSingleShot(true);
        bool metaTimedOut = false;
        QString metaError;
        QMetaObject::Connection readyConn;
        QMetaObject::Connection errConn;
        readyConn = connect(m_metaAggregator, &MetaAggregator::seriesMetaReady,
            this, [&](const QString& readyImdb,
                      const QMap<int, QList<StreamEpisode>>& readySeasons) {
                if (readyImdb != imdbId)
                    return;
                seasons = readySeasons;
                metaLoop.quit();
            });
        errConn = connect(m_metaAggregator, &MetaAggregator::seriesMetaError,
            this, [&](const QString& errorImdb, const QString& message) {
                if (errorImdb != imdbId)
                    return;
                metaError = message;
                metaLoop.quit();
            });
        connect(&metaTimer, &QTimer::timeout, &metaLoop, [&]() {
            metaTimedOut = true;
            metaLoop.quit();
        });
        m_metaAggregator->fetchSeriesMeta(imdbId);
        metaTimer.start(15000);
        metaLoop.exec();
        disconnect(readyConn);
        disconnect(errConn);

        if (metaTimedOut)
            return fail(QStringLiteral("series metadata timeout"));
        if (!metaError.isEmpty())
            return fail(metaError);
    }

    const QList<StreamEpisode> allEpisodes = seasons.value(season);
    if (allEpisodes.isEmpty())
        return fail(QStringLiteral("no episodes available for requested season"));

    const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
    if (roots.isEmpty() || roots.first().isEmpty())
        return fail(QStringLiteral("videos library root is not configured"));

    StreamLibraryEntry libraryEntry = m_library ? m_library->get(imdbId) : StreamLibraryEntry{};
    BulkPlanInput input;
    input.seriesId = imdbId;
    input.seriesTitle = libraryEntry.name.isEmpty() ? imdbId : libraryEntry.name;
    input.seriesYear = libraryEntry.year;
    input.seasonNumber = season;
    input.videosRootPath = roots.first();

    QSet<int> filter;
    for (int ep : episodeFilter)
        filter.insert(ep);
    for (const StreamEpisode& ep : allEpisodes) {
        if (!filter.isEmpty() && !filter.contains(ep.episode))
            continue;
        BulkPlanEpisodeInput row;
        row.season = season;
        row.episode = ep.episode;
        row.title = ep.title;
        row.extensionHint = QStringLiteral("mkv");
        input.episodes.push_back(row);
    }
    if (input.episodes.isEmpty())
        return fail(QStringLiteral("no matching episodes for requested dispatch"));

    BulkPlanResult plan = buildBulkPlan(input, [](const QString& path) {
        return QFileInfo::exists(path);
    });

    BulkSourceCollectionPayload payload;
    bool collectorTimedOut = false;
    {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        auto* collector = new BulkSourceCollector(m_addonRegistry, this);
        connect(collector, &BulkSourceCollector::collectionComplete,
                &loop, [&](const BulkSourceCollectionPayload& p) {
            payload = p;
            loop.quit();
        });
        connect(collector, &BulkSourceCollector::cancelled, &loop, [&]() {
            payload.cancelled = true;
            loop.quit();
        });
        connect(&timer, &QTimer::timeout, &loop, [&]() {
            collectorTimedOut = true;
            collector->cancel();
            loop.quit();
        });
        collector->begin(input);
        timer.start(55000);
        loop.exec();
        collector->disconnect();
        collector->deleteLater();
    }

    if (collectorTimedOut)
        return fail(QStringLiteral("source collection timeout"));
    if (payload.cancelled)
        return fail(QStringLiteral("source collection cancelled"));

    BulkSelectionPlan selection = buildBulkSelection(plan, payload);
    BulkPackVerificationResult verifierOutput;
    verifierOutput.updatedPlan = selection;

    if (selection.mode == BulkSelectionMode::Pack) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        bool finished = false;
        auto* verifier = new BulkPackVerifier(m_torrentClient, this);
        connect(verifier, &BulkPackVerifier::verificationComplete,
                &loop, [&](const BulkPackVerificationResult& result) {
            verifierOutput = result;
            finished = true;
            loop.quit();
        });
        connect(verifier, &BulkPackVerifier::verificationFailed,
                &loop, [&](const QString& reason) {
            Q_UNUSED(reason);
            finished = true;
            loop.quit();
        });
        connect(&timer, &QTimer::timeout, &loop, [&]() {
            verifier->cancel();
            loop.quit();
        });
        verifier->begin(selection, input.seasonNumber);
        timer.start(15000);
        loop.exec();
        verifier->disconnect();
        verifier->deleteLater();
        if (!finished)
            return fail(QStringLiteral("pack verification timeout"));
    }

    QHash<QString, BulkPlanItem> planByKey;
    for (const BulkPlanItem& item : plan.items)
        planByKey.insert(item.itemKey, item);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    StreamBulkGroupRecord group;
    group.groupId = QStringLiteral("stream:%1:s%2:%3")
        .arg(input.seriesId)
        .arg(input.seasonNumber, 2, 10, QChar('0'))
        .arg(now);
    group.groupKind = QStringLiteral("streamSeason");
    group.label = QStringLiteral("%1 - Season %2")
        .arg(input.seriesTitle)
        .arg(input.seasonNumber);
    group.sourceSeriesId = input.seriesId;
    group.sourceSeason = input.seasonNumber;
    group.destinationRoot = input.videosRootPath;
    group.createdAtMs = now;
    group.updatedAtMs = now;

    QString firstInfoHash;
    for (const BulkSelectionItem& selected : verifierOutput.updatedPlan.items) {
        const BulkPlanItem planItem = planByKey.value(selected.itemKey);
        if (planItem.itemKey.isEmpty())
            continue;
        StreamBulkGroupItem item;
        item.itemKey = planItem.itemKey;
        item.destinationKey = planItem.destinationKey;
        item.canonicalFilename = planItem.canonicalFilename;
        if (selected.reason == BulkSelectionReason::MissingNoSource) {
            item.itemState = StreamBulkItemState::MissingSource;
            item.lastError = tr("No source found for episode");
        } else {
            item.infoHash = selected.choice.infoHash;
            item.fileIndex = selected.choice.fileIndex;
            item.itemState = StreamBulkItemState::Pending;
            if (firstInfoHash.isEmpty())
                firstInfoHash = item.infoHash;
        }
        group.items.push_back(item);
    }

    if (group.items.isEmpty())
        return fail(QStringLiteral("selection produced no dispatchable items"));

    m_torrentClient->dispatchStreamBulkGroup(group, verifierOutput);

    QJsonObject out;
    out[QStringLiteral("status")] = QStringLiteral("dispatched");
    out[QStringLiteral("groupId")] = group.groupId;
    out[QStringLiteral("infoHash")] = firstInfoHash;
    out[QStringLiteral("items")] = group.items.size();
    out[QStringLiteral("mode")] =
        verifierOutput.updatedPlan.mode == BulkSelectionMode::Pack
            ? QStringLiteral("pack")
            : QStringLiteral("per-episode");
    return out;
}
void StreamPage::retryBulkSeasonDownload(const QString& groupId, const QStringList& itemKeys)
{
    using namespace tankostream::stream;

    cancelBulkSeasonDownload();

    if (!m_torrentClient || groupId.isEmpty() || itemKeys.isEmpty())
        return;

    const QJsonObject groupObj = m_torrentClient->streamBulkGroups().value(groupId).toObject();
    if (groupObj.isEmpty())
        return;

    const StreamBulkGroupRecord group = streamBulkGroupRecordFromJson(groupId, groupObj);
    QSet<QString> retryKeys;
    for (const QString& itemKey : itemKeys)
        retryKeys.insert(itemKey);

    m_bulkRetryMode = true;
    m_bulkRetryGroupId = groupId;
    m_bulkRetryItemKeys = itemKeys;
    m_bulkVerificationNote.clear();
    m_bulkSourcePayload = BulkSourceCollectionPayload{};

    m_bulkInput = BulkPlanInput{};
    m_bulkInput.seriesId = group.sourceSeriesId;
    m_bulkInput.seriesTitle = group.label;
    if (m_bulkInput.seriesTitle.isEmpty())
        m_bulkInput.seriesTitle = group.sourceSeriesId;
    m_bulkInput.seasonNumber = group.sourceSeason;
    m_bulkInput.videosRootPath = group.destinationRoot;

    m_bulkPlanResult = BulkPlanResult{};
    for (const StreamBulkGroupItem& groupItem : group.items) {
        if (!retryKeys.contains(groupItem.itemKey))
            continue;
        const int episodeNum = episodeNumberFromBulkItemKey(groupItem.itemKey);
        if (episodeNum <= 0)
            continue;
        const int seasonNum = group.sourceSeason > 0
            ? group.sourceSeason
            : seasonNumberFromBulkItemKey(groupItem.itemKey);

        BulkPlanEpisodeInput episode;
        episode.season = seasonNum;
        episode.episode = episodeNum;
        episode.extensionHint = QStringLiteral("mkv");
        m_bulkInput.episodes.push_back(episode);

        BulkPlanItem planItem;
        planItem.input = episode;
        planItem.canonicalFilename = groupItem.canonicalFilename;
        planItem.canonicalRelativePath = groupItem.destinationKey;
        planItem.canonicalAbsolutePath = group.destinationRoot.isEmpty()
            ? QString()
            : QDir(group.destinationRoot).filePath(groupItem.destinationKey);
        planItem.status = BulkPlanItemStatus::PendingSource;
        planItem.itemKey = groupItem.itemKey;
        planItem.destinationKey = groupItem.destinationKey;
        m_bulkPlanResult.items.push_back(planItem);
    }

    if (m_bulkInput.seasonNumber <= 0 && !m_bulkInput.episodes.isEmpty())
        m_bulkInput.seasonNumber = m_bulkInput.episodes.first().season;

    if (m_bulkInput.episodes.isEmpty() || m_bulkInput.seasonNumber <= 0) {
        m_bulkRetryMode = false;
        m_bulkRetryGroupId.clear();
        m_bulkRetryItemKeys.clear();
        return;
    }

    m_bulkProgressDialog = new QDialog(this);
    m_bulkProgressDialog->setWindowTitle(tr("Retrying stream download"));
    m_bulkProgressDialog->setModal(false);
    auto* layout = new QVBoxLayout(m_bulkProgressDialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    m_bulkProgressLabel = new QLabel(tr("Resolving retry sources..."), m_bulkProgressDialog);
    m_bulkProgressLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_bulkProgressLabel);
    m_bulkProgressBar = new QProgressBar(m_bulkProgressDialog);
    m_bulkProgressBar->setRange(0, 0);
    layout->addWidget(m_bulkProgressBar);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_bulkProgressDialog);
    connect(buttons, &QDialogButtonBox::rejected, this, &StreamPage::cancelBulkSeasonDownload);
    layout->addWidget(buttons);
    connect(m_bulkProgressDialog, &QDialog::rejected,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkProgressDialog->show();

    m_bulkSourceCollector = new BulkSourceCollector(m_addonRegistry, this);
    connect(m_bulkSourceCollector, &BulkSourceCollector::progressTick,
            this, [this](int resolved, int total) {
                if (m_bulkProgressLabel) {
                    m_bulkProgressLabel->setText(
                        tr("Resolving retry sources... %1/%2").arg(resolved).arg(total));
                }
            });
    connect(m_bulkSourceCollector, &BulkSourceCollector::collectionComplete,
            this, &StreamPage::onBulkSourcesCollected);
    connect(m_bulkSourceCollector, &BulkSourceCollector::cancelled,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkSourceCollector->begin(m_bulkInput);
}

void StreamPage::cancelBulkSeasonDownload()
{
    if (m_bulkSourceCollector) {
        auto* collector = m_bulkSourceCollector;
        m_bulkSourceCollector = nullptr;
        collector->disconnect(this);
        collector->cancel();
        collector->deleteLater();
    }
    if (m_bulkPackVerifier) {
        auto* verifier = m_bulkPackVerifier;
        m_bulkPackVerifier = nullptr;
        verifier->disconnect(this);
        verifier->cancel();
        verifier->deleteLater();
    }
    if (m_bulkProgressDialog) {
        auto* dialog = m_bulkProgressDialog;
        m_bulkProgressDialog = nullptr;
        m_bulkProgressLabel = nullptr;
        m_bulkProgressBar = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
    m_bulkRetryMode = false;
    m_bulkRetryGroupId.clear();
    m_bulkRetryItemKeys.clear();
}

void StreamPage::onBulkSourcesCollected(const tankostream::stream::BulkSourceCollectionPayload& payload)
{
    using namespace tankostream::stream;

    if (payload.cancelled) {
        cancelBulkSeasonDownload();
        return;
    }
    m_bulkSourcePayload = payload;
    if (m_bulkSourceCollector) {
        m_bulkSourceCollector->deleteLater();
        m_bulkSourceCollector = nullptr;
    }

    BulkSourceCollectionPayload selectionPayload = m_bulkSourcePayload;
    if (m_bulkRetryMode) {
        for (auto it = selectionPayload.byEpisode.begin(); it != selectionPayload.byEpisode.end(); ++it) {
            QList<StreamPickerChoice> filtered;
            for (const StreamPickerChoice& choice : it->choices) {
                if (choice.packType != QLatin1String("season"))
                    filtered.push_back(choice);
            }
            it->choices = filtered;
        }
    }

    BulkSelectionPlan selection = buildBulkSelection(m_bulkPlanResult, selectionPayload);
    if (m_bulkRetryMode) {
        BulkPackVerificationResult result;
        result.updatedPlan = selection;
        onBulkPackVerified(result);
        return;
    }

    if (selection.mode == BulkSelectionMode::Pack && m_torrentClient) {
        if (m_bulkProgressLabel)
            m_bulkProgressLabel->setText(tr("Verifying pack..."));
        m_bulkPackVerifier = new BulkPackVerifier(m_torrentClient, this);
        connect(m_bulkPackVerifier, &BulkPackVerifier::verificationComplete,
                this, &StreamPage::onBulkPackVerified);
        connect(m_bulkPackVerifier, &BulkPackVerifier::verificationFailed,
                this, &StreamPage::onBulkPackVerificationFailed);
        connect(m_bulkPackVerifier, &BulkPackVerifier::cancelled,
                this, &StreamPage::cancelBulkSeasonDownload);
        m_bulkPackVerifier->begin(selection, m_bulkInput.seasonNumber);
        return;
    }

    BulkPackVerificationResult result;
    result.updatedPlan = selection;
    onBulkPackVerified(result);
}

void StreamPage::onBulkPackVerificationFailed(const QString& reason)
{
    using namespace tankostream::stream;

    if (m_bulkPackVerifier) {
        m_bulkPackVerifier->deleteLater();
        m_bulkPackVerifier = nullptr;
    }
    m_bulkVerificationNote = tr("Pack verification failed; using per-episode sources.");
    if (!reason.isEmpty())
        m_bulkVerificationNote += QStringLiteral(" ") + reason;

    BulkSourceCollectionPayload fallbackPayload = m_bulkSourcePayload;
    for (auto it = fallbackPayload.byEpisode.begin(); it != fallbackPayload.byEpisode.end(); ++it) {
        QList<StreamPickerChoice> filtered;
        for (const StreamPickerChoice& choice : it->choices) {
            if (choice.packType != QLatin1String("season"))
                filtered.push_back(choice);
        }
        it->choices = filtered;
    }

    BulkPackVerificationResult result;
    result.updatedPlan = buildBulkSelection(m_bulkPlanResult, fallbackPayload);
    onBulkPackVerified(result);
}

void StreamPage::onBulkPackVerified(const tankostream::stream::BulkPackVerificationResult& result)
{
    using namespace tankostream::stream;

    if (m_bulkPackVerifier) {
        m_bulkPackVerifier->deleteLater();
        m_bulkPackVerifier = nullptr;
    }
    if (m_bulkProgressDialog) {
        auto* dialog = m_bulkProgressDialog;
        m_bulkProgressDialog = nullptr;
        m_bulkProgressLabel = nullptr;
        m_bulkProgressBar = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }

    if (m_bulkRetryMode) {
        const QString retryGroupId = m_bulkRetryGroupId;
        const QStringList retryItemKeys = m_bulkRetryItemKeys;
        m_bulkRetryMode = false;
        m_bulkRetryGroupId.clear();
        m_bulkRetryItemKeys.clear();

        if (!m_torrentClient || retryGroupId.isEmpty())
            return;

        const QJsonObject groupObj = m_torrentClient->streamBulkGroups().value(retryGroupId).toObject();
        if (groupObj.isEmpty())
            return;

        for (const BulkSelectionItem& item : result.updatedPlan.items) {
            if (item.reason == BulkSelectionReason::MissingNoSource) {
                m_torrentClient->updateStreamBulkGroupItemState(
                    retryGroupId,
                    item.itemKey,
                    StreamBulkItemState::MissingSource,
                    tr("No source found during retry"));
            }
        }

        BulkPackVerificationResult retryResult = result;
        StreamBulkGroupRecord group = streamBulkGroupRecordFromJson(
            retryGroupId,
            m_torrentClient->streamBulkGroups().value(retryGroupId).toObject());
        if (!group.items.isEmpty())
            m_torrentClient->dispatchStreamBulkGroup(group, retryResult);
        Q_UNUSED(retryItemKeys);
        return;
    }

    const QString displayLabel = QStringLiteral("%1 - Season %2")
        .arg(m_bulkInput.seriesTitle)
        .arg(m_bulkInput.seasonNumber);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — no preflight dialog.
    // Direct dispatch path (Spec §1 "no popup, no modal, no dialog").
    // All items from the verified selection plan are included; no picker filter.
    qInfo("STREAM_BULK direct dispatch: group=%s:%d items=%d",
          qUtf8Printable(m_bulkInput.seriesId), m_bulkInput.seasonNumber,
          (int)result.updatedPlan.items.size());

    QHash<QString, BulkPlanItem> planItemByKey;
    for (const BulkPlanItem& item : m_bulkPlanResult.items)
        planItemByKey.insert(item.itemKey, item);

    StreamBulkGroupRecord group;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    group.groupId = QStringLiteral("stream:%1:s%2:%3")
        .arg(m_bulkInput.seriesId)
        .arg(m_bulkInput.seasonNumber, 2, 10, QChar('0'))
        .arg(now);
    group.groupKind = QStringLiteral("streamSeason");
    group.label = displayLabel;
    group.sourceSeriesId = m_bulkInput.seriesId;
    group.sourceSeason = m_bulkInput.seasonNumber;
    group.destinationRoot = m_bulkInput.videosRootPath;
    group.createdAtMs = now;
    group.updatedAtMs = now;

    for (const BulkSelectionItem& selectionItem : result.updatedPlan.items) {
        const BulkPlanItem planItem = planItemByKey.value(selectionItem.itemKey);
        if (planItem.itemKey.isEmpty())
            continue;
        StreamBulkGroupItem groupItem;
        groupItem.itemKey = planItem.itemKey;
        groupItem.destinationKey = planItem.destinationKey;
        groupItem.canonicalFilename = planItem.canonicalFilename;
        if (selectionItem.reason == BulkSelectionReason::MissingNoSource) {
            groupItem.itemState = StreamBulkItemState::MissingSource;
            groupItem.lastError = tr("No source found for episode");
        } else if (selectionItem.reason == BulkSelectionReason::Picked ||
                   selectionItem.reason == BulkSelectionReason::PackCovered) {
            groupItem.infoHash = selectionItem.choice.infoHash;
            groupItem.fileIndex = selectionItem.choice.fileIndex;
            groupItem.itemState = StreamBulkItemState::Pending;
        } else {
            continue;
        }
        group.items.push_back(groupItem);
    }

    if (!group.items.isEmpty())
        emit streamBulkDispatchRequested(group, result, displayLabel);
    m_bulkVerificationNote.clear();
}

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” handler for the detail
// view's "downloaded episode click" path. Runs the SubtitlesAggregator
// fan-out with a synthetic Stream so the popover behaves identically to
// streamed playback (Spec Â§10.2), then re-emits the public signal up to
// MainWindow which actually opens the VideoPlayer.
//
// NOTE on StreamProgress (Spec Â§10.1): the existing time_update / progress
// flow runs through CoreBridge::saveProgress at the bridge layer. Local-file
// playback via VideoPlayer currently writes under the `videos` domain â€” the
// epKey/`stream`-domain bridging is a separable follow-up (it requires
// teaching VideoPlayer to write per-episode stream-domain progress when
// opened via this gateway). The Continue Watching strip already picks up
// `stream:imdb:sNN:eMM` keys; once the bridging is wired, these sessions
// will surface there. Phase 4 ships the open + signal + subtitles flow;
// continue-watching surfacing for downloaded-from-stream episodes is
// tracked as a follow-up.
void StreamPage::onDetailPlayLocalFileFromStream(
    const QString& localPath, const QString& imdbId,
    const QString& showTitle, int season, int episode)
{
    // Spec Â§10.2 â€” synthetic Stream + SubtitlesAggregator query so OpenSubs
    // and any other subtitle-resource addons populate the popover. videoHash
    // is intentionally empty: we don't compute the OpenSubs SubDB hash for
    // local files; the addon falls back to imdbId+filename matching.
    if (m_subtitlesAggregator && !imdbId.isEmpty() && season > 0 && episode > 0) {
        tankostream::stream::SubtitleLoadRequest req;
        req.type = QStringLiteral("series");
        req.id   = imdbId + QLatin1Char(':')
                          + QString::number(season) + QLatin1Char(':')
                          + QString::number(episode);

        tankostream::addon::Stream synth;
        const QFileInfo fi(localPath);
        synth.behaviorHints.filename  = fi.fileName();
        synth.behaviorHints.videoSize = fi.size();
        synth.behaviorHints.videoHash = QString();   // omitted for local files
        synth.name                    = showTitle;
        req.selectedStream = synth;

        m_subtitlesAggregator->load(req);
    }

    // Forward to MainWindow to actually open the local-file VideoPlayer.
    emit playLocalFileFromStreamRequested(localPath, imdbId, showTitle, season, episode);
}

void StreamPage::onSourceActivated(const tankostream::stream::StreamPickerChoice& choice)
{
    if (!m_session.pending.valid) return;   // late click after the user backed out

    // Phase 2 Batch 2.4 â€” if the user clicked a source card manually during
    // the auto-launch window, cancel the pending timer + hide the toast.
    // onAutoLaunchFire calls back into us for the automated path, but by the
    // time we're here the timer should already be stopped â€” this is the
    // user-click entry.
    if (m_autoLaunchTimer && m_autoLaunchTimer->isActive()) {
        cancelAutoLaunch();
    }

    const PendingPlay ctx = m_session.pending;
    m_session.pending.valid = false;

    // STREAM_NAV_BACK_STACK 2026-05-07 â€” Hemanth follow-up: closing the
    // player from a Season 2 episode was restoring the Detail view back
    // to Season 1 because the NavEntry.detailPreselectSeason was frozen
    // at the initial showDetail() call (typically -1 for "no
    // preselection" â†’ defaults to season 1). The user-driven season
    // combo change inside StreamDetailView never propagated to the
    // stack entry. Update the top Detail entry's preselects here, where
    // we have the actually-playing season/episode in `ctx`. By the time
    // launchPlayer's m_beforePlayerEntry snapshot fires, the top of
    // m_navStack already reflects what they were watching, so close +
    // restore lands on the correct season. Non-series media (movies,
    // ad-hoc trailers) skip the update â€” ctx.season/episode are 0/0
    // there and we don't want to clobber the entry's defaults.
    if (!m_navStack.isEmpty()
     && m_navStack.top().kind == NavEntry::Kind::Detail
     && ctx.mediaType == QLatin1String("series")
     && ctx.season > 0
     && ctx.episode > 0) {
        m_navStack.top().detailPreselectSeason  = ctx.season;
        m_navStack.top().detailPreselectEpisode = ctx.episode;
    }

    // STREAM_LIFECYCLE_FIX Phase 4 Batch 4.2 â€” audit P2-2 close. Pre-4.2 code
    // reset only m_session.nearEndCrossed + m_session.nextPrefetch inline,
    // skipping m_session.nextShortcutPending + the MetaAggregator/StreamAggregator
    // disconnect logic. resetNextEpisodePrefetch() is the canonical cleanup
    // that Phase 1 Batch 1.3 migrated to route through m_session â€” using it
    // here ensures all three prefetch-related session fields clear uniformly
    // AND in-flight aggregator connections from the prior episode's prefetch
    // get dropped (preventing a stale streamsReady lambda from landing against
    // the NEW episode's prefetch slot with the old episode's stream list).
    // Note: resetSession() would be over-broad â€” it also clears m_session.epKey
    // and pending, which onSourceActivated is about to re-install via ctx. The
    // narrower resetNextEpisodePrefetch is the right choice.
    resetNextEpisodePrefetch();

    // Save choice for re-use â€” extended payload with addon + source-kind fields.
    // Same persistence shape the dialog used to write so loadChoice in
    // future onPlayRequested calls keeps working.
    QJsonObject saved;
    saved["sourceKind"]    = choice.sourceKind;
    saved["addonId"]       = choice.addonId;
    saved["addonName"]     = choice.addonName;
    saved["magnetUri"]     = choice.magnetUri;
    saved["infoHash"]      = choice.infoHash;
    saved["fileIndex"]     = choice.fileIndex;
    saved["fileNameHint"]  = choice.fileNameHint;
    saved["directUrl"]     = choice.stream.source.url.toString();
    saved["youtubeId"]     = choice.stream.source.youtubeId;
    saved["quality"]       = choice.stream.behaviorHints.other
                                 .value("qualityLabel").toString();
    saved["trackerSource"] = choice.stream.behaviorHints.other
                                 .value("trackerSource").toString();
    saved["bingeGroup"]    = choice.stream.behaviorHints.bingeGroup;
    // Phase 2 Batch 2.4 â€” stamp the "last-used" timestamp so the auto-launch
    // 10-minute gate has a read source for both per-episode and per-series
    // resume paths.
    saved["updatedAt"]     = QDateTime::currentMSecsSinceEpoch();
    StreamChoices::saveChoice(ctx.epKey, saved);

    // Phase 2 Batch 2.3 â€” series-level source memory. When the addon
    // declares `behaviorHints.bingeGroup` on this stream, persist a parallel
    // per-series entry so the next episode's picker can default-highlight a
    // matching source from the same release group. Movies don't need this
    // layer â€” per-movie saveChoice covers single-title state already.
    if (ctx.mediaType == QLatin1String("series")
        && !choice.stream.behaviorHints.bingeGroup.isEmpty()) {
        StreamChoices::saveSeriesChoice(ctx.imdbId, saved);
    }

    m_session.epKey = ctx.epKey;

    // THEATRE_DOWNLOAD_ONLY P1.1 (2026-05-29) â€” the user picked a specific
    // source card. Download-only: if we already own this episode/movie play
    // it locally; otherwise download the source the user just picked. The
    // prior code streamed choice.stream through StreamPlayerController + showed
    // the buffer overlay; both are gone. Funnel through beginPlayOrDownload,
    // passing the picked choice so the not-owned path honors the user's pick.
    beginPlayOrDownload(ctx.imdbId, ctx.mediaType, ctx.season, ctx.episode,
                        &choice);
}

// THEATRE_DOWNLOAD_ONLY P1.1 (2026-05-29) â€” single reroute funnel for the
// "play" entry points. Download-only Theatre: if the file is already on disk
// play it locally (reusing the existing playLocalFileFromStreamRequested
// emit that MainWindow turns into a VideoPlayer::openFile); otherwise route
// to the existing download flow â€” never stream. When `picked` is non-null
// (the user already chose a specific source card) the not-owned branch
// downloads that exact source via onDirectDownloadRequested; otherwise it
// opens the per-episode download flow (the same path the [Download] affordance
// drives). Does NOT reimplement any download logic.
void StreamPage::beginPlayOrDownload(const QString& imdbId,
                                     const QString& mediaType,
                                     int season, int episode,
                                     const tankostream::stream::StreamPickerChoice* picked)
{
    if (m_streamDownloadIndex && !imdbId.isEmpty()) {
        const bool isMovie = (mediaType == QLatin1String("movie"));
        const std::optional<QString> owned =
            isMovie ? m_streamDownloadIndex->filePathForMovie(imdbId)
                    : m_streamDownloadIndex->filePathFor(imdbId, season, episode);
        if (owned.has_value() && QFileInfo::exists(*owned)) {
            const QString showTitle = m_detailView ? m_detailView->currentTitle()
                                                    : QString();
            emit playLocalFileFromStreamRequested(*owned, imdbId, showTitle,
                                                  season, episode);
            return;
        }
    }

    // Not owned â€” open the download flow.
    if (picked) {
        // Honor the source the user already picked. onDirectDownloadRequested
        // resolves the magnet/infoHash and dispatches via
        // TorrentClient::startDownload, branching season identity off the
        // detail view (the canonical existing per-source download path). For
        // non-magnet picks it no-ops defensively (direct-URL sources are a
        // spec Â§4 non-goal).
        onDirectDownloadRequested(*picked);
        return;
    }

    // THEATRE_STREAMING_RESTORE P1 (2026-06-09) — the "watch" click streams the
    // auto-picked source (forStream=true) instead of silently downloading.
    // Explicit download actions call startAutoDownload with the default
    // forStream=false (onSingleEpisodeDownloadRequested) or route through the
    // bulk/direct download paths — download is kept (Hemanth hybrid).
    startAutoDownload(imdbId, mediaType, season, episode, /*forStream=*/true);
}
