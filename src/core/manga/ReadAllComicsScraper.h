#pragma once

#include "MangaScraper.h"

// ReadAllComics (readallcomics.com) — the WORKING Western page-source.
//
// Why this exists: rcostation (ReadComicsScraper, sourceId "readcomicsonline")
// browses fine but its reader is browser-only obfuscation (rguard.min.js,
// ~18% descramble — dead for download, see project_comics_western_downloads_arc).
// readallcomics.com serves the SAME blogspot-CDN page images as plain <img>
// URLs — no descramble, no JS, no Cloudflare on plain HTTP. Proven end-to-end
// 2026-06-03: Invincible #144 -> 55/55 pages -> valid 33 MB cbz.
//
// Recipe: docs/superpowers/specs/2026-06-03-readallcomics-source-recipe.md
//   search        : GET /?story=<q>&s=&type=comic -> <a class="cat-title"> rows
//   fetchChapters : GET /category/<series-slug>/  -> /<issue-slug>/ links
//   fetchPages    : GET /<issue-slug>/            -> raw <img src=".bp.blogspot..">
//
// Identity-only scraper: implements search/fetchChapters/fetchPages. fetchDetail
// is a no-op (rcostation owns the rich Western detail/catalogue page); this
// source is the page-fetch engine that MangaDownloader drives to produce a cbz.
class ReadAllComicsScraper : public MangaScraper
{
    Q_OBJECT

public:
    explicit ReadAllComicsScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : MangaScraper(nam, parent) {}

    QString sourceId() const override { return QStringLiteral("readallcomics"); }
    QString sourceName() const override { return QStringLiteral("ReadAllComics"); }

    void search(const QString& query, int limit = 60) override;
    void fetchChapters(const QString& seriesSlug) override;
    void fetchPages(const QString& chapterId) override;
    void fetchDetail(const MangaResult& preview) override;

    // Pure parsers — exposed for unit testing against saved fixtures.
    static QList<MangaResult> parseSearchHtml(const QString& html);
    static QList<ChapterInfo> parseChaptersHtml(const QString& html);
    static QList<PageInfo>    parsePagesHtml(const QString& html);
};
