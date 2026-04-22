#include "AnnaArchiveScraper.h"

#include "AaSlowDownloadWaitHandler.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#ifdef HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif

namespace {

// Search-side kExtractJs does serial synchronous XHR per result to enrich
// search rows from each /books/<id> detail page; 20 results × ~1-2s each can
// legitimately take 30-60s on a slow AA day. Keep the budget generous so
// a 30s timeout doesn't cut off a healthy-but-slow extraction. M2.2+ cookie-
// harvest-then-raw-HTTP pattern will drop this dramatically.
constexpr int kLoadTimeoutMs = 90'000;
constexpr int kSettleMs = 1'500;
constexpr const char* kAaBase = "https://annas-archive.io";

// M2.2 — wait-handler polling budgets. Kept conservative; AA interstitials
// typically clear in 3-6s on a healthy network. 30s is well above p99.
constexpr int kChallengeWaitMs  = 30'000;
constexpr int kChallengePollMs  = 500;

// M2.2 — predicate JS for AaSlowDownloadWaitHandler. Returns true when the
// browser-verification interstitial is gone: no challenge form/stage and no
// "just a moment / verify you are human" body text. This is the mirror of
// the JS challenge-detect patterns already used in kExtractJs (line ~150)
// and kDetailExtractJs (line ~436), flipped to positive polarity.
constexpr const char* kChallengeClearedJs = R"JS(
(function() {
    try {
        var hasChallengeDom = !!document.querySelector('form[action*="challenge"], #challenge-stage');
        var txt = (document.body && document.body.innerText) ? document.body.innerText : '';
        var hasChallengeText = /just a moment|checking your browser|verify you are human|browser verification/i.test(txt);
        return !hasChallengeDom && !hasChallengeText;
    } catch (e) {
        return false;
    }
})();
)JS";

// M2.2 — JS extractor for the first-tier slow_download link list from an AA
// /books/<route-key> page. Returns { ok: true, urls: [...] } with de-duped
// absolute hrefs of all /slow_download/ anchors. Also surfaces the challenge
// error in the same shape kDetailExtractJs uses so the C++ side can
// uniformly detect interstitial and route through the wait handler.
constexpr const char* kSlowDownloadLinksJs = R"JS(
(function() {
    try {
        var bodyText = (document.body && document.body.innerText) ? document.body.innerText : '';
        var pageText = (document.title || '') + ' ' + bodyText;
        if (/just a moment|checking your browser|verify you are human|browser verification/i.test(pageText)
                || document.querySelector('form[action*="challenge"], #challenge-stage')) {
            return JSON.stringify({ ok: false, error: 'Anna\'s Archive browser verification blocked extraction' });
        }
        var anchors = Array.prototype.slice.call(document.querySelectorAll('a[href*="/slow_download/"]'));
        var urls = anchors.map(function(a) { return a.href; }).filter(Boolean);
        var seen = {};
        var unique = [];
        for (var i = 0; i < urls.length; i++) {
            if (!seen[urls[i]]) {
                seen[urls[i]] = true;
                unique.push(urls[i]);
            }
        }
        return JSON.stringify({ ok: true, urls: unique });
    } catch (e) {
        return JSON.stringify({ ok: false, error: String(e && e.message ? e.message : e) });
    }
})();
)JS";

// Extraction JS runs on the settled search results DOM. Current Anna's Archive
// search cards are anchored by h3 > a[href*="/books/"], while format,
// language, and size often live only on the same-origin detail page. Parse the
// list card first, then synchronously enrich missing fields from /books/<id>.
const char* kExtractJs = R"JS(
(function() {
    try {
        const textOf = (value) => ((value && value.textContent) || '')
            .replace(/\s+/g, ' ')
            .trim();
        const stringOf = (value) => String(value == null ? '' : value)
            .replace(/\s+/g, ' ')
            .trim();
        const normalizeLabel = (value) => textOf({ textContent: value })
            .toLowerCase()
            .replace(/[.:]/g, '')
            .trim();
        const parseMetaParts = (value) => textOf({ textContent: value })
            .split(/\s*[\u00B7\u2022|]\s*/)
            .map((part) => part.trim())
            .filter(Boolean);
        const sizeExactRe = /^\d+(?:\.\d+)?\s*(?:B|KB|MB|GB|TB)$/i;
        const yearStartRe = /^\s*(18\d{2}|19\d{2}|20\d{2}|21\d{2})(?:\b|-\d{2}-\d{2})/;
        const parseStandaloneYear = (value) => {
            const match = stringOf(value).match(/^(18\d{2}|19\d{2}|20\d{2}|21\d{2})$/);
            return match ? match[1] : '';
        };
        const parseDetailYear = (value) => {
            const match = stringOf(value).match(yearStartRe);
            return match ? match[1] : '';
        };
        const parseFormat = (value, allowFileFallback) => {
            const text = stringOf(value);
            const match = text.match(/\b(epub|pdf|mobi|azw3|djvu|cbz|cbr|fb2|txt|rtf|html)\b/i);
            if (match) return match[1].toLowerCase();
            if (allowFileFallback && /^file$/i.test(text)) return 'FILE';
            return '';
        };
        const parseExactSize = (value) => {
            const text = stringOf(value);
            return sizeExactRe.test(text) ? text.toUpperCase() : '';
        };
        const parseLanguage = (value) => {
            const match = stringOf(value).match(/\b(English|Spanish|French|German|Russian|Chinese|Japanese|Italian|Portuguese|Dutch|Korean|Arabic|Hindi|Polish|Turkish|Swedish|Czech|Greek|Hebrew|Danish|Ukrainian|Romanian|Hungarian)\b/i);
            return match ? match[1] : '';
        };
        const looksLikeSource = (value) => /\bcatalog\b|\blibrary\b|\bdataset\b|\bsource\b/i.test(value);
        const sanitizeAuthor = (value) => {
            const text = stringOf(value);
            if (!text) return '';
            if (/^(unknown author|unknown authors|n\/a|none|null|undefined|[-—–•·]+)$/i.test(text)) return '';
            return text;
        };
        const flattenJsonLd = (input, out) => {
            if (!input) return;
            if (Array.isArray(input)) {
                for (const item of input) flattenJsonLd(item, out);
                return;
            }
            if (typeof input !== 'object') return;
            out.push(input);
            if (input['@graph']) flattenJsonLd(input['@graph'], out);
        };
        const parseJsonLdAuthor = (value) => {
            if (Array.isArray(value)) {
                return sanitizeAuthor(value.map((entry) => parseJsonLdAuthor(entry)).filter(Boolean).join(', '));
            }
            if (value && typeof value === 'object') {
                return sanitizeAuthor(value.name || value['@name'] || '');
            }
            return sanitizeAuthor(value);
        };
        const parseJsonLdBook = (doc) => {
            const nodes = [];
            for (const script of Array.from(doc.querySelectorAll('script[type="application/ld+json"]'))) {
                const raw = stringOf(script.textContent);
                if (!raw) continue;
                try {
                    flattenJsonLd(JSON.parse(raw), nodes);
                } catch (_error) {
                }
            }

            for (const node of nodes) {
                const rawType = node['@type'];
                const types = Array.isArray(rawType) ? rawType : [rawType];
                if (!types.some((type) => stringOf(type).toLowerCase() === 'book')) continue;
                return {
                    title: stringOf(node.name),
                    author: parseJsonLdAuthor(node.author),
                    publisher: stringOf(node.publisher && typeof node.publisher === 'object' ? node.publisher.name : node.publisher),
                    year: parseDetailYear(node.datePublished),
                    language: parseLanguage(node.inLanguage),
                    format: parseFormat(node.bookFormat, false),
                    coverurl: stringOf(node.image),
                };
            }

            return {};
        };
        const parseDownloadHintFormat = (doc) => {
            for (const anchor of Array.from(doc.querySelectorAll('a[href], a[download]'))) {
                const candidates = [
                    anchor.getAttribute('download') || '',
                    anchor.getAttribute('href') || '',
                    textOf(anchor),
                ];
                for (const candidate of candidates) {
                    const match = stringOf(candidate).match(/\.(epub|pdf|mobi|azw3|djvu|cbz|cbr|fb2|txt|rtf|html)(?:$|[?#\s])/i);
                    if (match) return match[1].toLowerCase();
                }
            }
            return '';
        };

        const bodyText = textOf(document.body);
        const pageText = textOf(document.title) + ' ' + bodyText;

        if (/parklogic|category search/i.test(pageText)) {
            return JSON.stringify({ ok: false, error: 'Anna\'s Archive returned a parked or non-live page' });
        }

        if (/just a moment|checking your browser|verify you are human|browser verification/i.test(pageText)
                || document.querySelector('form[action*="challenge"], #challenge-stage')) {
            return JSON.stringify({ ok: false, error: 'Anna\'s Archive browser verification blocked extraction' });
        }

        const anchors = Array.from(document.querySelectorAll('h3 a[href*="/books/"]'));
        if (!anchors.length) {
            if (/no records found/i.test(bodyText) || /showing 0 results/i.test(bodyText)) {
                return JSON.stringify({ ok: true, rows: [], raw_anchor_count: 0 });
            }
            return JSON.stringify({ ok: false, error: 'Anna\'s Archive results page did not contain any /books/ result anchors' });
        }

        const detailCache = new Map();
        const readDetailFacts = (detailUrl) => {
            if (detailCache.has(detailUrl)) {
                return detailCache.get(detailUrl);
            }

            const facts = {};
            try {
                const xhr = new XMLHttpRequest();
                xhr.open('GET', detailUrl, false);
                xhr.send(null);

                if (xhr.status >= 200 && xhr.status < 300 && xhr.responseText) {
                    const doc = new DOMParser().parseFromString(xhr.responseText, 'text/html');
                    Object.assign(facts, parseJsonLdBook(doc));

                    for (const row of Array.from(doc.querySelectorAll('dl > div'))) {
                        const dt = row.querySelector('dt');
                        const dd = row.querySelector('dd');
                        const label = normalizeLabel(textOf(dt));
                        const value = textOf(dd);
                        if (label && value) facts[label] = value;
                    }

                    for (const row of Array.from(doc.querySelectorAll('table tr'))) {
                        const cells = row.querySelectorAll('td');
                        if (cells.length < 2) continue;
                        const label = normalizeLabel(textOf(cells[0]));
                        const value = textOf(cells[1]);
                        if (label && value && !facts[label]) facts[label] = value;
                    }

                    const ogImage = doc.querySelector('meta[property="og:image"]');
                    if (ogImage && ogImage.content) {
                        facts.coverurl = ogImage.content;
                    }

                    if (!facts.downloadformat) {
                        facts.downloadformat = parseDownloadHintFormat(doc);
                    }
                }
            } catch (_error) {
            }

            detailCache.set(detailUrl, facts);
            return facts;
        };

        const seen = new Set();
        const rows = [];

        for (const anchor of anchors) {
            const detailUrl = new URL(anchor.getAttribute('href') || '', window.location.origin).href;
            const routeMatch = detailUrl.match(/\/books\/([^/?#]+)/i);
            const routeKey = routeMatch ? routeMatch[1] : '';
            if (!routeKey || seen.has(routeKey)) continue;
            seen.add(routeKey);

            const card = anchor.closest('div.bg-white.rounded-lg.shadow.p-4.mb-4')
                || anchor.closest('div.bg-white')
                || anchor.parentElement;
            const metaNode = card
                ? Array.from(card.querySelectorAll('div')).find((node) => /[\u00B7\u2022]/.test(textOf(node)))
                : null;
            const metaParts = parseMetaParts(textOf(metaNode));

            let title = textOf(anchor);
            let author = '';
            let format = '';
            let year = '';
            let fileSize = '';
            let language = '';
            let coverUrl = '';

            const img = card ? card.querySelector('img[src]') : null;
            if (img && img.src) {
                coverUrl = img.src;
            }

            const searchAuthor = sanitizeAuthor(metaParts.find((part) => {
                return !looksLikeSource(part)
                    && !parseStandaloneYear(part)
                    && !parseFormat(part, false)
                    && !parseExactSize(part)
                    && !parseLanguage(part);
            }) || '');
            const searchYear = metaParts.map((part) => parseStandaloneYear(part)).find(Boolean) || '';
            const searchFormat = metaParts.map((part) => parseFormat(part, false)).find(Boolean) || '';

            let fallbackAuthor = '';
            if (card) {
                fallbackAuthor = sanitizeAuthor(Array.from(card.querySelectorAll('div, span, p'))
                    .map((node) => textOf(node))
                    .find((value) => value
                        && value !== title
                        && !/^Publisher:/i.test(value)
                        && !/[\u00B7\u2022|]/.test(value)) || '');
            }

            const facts = readDetailFacts(detailUrl);
            if (!title) title = stringOf(facts.title);

            const detailAuthor = sanitizeAuthor(
                facts.author
                || facts.authors
                || facts['author(s)']
                || '');
            const detailYear = parseDetailYear(
                facts['publication year']
                || facts['published']
                || facts['date published']
                || facts.year
                || '');
            const detailFormat = parseFormat(
                facts.format
                || facts.bookformat
                || facts.fileformat
                || '',
                false);
            const detailFileFallback = parseFormat(
                facts.format
                || facts.bookformat
                || facts.fileformat
                || '',
                true);
            const detailSize = parseExactSize(
                facts['approx size']
                || facts['file size']
                || facts.size
                || '');
            const detailLanguage = parseLanguage(facts.language || facts.inlanguage || '');
            const hintedFormat = parseFormat(facts.downloadformat || '', false) || stringOf(facts.downloadformat);

            author = detailAuthor || searchAuthor || fallbackAuthor;
            year = detailYear || searchYear;
            format = detailFormat || hintedFormat || searchFormat || detailFileFallback;
            fileSize = detailSize;
            language = detailLanguage || parseLanguage(metaParts.join(' '));
            if (!coverUrl) coverUrl = stringOf(facts.coverurl);

            rows.push({
                md5: routeKey,
                title: title,
                author: author,
                format: format,
                year: year,
                fileSize: fileSize,
                language: language,
                coverUrl: coverUrl,
                detailUrl: detailUrl
            });
        }

        return JSON.stringify({ ok: true, rows: rows, raw_anchor_count: anchors.length });
    } catch (error) {
        return JSON.stringify({ ok: false, error: String(error && error.message ? error.message : error) });
    }
})();
)JS";

// M2.1 detail-page extraction. Runs on the already-navigated detail DOM
// (https://annas-archive.io/books/<route-key>) so no XHR round-trip needed —
// contrast with kExtractJs's readDetailFacts helper which uses sync XHR from
// inside the search webview. Same field contract as readDetailFacts plus
// publisher / pages / isbn / description for the richer detail panel.
const char* kDetailExtractJs = R"JS(
(function() {
    try {
        const textOf = (value) => ((value && value.textContent) || '')
            .replace(/\s+/g, ' ')
            .trim();
        const stringOf = (value) => String(value == null ? '' : value)
            .replace(/\s+/g, ' ')
            .trim();
        const normalizeLabel = (value) => textOf({ textContent: value })
            .toLowerCase()
            .replace(/[.:]/g, '')
            .trim();
        const sizeExactRe = /^\d+(?:\.\d+)?\s*(?:B|KB|MB|GB|TB)$/i;
        const yearStartRe = /^\s*(18\d{2}|19\d{2}|20\d{2}|21\d{2})(?:\b|-\d{2}-\d{2})/;
        const parseDetailYear = (value) => {
            const match = stringOf(value).match(yearStartRe);
            return match ? match[1] : '';
        };
        const parseFormat = (value, allowFileFallback) => {
            const text = stringOf(value);
            const match = text.match(/\b(epub|pdf|mobi|azw3|djvu|cbz|cbr|fb2|txt|rtf|html)\b/i);
            if (match) return match[1].toLowerCase();
            if (allowFileFallback && /^file$/i.test(text)) return 'FILE';
            return '';
        };
        const parseExactSize = (value) => {
            const text = stringOf(value);
            return sizeExactRe.test(text) ? text.toUpperCase() : '';
        };
        const parseLanguage = (value) => {
            const match = stringOf(value).match(/\b(English|Spanish|French|German|Russian|Chinese|Japanese|Italian|Portuguese|Dutch|Korean|Arabic|Hindi|Polish|Turkish|Swedish|Czech|Greek|Hebrew|Danish|Ukrainian|Romanian|Hungarian)\b/i);
            return match ? match[1] : '';
        };
        const parsePages = (value) => {
            const match = stringOf(value).match(/\b(\d{1,5})\b/);
            return match ? match[1] : '';
        };
        const sanitizeAuthor = (value) => {
            const text = stringOf(value);
            if (!text) return '';
            if (/^(unknown author|unknown authors|n\/a|none|null|undefined|[-—–•·]+)$/i.test(text)) return '';
            return text;
        };
        const sanitizePublisher = (value) => {
            const text = stringOf(value);
            if (!text) return '';
            if (/^(unknown|unknown publisher|n\/a|none|null|undefined|[-—–•·]+)$/i.test(text)) return '';
            return text;
        };
        const flattenJsonLd = (input, out) => {
            if (!input) return;
            if (Array.isArray(input)) {
                for (const item of input) flattenJsonLd(item, out);
                return;
            }
            if (typeof input !== 'object') return;
            out.push(input);
            if (input['@graph']) flattenJsonLd(input['@graph'], out);
        };
        const parseJsonLdAuthor = (value) => {
            if (Array.isArray(value)) {
                return sanitizeAuthor(value.map((entry) => parseJsonLdAuthor(entry)).filter(Boolean).join(', '));
            }
            if (value && typeof value === 'object') {
                return sanitizeAuthor(value.name || value['@name'] || '');
            }
            return sanitizeAuthor(value);
        };
        const parseJsonLdBook = (doc) => {
            const nodes = [];
            for (const script of Array.from(doc.querySelectorAll('script[type="application/ld+json"]'))) {
                const raw = stringOf(script.textContent);
                if (!raw) continue;
                try {
                    flattenJsonLd(JSON.parse(raw), nodes);
                } catch (_error) {
                }
            }
            for (const node of nodes) {
                const rawType = node['@type'];
                const types = Array.isArray(rawType) ? rawType : [rawType];
                if (!types.some((type) => stringOf(type).toLowerCase() === 'book')) continue;
                return {
                    title: stringOf(node.name),
                    author: parseJsonLdAuthor(node.author),
                    publisher: sanitizePublisher(
                        node.publisher && typeof node.publisher === 'object'
                            ? node.publisher.name
                            : node.publisher),
                    year: parseDetailYear(node.datePublished),
                    language: parseLanguage(node.inLanguage),
                    format: parseFormat(node.bookFormat, false),
                    isbn: stringOf(node.isbn),
                    pages: parsePages(node.numberOfPages),
                    description: stringOf(node.description),
                    coverurl: stringOf(node.image),
                };
            }
            return {};
        };

        const bodyText = textOf(document.body);
        const pageText = textOf(document.title) + ' ' + bodyText;

        if (/parklogic|category search/i.test(pageText)) {
            return JSON.stringify({ ok: false, error: 'Anna\'s Archive returned a parked or non-live page' });
        }
        if (/just a moment|checking your browser|verify you are human|browser verification/i.test(pageText)
                || document.querySelector('form[action*="challenge"], #challenge-stage')) {
            return JSON.stringify({ ok: false, error: 'Anna\'s Archive browser verification blocked extraction' });
        }

        // Start from JSON-LD @type=Book (most structured + authoritative).
        const ld = parseJsonLdBook(document);

        // Walk <dl><div><dt><dd></dt></dd></div></dl> labeled facts.
        const facts = {};
        for (const row of Array.from(document.querySelectorAll('dl > div'))) {
            const dt = row.querySelector('dt');
            const dd = row.querySelector('dd');
            const label = normalizeLabel(textOf(dt));
            const value = textOf(dd);
            if (label && value) facts[label] = value;
        }
        for (const row of Array.from(document.querySelectorAll('table tr'))) {
            const cells = row.querySelectorAll('td');
            if (cells.length < 2) continue;
            const label = normalizeLabel(textOf(cells[0]));
            const value = textOf(cells[1]);
            if (label && value && !facts[label]) facts[label] = value;
        }

        // og:image as cover fallback when JSON-LD image is absent.
        const ogImage = document.querySelector('meta[property="og:image"]');
        const ogImageSrc = ogImage && ogImage.getAttribute('content') ? ogImage.getAttribute('content') : '';

        // Description: JSON-LD description first, then meta[name="description"],
        // then the first prose <div> / <article> / <section> longer than 80 chars.
        // Cap to ~800 chars to keep the UI panel sane.
        let description = stringOf(ld.description);
        if (!description) {
            const metaDesc = document.querySelector('meta[name="description"]');
            description = metaDesc && metaDesc.getAttribute('content') ? stringOf(metaDesc.getAttribute('content')) : '';
        }
        if (!description) {
            const candidates = Array.from(document.querySelectorAll('div.prose, article, section, main > div'));
            for (const node of candidates) {
                const t = textOf(node);
                if (t.length >= 120) { description = t; break; }
            }
        }
        if (description.length > 800) description = description.slice(0, 800).trim() + '…';

        // md5OrId echoed from URL so C++ can sanity-check we landed on the
        // page we asked for.
        const routeMatch = (window.location.pathname || '').match(/\/books\/([^/?#]+)/i);
        const md5OrId = routeMatch ? routeMatch[1] : '';

        const title = stringOf(ld.title) || textOf(document.querySelector('h1, h2'));
        const author = sanitizeAuthor(
            ld.author
            || facts.author
            || facts.authors
            || facts['author(s)']
            || '');
        const publisher = sanitizePublisher(
            ld.publisher
            || facts.publisher
            || '');
        const year = parseDetailYear(
            ld.year
            || facts['publication year']
            || facts['published']
            || facts['date published']
            || facts.year
            || '');
        const format = parseFormat(
            ld.format
            || facts.format
            || facts.bookformat
            || facts.fileformat
            || '',
            false)
            || parseFormat(
                ld.format
                || facts.format
                || facts.bookformat
                || facts.fileformat
                || '',
                true);
        const fileSize = parseExactSize(
            facts['approx size']
            || facts['file size']
            || facts.size
            || '');
        const language = parseLanguage(
            ld.language
            || facts.language
            || facts.inlanguage
            || '');
        const pages = parsePages(
            ld.pages
            || facts.pages
            || facts['number of pages']
            || '');
        const isbn = stringOf(
            ld.isbn
            || facts.isbn
            || facts['isbn-10']
            || facts['isbn-13']
            || '');
        const coverUrl = stringOf(ld.coverurl) || ogImageSrc;
        const detailUrl = window.location.href;

        return JSON.stringify({
            ok: true,
            detail: {
                md5OrId: md5OrId,
                title: title,
                author: author,
                publisher: publisher,
                year: year,
                format: format,
                fileSize: fileSize,
                language: language,
                pages: pages,
                isbn: isbn,
                description: description,
                coverUrl: coverUrl,
                detailUrl: detailUrl
            }
        });
    } catch (error) {
        return JSON.stringify({ ok: false, error: String(error && error.message ? error.message : error) });
    }
})();
)JS";

} // namespace

AnnaArchiveScraper::AnnaArchiveScraper(QNetworkAccessManager* nam, QObject* parent)
    : BookScraper(nam, parent)
{
    m_loadTimeout = new QTimer(this);
    m_loadTimeout->setSingleShot(true);
    m_loadTimeout->setInterval(kLoadTimeoutMs);
    connect(m_loadTimeout, &QTimer::timeout, this, [this]() {
        QString which;
        switch (m_mode) {
        case Mode::FetchingDetail:    which = QStringLiteral("detail fetch"); break;
        case Mode::ResolvingDownload: which = QStringLiteral("download resolve"); break;
        default:                      which = QStringLiteral("search"); break;
        }
        fail(QStringLiteral("Anna's Archive %1 timed out after %2s")
             .arg(which)
             .arg(kLoadTimeoutMs / 1000));
    });

    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    m_settleTimer->setInterval(kSettleMs);
    connect(m_settleTimer, &QTimer::timeout, this, &AnnaArchiveScraper::onExtractTimerFired);
}

AnnaArchiveScraper::~AnnaArchiveScraper()
{
#ifdef HAS_WEBENGINE
    delete m_view;
#endif
}

void AnnaArchiveScraper::reset()
{
    m_mode = Mode::Idle;
    m_currentQuery.clear();
    m_currentMd5OrId.clear();
    m_loadTimeout->stop();
    m_settleTimer->stop();

    // M2.2 — tear down any in-flight wait handler. cancel() is no-op if
    // already finished; deleteLater keeps teardown safe if called from
    // within the handler's own signal handler.
    if (m_waitHandler) {
        m_waitHandler->cancel();
        m_waitHandler->deleteLater();
        m_waitHandler = nullptr;
    }
    m_detailRetryUsed  = false;
    m_resolveRetryUsed = false;
}

void AnnaArchiveScraper::fail(const QString& reason)
{
    reset();
    emit errorOccurred(reason);
}

#ifdef HAS_WEBENGINE

void AnnaArchiveScraper::ensureView()
{
    if (m_view) return;

    // Off-the-record profile keeps AA cookies ephemeral; they don't bleed
    // into the Foliate book reader's webengine profile or disk storage.
    m_profile = new QWebEngineProfile(this);

    m_view = new QWebEngineView;  // parentless — see ~dtor
    m_view->setAttribute(Qt::WA_DontShowOnScreen, true);

    auto* page = new QWebEnginePage(m_profile, m_view);
    m_view->setPage(page);

    connect(m_view, &QWebEngineView::loadFinished,
            this, &AnnaArchiveScraper::onLoadFinished);
}

void AnnaArchiveScraper::search(const QString& query, int limit)
{
    if (m_mode != Mode::Idle) {
        reset();
    }

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(QStringLiteral("Empty query"));
        return;
    }

    m_mode         = Mode::Searching;
    m_currentQuery = trimmed;
    m_currentLimit = limit;

    ensureView();

    QUrl target(QStringLiteral("%1/search").arg(kAaBase));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), m_currentQuery);
    target.setQuery(q);

    qDebug() << "[AnnaArchiveScraper] search ->" << target.toString();

    m_loadTimeout->start();
    m_view->load(target);
}

void AnnaArchiveScraper::onLoadFinished(bool ok)
{
    if (m_mode == Mode::Idle) return;
    if (!ok) {
        fail(QStringLiteral("Anna's Archive page load failed - check network + annas-archive.io reachability"));
        return;
    }

    // Some AA variants still bounce through JS redirects (and the detail page
    // may do it too). Restart the settle timer on every loadFinished so
    // extraction runs after the last DOM settle, regardless of mode.
    m_settleTimer->start();
}

void AnnaArchiveScraper::onExtractTimerFired()
{
    switch (m_mode) {
    case Mode::Idle:              return;
    case Mode::Searching:         extractResults();          return;
    case Mode::FetchingDetail:    extractDetail();           return;
    case Mode::ResolvingDownload: extractSlowDownloadLinks(); return;
    }
}

void AnnaArchiveScraper::extractResults()
{
    if (!m_view || !m_view->page()) {
        fail(QStringLiteral("Anna's Archive webview missing at extract time"));
        return;
    }

    m_view->page()->runJavaScript(QString::fromLatin1(kExtractJs), [this](const QVariant& jsResult) {
        if (m_mode != Mode::Searching) return;  // cancelled or taken over by fetchDetail

        const QString jsonStr = jsResult.toString();
        if (jsonStr.isEmpty()) {
            fail(QStringLiteral("Anna's Archive extract returned empty payload"));
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            fail(QStringLiteral("Anna's Archive extract JSON parse failed: %1").arg(parseErr.errorString()));
            return;
        }

        const QJsonObject root = doc.object();
        if (!root.value(QStringLiteral("ok")).toBool()) {
            fail(QStringLiteral("Anna's Archive extract JS error: %1")
                 .arg(root.value(QStringLiteral("error")).toString()));
            return;
        }

        const QJsonArray jsonRows = root.value(QStringLiteral("rows")).toArray();
        const int rawAnchorCount = root.value(QStringLiteral("raw_anchor_count")).toInt();

        QList<BookResult> results;
        results.reserve(jsonRows.size());

        for (const QJsonValue& value : jsonRows) {
            if (!value.isObject()) continue;
            const QJsonObject o = value.toObject();

            BookResult r;
            r.source = sourceId();
            r.sourceId = o.value(QStringLiteral("md5")).toString();
            r.md5 = r.sourceId;
            r.title = o.value(QStringLiteral("title")).toString();
            r.author = o.value(QStringLiteral("author")).toString();
            r.format = o.value(QStringLiteral("format")).toString();
            r.year = o.value(QStringLiteral("year")).toString();
            r.fileSize = o.value(QStringLiteral("fileSize")).toString();
            r.language = o.value(QStringLiteral("language")).toString();
            r.coverUrl = o.value(QStringLiteral("coverUrl")).toString();
            r.detailUrl = o.value(QStringLiteral("detailUrl")).toString();

            if (r.md5.isEmpty() || r.title.isEmpty()) continue;
            results.append(r);
            if (results.size() >= m_currentLimit) break;
        }

        qInfo() << "[AnnaArchiveScraper] extracted" << results.size()
                << "results from" << rawAnchorCount << "AA result anchors";

        reset();
        emit searchFinished(results);
    });
}

void AnnaArchiveScraper::fetchDetail(const QString& md5OrId)
{
    if (m_mode != Mode::Idle) {
        reset();
    }

    const QString trimmed = md5OrId.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(QStringLiteral("Anna's Archive detail fetch called with empty id"));
        return;
    }

    m_mode           = Mode::FetchingDetail;
    m_currentMd5OrId = trimmed;

    ensureView();

    // md5OrId is an AA route-key (e.g. "36143020-orwell-1984") — the BookResult
    // field is called md5 for symmetry with LibGen etc., but AA's contract
    // is a slugged route-key, not a hex hash. URL-building uses verbatim.
    const QUrl target(QStringLiteral("%1/books/%2").arg(kAaBase, trimmed));

    qDebug() << "[AnnaArchiveScraper] fetchDetail ->" << target.toString();

    m_loadTimeout->start();
    m_view->load(target);
}

void AnnaArchiveScraper::extractDetail()
{
    if (!m_view || !m_view->page()) {
        fail(QStringLiteral("Anna's Archive webview missing at detail-extract time"));
        return;
    }

    m_view->page()->runJavaScript(QString::fromLatin1(kDetailExtractJs), [this](const QVariant& jsResult) {
        if (m_mode != Mode::FetchingDetail) return;  // cancelled or taken over

        const QString jsonStr = jsResult.toString();
        if (jsonStr.isEmpty()) {
            fail(QStringLiteral("Anna's Archive detail extract returned empty payload"));
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            fail(QStringLiteral("Anna's Archive detail JSON parse failed: %1").arg(parseErr.errorString()));
            return;
        }

        const QJsonObject root = doc.object();
        if (!root.value(QStringLiteral("ok")).toBool()) {
            const QString errStr = root.value(QStringLiteral("error")).toString();

            // M2.2 — challenge-specific retry. If the error is the browser-
            // verification interstitial AND we haven't already retried, kick
            // off the wait handler and re-run extractDetail when it clears.
            // Non-challenge errors and second-strike challenge both fail
            // honestly.
            const bool isChallenge = errStr.contains(
                QStringLiteral("browser verification"), Qt::CaseInsensitive);
            if (isChallenge && !m_detailRetryUsed && m_view && m_view->page()) {
                m_detailRetryUsed = true;
                qInfo() << "[AnnaArchiveScraper] detail hit browser-verification interstitial"
                        << "- starting wait handler (up to" << kChallengeWaitMs / 1000 << "s)";

                if (m_waitHandler) {
                    m_waitHandler->cancel();
                    m_waitHandler->deleteLater();
                }
                m_waitHandler = new AaSlowDownloadWaitHandler(
                    m_view,
                    QString::fromLatin1(kChallengeClearedJs),
                    kChallengePollMs,
                    kChallengeWaitMs,
                    this);
                connect(m_waitHandler, &AaSlowDownloadWaitHandler::waitCompleted,
                        this, [this]() {
                            if (m_mode != Mode::FetchingDetail) return;
                            qInfo() << "[AnnaArchiveScraper] browser-verification cleared, retrying extractDetail";
                            // Small settle so DOM fully renders post-interstitial.
                            QTimer::singleShot(500, this, [this]() {
                                if (m_mode == Mode::FetchingDetail) extractDetail();
                            });
                        });
                connect(m_waitHandler, &AaSlowDownloadWaitHandler::waitFailed,
                        this, [this](const QString& reason) {
                            if (m_mode != Mode::FetchingDetail) return;
                            fail(QStringLiteral("Anna's Archive browser verification did not clear: %1").arg(reason));
                        });
                m_waitHandler->start();
                return;
            }

            fail(QStringLiteral("Anna's Archive detail JS error: %1").arg(errStr));
            return;
        }

        const QJsonObject d = root.value(QStringLiteral("detail")).toObject();

        BookResult r;
        r.source      = sourceId();
        r.sourceId    = d.value(QStringLiteral("md5OrId")).toString();
        if (r.sourceId.isEmpty()) r.sourceId = m_currentMd5OrId;
        r.md5         = r.sourceId;
        r.title       = d.value(QStringLiteral("title")).toString();
        r.author      = d.value(QStringLiteral("author")).toString();
        r.publisher   = d.value(QStringLiteral("publisher")).toString();
        r.year        = d.value(QStringLiteral("year")).toString();
        r.format      = d.value(QStringLiteral("format")).toString();
        r.fileSize    = d.value(QStringLiteral("fileSize")).toString();
        r.language    = d.value(QStringLiteral("language")).toString();
        r.pages       = d.value(QStringLiteral("pages")).toString();
        r.isbn        = d.value(QStringLiteral("isbn")).toString();
        r.description = d.value(QStringLiteral("description")).toString();
        r.coverUrl    = d.value(QStringLiteral("coverUrl")).toString();
        r.detailUrl   = d.value(QStringLiteral("detailUrl")).toString();

        // Validation: at least one structured field must have come through.
        // An AA page that rendered but had zero JSON-LD + zero labeled table
        // entries is almost certainly a broken record or a verification stub.
        const bool anyStructured = !r.title.isEmpty()
            || !r.author.isEmpty()
            || !r.publisher.isEmpty()
            || !r.year.isEmpty()
            || !r.format.isEmpty()
            || !r.fileSize.isEmpty()
            || !r.language.isEmpty()
            || !r.pages.isEmpty()
            || !r.isbn.isEmpty()
            || !r.description.isEmpty()
            || !r.coverUrl.isEmpty();
        if (!anyStructured) {
            fail(QStringLiteral("Anna's Archive detail page contained no structured metadata"));
            return;
        }

        qInfo() << "[AnnaArchiveScraper] detail ->" << r.sourceId
                << "title=" << r.title << "author=" << r.author
                << "format=" << r.format << "year=" << r.year;

        reset();
        emit detailReady(r);
    });
}

void AnnaArchiveScraper::resolveDownload(const QString& md5OrId)
{
    // M2.2 — scaffolded first-tier resolver. Loads /books/<md5OrId>, enumerates
    // all a[href*="/slow_download/"] anchors, emits downloadResolved with the
    // de-duped list. Second-tier resolution (navigate each slow_download page,
    // wait countdown, extract final direct-mirror URL) is M2.3's job — the
    // current emit exposes the first tier so a human can manually verify
    // the AA flow works end-to-end before BookDownloader lands.
    if (m_mode != Mode::Idle) {
        // Refuse-concurrent policy per plan §5 Rule-14 call #4.
        emit downloadFailed(md5OrId.trimmed(), QStringLiteral("scraper busy"));
        return;
    }

    const QString trimmed = md5OrId.trimmed();
    if (trimmed.isEmpty()) {
        emit downloadFailed(trimmed, QStringLiteral("empty id passed to resolveDownload"));
        return;
    }

    m_mode             = Mode::ResolvingDownload;
    m_currentMd5OrId   = trimmed;
    m_resolveRetryUsed = false;

    ensureView();

    // Always navigate — simpler state machine than branching on current URL.
    // Cost: 1-2s extra if the webview was already on /books/<md5OrId>.
    // Benefit: zero coupling to whatever the previous mode left on screen.
    const QUrl target(QStringLiteral("%1/books/%2").arg(kAaBase, trimmed));

    qDebug() << "[AnnaArchiveScraper] resolveDownload ->" << target.toString();

    m_loadTimeout->start();
    m_view->load(target);
}

void AnnaArchiveScraper::extractSlowDownloadLinks()
{
    if (!m_view || !m_view->page()) {
        fail(QStringLiteral("Anna's Archive webview missing at download-extract time"));
        return;
    }

    m_view->page()->runJavaScript(QString::fromLatin1(kSlowDownloadLinksJs), [this](const QVariant& jsResult) {
        if (m_mode != Mode::ResolvingDownload) return;  // cancelled

        const QString md5 = m_currentMd5OrId;
        const QString jsonStr = jsResult.toString();
        if (jsonStr.isEmpty()) {
            reset();
            emit downloadFailed(md5, QStringLiteral("Anna's Archive download-extract returned empty payload"));
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            reset();
            emit downloadFailed(md5, QStringLiteral("Anna's Archive download-extract JSON parse failed: %1").arg(parseErr.errorString()));
            return;
        }

        const QJsonObject root = doc.object();
        if (!root.value(QStringLiteral("ok")).toBool()) {
            const QString errStr = root.value(QStringLiteral("error")).toString();
            const bool isChallenge = errStr.contains(
                QStringLiteral("browser verification"), Qt::CaseInsensitive);

            if (isChallenge && !m_resolveRetryUsed && m_view && m_view->page()) {
                m_resolveRetryUsed = true;
                qInfo() << "[AnnaArchiveScraper] resolveDownload hit browser-verification interstitial"
                        << "- starting wait handler";

                if (m_waitHandler) {
                    m_waitHandler->cancel();
                    m_waitHandler->deleteLater();
                }
                m_waitHandler = new AaSlowDownloadWaitHandler(
                    m_view,
                    QString::fromLatin1(kChallengeClearedJs),
                    kChallengePollMs,
                    kChallengeWaitMs,
                    this);
                connect(m_waitHandler, &AaSlowDownloadWaitHandler::waitCompleted,
                        this, [this]() {
                            if (m_mode != Mode::ResolvingDownload) return;
                            QTimer::singleShot(500, this, [this]() {
                                if (m_mode == Mode::ResolvingDownload) extractSlowDownloadLinks();
                            });
                        });
                connect(m_waitHandler, &AaSlowDownloadWaitHandler::waitFailed,
                        this, [this, md5](const QString& reason) {
                            if (m_mode != Mode::ResolvingDownload) return;
                            reset();
                            emit downloadFailed(md5, QStringLiteral("Anna's Archive browser verification did not clear: %1").arg(reason));
                        });
                m_waitHandler->start();
                return;
            }

            reset();
            emit downloadFailed(md5, QStringLiteral("Anna's Archive download-extract JS error: %1").arg(errStr));
            return;
        }

        const QJsonArray urlArr = root.value(QStringLiteral("urls")).toArray();
        QStringList urls;
        urls.reserve(urlArr.size());
        for (const QJsonValue& v : urlArr) {
            const QString s = v.toString();
            if (!s.isEmpty()) urls.append(s);
        }

        qInfo() << "[AnnaArchiveScraper] resolveDownload extracted"
                << urls.size() << "slow_download URL(s) for" << md5;

        if (urls.isEmpty()) {
            reset();
            emit downloadFailed(md5, QStringLiteral("Anna's Archive detail page had no /slow_download/ links"));
            return;
        }

        reset();
        emit downloadResolved(md5, urls);
    });
}

#else // HAS_WEBENGINE

void AnnaArchiveScraper::ensureView() {}

void AnnaArchiveScraper::search(const QString& /*query*/, int /*limit*/)
{
    emit errorOccurred(QStringLiteral("Anna's Archive requires Qt WebEngine - not available in this build"));
}

void AnnaArchiveScraper::onLoadFinished(bool /*ok*/) {}
void AnnaArchiveScraper::onExtractTimerFired() {}
void AnnaArchiveScraper::extractResults() {}
void AnnaArchiveScraper::extractDetail() {}
void AnnaArchiveScraper::extractSlowDownloadLinks() {}

void AnnaArchiveScraper::fetchDetail(const QString& /*md5OrId*/)
{
    emit errorOccurred(QStringLiteral("Anna's Archive requires Qt WebEngine - not available in this build"));
}

void AnnaArchiveScraper::resolveDownload(const QString& md5OrId)
{
    emit downloadFailed(md5OrId.trimmed(),
                        QStringLiteral("Anna's Archive requires Qt WebEngine - not available in this build"));
}

#endif // HAS_WEBENGINE
