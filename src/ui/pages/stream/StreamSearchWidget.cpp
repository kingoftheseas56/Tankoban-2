#include "StreamSearchWidget.h"

#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamLibrary.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QVBoxLayout>

using tankostream::addon::MetaItemPreview;
using tankostream::stream::MetaAggregator;

StreamSearchWidget::StreamSearchWidget(MetaAggregator* meta, StreamLibrary* library,
                                       QWidget* parent)
    : QWidget(parent)
    , m_meta(meta)
    , m_library(library)
    , m_nam(new QNetworkAccessManager(this))
{
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/Tankoban/data/stream_posters";
    QDir().mkpath(m_posterCacheDir);

    buildUI();

    if (m_meta) {
        connect(m_meta, &MetaAggregator::catalogResults,
                this, &StreamSearchWidget::onCatalogResults);
        connect(m_meta, &MetaAggregator::catalogError,
                this, &StreamSearchWidget::onCatalogError);
    }

    // Phase 1 Batch 1.2 — the detail view now owns the Add/Remove library
    // toggle. When the user toggles state there, StreamLibrary fires
    // libraryChanged; walk our tiles and refresh the "In Library" badge so
    // the user-visible state stays coherent on back-navigate to the search
    // results.
    if (m_library) {
        connect(m_library, &StreamLibrary::libraryChanged,
                this, &StreamSearchWidget::refreshAllBadges);
    }
}

void StreamSearchWidget::search(const QString& query)
{
    clearResults();
    m_currentQuery = query.trimmed().toLower();
    // Phase 4 Batch 4.1 — full-page "Searching..." label removed; the subtle
    // spinner in StreamPage's search bar is the loading affordance (TODO
    // explicitly calls out "not a full-page Searching state"). Status label
    // still used for error + no-results messages.
    m_statusLabel->hide();
    show();
    if (m_meta) {
        m_meta->searchCatalog(query);
    } else {
        m_statusLabel->setText("Meta aggregator unavailable");
        m_statusLabel->show();
    }
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void StreamSearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    topLayout->setSpacing(8);

    m_backBtn = new QPushButton("\u2190 Stream", topRow);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit backRequested();
    });
    topLayout->addWidget(m_backBtn);

    m_statusLabel = new QLabel(topRow);
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-size: 12px;");
    topLayout->addWidget(m_statusLabel);
    topLayout->addStretch();

    root->addWidget(topRow);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidgetResizable(true);

    auto* scrollContent = new QWidget();
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(16, 8, 16, 16);
    scrollLayout->setSpacing(12);

    // Stremio-parity section layout: Movies first, then Series. Each
    // section has a small-caps header + its own TileStrip. Headers hide
    // when the section is empty so a movies-only or series-only query
    // doesn't show a dangling "SERIES" label with no tiles under it.
    auto makeHeader = [&](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text, scrollContent);
        lbl->setStyleSheet(
            "color: rgba(255,255,255,0.55); font-size: 11px; font-weight: 600;"
            " letter-spacing: 1.5px; padding: 4px 0 2px 0;");
        return lbl;
    };

    // "Show N more" expander button. Hidden until onCatalogResults
    // stashes more than kInitialCap tiles in the matching section;
    // clicked once, it drains the overflow into the strip and hides
    // itself. Left-aligned muted text so it reads as an affordance,
    // not a primary action.
    auto makeShowMore = [&](QWidget* parent) -> QPushButton* {
        auto* btn = new QPushButton(parent);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setStyleSheet(
            "QPushButton { color: rgba(255,255,255,0.65); background: transparent;"
            " border: none; padding: 6px 2px; text-align: left;"
            " font-size: 12px; font-weight: 500; }"
            "QPushButton:hover { color: rgba(255,255,255,0.92); }");
        btn->hide();
        return btn;
    };

    m_moviesHeader = makeHeader(QStringLiteral("MOVIES"));
    scrollLayout->addWidget(m_moviesHeader);
    m_moviesStrip = new TileStrip(scrollContent);
    scrollLayout->addWidget(m_moviesStrip);
    m_moviesShowMore = makeShowMore(scrollContent);
    scrollLayout->addWidget(m_moviesShowMore);
    connect(m_moviesShowMore, &QPushButton::clicked,
            this, &StreamSearchWidget::revealMoviesOverflow);

    m_seriesHeader = makeHeader(QStringLiteral("SERIES"));
    scrollLayout->addWidget(m_seriesHeader);
    m_seriesStrip = new TileStrip(scrollContent);
    scrollLayout->addWidget(m_seriesStrip);
    m_seriesShowMore = makeShowMore(scrollContent);
    scrollLayout->addWidget(m_seriesShowMore);
    connect(m_seriesShowMore, &QPushButton::clicked,
            this, &StreamSearchWidget::revealSeriesOverflow);

    scrollLayout->addStretch(1);

    m_moviesHeader->hide();
    m_seriesHeader->hide();

    m_scroll->setWidget(scrollContent);
    root->addWidget(m_scroll, 1);
}

void StreamSearchWidget::clearResults()
{
    if (m_moviesStrip) m_moviesStrip->clear();
    if (m_seriesStrip) m_seriesStrip->clear();
    if (m_moviesHeader) m_moviesHeader->hide();
    if (m_seriesHeader) m_seriesHeader->hide();
    if (m_moviesShowMore) m_moviesShowMore->hide();
    if (m_seriesShowMore) m_seriesShowMore->hide();
    m_moviesOverflow.clear();
    m_seriesOverflow.clear();
    m_tiles.clear();
    m_previewsById.clear();
}

void StreamSearchWidget::refreshAllBadges()
{
    for (TileCard* card : m_tiles) {
        if (card) updateInLibraryBadge(card);
    }
}

// ─── Search results ──────────────────────────────────────────────────────────

void StreamSearchWidget::onCatalogResults(const QList<MetaItemPreview>& results)
{
    clearResults();

    if (results.isEmpty()) {
        m_statusLabel->setText("No results found");
        m_statusLabel->show();
        return;
    }

    // Stremio-parity (2026-04-20): split into Movies vs Series sections,
    // then sort each section by a relevance score keyed on the user's
    // query. Addons return results in their own arbitrary order (often
    // popularity-only, which misbehaves when you search for a specific
    // title and a more-popular loosely-matched title sorts first).
    QList<MetaItemPreview> movies;
    QList<MetaItemPreview> series;
    for (const MetaItemPreview& entry : results) {
        const QString type = entry.type.toLower();
        if (type == QLatin1String("series")) {
            series.append(entry);
        } else {
            // Default bucket = movies. Also catches "movie" and any
            // adjacent types (anime → "series" upstream in most addons,
            // but defensive for unmapped types).
            movies.append(entry);
        }
    }

    // Relevance scoring. Higher = better match to the current query.
    // Tiers:
    //   exact title match (case-insensitive)            → 1000
    //   title starts with query                         → 500
    //   query matches a word boundary in the title      → 300
    //   query appears anywhere in the title (substring) → 100
    //   otherwise                                       → 0
    // Ties broken by (a) rating DESC, (b) year DESC — recent popular
    // content wins over old obscure content among equal-match tiers.
    const QString q = m_currentQuery;
    auto relevance = [&q](const MetaItemPreview& e) -> int {
        if (q.isEmpty()) return 0;
        const QString name = e.name.toLower();
        if (name == q)                        return 1000;
        if (name.startsWith(q))               return 500;
        // Word-boundary: query appears at the start of any whitespace-
        // delimited word in the title (e.g. "piece" matches "one piece"
        // stronger than "masterpiece").
        if (name.contains(QStringLiteral(" ") + q)) return 300;
        if (name.contains(q))                 return 100;
        return 0;
    };
    auto sortByRelevance = [&](QList<MetaItemPreview>& list) {
        std::sort(list.begin(), list.end(),
            [&](const MetaItemPreview& a, const MetaItemPreview& b) {
                const int ra = relevance(a);
                const int rb = relevance(b);
                if (ra != rb) return ra > rb;
                // Rating tiebreak (parse as float; empty → 0).
                const double raRat = a.imdbRating.toDouble();
                const double rbRat = b.imdbRating.toDouble();
                if (qFuzzyCompare(1.0 + raRat, 1.0 + rbRat) == false)
                    return raRat > rbRat;
                // Year tiebreak (releaseInfo is "2023" or "2023–2025";
                // compare on leading 4 chars as int).
                const int aY = a.releaseInfo.left(4).toInt();
                const int bY = b.releaseInfo.left(4).toInt();
                return aY > bY;
            });
    };
    sortByRelevance(movies);
    sortByRelevance(series);

    m_statusLabel->setText(QString::number(results.size()) + " results");
    m_statusLabel->show();

    // Initial-cap render: only the top kInitialCap per section lands in the
    // strip on first paint so the user sees the most-relevant set at a
    // glance (one tight row per section) instead of a multi-row wall.
    // The rest goes into m_{movies,series}Overflow and is surfaced by the
    // "Show N more" button under the strip.
    auto renderCapped = [&](QList<MetaItemPreview>& bucket,
                            QLabel* header,
                            QPushButton* showMore,
                            QList<MetaItemPreview>& overflow) {
        if (bucket.isEmpty()) return;
        header->show();
        const int shown = std::min<int>(bucket.size(), kInitialCap);
        for (int i = 0; i < shown; ++i) addResultCard(bucket[i]);
        const int remaining = bucket.size() - shown;
        if (remaining > 0) {
            overflow.reserve(remaining);
            for (int i = shown; i < bucket.size(); ++i)
                overflow.append(bucket[i]);
            showMore->setText(QStringLiteral("Show %1 more").arg(remaining));
            showMore->show();
        }
    };
    renderCapped(movies, m_moviesHeader, m_moviesShowMore, m_moviesOverflow);
    renderCapped(series, m_seriesHeader, m_seriesShowMore, m_seriesOverflow);
}

void StreamSearchWidget::revealMoviesOverflow()
{
    for (const MetaItemPreview& entry : m_moviesOverflow) addResultCard(entry);
    m_moviesOverflow.clear();
    if (m_moviesShowMore) m_moviesShowMore->hide();
}

void StreamSearchWidget::revealSeriesOverflow()
{
    for (const MetaItemPreview& entry : m_seriesOverflow) addResultCard(entry);
    m_seriesOverflow.clear();
    if (m_seriesShowMore) m_seriesShowMore->hide();
}

void StreamSearchWidget::onCatalogError(const QString& message)
{
    m_statusLabel->setText("Search failed: " + message);
}

void StreamSearchWidget::addResultCard(const MetaItemPreview& entry)
{
    const QString posterPath = m_posterCacheDir + "/" + entry.id + ".jpg";
    const QString thumbPath = QFile::exists(posterPath) ? posterPath : QString();

    // Canonical Stream subtitle: year + IMDb rating (matches StreamLibraryLayout).
    QStringList sub;
    if (!entry.releaseInfo.isEmpty()) sub << entry.releaseInfo;
    if (!entry.imdbRating.isEmpty())  sub << QStringLiteral("IMDb ") + entry.imdbRating;
    QString subtitle = sub.join(" \u00B7 ");

    auto* card = new TileCard(thumbPath, entry.name, subtitle);

    const QString posterUrl = entry.poster.toString();
    card->setProperty("imdbId", entry.id);
    card->setProperty("entryType", entry.type);
    card->setProperty("entryName", entry.name);
    card->setProperty("entryYear", entry.releaseInfo);
    card->setProperty("entryPoster", posterUrl);
    card->setProperty("entryDesc", entry.description);
    card->setProperty("entryRating", entry.imdbRating);

    updateInLibraryBadge(card);

    // Phase 1 Batch 1.2 — click on a search result now opens the detail view
    // via StreamPage::showDetail(preview). The Add/Remove library toggle
    // moved into the detail view header. "In Library" badge remains as a
    // visual cue and is refreshed externally via libraryChanged above.
    connect(card, &TileCard::clicked, this, [this, card]() {
        const QString imdbId = card->property("imdbId").toString();
        if (imdbId.isEmpty()) return;
        const auto it = m_previewsById.constFind(imdbId);
        if (it == m_previewsById.constEnd()) return;
        emit metaActivated(it.value());
    });

    // Route to the correct section strip (Stremio parity). Everything
    // that isn't explicitly "series" goes in Movies.
    TileStrip* targetStrip =
        (entry.type.toLower() == QLatin1String("series"))
            ? m_seriesStrip
            : m_moviesStrip;
    if (targetStrip) targetStrip->addTile(card);
    m_tiles.push_back(card);
    m_previewsById.insert(entry.id, entry);

    if (thumbPath.isEmpty() && !posterUrl.isEmpty())
        downloadPoster(entry.id, posterUrl, card);
}

void StreamSearchWidget::updateInLibraryBadge(TileCard* card)
{
    QString imdbId = card->property("imdbId").toString();
    if (m_library->has(imdbId)) {
        card->setBadges(1.0, {}, "In Library", "finished");
    } else {
        card->setBadges(0.0, {}, {}, {});
    }
}

// ─── Poster download ─────────────────────────────────────────────────────────

void StreamSearchWidget::downloadPoster(const QString& imdbId, const QString& posterUrl,
                                         TileCard* card)
{
    QUrl url(posterUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(10000);
    // See CatalogBrowseScreen::ensurePoster — Qt6 default redirect
    // policy is ManualRedirectPolicy, which silently drops poster
    // CDN 301/302 responses. Explicit NoLessSafeRedirectPolicy fixes
    // missing search-result thumbnails (2026-04-20).
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QPointer<TileCard> guard(card);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;

        QString path = m_posterCacheDir + "/" + imdbId + ".jpg";
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }

        if (guard) {
            guard->setThumbPath(path);
        }
    });
}
