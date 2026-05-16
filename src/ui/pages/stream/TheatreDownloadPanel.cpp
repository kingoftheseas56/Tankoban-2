// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task C1) - TheatreDownloadPanel
// skeleton implementation. PackList <-> ScopePicker state machine wired;
// pack-row + tile rendering deferred to Tasks C2/C3/D1/D2/D4.

#include "ui/pages/stream/TheatreDownloadPanel.h"

#include "core/stream/BulkPackVerifier.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "ui/dialogs/AddTorrentDialog.h"  // for AddTorrentConfig struct only
#include "ui/pages/stream/EpisodeTile.h"
#include "ui/pages/stream/PackListItem.h"

#include <algorithm>

#include <QDebug>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// Property-name + dimension-value constants for the filter-chip dynamic
// property API. Centralized to prevent typos that would silently break
// the single-select sweep (per code-review I2, 2026-05-16).
constexpr const char* kPropDimension = "filterDimension";
constexpr const char* kPropValue     = "filterValue";
constexpr const char* kDimType       = "type";
constexpr const char* kDimSource     = "source";

// THEATRE_DOWNLOAD_OVERHAUL Task D2 (2026-05-16): pack (season, episode) into
// the QMap<quint32, bool> m_tileChecked key. Mirrors the keying scheme
// documented in TheatreDownloadPanel.h:110.
quint32 tileKey(int season, int episode) {
    return (static_cast<quint32>(season) << 16) | static_cast<quint32>(episode);
}

// THEATRE_DOWNLOAD_OVERHAUL Task C3 (2026-05-16): file-local helper to
// construct a filter chip with consistent QSS + sizing.
QPushButton* makeFilterChip(const QString& text, bool isActive, QWidget* parent) {
    auto* chip = new QPushButton(text, parent);
    chip->setCheckable(true);
    chip->setChecked(isActive);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setFixedHeight(26);
    chip->setObjectName(QStringLiteral("TheatreFilterChip"));
    chip->setStyleSheet(
        "QPushButton#TheatreFilterChip {"
        "  background: transparent;"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.62);"
        "  padding: 0 6px;"
        "  font-size: 10px; font-weight: 500; }"
        "QPushButton#TheatreFilterChip:hover {"
        "  background: rgba(255,255,255,0.06); }"
        "QPushButton#TheatreFilterChip:checked {"
        "  background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.26);"
        "  color: #f3f4f6; font-weight: 600; }");
    return chip;
}

}  // namespace

namespace tankoban::stream::theatre {

TheatreDownloadPanel::TheatreDownloadPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("TheatreDownloadPanel"));
    buildUI();
}

void TheatreDownloadPanel::setSearchEngine(UnifiedPackSearchEngine* engine) {
    if (m_searchEngine == engine) return;
    if (m_searchEngine) {
        disconnect(m_searchEngine, nullptr, this, nullptr);
    }
    m_searchEngine = engine;
    if (m_searchEngine) {
        connect(m_searchEngine, &UnifiedPackSearchEngine::packResults,
                this, &TheatreDownloadPanel::onPackResults);
        connect(m_searchEngine, &UnifiedPackSearchEngine::searchComplete,
                this, &TheatreDownloadPanel::onSearchComplete);
    }
}

void TheatreDownloadPanel::setStreamDownloadIndex(StreamDownloadIndex* idx) {
    m_downloadIndex = idx;
}

void TheatreDownloadPanel::setTorrentClient(TorrentClient* client) {
    if (m_torrentClient == client) return;
    if (m_torrentClient && m_torrentClient->engine()) {
        disconnect(m_torrentClient->engine(), &TorrentEngine::metadataReady,
                   this, &TheatreDownloadPanel::onMetadataReady);
    }
    m_torrentClient = client;
    if (m_torrentClient && m_torrentClient->engine()) {
        connect(m_torrentClient->engine(), &TorrentEngine::metadataReady,
                this, &TheatreDownloadPanel::onMetadataReady,
                Qt::QueuedConnection);
    }
}

void TheatreDownloadPanel::openFor(const QString& imdbId,
                                   const QString& showName,
                                   int season,
                                   const QString& mediaType) {
    m_imdbId    = imdbId;
    m_showName  = showName;
    m_season    = season;
    m_mediaType = mediaType;
    m_packs.clear();
    m_filteredPacks.clear();
    m_widenedAutoFallback = false;
    m_tileChecked.clear();

    if (m_packHeading) {
        // ASCII-only heading: use " - " separator (no middle-dot/U+00B7).
        const QString suffix = (mediaType == QLatin1String("movie"))
            ? QString()
            : (season > 0 ? QStringLiteral(" - Season %1").arg(season)
                          : QStringLiteral(" - Whole show"));
        m_packHeading->setText(tr("Download - %1%2").arg(showName, suffix));
    }
    if (m_statusLine)
        m_statusLine->setText(tr("Searching sources..."));
    if (m_loadingBar) m_loadingBar->show();

    transitionTo(State::PackList);
    if (m_searchEngine)
        m_searchEngine->search(imdbId, showName, season);
}

void TheatreDownloadPanel::reset() {
    m_packs.clear();
    m_filteredPacks.clear();
    m_tileChecked.clear();
    // THEATRE_DOWNLOAD_OVERHAUL Task D3 (2026-05-16): clear pending-metadata
    // state so closing+reopening the panel doesn't leak a stale hash that
    // could match a late metadataReady from a prior selection.
    m_pendingMetadataHash.clear();
    m_realFiles = QJsonArray();
    // I2 fix (code-quality review 2026-05-16): invalidate the derive-scope
    // cache on full reset.
    m_derivedScopeCacheKey.clear();
    m_derivedScopeCache = DerivedScope();
    if (m_packList) m_packList->clear();
    if (m_statusLine) m_statusLine->clear();
    transitionTo(State::PackList);
    if (m_loadingBar) m_loadingBar->hide();
}

void TheatreDownloadPanel::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, /*stretch=*/1);

    buildPackListState();
    buildScopePickerState();

    m_stack->addWidget(m_packListPage);     // index 0
    m_stack->addWidget(m_scopePickerPage);  // index 1
    m_stack->setCurrentIndex(0);
}

void TheatreDownloadPanel::buildPackListState() {
    m_packListPage = new QWidget(this);
    auto* col = new QVBoxLayout(m_packListPage);
    col->setContentsMargins(12, 12, 12, 12);
    col->setSpacing(8);

    m_packHeading = new QLabel(m_packListPage);
    m_packHeading->setObjectName(QStringLiteral("TheatreDownloadPackHeading"));
    m_packHeading->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #f3f4f6;");
    col->addWidget(m_packHeading);

    m_filterChipRow = new QWidget(m_packListPage);
    m_filterChipRow->setObjectName(QStringLiteral("TheatreDownloadFilterChipRow"));
    auto* chipLayout = new QHBoxLayout(m_filterChipRow);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(6);
    col->addWidget(m_filterChipRow);

    // THEATRE_DOWNLOAD_OVERHAUL Task C3 (2026-05-16): populate filter chip row
    // with two single-select dimension groups (Type + Source) per Codex
    // expansion 5.2.B. 10px visual gap separates the two groups.
    {
        // Type filter group: All / Complete Series / Multi-Season / Season Pack / Single Episode.
        const QStringList typeOptions = {
            QStringLiteral("All"),
            QStringLiteral("Complete Series"),
            QStringLiteral("Multi-Season"),
            QStringLiteral("Season Pack"),
            QStringLiteral("Single Episode"),
        };
        for (const QString& opt : typeOptions) {
            auto* chip = makeFilterChip(opt, opt == m_typeFilter, m_filterChipRow);
            chip->setProperty(kPropDimension, kDimType);
            chip->setProperty(kPropValue, opt);
            connect(chip, &QPushButton::clicked, this, &TheatreDownloadPanel::onFilterChipClicked);
            chipLayout->addWidget(chip);
        }

        chipLayout->addSpacing(10);  // visual separator between dimension groups

        // Source filter group: All sources / Stremio / Indexers.
        const QStringList sourceOptions = {
            QStringLiteral("All sources"),
            QStringLiteral("Stremio"),
            QStringLiteral("Indexers"),
        };
        for (const QString& opt : sourceOptions) {
            auto* chip = makeFilterChip(opt, opt == m_sourceFilter, m_filterChipRow);
            chip->setProperty(kPropDimension, kDimSource);
            chip->setProperty(kPropValue, opt);
            connect(chip, &QPushButton::clicked, this, &TheatreDownloadPanel::onFilterChipClicked);
            chipLayout->addWidget(chip);
        }

        chipLayout->addStretch();
    }

    m_statusLine = new QLabel(m_packListPage);
    m_statusLine->setObjectName(QStringLiteral("TheatreDownloadStatusLine"));
    m_statusLine->setStyleSheet(
        "font-size: 11px; color: rgba(255,255,255,0.56);");
    col->addWidget(m_statusLine);

    // THEATRE_DOWNLOAD_OVERHAUL Task C4 (2026-05-16): indeterminate 2px loading
    // bar pinned below the status line per Codex expansion 5.2.C. Shown on
    // openFor() search start, hidden on searchComplete + reset.
    m_loadingBar = new QProgressBar(m_packListPage);
    m_loadingBar->setObjectName(QStringLiteral("TheatreLoadingBar"));
    m_loadingBar->setMinimum(0);
    m_loadingBar->setMaximum(0);  // indeterminate
    m_loadingBar->setFixedHeight(2);
    m_loadingBar->setTextVisible(false);
    m_loadingBar->setStyleSheet(
        "QProgressBar#TheatreLoadingBar {"
        "  background: rgba(255,255,255,0.08);"
        "  border: none; border-radius: 1px; }"
        "QProgressBar#TheatreLoadingBar::chunk {"
        "  background: rgba(255,255,255,0.34); }");
    m_loadingBar->hide();  // hidden until openFor()
    col->addWidget(m_loadingBar);

    m_packList = new QListWidget(m_packListPage);
    m_packList->setObjectName(QStringLiteral("TheatreDownloadPackList"));
    m_packList->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 0; }");
    connect(m_packList, &QListWidget::currentRowChanged,
            this, &TheatreDownloadPanel::onPackRowSelected);
    col->addWidget(m_packList, /*stretch=*/1);
}

void TheatreDownloadPanel::buildScopePickerState() {
    m_scopePickerPage = new QWidget(this);
    auto* col = new QVBoxLayout(m_scopePickerPage);
    col->setContentsMargins(12, 12, 12, 12);
    col->setSpacing(8);

    auto* topRow = new QHBoxLayout();
    // ASCII-only back glyph: use "<" instead of left-arrow U+2190.
    m_scopeBackBtn = new QPushButton(QStringLiteral("<"), m_scopePickerPage);
    m_scopeBackBtn->setObjectName(QStringLiteral("TheatreScopeBackBtn"));
    m_scopeBackBtn->setFixedSize(30, 30);
    m_scopeBackBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scopeBackBtn, &QPushButton::clicked,
            this, &TheatreDownloadPanel::onScopeBackClicked);
    topRow->addWidget(m_scopeBackBtn);

    m_scopeHeading = new QLabel(m_scopePickerPage);
    m_scopeHeading->setObjectName(QStringLiteral("TheatreScopeHeading"));
    m_scopeHeading->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #f3f4f6;");
    topRow->addWidget(m_scopeHeading, /*stretch=*/1);
    col->addLayout(topRow);

    m_scopeStatusLine = new QLabel(m_scopePickerPage);
    m_scopeStatusLine->setObjectName(QStringLiteral("TheatreScopeStatusLine"));
    m_scopeStatusLine->setStyleSheet(
        "font-size: 10px; color: rgba(255,255,255,0.48);");
    col->addWidget(m_scopeStatusLine);

    m_scopeTileContainer = new QWidget(m_scopePickerPage);
    m_scopeTileContainer->setObjectName(QStringLiteral("TheatreScopeTileContainer"));
    auto* tileLayout = new QVBoxLayout(m_scopeTileContainer);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setSpacing(4);
    col->addWidget(m_scopeTileContainer, /*stretch=*/1);
    // Tile widgets get populated in Task D1+D2.

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_scopeCancelBtn = new QPushButton(tr("Cancel"), m_scopePickerPage);
    m_scopeCancelBtn->setObjectName(QStringLiteral("TheatreScopeCancelBtn"));
    m_scopeCancelBtn->setFixedHeight(30);
    m_scopeCancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scopeCancelBtn, &QPushButton::clicked, this, [this]() {
        reset();
        emit dismissRequested();
    });
    btnRow->addWidget(m_scopeCancelBtn);

    m_scopeDownloadBtn = new QPushButton(tr("Download"), m_scopePickerPage);
    m_scopeDownloadBtn->setObjectName(QStringLiteral("TheatreScopeDownloadBtn"));
    m_scopeDownloadBtn->setFixedHeight(30);
    m_scopeDownloadBtn->setCursor(Qt::PointingHandCursor);
    m_scopeDownloadBtn->setEnabled(false);
    connect(m_scopeDownloadBtn, &QPushButton::clicked,
            this, &TheatreDownloadPanel::onDownloadClicked);
    btnRow->addWidget(m_scopeDownloadBtn);
    col->addLayout(btnRow);
}

void TheatreDownloadPanel::transitionTo(State newState) {
    if (!m_stack) return;
    m_stack->setCurrentIndex(newState == State::ScopePicker ? 1 : 0);
}

void TheatreDownloadPanel::onPackResults(const QString& imdbId, int season,
                                        const QList<EnrichedPack>& results) {
    if (imdbId != m_imdbId || season != m_season)
        return;
    m_packs.append(results);
    rerenderPackList();
}

void TheatreDownloadPanel::onSearchComplete(const QString& imdbId, int season,
                                           int totalPacks) {
    if (imdbId != m_imdbId || season != m_season)
        return;
    // THEATRE_DOWNLOAD_OVERHAUL Task C4 (2026-05-16): hide loading bar before
    // any branching - autoFallbackToShowWide (C5) will re-show it if it fires.
    if (m_loadingBar) m_loadingBar->hide();
    if (totalPacks == 0 && !m_widenedAutoFallback) {
        autoFallbackToShowWide();
        return;
    }
    if (m_statusLine)
        m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
}

void TheatreDownloadPanel::onPackRowSelected(int row) {
    if (row < 0 || row >= m_filteredPacks.size())
        return;
    m_selectedPack = m_filteredPacks.at(row);
    m_scopeEstimate = estimate(m_selectedPack.raw.title);

    // THEATRE_DOWNLOAD_OVERHAUL Task D3 (2026-05-16): kick off real metadata
    // resolution so the scope picker tiles can refresh with real filenames +
    // sizes when libtorrent delivers them. The reset of m_pendingMetadataHash
    // + m_realFiles clears stale state from any prior pack selection in this
    // same panel-open cycle.
    m_pendingMetadataHash.clear();
    m_realFiles = QJsonArray();
    // I2 fix (code-quality review 2026-05-16): invalidate the derive-scope
    // cache (new pack = new files).
    m_derivedScopeCacheKey.clear();
    m_derivedScopeCache = DerivedScope();
    if (m_torrentClient && !m_selectedPack.raw.magnetUri.isEmpty()) {
        m_pendingMetadataHash = m_torrentClient->resolveMetadata(m_selectedPack.raw.magnetUri);
    }

    if (m_scopeHeading)
        m_scopeHeading->setText(m_selectedPack.raw.title);
    rerenderScopePicker();
    transitionTo(State::ScopePicker);
}

void TheatreDownloadPanel::onMetadataReady(const QString& infoHash, const QString& name,
                                           qint64 totalSize, const QJsonArray& files) {
    Q_UNUSED(name);
    Q_UNUSED(totalSize);
    // THEATRE_DOWNLOAD_OVERHAUL Task D3 (2026-05-16): only react to the hash
    // we're currently waiting on; other live resolutions in the engine (e.g.
    // from TankorentPage, BulkPackVerifier, StreamDetailView) MUST NOT
    // hijack our scope picker.
    if (infoHash != m_pendingMetadataHash) return;
    m_realFiles = files;
    // I2 fix (code-quality review 2026-05-16): invalidate the derive-scope
    // cache so the next rerender uses the freshly-arrived m_realFiles instead
    // of the prior empty result.
    m_derivedScopeCacheKey.clear();
    m_derivedScopeCache = DerivedScope();

    // Rebuild scope picker; Task D4 will use m_realFiles to drive
    // per-file priorities via BulkPackVerifier::matchEpisodeFileForSeason.
    rerenderScopePicker();
}

void TheatreDownloadPanel::onScopeBackClicked() {
    transitionTo(State::PackList);
}

void TheatreDownloadPanel::onDownloadClicked() {
    // THEATRE_DOWNLOAD_OVERHAUL Task D4 (2026-05-16): build AddTorrentConfig
    // from the user's tile selections + compute per-file priorities by running
    // BulkPackVerifier::matchEpisodeFileForSeason against each real file. Tile-
    // unchecked files get priority 0; non-episode files (no season-match - i.e.
    // samples, .nfo, extras) get priority 0 per brainstorm decision 19
    // (preemptive skip). Emits downloadRequested for the host to dispatch via
    // TorrentClient, then resets the panel for further picking.
    if (m_selectedPack.raw.magnetUri.isEmpty() && m_selectedPack.raw.infoHash.isEmpty())
        return;
    if (!m_torrentClient) return;

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value("videos");
    config.contentLayout   = QStringLiteral("original");
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = m_imdbId;
    config.season          = m_season;

    // F1 fix (Codex audit 2026-05-16): movie mode has no detectedSeasons, so
    // the season-iteration loop below would write priority 0 to every file
    // and libtorrent would download nothing. Handle movie mode explicitly.
    if (m_mediaType == QLatin1String("movie")) {
        // If the movie tile is unchecked, the button should have been disabled
        // by the live-update lambda. Defensive guard.
        if (!m_tileChecked.value(tileKey(0, 0), false)) {
            qWarning() << "TheatreDownloadPanel::onDownloadClicked: movie tile unchecked; aborting";
            return;
        }
        // Movie packs are typically a single main video file + samples/.nfo
        // extras. Find the largest .mkv/.mp4/.avi/.mov/.webm file and set its
        // priority to 4 (normal); everything else to 0 (skip). If no video
        // file is identifiable (metadata not yet arrived), leave filePriorities
        // empty so libtorrent downloads all files by default.
        if (!m_realFiles.isEmpty()) {
            qint64 bestSize = 0;
            int bestIdx = -1;
            for (int idx = 0; idx < m_realFiles.size(); ++idx) {
                const QJsonObject f = m_realFiles.at(idx).toObject();
                const QString path = f.value("path").toString();
                const qint64 size = f.value("size").toVariant().toLongLong();
                if (path.endsWith(QStringLiteral(".mkv"), Qt::CaseInsensitive)
                    || path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
                    || path.endsWith(QStringLiteral(".avi"), Qt::CaseInsensitive)
                    || path.endsWith(QStringLiteral(".mov"), Qt::CaseInsensitive)
                    || path.endsWith(QStringLiteral(".webm"), Qt::CaseInsensitive)) {
                    if (size > bestSize) {
                        bestSize = size;
                        bestIdx = idx;
                    }
                }
            }
            // I1 fix (code-quality review 2026-05-16): if no video-extension match
            // (e.g. pack uses .ts / .divx / .m2ts / .wmv / .mpg main file),
            // fall back to "largest file overall." Movie pack main features are
            // typically 95%+ of total size, so largest-overall is the right call
            // and avoids accidentally downloading samples/extras/.nfo files.
            if (bestIdx < 0) {
                for (int idx = 0; idx < m_realFiles.size(); ++idx) {
                    const qint64 size = m_realFiles.at(idx).toObject()
                        .value("size").toVariant().toLongLong();
                    if (size > bestSize) {
                        bestSize = size;
                        bestIdx = idx;
                    }
                }
            }
            if (bestIdx >= 0) {
                QMap<int, int> moviePriorities;
                QVector<int> movieSelected;
                for (int idx = 0; idx < m_realFiles.size(); ++idx) {
                    moviePriorities[idx] = (idx == bestIdx) ? 4 : 0;
                }
                movieSelected.append(bestIdx);
                config.filePriorities = moviePriorities;
                config.selectedIndices = movieSelected;
            }
            // If bestIdx < 0 (no video file found in metadata), leave filePriorities
            // empty -> libtorrent default downloads everything (safer than zeroing all).
        }
        // If m_realFiles is empty (metadata not arrived yet), leave filePriorities
        // empty -> libtorrent default behavior. Acceptable since the user
        // explicitly clicked Download knowing this is a movie pack.

        // Skip the series-mode loop below by jumping to hash resolution.
        goto hash_resolution;
    }

    // F2 fix companion: "Download entire pack" fallback tile (rendered by
    // rerenderScopePicker when both title-estimate AND file-probe yield no
    // seasons). This tile uses key (0,0) just like movie mode. If it's the
    // ONLY tile in m_tileChecked and we're not in movie mode, treat it as
    // "download all files, no priority filter."
    {
        const bool isEntirePackFallback = (m_mediaType != QLatin1String("movie"))
            && m_tileChecked.size() == 1
            && m_tileChecked.contains(tileKey(0, 0));
        if (isEntirePackFallback) {
            if (!m_tileChecked.value(tileKey(0, 0), false)) {
                qWarning() << "TheatreDownloadPanel::onDownloadClicked: entire-pack tile unchecked; aborting";
                return;
            }
            // Leave filePriorities + selectedIndices empty -> libtorrent default
            // downloads all files. Correct semantics for the "entire pack" path.
            goto hash_resolution;
        }
    }

    // Build filePriorities based on tile selection. For each real file in
    // m_realFiles, run BulkPackVerifier::matchEpisodeFileForSeason for each
    // detected season; if the matched (season, episode) is in m_tileChecked
    // and checked, set priority=4 (normal), else priority=0. Non-episode files
    // (samples, .nfo, extras) won't match any season and stay at priority 0.
    //
    // Note: if m_realFiles is empty (metadata not yet arrived OR same-pack
    // re-selection edge where libtorrent's metadata_received_alert is single-
    // shot per handle), the for-loop iterates zero times and filePriorities
    // stays empty. That's correct end-state for D4 - libtorrent will start the
    // torrent and download all files by default. Metadata-driven filtering
    // refinement is D5+ scope.
    {
        QMap<int, int> priorities;
        QVector<int> selectedIndices;
        for (int idx = 0; idx < m_realFiles.size(); ++idx) {
            QJsonObject file = m_realFiles.at(idx).toObject();
            if (!file.contains("index")) file.insert("index", idx);

            int matchedEp = 0;
            int matchedIdx = 0;
            bool keepFile = false;
            const auto seasons = m_scopeEstimate.detectedSeasons;
            for (int season : seasons) {
                const bool ok = tankostream::stream::BulkPackVerifier::matchEpisodeFileForSeason(
                    file, season, &matchedEp, &matchedIdx);
                if (ok && matchedEp > 0) {
                    const quint32 key = tileKey(season, matchedEp);
                    if (m_tileChecked.value(key, false)) {
                        keepFile = true;
                        selectedIndices.append(idx);
                    }
                    break;
                }
            }
            priorities[idx] = keepFile ? 4 : 0;
        }
        config.filePriorities = priorities;
        config.selectedIndices = selectedIndices;
    }

hash_resolution:
    // Dispatch via host signal. resolveMetadata() returns the canonical info-
    // hash for the magnet URI (cached if metadata already arrived; triggers a
    // fresh resolve otherwise).
    QString hash = m_pendingMetadataHash;
    if (hash.isEmpty()) hash = m_selectedPack.raw.infoHash;
    if (hash.isEmpty() && !m_selectedPack.raw.magnetUri.isEmpty())
        hash = m_torrentClient->resolveMetadata(m_selectedPack.raw.magnetUri);
    if (hash.isEmpty()) {
        qWarning() << "TheatreDownloadPanel::onDownloadClicked: empty hash; aborting";
        return;
    }

    emit downloadRequested(m_imdbId, m_season,
                           m_selectedPack.raw.magnetUri, hash, config);

    // Reset + return to PackList state for further picking.
    reset();
}

void TheatreDownloadPanel::onFilterChipClicked() {
    // THEATRE_DOWNLOAD_OVERHAUL Task C3 (2026-05-16): single-select-within-
    // dimension. Read sender's filterDimension/filterValue properties, update
    // the matching member, then uncheck all other chips in the same dimension
    // and re-render the pack list to apply the new filter.
    auto* sender = qobject_cast<QPushButton*>(QObject::sender());
    if (!sender) return;
    const QString dim = sender->property(kPropDimension).toString();
    const QString val = sender->property(kPropValue).toString();
    if (dim == QLatin1String(kDimType))
        m_typeFilter = val;
    else if (dim == QLatin1String(kDimSource))
        m_sourceFilter = val;

    // Single-select within dimension: uncheck other chips in same dimension.
    if (m_filterChipRow) {
        const auto chips = m_filterChipRow->findChildren<QPushButton*>(
            QStringLiteral("TheatreFilterChip"));
        for (auto* c : chips) {
            const QString cDim = c->property(kPropDimension).toString();
            const QString cVal = c->property(kPropValue).toString();
            if (cDim == dim)
                c->setChecked(cVal == val);
        }
    }
    rerenderPackList();
}

void TheatreDownloadPanel::rerenderPackList() {
    if (!m_packList) return;
    m_packList->clear();

    // THEATRE_DOWNLOAD_OVERHAUL Task C3 (2026-05-16): apply both filter
    // dimensions (Type + Source) before the score-descending sort.
    m_filteredPacks.clear();
    for (const auto& p : m_packs) {
        // Type filter: compare classification label against m_typeFilter.
        // "All" matches everything.
        if (m_typeFilter != QLatin1String("All")) {
            if (labelForType(p.classification.type) != m_typeFilter)
                continue;
        }
        // Source filter: "All sources" matches everything; "Stremio" only
        // matches PackSource::Stremio; "Indexers" only matches PackSource::Tankorent.
        if (m_sourceFilter == QLatin1String("Stremio") && p.source != PackSource::Stremio)
            continue;
        if (m_sourceFilter == QLatin1String("Indexers") && p.source != PackSource::Tankorent)
            continue;

        // Auto-fallback filter (C5): when widened, only show packs that
        // actually include the originally-requested season. Complete Series
        // packs implicitly include every season. Other packs must have
        // m_season in their classification.detectedSeasons set.
        if (m_widenedAutoFallback) {
            const bool isComplete = p.classification.isCompleteSeries;
            const bool includesRequested =
                isComplete || p.classification.detectedSeasons.contains(m_season);
            if (!includesRequested)
                continue;
        }

        m_filteredPacks.append(p);
    }

    // Sort by combinedScore descending.
    std::sort(m_filteredPacks.begin(), m_filteredPacks.end(),
        [](const EnrichedPack& a, const EnrichedPack& b) {
            return a.combinedScore > b.combinedScore;
        });

    for (int i = 0; i < m_filteredPacks.size(); ++i) {
        const auto& p = m_filteredPacks.at(i);
        auto* item = new QListWidgetItem(m_packList);
        auto* widget = new PackListItem(p, m_packList);
        item->setSizeHint(widget->minimumSizeHint());
        m_packList->addItem(item);
        m_packList->setItemWidget(item, widget);
        // I1 fix (2026-05-16 code review): route widget-click to QListWidget's
        // selection model so currentRowChanged fires onPackRowSelected normally.
        const int row = i;
        connect(widget, &PackListItem::clicked, this,
                [this, row]() { if (m_packList) m_packList->setCurrentRow(row); });
    }

    // Status line: distinguish post-search "N packs found" from post-filter
    // "N of M match" so late callbacks don't visually conflict with a filter
    // already in effect (per code-review I1, 2026-05-16).
    if (m_statusLine && !m_packs.isEmpty()) {
        const bool hasFilter = (m_typeFilter != QLatin1String("All"))
                            || (m_sourceFilter != QLatin1String("All sources"));
        if (hasFilter) {
            m_statusLine->setText(tr("%1 of %2 packs match")
                                       .arg(m_filteredPacks.size())
                                       .arg(m_packs.size()));
        } else {
            m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
        }
    }
}

void TheatreDownloadPanel::rerenderScopePicker() {
    // THEATRE_DOWNLOAD_OVERHAUL Task D2 (2026-05-16): populate
    // m_scopeTileContainer with EpisodeTile widgets grouped under per-season
    // headers. Movie mode renders a single degenerate tile. Live-update the
    // Download button label/state as the user toggles tiles. Already-have
    // detection runs through StreamDownloadIndex::filePathFor /
    // filePathForMovie.
    if (!m_scopeTileContainer) return;

    // Clear existing children. Use deleteLater so any in-flight queued slots
    // on a tile finish before the widget vanishes (tiles emit toggled from
    // their checkbox; safer than delete for queued cross-thread cases).
    auto* layout = qobject_cast<QVBoxLayout*>(m_scopeTileContainer->layout());
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    // F3 fix (Codex audit 2026-05-16): snapshot current tile state so we can
    // restore user selections across a metadata-driven re-render. Without
    // this, a late metadataReady would silently uncheck everything the user
    // had already selected via title-estimate tiles.
    const QMap<quint32, bool> previousChecked = m_tileChecked;
    m_tileChecked.clear();

    // Movie mode: degenerate single-tile.
    if (m_mediaType == QLatin1String("movie")) {
        EpisodeTileData d;
        d.season  = 0;
        d.episode = 0;
        d.title   = m_selectedPack.raw.title;
        d.sizeBytes = m_selectedPack.raw.sizeBytes;
        d.alreadyHave = m_downloadIndex
            && m_downloadIndex->filePathForMovie(m_imdbId).has_value();
        auto* tile = new EpisodeTile(d, m_scopeTileContainer);
        layout->addWidget(tile);
        const quint32 movieKey = tileKey(0, 0);
        // F3 fix: restore previously-checked state across metadata-driven re-renders.
        if (previousChecked.contains(movieKey)) {
            tile->setChecked(previousChecked.value(movieKey));
        }
        m_tileChecked[movieKey] = tile->isChecked();
        connect(tile, &EpisodeTile::toggled, this, [this, tile](bool checked) {
            m_tileChecked[tileKey(tile->season(), tile->episode())] = checked;
            // F1 fix (Codex audit 2026-05-16): movie-mode button must live-update
            // so the user can re-check an already-have movie if they want to
            // re-download, and so the unchecked state correctly disables the button.
            if (m_scopeDownloadBtn) {
                m_scopeDownloadBtn->setEnabled(checked);
            }
        });
        // Movie-mode action button label.
        if (m_scopeDownloadBtn) {
            // F1 fix: initial enable matches the tile's default-checked state from D1
            // (default-checked = !alreadyHave). User can toggle to override.
            m_scopeDownloadBtn->setEnabled(tile->isChecked());
            m_scopeDownloadBtn->setText(tr("Download - %1 GB")
                                          .arg(d.sizeBytes / 1'000'000'000.0, 0, 'f', 2));
        }
        return;
    }

    // Series mode: derive bySeason from title estimate (instant path) OR
    // from real file metadata (F2 fix cascade for complete-series packs
    // with no season tokens in the title).
    QMap<int, QList<EpisodeEstimate>> bySeason;
    for (const auto& ep : m_scopeEstimate.episodes)
        bySeason[ep.season].append(ep);

    // F2 fix (Codex audit 2026-05-16): if title estimate yielded nothing AND
    // real files are available, probe files for season tokens.
    // I2/I3 fix (code-quality review 2026-05-16): the derived result (cached
    // on m_pendingMetadataHash) also feeds the per-tile O(1) overlay lookup
    // below via tileKeyToFileIndex.
    DerivedScope derivedForOverlay;
    if (bySeason.isEmpty() && !m_realFiles.isEmpty()) {
        derivedForOverlay = deriveScopeFromFiles();
        bySeason = derivedForOverlay.episodesBySeason;
    } else if (!m_realFiles.isEmpty()) {
        // Title estimate produced bySeason; we still want the file-index map
        // so addSeasonGroup can do O(1) overlay lookups. deriveScopeFromFiles
        // is cache-keyed so this is one regex sweep total per pack.
        derivedForOverlay = deriveScopeFromFiles();
    }

    // F2 fix: if STILL empty after the probe, render a fallback path.
    if (bySeason.isEmpty()) {
        if (m_realFiles.isEmpty()) {
            // Metadata not yet arrived - show a loading placeholder.
            auto* placeholder = new QLabel(
                tr("Loading pack metadata..."), m_scopeTileContainer);
            placeholder->setStyleSheet(
                "font-size: 11px; color: rgba(255,255,255,0.48);"
                "padding: 16px 8px; font-style: italic;");
            placeholder->setAlignment(Qt::AlignCenter);
            layout->addWidget(placeholder);
            if (m_scopeDownloadBtn) m_scopeDownloadBtn->setEnabled(false);
            return;
        }
        // Metadata arrived but no season tokens found anywhere - fallback to
        // a single "Download entire pack" tile.
        EpisodeTileData d;
        d.season = 0;
        d.episode = 0;
        d.title = m_selectedPack.raw.title;
        d.sizeBytes = m_selectedPack.raw.sizeBytes;
        d.alreadyHave = false;
        auto* tile = new EpisodeTile(d, m_scopeTileContainer);
        layout->addWidget(tile);
        const quint32 fallbackKey = tileKey(0, 0);
        // F3 fix: restore previously-checked state across re-renders.
        if (previousChecked.contains(fallbackKey)) {
            tile->setChecked(previousChecked.value(fallbackKey));
        }
        m_tileChecked[fallbackKey] = tile->isChecked();
        connect(tile, &EpisodeTile::toggled, this, [this, tile](bool checked) {
            m_tileChecked[tileKey(tile->season(), tile->episode())] = checked;
            if (m_scopeDownloadBtn) m_scopeDownloadBtn->setEnabled(checked);
        });
        if (m_scopeDownloadBtn) {
            m_scopeDownloadBtn->setEnabled(tile->isChecked());
            m_scopeDownloadBtn->setText(tr("Download entire pack"));
        }
        return;
    }

    auto addSeasonGroup = [this, layout, &previousChecked, &derivedForOverlay](int season, const QList<EpisodeEstimate>& eps) {
        auto* header = new QLabel(
            QStringLiteral("Season %1  -  %2 episodes").arg(season).arg(eps.size()),
            m_scopeTileContainer);
        header->setStyleSheet(
            "font-size: 11px; font-weight: 700; color: rgba(255,255,255,0.66);"
            "padding: 6px 8px; background: rgba(255,255,255,0.04);"
            "border-bottom: 1px solid rgba(255,255,255,0.08);");
        layout->addWidget(header);

        for (const auto& ep : eps) {
            EpisodeTileData d;
            d.season  = ep.season;
            d.episode = ep.episode;
            d.title   = ep.title;
            // F3 fix (Codex audit 2026-05-16): if real metadata is available,
            // look up this episode's file to populate real title + size. This
            // is the visible payoff of the D3 hybrid-metadata-strategy.
            // I3 fix (code-quality review 2026-05-16): O(1) lookup via cached
            // tileKey->fileIndex map. Falls back gracefully to no-overlay if
            // the map doesn't have this episode (which can happen if the
            // file-probe found different episodes than the title-estimate).
            if (!derivedForOverlay.tileKeyToFileIndex.isEmpty()) {
                const quint32 lookupKey = tileKey(ep.season, ep.episode);
                if (derivedForOverlay.tileKeyToFileIndex.contains(lookupKey)) {
                    const int fileIdx = derivedForOverlay.tileKeyToFileIndex.value(lookupKey);
                    if (fileIdx >= 0 && fileIdx < m_realFiles.size()) {
                        const QJsonObject f = m_realFiles.at(fileIdx).toObject();
                        d.title = f.value("path").toString().section('/', -1);
                        d.sizeBytes = f.value("size").toVariant().toLongLong();
                    }
                }
            }
            d.alreadyHave = m_downloadIndex
                && m_downloadIndex->filePathFor(m_imdbId, ep.season, ep.episode).has_value();
            auto* tile = new EpisodeTile(d, m_scopeTileContainer);
            layout->addWidget(tile);
            const quint32 key = tileKey(ep.season, ep.episode);
            // F3 fix: restore previously-checked state if this tile existed in
            // a prior render (e.g., user selected via title-estimate tile,
            // then metadata arrived and triggered a refresh - keep the choice).
            if (previousChecked.contains(key)) {
                tile->setChecked(previousChecked.value(key));
            }
            m_tileChecked[key] = tile->isChecked();
            connect(tile, &EpisodeTile::toggled, this, [this, tile](bool checked) {
                m_tileChecked[tileKey(tile->season(), tile->episode())] = checked;
                updateSeriesDownloadButton();
            });
        }
    };

    for (auto it = bySeason.constBegin(); it != bySeason.constEnd(); ++it)
        addSeasonGroup(it.key(), it.value());

    // Initial Download button label.
    updateSeriesDownloadButton();
}

TheatreDownloadPanel::DerivedScope TheatreDownloadPanel::deriveScopeFromFiles() const {
    // I2 fix (code-quality review 2026-05-16): cache hit. Same metadata hash
    // means same files; the regex sweep would produce identical results. Skip
    // the work (~25K regex evals on Complete-Series packs).
    if (!m_pendingMetadataHash.isEmpty()
        && m_derivedScopeCacheKey == m_pendingMetadataHash) {
        return m_derivedScopeCache;
    }

    DerivedScope result;
    if (m_realFiles.isEmpty()) return result;

    QSet<int> detectedSet;
    QMap<int, QList<EpisodeEstimate>> bySeason;
    QMap<quint32, int> keyToFile;
    for (int idx = 0; idx < m_realFiles.size(); ++idx) {
        const QJsonObject f = m_realFiles.at(idx).toObject();
        int ep = 0, fileIdx = 0;
        // Probe seasons 1-50. Most TV ceilings well under that (Simpsons ~35).
        for (int s = 1; s <= 50; ++s) {
            if (tankostream::stream::BulkPackVerifier::matchEpisodeFileForSeason(
                    f, s, &ep, &fileIdx)) {
                if (ep > 0) {
                    detectedSet.insert(s);
                    EpisodeEstimate est;
                    est.season = s;
                    est.episode = ep;
                    est.title = f.value("path").toString().section('/', -1);  // basename
                    bySeason[s].append(est);
                    // I3 fix (code-quality review 2026-05-16): record the
                    // tile-key -> file-index mapping so addSeasonGroup can do
                    // O(1) lookups instead of O(files) regex per tile.
                    keyToFile[tileKey(s, ep)] = idx;
                }
                break;  // file belongs to at most one season; move on
            }
        }
    }
    QList<int> sorted(detectedSet.begin(), detectedSet.end());
    std::sort(sorted.begin(), sorted.end());
    result.seasons = sorted;
    // Sort each season's episode list by episode number.
    for (auto it = bySeason.begin(); it != bySeason.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
            [](const EpisodeEstimate& a, const EpisodeEstimate& b) {
                return a.episode < b.episode;
            });
    }
    result.episodesBySeason = bySeason;
    result.tileKeyToFileIndex = keyToFile;

    // I2 fix: populate cache for next call with same metadata hash.
    m_derivedScopeCacheKey = m_pendingMetadataHash;
    m_derivedScopeCache = result;
    return result;
}

void TheatreDownloadPanel::updateSeriesDownloadButton() {
    if (!m_scopeDownloadBtn) return;
    int count = 0;
    for (auto it = m_tileChecked.constBegin(); it != m_tileChecked.constEnd(); ++it)
        if (it.value()) ++count;
    m_scopeDownloadBtn->setEnabled(count > 0);
    m_scopeDownloadBtn->setText(tr("Download %1 episode%2")
                                  .arg(count)
                                  .arg(count == 1 ? QString() : QStringLiteral("s")));
}

void TheatreDownloadPanel::autoFallbackToShowWide() {
    // Per Codex cross-cutting pushback on brainstorm decision 11 (2026-05-16):
    // Fallback is NOT silent. Show an inline explanation so the user
    // understands why whole-show packs are now appearing. Re-show the
    // loading bar because we are firing a second search.
    m_widenedAutoFallback = true;
    if (m_statusLine) {
        m_statusLine->setText(
            tr("No Season %1 packs found - showing whole-show packs that include this season")
                .arg(m_season));
    }
    if (m_loadingBar) m_loadingBar->show();
    if (m_searchEngine)
        m_searchEngine->search(m_imdbId, m_showName, /*season=*/0);
}

}  // namespace tankoban::stream::theatre
