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

#include <QComboBox>
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
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// Property-name + dimension-value constants for the filter-chip dynamic
// property API. Centralized to prevent typos that would silently break
// the single-select sweep (per code-review I2, 2026-05-16).
constexpr const char* kPropDimension = "filterDimension";
constexpr const char* kPropValue     = "filterValue";
constexpr const char* kDimType       = "type";

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

QStringList titleTokens(const QString& text)
{
    QString normalized = text.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                       QStringLiteral(" "));
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString fileDisplayPath(const QJsonObject& file)
{
    const QString path = file.value(QStringLiteral("path")).toString();
    return path.isEmpty() ? file.value(QStringLiteral("name")).toString() : path;
}

bool isPackBoundaryToken(const QString& token)
{
    static const QSet<QString> kBoundary = {
        QStringLiteral("s01"), QStringLiteral("s02"), QStringLiteral("s03"),
        QStringLiteral("s04"), QStringLiteral("s05"), QStringLiteral("s06"),
        QStringLiteral("s07"), QStringLiteral("s08"), QStringLiteral("s09"),
        QStringLiteral("s10"), QStringLiteral("season"), QStringLiteral("complete"),
        QStringLiteral("full"), QStringLiteral("series"), QStringLiteral("1080p"),
        QStringLiteral("720p"), QStringLiteral("2160p"), QStringLiteral("480p"),
        QStringLiteral("web"), QStringLiteral("webdl"), QStringLiteral("webrip"),
        QStringLiteral("bluray"), QStringLiteral("bdrip"), QStringLiteral("h264"),
        QStringLiteral("h265"), QStringLiteral("x264"), QStringLiteral("x265"),
        QStringLiteral("hevc"), QStringLiteral("dsnp"), QStringLiteral("amzn"),
        QStringLiteral("nf")
    };
    if (kBoundary.contains(token))
        return true;
    static const QRegularExpression kSeasonToken(QStringLiteral("^s\\d{1,2}$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kEpisodeToken(QStringLiteral("^e\\d{1,3}$"),
                                                  QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kYearToken(QStringLiteral("^(19|20)\\d{2}$"));
    return kSeasonToken.match(token).hasMatch()
        || kEpisodeToken.match(token).hasMatch()
        || kYearToken.match(token).hasMatch();
}

bool packTitleMatchesShowIdentity(const QString& packTitle,
                                  const QString& showName,
                                  const QString& showYear)
{
    const QStringList pack = titleTokens(packTitle);
    const QStringList show = titleTokens(showName);
    if (pack.isEmpty() || show.isEmpty())
        return false;

    QStringList identity = show;
    int start = -1;
    for (int i = 0; i <= pack.size() - identity.size(); ++i) {
        bool match = true;
        for (int j = 0; j < identity.size(); ++j) {
            if (pack.at(i + j) != identity.at(j)) {
                match = false;
                break;
            }
        }
        if (match) {
            start = i;
            break;
        }
    }
    if (start < 0 && show.size() > 1) {
        identity = QStringList{show.last()};
        for (int i = 0; i <= pack.size() - identity.size(); ++i) {
            if (pack.at(i) == identity.first()) {
                start = i;
                break;
            }
        }
    }
    if (start < 0)
        return false;

    const int afterShow = start + identity.size();
    const QString expectedYear = showYear.trimmed();
    for (int i = afterShow; i < pack.size(); ++i) {
        const QString token = pack.at(i);
        if (isPackBoundaryToken(token))
            return true;
        if (!expectedYear.isEmpty() && token == expectedYear)
            continue;
        return false;
    }
    return true;
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
                                   const QString& showYear,
                                   int season,
                                   const QString& mediaType,
                                   const QMap<int, int>& knownEpisodeCounts) {
    m_imdbId    = imdbId;
    m_showName  = showName;
    m_showYear  = showYear;
    m_season    = season;
    m_mediaType = mediaType;
    m_knownEpisodeCounts = knownEpisodeCounts;  // T3
    m_packs.clear();
    m_filteredPacks.clear();
    m_widenedAutoFallback = false;
    m_tileChecked.clear();
    // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — reset the shift+click
    // range-fill anchor so a stale tile-key from a prior pack can't
    // bleed into this new openFor() cycle.
    m_lastToggledKey = 0;
    m_lastToggledValid = false;

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

    // TANKORENT_QUALITY_AND_QUEUE audit DEFECT 1 (2026-05-27) — clear the
    // visual pack list NOW, not just the data models above. Without this the
    // previous show's rendered rows stay painted through the "Searching..."
    // phase (e.g. One Piece rows lingering on a freshly-opened Community S4
    // panel) until new results repaint. m_packs/m_filteredPacks are already
    // cleared, so rerenderPackList() empties m_packList and renders nothing.
    rerenderPackList();

    transitionTo(State::PackList);
    if (m_searchEngine)
        m_searchEngine->search(imdbId, showName, season, m_sourceFilter);
}

void TheatreDownloadPanel::reset() {
    m_packs.clear();
    m_filteredPacks.clear();
    m_tileChecked.clear();
    m_knownEpisodeCounts.clear();  // T3
    // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — mirror openFor's
    // reset so dismiss-then-reopen never resurrects the anchor.
    m_lastToggledKey = 0;
    m_lastToggledValid = false;
    // THEATRE_DOWNLOAD_OVERHAUL Task D3 (2026-05-16): clear pending-metadata
    // state so closing+reopening the panel doesn't leak a stale hash that
    // could match a late metadataReady from a prior selection.
    m_pendingMetadataHash.clear();
    m_realFiles = QJsonArray();
    // I2 fix (code-quality review 2026-05-16): invalidate the derive-scope
    // cache on full reset.
    m_derivedScopeCacheKey.clear();
    m_derivedScopeCache = DerivedScope();
    // THEATRE_SOURCE_PICKER 2026-05-17 (Hemanth feedback): source-pick is
    // per-show contextual, not a persistent preference. Reset combo + filter
    // to "All Sources" on dismiss so the next show starts fresh.
    m_sourceFilter = QStringLiteral("all");
    if (m_sourceCombo) {
        const QSignalBlocker blocker(m_sourceCombo);
        m_sourceCombo->setCurrentIndex(0);
    }
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

    // THEATRE_SOURCE_PICKER 2026-05-17 - source-selection dropdown.
    // Mirrors the standalone Tankorent tab's source combo
    // (TankorentPage.cpp:1136-1143). "All Sources" default fans out to
    // all 6 Tankorent indexers; explicit source pick gates dispatch in
    // StreamAggregator::searchPacks. Distinct from the source-FILTER
    // chip row removed by the chip-simplification arc earlier today:
    // that was a post-search result filter; this is a pre-search
    // dispatch gate.
    {
        auto* sourceRow = new QWidget(m_packListPage);
        sourceRow->setObjectName(QStringLiteral("TheatreDownloadSourceRow"));
        auto* sourceLayout = new QHBoxLayout(sourceRow);
        sourceLayout->setContentsMargins(0, 0, 0, 0);
        sourceLayout->setSpacing(8);

        auto* label = new QLabel(QStringLiteral("Source"), sourceRow);
        label->setObjectName(QStringLiteral("TheatreDownloadSourceLabel"));
        label->setStyleSheet(
            "color: rgba(255,255,255,0.65); font-size: 12px;");
        sourceLayout->addWidget(label);

        m_sourceCombo = new QComboBox(sourceRow);
        m_sourceCombo->setObjectName(QStringLiteral("TheatreDownloadSourceCombo"));
        m_sourceCombo->setFixedHeight(28);
        m_sourceCombo->setMinimumWidth(160);
        // Option list matches TankorentPage.cpp:1136-1143 minus
        // torrents-csv (books-only; preserved off the Theatre surface
        // to mirror existing parity).
        m_sourceCombo->addItem(QStringLiteral("All Sources"),  QStringLiteral("all"));
        m_sourceCombo->addItem(QStringLiteral("Nyaa"),         QStringLiteral("nyaa"));
        m_sourceCombo->addItem(QStringLiteral("PirateBay"),    QStringLiteral("piratebay"));
        m_sourceCombo->addItem(QStringLiteral("1337x"),        QStringLiteral("1337x"));
        m_sourceCombo->addItem(QStringLiteral("YTS"),          QStringLiteral("yts"));
        m_sourceCombo->addItem(QStringLiteral("EZTV"),         QStringLiteral("eztv"));
        m_sourceCombo->addItem(QStringLiteral("ExtraTorrents"), QStringLiteral("exttorrents"));
        m_sourceCombo->setStyleSheet(
            "QComboBox#TheatreDownloadSourceCombo {"
            " background: rgba(255,255,255,0.04);"
            " color: #f3f4f6;"
            " border: 1px solid rgba(255,255,255,0.10);"
            " border-radius: 4px;"
            " padding: 2px 8px;"
            "}"
            "QComboBox#TheatreDownloadSourceCombo:hover {"
            " background: rgba(255,255,255,0.07);"
            "}"
            "QComboBox#TheatreDownloadSourceCombo::drop-down {"
            " border: none;"
            "}");
        sourceLayout->addWidget(m_sourceCombo);
        sourceLayout->addStretch(1);

        col->addWidget(sourceRow);
        connect(m_sourceCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TheatreDownloadPanel::onSourceComboChanged);
    }

    m_filterChipRow = new QWidget(m_packListPage);
    m_filterChipRow->setObjectName(QStringLiteral("TheatreDownloadFilterChipRow"));
    auto* chipLayout = new QHBoxLayout(m_filterChipRow);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(6);
    col->addWidget(m_filterChipRow);

    // THEATRE_DOWNLOAD_OVERHAUL Task C3 (2026-05-16) + chip-simplification
    // (2026-05-17): populate filter chip row with the Type single-select
    // chip group (originally per Codex expansion 5.2.B). Source dimension
    // dropped 2026-05-17 - see comment below the typeOptions list.
    {
        // THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - "Single
        // Episode" chip dropped. The primary Download button (post-E1 fast-
        // path restore) already handles per-episode highest-seeded dispatch
        // via onDownloadSeasonClicked / theatreTopSeededDownloadRequested.
        // Surfacing a Single-Episode chip in the pack panel duplicates that
        // entry point + adds visual noise. Per-episode flow lives on the
        // primary button; pack flow lives here.
        const QStringList typeOptions = {
            QStringLiteral("All"),
            QStringLiteral("Complete Series"),
            QStringLiteral("Multi-Season"),
            QStringLiteral("Season Pack"),
        };
        for (const QString& opt : typeOptions) {
            auto* chip = makeFilterChip(opt, opt == m_typeFilter, m_filterChipRow);
            chip->setProperty(kPropDimension, kDimType);
            chip->setProperty(kPropValue, opt);
            connect(chip, &QPushButton::clicked, this, &TheatreDownloadPanel::onFilterChipClicked);
            chipLayout->addWidget(chip);
        }

        // THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - source
        // chip row removed. Stremio results now live exclusively in the
        // Sources sidebar; the pack panel is dedicated to Tankorent's custom
        // indexer scraper (PirateBay/1337x/YTS/EZTV/ExtTorrents fan-out).
        // With Stremio gone, the source-axis filter has only one option
        // (Indexers = everything in the panel) - a single-option filter
        // adds no value. Source chip row dropped; type chip row stretches
        // to fill the chip-row width.
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

    m_scopeTileContainer = new QWidget();
    m_scopeTileContainer->setObjectName(QStringLiteral("TheatreScopeTileContainer"));
    auto* tileLayout = new QVBoxLayout(m_scopeTileContainer);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setSpacing(4);
    tileLayout->addStretch();
    // Tile widgets get populated in Task D1+D2.

    // THEATRE_DOWNLOAD_OVERHAUL action-row-clipping fix 2026-05-17 - wrap tile
    // container in QScrollArea so a long episode list (e.g. Invincible S1 has
    // 8+ episodes which exceed the visible panel height) does not push the
    // action row (Cancel + Download) below the panel boundary and clip it.
    // Tile list scrolls internally; action row stays anchored at the bottom
    // of the panel. setWidgetResizable(true) makes the inner widget track the
    // scroll area's width so the tiles do not horizontally clip.
    auto* tileScroll = new QScrollArea(m_scopePickerPage);
    tileScroll->setObjectName(QStringLiteral("TheatreScopeTileScroll"));
    tileScroll->setWidgetResizable(true);
    tileScroll->setFrameShape(QFrame::NoFrame);
    tileScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tileScroll->setWidget(m_scopeTileContainer);
    col->addWidget(tileScroll, /*stretch=*/1);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_scopeCancelBtn = new QPushButton(tr("Cancel"), m_scopePickerPage);
    m_scopeCancelBtn->setObjectName(QStringLiteral("TheatreScopeCancelBtn"));
    m_scopeCancelBtn->setFixedHeight(30);
    m_scopeCancelBtn->setCursor(Qt::PointingHandCursor);
    // THEATRE_DOWNLOAD_OVERHAUL action-row-visibility fix 2026-05-17 - explicit
    // grayscale QSS so the Cancel button is legible on the dark panel
    // background. Without this, default QPushButton style renders nearly
    // invisible against the rgba(255,255,255,0.04) panel backdrop.
    m_scopeCancelBtn->setStyleSheet(
        "#TheatreScopeCancelBtn { background: transparent;"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        "  color: #ddd; padding: 0 14px; font-size: 12px; }"
        "#TheatreScopeCancelBtn:hover { background: rgba(255,255,255,0.06);"
        "  border-color: rgba(255,255,255,0.22); }");
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
    // THEATRE_DOWNLOAD_OVERHAUL action-row-visibility fix 2026-05-17 - explicit
    // primary-action QSS for the Download button (slightly stronger bg than
    // Cancel, bolder text). Disabled-state QSS dims the text + border so the
    // user can tell when no episodes are selected.
    m_scopeDownloadBtn->setStyleSheet(
        "#TheatreScopeDownloadBtn { background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.18); border-radius: 6px;"
        "  color: #f3f4f6; padding: 0 14px; font-size: 12px; font-weight: 600; }"
        "#TheatreScopeDownloadBtn:hover { background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.26); }"
        "#TheatreScopeDownloadBtn:disabled { color: rgba(255,255,255,0.30);"
        "  border-color: rgba(255,255,255,0.08); }");
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
    if (m_statusLine) {
        // THEATRE_SOURCE_PICKER 2026-05-18 (smoke finding): when a type filter
        // is active and packs were received, show "X of Y match" so the user
        // can tell results exist but are filter-hidden. Mirrors the same
        // conditional in rerenderPackList() so the search-complete + chip-
        // click paths render identically. The naked "0 packs found" pre-fix
        // misled Hemanth's 2026-05-17 smoke into thinking Nyaa was unreachable
        // when really it returned 50+ single-episode results that the
        // "Complete Series" filter classified out.
        const bool hasFilter = (m_typeFilter != QLatin1String("All"));
        if (hasFilter && !m_packs.isEmpty()) {
            m_statusLine->setText(tr("%1 of %2 packs match")
                                       .arg(m_filteredPacks.size())
                                       .arg(m_packs.size()));
        } else {
            m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
        }
    }
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

    QList<int> selectedEpisodes;
    QSet<int> selectedSeasons;
    for (auto it = m_tileChecked.constBegin(); it != m_tileChecked.constEnd(); ++it) {
        if (!it.value())
            continue;
        const int season = static_cast<int>(it.key() >> 16);
        const int episode = static_cast<int>(it.key() & 0xffff);
        if (season > 0 && episode > 0) {
            selectedSeasons.insert(season);
            selectedEpisodes.append(episode);
        }
    }
    std::sort(selectedEpisodes.begin(), selectedEpisodes.end());

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value("videos");
    config.contentLayout   = QStringLiteral("original");
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = m_imdbId;
    config.season          = m_season;
    QMap<int, int> fileIndexByEpisode;

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
                const QString path = fileDisplayPath(f);
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
    // F7 2026-05-19: do not dispatch a series pack until metadata has arrived.
    // The host now creates a visible bulk-group record from the selected
    // episode/file-index mapping before starting the direct Tankorent download;
    // without real file indexes that group would be unbound and the old path
    // would close the panel after a silent no-op-looking dispatch.
    {
        if (m_realFiles.isEmpty()) {
            if (m_scopeStatusLine)
                m_scopeStatusLine->setText(tr("Loading pack metadata..."));
            return;
        }
        QMap<int, int> priorities;
        QVector<int> selectedIndices;
        for (int idx = 0; idx < m_realFiles.size(); ++idx) {
            QJsonObject file = m_realFiles.at(idx).toObject();
            if (!file.contains("index")) file.insert("index", idx);

            int matchedEp = 0;
            int matchedIdx = 0;
            bool keepFile = false;
            const auto seasons = selectedSeasons.values();
            for (int season : seasons) {
                const bool ok = tankostream::stream::BulkPackVerifier::matchEpisodeFileForSeason(
                    file, season, &matchedEp, &matchedIdx);
                if (ok && matchedEp > 0) {
                    const quint32 key = tileKey(season, matchedEp);
                    if (m_tileChecked.value(key, false)) {
                        keepFile = true;
                        selectedIndices.append(idx);
                        fileIndexByEpisode[matchedEp] = idx;
                    }
                    break;
                }
            }
            priorities[idx] = keepFile ? 4 : 0;
        }
        if (selectedIndices.isEmpty()) {
            if (m_scopeStatusLine)
                m_scopeStatusLine->setText(tr("No selected episodes matched this pack."));
            return;
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
                           m_selectedPack.raw.magnetUri, hash, config,
                           selectedEpisodes, fileIndexByEpisode,
                           m_selectedPack.raw.title);
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

    // THEATRE_DOWNLOAD_OVERHAUL Task C3 (2026-05-16) + chip-simplification
    // (2026-05-17): apply the Type filter dimension (Source dimension
    // dropped 2026-05-17, see comment block inside the loop) before the
    // score-descending sort.
    m_filteredPacks.clear();
    for (const auto& p : m_packs) {
        // F6 2026-05-19: Tankorent pack search is title-query based, so a
        // broad title like "Daredevil" can return a sequel/spinoff pack such
        // as "Daredevil Born Again". Keep only rows whose release title
        // reaches a season/quality boundary immediately after the requested
        // show identity. Real metadata still owns episode counts below.
        if (!packTitleMatchesShowIdentity(p.raw.title, m_showName, m_showYear))
            continue;

        // Type filter: compare classification label against m_typeFilter.
        // "All" matches everything.
        if (m_typeFilter != QLatin1String("All")) {
            if (labelForType(p.classification.type) != m_typeFilter)
                continue;
        }
        // THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - source
        // filter dropped. Panel is Tankorent-only after Task 2's engine strip;
        // all packs have p.source == PackSource::Tankorent. No filtering needed.

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
        const bool hasFilter = (m_typeFilter != QLatin1String("All"));
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
        // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — bind tile to
        // live index state for chip rendering during in-flight downloads.
        tile->setImdbId(m_imdbId);
        tile->setStreamDownloadIndex(m_downloadIndex);
        layout->addWidget(tile);
        const quint32 movieKey = tileKey(0, 0);
        // F3 fix: restore previously-checked state across metadata-driven re-renders.
        if (previousChecked.contains(movieKey)) {
            tile->setChecked(previousChecked.value(movieKey));
        }
        m_tileChecked[movieKey] = tile->isChecked();
        // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — movie mode is a
        // degenerate single-tile pack so shift-range fill is a no-op
        // here, but we wire toggledShift instead of the legacy toggled
        // signal for API consistency across all 3 tile-connect sites.
        connect(tile, &EpisodeTile::toggledShift, this,
                [this, tile](bool checked, bool shiftHeld) {
            Q_UNUSED(shiftHeld);
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

    // Series mode source-of-truth hierarchy (highest priority first):
    //   1. m_realFiles - libtorrent file probe (the truest source)
    //   2. m_knownEpisodeCounts - Cinemeta per-season counts (THEATRE_BULK_
    //      PICKER_EPISODE_COUNT_FIX 2026-05-22)
    //   3. m_scopeEstimate - title-token parse with 10-default fallback
    QMap<int, QList<EpisodeEstimate>> bySeason;

    // Default: title-estimate path (least informed; supplies title + filename
    // hints when the estimator extracts them).
    for (const auto& ep : m_scopeEstimate.episodes)
        bySeason[ep.season].append(ep);

    // Override 1: Cinemeta-known episode counts. Honored when:
    //   (a) at least one season has a positive count, AND
    //   (b) for each known season, replace any title-estimate rows
    //       with a fresh 1..count list. Seasons present only in the
    //       estimate (not in Cinemeta) are kept as-is.
    // This addresses the 10-default-everywhere bug for packs whose titles
    // (e.g. "Community S05 + Featurettes 1080p Bluray x265") carry no
    // SxxEyy / "13 Episodes" tokens.
    if (!m_knownEpisodeCounts.isEmpty()) {
        for (auto it = m_knownEpisodeCounts.constBegin();
             it != m_knownEpisodeCounts.constEnd(); ++it) {
            const int season = it.key();
            const int count  = it.value();
            if (count <= 0) continue;
            QList<EpisodeEstimate> fresh;
            fresh.reserve(count);
            for (int ep = 1; ep <= count; ++ep) {
                EpisodeEstimate e;
                e.season  = season;
                e.episode = ep;
                fresh.append(e);
            }
            bySeason[season] = fresh;
        }
    }

    // Override 2 (top priority): real torrent file probe. Once libtorrent
    // delivers the file list we trust it absolutely — it's the actual on-
    // disk truth, and it may also detect episodes Cinemeta doesn't know
    // about (e.g. featurettes the addon meta omits).
    DerivedScope derivedForOverlay;
    if (!m_realFiles.isEmpty()) {
        derivedForOverlay = deriveScopeFromFiles();
        if (!derivedForOverlay.episodesBySeason.isEmpty())
            bySeason = derivedForOverlay.episodesBySeason;
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
        // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — bind tile to
        // live index state (fallback whole-pack path).
        tile->setImdbId(m_imdbId);
        tile->setStreamDownloadIndex(m_downloadIndex);
        layout->addWidget(tile);
        const quint32 fallbackKey = tileKey(0, 0);
        // F3 fix: restore previously-checked state across re-renders.
        if (previousChecked.contains(fallbackKey)) {
            tile->setChecked(previousChecked.value(fallbackKey));
        }
        m_tileChecked[fallbackKey] = tile->isChecked();
        // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — fallback "Download
        // entire pack" path is also single-tile so shift-range is a no-op,
        // but we mirror the toggledShift connect for API consistency.
        connect(tile, &EpisodeTile::toggledShift, this,
                [this, tile](bool checked, bool shiftHeld) {
            Q_UNUSED(shiftHeld);
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
                        d.title = fileDisplayPath(f).section('/', -1).section('\\', -1);
                        d.sizeBytes = f.value("size").toVariant().toLongLong();
                    }
                }
            }
            d.alreadyHave = m_downloadIndex
                && m_downloadIndex->filePathFor(m_imdbId, ep.season, ep.episode).has_value();
            auto* tile = new EpisodeTile(d, m_scopeTileContainer);
            // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — bind tile
            // to live index state for the per-episode case.
            tile->setImdbId(m_imdbId);
            tile->setStreamDownloadIndex(m_downloadIndex);
            layout->addWidget(tile);
            const quint32 key = tileKey(ep.season, ep.episode);
            // F3 fix: restore previously-checked state if this tile existed in
            // a prior render (e.g., user selected via title-estimate tile,
            // then metadata arrived and triggered a refresh - keep the choice).
            if (previousChecked.contains(key)) {
                tile->setChecked(previousChecked.value(key));
            }
            m_tileChecked[key] = tile->isChecked();
            // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — series-mode
            // per-episode tile connect. On a shift-held click we walk
            // the scope tile container's layout and apply the new
            // checked state to every EpisodeTile whose key falls
            // between the prior anchor and this tile (inclusive) AND
            // whose season matches this tile's season. Cross-season
            // range-fill is intentionally skipped (rarely the user
            // intent on a Complete Series pack). The anchor advances
            // to this tile on every click — shift or not.
            connect(tile, &EpisodeTile::toggledShift, this,
                    [this, tile](bool checked, bool shiftHeld) {
                const quint32 clickedKey = tileKey(tile->season(), tile->episode());
                if (shiftHeld && m_lastToggledValid
                    && static_cast<int>(m_lastToggledKey >> 16) == tile->season()
                    && m_scopeTileContainer) {
                    if (auto* lay = m_scopeTileContainer->layout()) {
                        const quint32 lo = qMin(m_lastToggledKey, clickedKey);
                        const quint32 hi = qMax(m_lastToggledKey, clickedKey);
                        for (int i = 0; i < lay->count(); ++i) {
                            auto* item = lay->itemAt(i);
                            if (!item) continue;
                            auto* w = item->widget();
                            if (!w) continue;
                            auto* etile = qobject_cast<EpisodeTile*>(w);
                            if (!etile) continue;
                            const quint32 ekey =
                                tileKey(etile->season(), etile->episode());
                            if (ekey >= lo && ekey <= hi
                                && etile->season() == tile->season()) {
                                etile->setCheckedQuiet(checked);
                                m_tileChecked[ekey] = checked;
                            }
                        }
                    }
                }
                m_tileChecked[clickedKey] = checked;
                m_lastToggledKey   = clickedKey;
                m_lastToggledValid = true;
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
                    est.title = fileDisplayPath(f).section('/', -1).section('\\', -1);  // basename
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
        m_searchEngine->search(m_imdbId, m_showName, /*season=*/0, m_sourceFilter);
}

void TheatreDownloadPanel::onSourceComboChanged(int /*index*/) {
    if (!m_sourceCombo) return;
    m_sourceFilter = m_sourceCombo->currentData().toString();
    if (m_sourceFilter.isEmpty())
        m_sourceFilter = QStringLiteral("all");

    // TANKORENT audit DEFECT 2 (2026-05-28) — re-run the search immediately on
    // source change. Previously this was a no-op (the change only took effect
    // on dismiss+reopen), which was Hemanth's complaint #3: "no search button
    // when I change the source." Auto-re-firing is now safe: StreamAggregator's
    // m_packEpoch guard (added this fix) suppresses the prior in-flight fan-out
    // so its results can't bleed into the new source's results. UnifiedPack-
    // SearchEngine::search also force-completes the prior search on a new call.
    //
    // Guard: only re-fire when a show context is actually loaded (openFor ran).
    // reset() flips the combo back to index 0 under a QSignalBlocker, so this
    // slot does not fire during reset.
    if (m_imdbId.isEmpty() || !m_searchEngine)
        return;

    m_packs.clear();
    m_filteredPacks.clear();
    m_tileChecked.clear();
    m_lastToggledKey = 0;
    m_lastToggledValid = false;
    if (m_statusLine)
        m_statusLine->setText(tr("Searching sources..."));
    if (m_loadingBar) m_loadingBar->show();
    rerenderPackList();  // clear visual rows now (mirror openFor's DEFECT 1 fix)

    m_searchEngine->search(m_imdbId, m_showName, m_season, m_sourceFilter);
}

}  // namespace tankoban::stream::theatre
