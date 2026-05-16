// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — Phase 3 Task 17.
// Forked from src/ui/pages/stream/StreamSearchWidget.cpp per brainstorm §6.1.
// Two-section result grid (Manga / Comics) split by MangaResult::type;
// fan-out across MangaSourceRegistry::scrapers(); kInitialCap=6 with
// "Show more" overflow per section. Click on a result tile emits
// seriesActivated(preview) — ComicsPage's Phase 3 stub logs it; Phase 4
// wires the detail-view path.

#include "ComicsTankoyomiSearchWidget.h"
#include "core/manga/MangaPosterCache.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/PremiumCatalog.h"
#include "ui/Theme.h"
#include "ui/pages/TileStrip.h"
#include "ui/pages/TileCard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

ComicsTankoyomiSearchWidget::ComicsTankoyomiSearchWidget(
    MangaSourceRegistry* registry,
    QNetworkAccessManager* nam, QWidget* parent)
    : QWidget(parent), m_registry(registry), m_nam(nam)
{
    // Poster cache — match TankoyomiPage's directory so cache entries
    // are shared across both surfaces (a series first hit via Tankoyomi
    // before Phase 8 retires the surface still warms cache for here).
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/Tankoban/data/manga_posters";
    QDir().mkpath(m_posterCacheDir);
    buildUI();

    // NOTE: scraper signal connections moved into search() per code-quality
    // review I3 — each call disconnects prior connections first so late
    // arrivals from a previous search() (the rapid-retype case) become
    // no-ops at the wire instead of painting stale results into the new
    // search's strips. ComicsPage's separate per-scraper errorOccurred
    // Toast loop is untouched (different receiver).
}

void ComicsTankoyomiSearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    topLayout->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("← Back to library"), topRow);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsTankoyomiSearchWidget::backRequested);
    topLayout->addWidget(m_backBtn);

    m_statusLabel = new QLabel(topRow);
    m_statusLabel->setObjectName("ComicsSearchStatus");
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

    auto makeHeader = [&](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text, scrollContent);
        lbl->setObjectName("LibraryHeadingSmall");
        return lbl;
    };

    auto makeShowMore = [&](QWidget* parent) -> QPushButton* {
        auto* btn = new QPushButton(parent);
        btn->setObjectName("ComicsSearchShowMore");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setStyleSheet(
            "QPushButton#ComicsSearchShowMore { color: rgba(255,255,255,0.65); background: transparent;"
            " border: none; padding: 6px 2px; text-align: left;"
            " font-size: 12px; font-weight: 500; }"
            "QPushButton#ComicsSearchShowMore:hover { color: rgba(255,255,255,0.92); }");
        btn->hide();
        return btn;
    };

    // TANKOYOMI_PREMIUM Phase 8 -- Premium section above the original
    // Manga / Comics pair. Premium-catalog hits (live scraper results whose
    // title matches PremiumCatalog::isPremiumSeries + synthetic catalog-only
    // hits) land here instead of in the original type-routed strips.
    m_premiumHeader = makeHeader(QStringLiteral("PREMIUM"));
    m_premiumHeader->setObjectName(QStringLiteral("premiumHeader"));
    scrollLayout->addWidget(m_premiumHeader);
    m_premiumStrip = new TileStrip(scrollContent);
    m_premiumStrip->setDensity(0);
    scrollLayout->addWidget(m_premiumStrip);
    m_premiumShowMore = makeShowMore(scrollContent);
    scrollLayout->addWidget(m_premiumShowMore);
    connect(m_premiumShowMore, &QPushButton::clicked,
            this, &ComicsTankoyomiSearchWidget::revealPremiumOverflow);

    // Manga section.
    m_mangaHeader = makeHeader(QStringLiteral("MANGA"));
    scrollLayout->addWidget(m_mangaHeader);
    m_mangaStrip = new TileStrip(scrollContent);
    m_mangaStrip->setDensity(0);
    scrollLayout->addWidget(m_mangaStrip);
    m_mangaShowMore = makeShowMore(scrollContent);
    scrollLayout->addWidget(m_mangaShowMore);
    connect(m_mangaShowMore, &QPushButton::clicked,
            this, &ComicsTankoyomiSearchWidget::revealMangaOverflow);

    // Comics section.
    m_comicsHeader = makeHeader(QStringLiteral("COMICS"));
    scrollLayout->addWidget(m_comicsHeader);
    m_comicsStrip = new TileStrip(scrollContent);
    m_comicsStrip->setDensity(0);
    scrollLayout->addWidget(m_comicsStrip);
    m_comicsShowMore = makeShowMore(scrollContent);
    scrollLayout->addWidget(m_comicsShowMore);
    connect(m_comicsShowMore, &QPushButton::clicked,
            this, &ComicsTankoyomiSearchWidget::revealComicsOverflow);

    scrollLayout->addStretch(1);

    m_premiumHeader->hide();
    m_mangaHeader->hide();
    m_comicsHeader->hide();

    m_scroll->setWidget(scrollContent);
    root->addWidget(m_scroll, 1);
}

void ComicsTankoyomiSearchWidget::search(const QString& query)
{
    const int generation = ++m_searchGeneration;
    m_currentQuery = query;
    if (!m_registry) {
        m_statusLabel->setText(QStringLiteral("Search unavailable"));
        return;
    }
    const auto scrapers = m_registry->scrapers();
    // I2: defensive guard for the future-v2 empty-registry case (e.g., user
    // disables every source). Without this, m_pendingSearches stays at 0,
    // onSearchFinished never fires, and the status line hangs on
    // "Searching...".
    if (scrapers.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No sources available"));
        return;
    }

    // I3: disconnect this widget's prior signal connections from each
    // scraper before re-binding. Late-arriving searchFinished/errorOccurred
    // emissions from a previous search() call become no-ops because no
    // receiver exists; the new connections below capture only the current
    // run. ComicsPage's separate errorOccurred → Toast loop has a different
    // receiver (the page itself), so it survives the disconnect.
    for (auto* s : scrapers) {
        QObject::disconnect(s, &MangaScraper::searchFinished, this, nullptr);
        QObject::disconnect(s, &MangaScraper::errorOccurred,  this, nullptr);
    }

    clearResults();
    m_pendingSearches = scrapers.size();
    m_statusLabel->setText(QStringLiteral("Searching... (%1)").arg(query));

    // TANKOYOMI_PREMIUM Phase 8 -- synthetic catalog injection runs BEFORE
    // live scrapers dispatch. The synthetics paint the Premium strip
    // immediately so the user sees Premium-eligible titles even when both
    // scrapers are still in flight (or fail entirely). Live scraper results
    // for the same catalog title layer in via onSearchFinished and dedup
    // against m_previewsByKey by the synthetic "tankoyomi_premium" key, so
    // a WeebCentral hit for a catalog title will still appear in the
    // Premium strip alongside the synthetic tile (different m_previewsByKey
    // keys, but the Premium-routing decision puts them in the same strip).
    injectPremiumCatalogSynthetics(query);

    for (auto* s : scrapers) {
        connect(s, &MangaScraper::searchFinished,
                this, [this, generation](const QList<MangaResult>& batch) {
                    onSearchFinished(batch, generation);
                });
        connect(s, &MangaScraper::errorOccurred,
                this, [this, generation](const QString& message) {
                    onSearchError(message, generation);
                });
        s->search(query, 60);
    }
}

void ComicsTankoyomiSearchWidget::clearResults()
{
    // TANKOYOMI_PREMIUM Phase 8 -- Premium strip clears alongside Manga/Comics.
    if (m_premiumStrip) m_premiumStrip->clear();
    if (m_mangaStrip)   m_mangaStrip->clear();
    if (m_comicsStrip)  m_comicsStrip->clear();
    m_premiumOverflow.clear();
    m_mangaOverflow.clear();
    m_comicsOverflow.clear();
    m_previewsByKey.clear();
    if (m_premiumShowMore) m_premiumShowMore->hide();
    if (m_mangaShowMore)   m_mangaShowMore->hide();
    if (m_comicsShowMore)  m_comicsShowMore->hide();
    if (m_premiumHeader)   m_premiumHeader->hide();
    if (m_mangaHeader)     m_mangaHeader->hide();
    if (m_comicsHeader)    m_comicsHeader->hide();
    if (m_statusLabel)     m_statusLabel->clear();
}

void ComicsTankoyomiSearchWidget::onSearchFinished(const QList<MangaResult>& batch,
                                                   int generation)
{
    if (generation != m_searchGeneration) return;
    for (const auto& r : batch) {
        // I1: defensive cross-fan-out dedup. Today's two scrapers have
        // disjoint source IDs (weebcentral / readcomicsonline) so the
        // collision is zero in practice, but a future v2 source that
        // mirrors one of them under the same (source, id) tuple would
        // double-tile without this gate. Cheap: one QHash::contains.
        const QString key = r.source + ":" + r.id;
        if (m_previewsByKey.contains(key)) continue;
        m_previewsByKey.insert(key, r);

        // TANKOYOMI_PREMIUM Phase 8 -- route Premium-catalog matches to the
        // Premium strip BEFORE the original manga/comics type split. Title
        // match runs through PremiumCatalog::isPremiumSeries which checks
        // primary + alternate titles case-insensitively. Catalog absence is
        // a silent no-op: routing falls through to the prior shape.
        const bool isPremium = (m_premiumCatalog != nullptr)
                            && m_premiumCatalog->isPremiumSeries(r.title);
        if (isPremium) {
            if (m_premiumStrip->totalCount() < kInitialCap) {
                m_premiumHeader->setVisible(true);
                addResultCard(r, m_premiumStrip);
            } else {
                m_premiumOverflow.append(r);
                m_premiumShowMore->setText(
                    QStringLiteral("Show %1 more").arg(m_premiumOverflow.size()));
                m_premiumShowMore->setVisible(true);
                m_premiumHeader->setVisible(true);
            }
            continue;
        }

        const bool isManga = (r.type.compare("manga", Qt::CaseInsensitive) == 0);
        auto& overflow = isManga ? m_mangaOverflow : m_comicsOverflow;
        auto* strip    = isManga ? m_mangaStrip    : m_comicsStrip;
        auto* header   = isManga ? m_mangaHeader   : m_comicsHeader;
        auto* showMore = isManga ? m_mangaShowMore : m_comicsShowMore;
        header->setVisible(true);
        if (strip->totalCount() < kInitialCap) {
            addResultCard(r, strip);
        } else {
            overflow.append(r);
            showMore->setText(QStringLiteral("Show %1 more").arg(overflow.size()));
            showMore->setVisible(true);
        }
    }
    if (--m_pendingSearches <= 0) {
        // TANKOYOMI_PREMIUM Phase 8 -- status reflects Premium head count too.
        const int premiumN = m_premiumStrip->totalCount() + m_premiumOverflow.size();
        const int mangaN   = m_mangaStrip->totalCount()   + m_mangaOverflow.size();
        const int comicsN  = m_comicsStrip->totalCount()  + m_comicsOverflow.size();
        if (premiumN > 0) {
            m_statusLabel->setText(QStringLiteral("Done: %1 premium / %2 manga / %3 comics")
                                    .arg(premiumN).arg(mangaN).arg(comicsN));
        } else {
            m_statusLabel->setText(QStringLiteral("Done: %1 manga / %2 comics")
                                    .arg(mangaN).arg(comicsN));
        }
    }
}

void ComicsTankoyomiSearchWidget::onSearchError(const QString& message, int generation)
{
    if (generation != m_searchGeneration) return;
    // Source-failure toast is fired by ComicsPage (Task 20). Here we just
    // decrement the pending counter so the status line settles.
    if (--m_pendingSearches <= 0) {
        m_statusLabel->setText(QStringLiteral("Done with errors. Last: %1").arg(message));
    }
}

void ComicsTankoyomiSearchWidget::addResultCard(const MangaResult& r, TileStrip* targetStrip)
{
    // Cache hit → load right away; cache miss → spin up downloadPoster
    // and let it set the thumbnail on completion. Mirrors TankoyomiPage's
    // ensureCover pattern but inlines the file-existence check here so
    // we avoid pulling in TankoyomiPage's slot signature.
    const QString thumbPath = MangaPosterCache::existingPath(r.source, r.id);

    auto* card = new TileCard(thumbPath, r.title,
                              r.source.isEmpty() ? QString() : mangaSourceDisplayName(r.source));
    card->setProperty("seriesKey", r.source + ":" + r.id);
    card->setProperty("sourceId",  r.source);
    card->setProperty("seriesId",  r.id);

    // TANKOYOMI_PREMIUM Phase 8 -- chip provenance. Tiles routed to the
    // Premium strip carry the "tankoyomi_premium" provenance, which the
    // TileCard painter renders as [Tankoyomi] + [Premium] (accent-tinted)
    // side-by-side. Non-Premium results in the search widget stay un-
    // chipped here -- the Tankoyomi chip on those is painted by the
    // library-side tiles, not by ephemeral search-result cards.
    if (targetStrip == m_premiumStrip) {
        card->setProvenance(QStringLiteral("tankoyomi_premium"));
    }

    connect(card, &TileCard::clicked, this, [this, r]() {
        emit seriesActivated(r);
    });

    targetStrip->addTile(card);

    if (thumbPath.isEmpty() && !r.thumbnailUrl.isEmpty()) {
        QPointer<TileCard> guard(card);
        MangaPosterCache::download(r, r.thumbnailUrl, m_nam, this,
            [guard](const QString& path) {
                if (guard) guard->setThumbPath(path);
            });
    }
}

void ComicsTankoyomiSearchWidget::downloadPoster(const MangaResult& r, TileCard* card)
{
    QString safeId = r.id;
    safeId.replace(QRegularExpression(R"([<>:"/\\|?*\s])"), "_");
    const QString key  = r.source + "_" + safeId;
    const QString path = m_posterCacheDir + "/" + key + ".jpg";

    QNetworkRequest req{QUrl(r.thumbnailUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    // Referer — some CDNs reject hotlinks without it. Match TankoyomiPage's
    // per-source mapping so existing cache entries stay reachable.
    if (r.source == QLatin1String("weebcentral"))
        req.setRawHeader("Referer", "https://weebcentral.com/");
    else if (r.source == QLatin1String("readcomicsonline"))
        req.setRawHeader("Referer", "https://readcomicsonline.ru/");
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QPointer<TileCard> guard(card);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, path, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QByteArray data = reply->readAll();
        if (data.isEmpty()) return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(data);
        f.close();

        if (guard) guard->setThumbPath(path);
    });
}

void ComicsTankoyomiSearchWidget::revealMangaOverflow()
{
    for (const auto& r : m_mangaOverflow) addResultCard(r, m_mangaStrip);
    m_mangaOverflow.clear();
    if (m_mangaShowMore) m_mangaShowMore->hide();
}

void ComicsTankoyomiSearchWidget::revealComicsOverflow()
{
    for (const auto& r : m_comicsOverflow) addResultCard(r, m_comicsStrip);
    m_comicsOverflow.clear();
    if (m_comicsShowMore) m_comicsShowMore->hide();
}

// TANKOYOMI_PREMIUM Phase 8 ────────────────────────────────────────────────

void ComicsTankoyomiSearchWidget::setPremiumCatalog(
    tankoban::manga::premium::PremiumCatalog* catalog)
{
    m_premiumCatalog = catalog;
}

void ComicsTankoyomiSearchWidget::revealPremiumOverflow()
{
    for (const auto& r : m_premiumOverflow) addResultCard(r, m_premiumStrip);
    m_premiumOverflow.clear();
    if (m_premiumShowMore) m_premiumShowMore->hide();
}

// Inject synthetic catalog-only Premium tiles. Iterates the loaded catalog,
// matches the query (case-insensitive contains) against primary + alternate
// titles, and emits a MangaResult per match with source="tankoyomi_premium".
// The synthetic source key never collides with a live scraper's source ID
// (weebcentral / readcomicsonline), so m_previewsByKey will not suppress a
// later real scraper hit for the same title from also rendering in the
// Premium strip.
//
// PHASE 9+ TODO: a click on a synthetic tile emits seriesActivated with
// source="tankoyomi_premium"; the page-side handler (ComicsPage::
// onSearchResultActivated) currently dispatches by source to a real scraper
// for chapter listing, which the synthetic source has none of. Visible-only
// until Phase 9 adopt-folder migration adds a synthetic-aware route.
void ComicsTankoyomiSearchWidget::injectPremiumCatalogSynthetics(const QString& query)
{
    if (!m_premiumCatalog) return;
    if (!m_premiumStrip)   return;
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return;
    const QString needle = trimmed.toLower();

    const auto entries = m_premiumCatalog->allEntries();
    for (const auto& entry : entries) {
        const bool titleMatches = entry.title.toLower().contains(needle);
        bool altMatches = false;
        if (!titleMatches) {
            for (const auto& alt : entry.alternateTitles) {
                if (alt.toLower().contains(needle)) { altMatches = true; break; }
            }
        }
        if (!titleMatches && !altMatches) continue;

        const QString key = QStringLiteral("tankoyomi_premium:") + entry.seriesId;
        if (m_previewsByKey.contains(key)) continue;

        MangaResult synthetic;
        synthetic.id           = entry.seriesId;
        synthetic.title        = entry.title;
        synthetic.source       = QStringLiteral("tankoyomi_premium");
        synthetic.status       = entry.status;
        synthetic.type         = QStringLiteral("manga");  // catalog v1 is manga-only
        // url / author / thumbnailUrl left empty; TileCard handles missing
        // poster via its placeholder paint.

        if (m_premiumStrip->totalCount() < kInitialCap) {
            m_premiumHeader->setVisible(true);
            addResultCard(synthetic, m_premiumStrip);
        } else {
            m_premiumOverflow.append(synthetic);
            m_premiumShowMore->setText(
                QStringLiteral("Show %1 more").arg(m_premiumOverflow.size()));
            m_premiumShowMore->setVisible(true);
            m_premiumHeader->setVisible(true);
        }
        m_previewsByKey.insert(key, synthetic);
    }
}
