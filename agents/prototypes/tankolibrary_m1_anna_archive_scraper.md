// Prototype by Agent 7 (Codex), 2026-04-21. For [Agent 4B, M1 completion].
// Reference only — domain agent implements their own version.
// Do not compile. Do not include from src/.

## 1. Summary

As of 2026-04-21, Agent 4B's selector failure is not mainly a "pick better `/md5/...` anchors" problem. The bigger contract change is that Anna's Archive's live public domain has moved. The official Telegram currently says `annas-archive.io` is the only official domain, and all of `.org`, `.se`, and `.li` are compromised or gone ([t.me/s/annasarchiveofficial](https://t.me/s/annasarchiveofficial), lines 41-46). From this network on 2026-04-21, `https://annas-archive.li/search?q=orwell+1984` returns a parked ParkLogic page titled `Redirecting...`, while `https://annas-archive.io/search?q=orwell+1984` returns the real search HTML directly with one result card and a detail URL under `/books/...`. So the M1 fix is: switch the base URL to `.io`, stop looking for `/md5/<hash>` anchors, and extract rows from `/books/<route-key>` cards instead.

## 2. Load Flow

Your current `QWebEngineView` + `loadFinished` + 1.5s settle timer flow in [AnnaArchiveScraper.h](/C:/Users/Suprabha/Desktop/Tankoban%202/src/core/book/AnnaArchiveScraper.h:62) and [AnnaArchiveScraper.cpp](/C:/Users/Suprabha/Desktop/Tankoban%202/src/core/book/AnnaArchiveScraper.cpp:239) is acceptable for M1. I would not rewrite the flow yet.

What changed is the domain, not the need for a longer settle strategy:

- Your current base constant points at `.li` ([AnnaArchiveScraper.cpp](/C:/Users/Suprabha/Desktop/Tankoban%202/src/core/book/AnnaArchiveScraper.cpp:21)).
- On 2026-04-21, `.io` serves the final search HTML directly. I did not observe the old JS interstitial there.
- Because your scaffold already works end-to-end, the lowest-risk M1 port is: keep the browser flow, change `kAaBase`, replace `kExtractJs`, and keep the rest.

M2 note: current `.io` search/detail pages are plain HTTP-fetchable, so `QNetworkRequest` is back on the table for search/detail. Anonymous download resolution is a separate problem now, because live detail pages currently gate download buttons behind login/member flow.

## 3. Extraction JavaScript

Drop-in replacement for `kExtractJs`. It preserves the current payload shape (`{ ok, rows, raw_anchor_count, ... }`). The `md5` field is intentionally reused as a source-id carrier, because live AA search cards no longer expose MD5 hashes; they expose `/books/<route-key>` paths.

```javascript
(function () {
    function text(value) {
        return (value || "").replace(/\u00a0/g, " ").replace(/\s+/g, " ").trim();
    }

    function absUrl(href) {
        try { return new URL(href, window.location.origin).href; }
        catch (_) { return ""; }
    }

    function splitMeta(line) {
        return text(line).split(/\s*[·•]\s*/).map(text).filter(Boolean);
    }

    function looksLikeYear(value) {
        return /^(?:(?:18|19|20)\d{2}|0)$/.test(text(value));
    }

    function looksLikeSize(value) {
        return /^\d[\d.,]*\s*(?:B|KB|MB|GB|TB)$/i.test(text(value));
    }

    function looksLikeFormat(value) {
        return /^(?:7z|azw\d?|azw3|cbr|cbz|djvu|docx?|epub|fb2|file|html?|mobi|pdf|rtf|txt|zip)$/i.test(text(value));
    }

    function looksLikeLanguage(value) {
        return /^(?:English|French|German|Spanish|Russian|Italian|Chinese(?: \(?:Traditional|Simplified\))?|Portuguese(?: \(?:Brazil|Portugal\))?|Dutch|Hungarian|Japanese|Polish|Turkish|Czech|Hindi|Romanian|Catalan|Swedish|Indonesian|Arabic|Korean|Greek|Latin|Danish|Ukrainian|Norwegian|Finnish|Vietnamese|Persian|Hebrew|Lithuanian|Bulgarian|Slovenian|Serbian|Slovak|Tamil|Welsh|Thai|Kazakh|Estonian|Tagalog|Icelandic|Latvian|Marathi|Georgian|Telugu|Armenian|Mongolian|Bosnian|Swahili|Sanskrit|Esperanto|Nepali|Punjabi|Gujarati|Malayalam|Irish|Yiddish|Kannada|Amharic)$/i.test(text(value));
    }

    function looksLikeSource(value) {
        return /(catalog|libgen|z-lib|sci-hub|journal|digital lending)/i.test(text(value));
    }

    function cleanLabel(value) {
        return text(value).replace(/[:\s]+$/, "");
    }

    function detailFacts(detailUrl) {
        var facts = { language: "", format: "", year: "", fileSize: "", isbn: "" };
        try {
            var xhr = new XMLHttpRequest();
            xhr.open("GET", detailUrl, false);
            xhr.send(null);
            if (xhr.status < 200 || xhr.status >= 300 || !xhr.responseText) {
                return facts;
            }

            var doc = new DOMParser().parseFromString(xhr.responseText, "text/html");
            var map = new Map();

            Array.from(doc.querySelectorAll("tr")).forEach(function (tr) {
                var cells = tr.querySelectorAll("td");
                if (cells.length >= 2) {
                    map.set(cleanLabel(cells[0].textContent), text(cells[1].textContent));
                }
            });

            Array.from(doc.querySelectorAll("dt")).forEach(function (dt) {
                var dd = dt.nextElementSibling;
                if (dd) {
                    map.set(cleanLabel(dt.textContent), text(dd.textContent));
                }
            });

            facts.language = map.get("Language") || "";
            facts.format = text(map.get("Format") || map.get("Extension") || "").toLowerCase();
            facts.year = map.get("Year") || "";
            facts.fileSize = map.get("Approx. size") || map.get("File size") || "";
            facts.isbn = map.get("ISBN") || "";
        } catch (_) {
        }
        return facts;
    }

    try {
        var title = text(document.title);
        var bodyText = text(document.body ? document.body.innerText : "");
        var lowerDump = (title + "\n" + bodyText).toLowerCase();

        if (window.location.hostname !== "annas-archive.io" &&
            /parklogic|category search|domain status|parking/i.test(lowerDump)) {
            return JSON.stringify({
                ok: false,
                error: "Live Anna's Archive search is no longer on this host. Switch kAaBase to https://annas-archive.io.",
                raw_anchor_count: 0,
                page_url: window.location.href,
                page_title: document.title
            });
        }

        if (/browser verification|captcha|too many requests|just a moment|temporarily unavailable/i.test(lowerDump)) {
            return JSON.stringify({
                ok: false,
                error: "Search page is showing a verification/rate-limit screen instead of results.",
                raw_anchor_count: 0,
                page_url: window.location.href,
                page_title: document.title
            });
        }

        if (/showing 0 results on this page/i.test(bodyText) || /no records found/i.test(bodyText)) {
            return JSON.stringify({
                ok: true,
                rows: [],
                raw_anchor_count: 0,
                page_url: window.location.href,
                page_title: document.title,
                empty_reason: "no_results"
            });
        }

        var anchorCandidates = Array.from(document.querySelectorAll('h3 a[href*="/books/"]'));
        if (!anchorCandidates.length) {
            anchorCandidates = Array.from(document.querySelectorAll('a[href*="/books/"]')).filter(function (a) {
                return !!a.closest('div.bg-white.rounded-lg.shadow.p-4.mb-4');
            });
        }

        var rows = [];
        var seen = new Set();
        var detailFetchCount = 0;

        anchorCandidates.forEach(function (anchor) {
            var detailUrl = absUrl(anchor.getAttribute("href") || "");
            var routeMatch = detailUrl.match(/\/books\/([^/?#]+)/i);
            var routeKey = routeMatch ? decodeURIComponent(routeMatch[1]) : "";
            if (!routeKey || seen.has(routeKey)) {
                return;
            }
            seen.add(routeKey);

            var card = anchor.closest('div.bg-white.rounded-lg.shadow.p-4.mb-4') || anchor.closest("div");
            var h3 = anchor.closest("h3");
            var infoRoot = h3 && h3.parentElement ? h3.parentElement : card;

            var metadataLine = "";
            Array.from(infoRoot.children || []).some(function (el) {
                if (el === h3) return false;
                var candidate = text(el.textContent);
                if (candidate.indexOf("·") !== -1) {
                    metadataLine = candidate;
                    return true;
                }
                return false;
            });

            var publisherLine = "";
            Array.from(infoRoot.children || []).some(function (el) {
                var candidate = text(el.textContent);
                if (/^Publisher:/i.test(candidate)) {
                    publisherLine = candidate.replace(/^Publisher:\s*/i, "");
                    return true;
                }
                return false;
            });

            var description = text((infoRoot.querySelector("p") || {}).textContent || "");
            var coverUrl = absUrl(((card.querySelector("img") || {}).getAttribute || function(){ return ""; }).call(card.querySelector("img"), "src"));

            var tokens = splitMeta(metadataLine);
            var author = "";
            var year = "";
            var format = "";
            var fileSize = "";
            var language = "";

            if (tokens.length &&
                !looksLikeYear(tokens[0]) &&
                !looksLikeSize(tokens[0]) &&
                !looksLikeFormat(tokens[0]) &&
                !looksLikeLanguage(tokens[0]) &&
                !looksLikeSource(tokens[0])) {
                author = tokens.shift();
            }

            tokens.forEach(function (token) {
                if (!year && looksLikeYear(token)) {
                    year = token;
                } else if (!format && looksLikeFormat(token)) {
                    format = token.toLowerCase();
                } else if (!fileSize && looksLikeSize(token)) {
                    fileSize = token;
                } else if (!language && looksLikeLanguage(token)) {
                    language = token;
                }
            });

            if ((!format || !language || !fileSize || !year) && detailFetchCount < 8 && detailUrl) {
                detailFetchCount += 1;
                var extra = detailFacts(detailUrl);
                language = language || extra.language;
                format = format || extra.format;
                fileSize = fileSize || extra.fileSize;
                year = year || extra.year;
            }

            rows.push({
                md5: routeKey,
                title: text(anchor.textContent),
                author: author,
                format: format,
                year: year,
                fileSize: fileSize,
                language: language,
                coverUrl: coverUrl,
                detailUrl: detailUrl,
                description: description || publisherLine
            });
        });

        if (!rows.length && /showing \d+ results? on this page/i.test(bodyText)) {
            return JSON.stringify({
                ok: false,
                error: "Results page loaded, but no /books/ anchors matched. The live DOM drifted again.",
                raw_anchor_count: anchorCandidates.length,
                page_url: window.location.href,
                page_title: document.title
            });
        }

        return JSON.stringify({
            ok: true,
            rows: rows,
            raw_anchor_count: anchorCandidates.length,
            page_url: window.location.href,
            page_title: document.title,
            detail_fetch_count: detailFetchCount
        });
    } catch (e) {
        return JSON.stringify({
            ok: false,
            error: String(e),
            raw_anchor_count: 0,
            page_url: window.location.href,
            page_title: document.title
        });
    }
})();
```

## 4. Selector Rationale

- **Base domain**: use `https://annas-archive.io`, not `.li`. Official Telegram says `.io` is the only official domain and that `.li/.org/.se` are compromised or gone ([t.me/s/annasarchiveofficial](https://t.me/s/annasarchiveofficial), lines 41-46). Live observation on 2026-04-21: `.li` returned a ParkLogic page titled `Redirecting...`, while `.io` returned the real search HTML.
- **Search route**: keep `/search?q=...`. Both the live `.io` page and the LilyLoops search template still use `/search` ([live search page](https://annas-archive.io/search?q=orwell+1984), lines 68-76; LilyLoops `search.html`, lines 53-59 from `allthethings/page/templates/page/search.html`).
- **Result-row anchor selector**: `h3 a[href*="/books/"]`. On the live `orwell 1984` page, the canonical row link is the title anchor under `### ORWELL 1984`, and its href is `/books/36143020-orwell-1984` ([live search page](https://annas-archive.io/search?q=orwell+1984), lines 70-75). On a broader query (`dune`) multiple rows use the same `/books/<route-key>` pattern, including `/books/52781-dune-52781` and `/books/20967401-dune-messiah-dune-chronicles-econo-clad-hardcover` (live observation 2026-04-21).
- **Do not use `/md5/<hash>`**: I did not observe `/md5/...` search-result anchors on the live `.io` search pages I tested on 2026-04-21. Current search cards are `/books/...` cards, not old `/md5/...` cards.
- **Result-row card container**: `div.bg-white.rounded-lg.shadow.p-4.mb-4`. This exact wrapper appears around each live list-view search result on `.io`, including the `orwell 1984` row and multi-row `dune` results (live observation 2026-04-21; [live search page](https://annas-archive.io/search?q=orwell+1984), lines 70-75).
- **Title selector**: `h3 a[href*="/books/"]`. The title text is inside the anchor itself on live `.io` ([live search page](https://annas-archive.io/search?q=orwell+1984), lines 72-75).
- **Metadata selector**: use the first sibling text block under the title root that contains `·`. Live cards encode metadata as a single bullet-delimited line: `unknown author · 1 B · Books catalog` for `orwell 1984`, and `Brian Herbert, Kevin J. Anderson · 2020 · CBR · 224.7 MB · Books catalog` for one `dune` result (live observation 2026-04-21; [live search page](https://annas-archive.io/search?q=orwell+1984), line 75).
- **Publisher / description**: some live cards add `Publisher: ...` in a second gray metadata line and a description `<p>` under that. Example observed on `dune`: `Publisher: Harry N. Abrams` plus a truncated description paragraph. This is live-DOM-backed, not a LilyLoops-template claim. Hypothesis — Agent 4B to validate via smoke if you want to surface it in M1 diagnostics.
- **Cover selector**: first `img` inside the card. Live cards either render a real `<img>` with a cover URL or a fallback color block with title/author text when the image is missing ([live search page](https://annas-archive.io/search?q=orwell+1984), lines 72-75 show the real-cover case; live `dune` observation showed both real-cover and fallback-cover variants).
- **Format / language / size enrichment**: some search rows expose these directly in the bullet line; others do not. For `orwell 1984`, the search card only exposes `unknown author · 1 B · Books catalog`, but the detail page exposes `English · FILE · 1 B` and labeled technical-detail rows for `Language`, `Format`, and `Approx. size` ([live detail page](https://annas-archive.io/books/36143020-orwell-1984), lines 61-65 and 134-151). That is why the JS does synchronous same-origin detail enrichment for rows missing those fields.
- **Zero-results contract**: current `.io` zero-results pages render `Showing 0 results on this page` plus `No records found` ([live zero-results page](https://annas-archive.io/search?q=zzzxxyyqqq_unfindable_1984), lines 70-73). The JS should return `ok: true, rows: []` there, not an extraction error.
- **Old slow-download/browser-verification contract**: LilyLoops still has `partner_download.html` branches for `no_cloudflare` and a countdown wait banner (`partner_download.html`, lines 19-21 and 46, plus the countdown JS at 96-113), but live anonymous `.io` detail pages currently show `Log in to access downloads` instead of direct partner links ([live detail page](https://annas-archive.io/books/36143020-orwell-1984), lines 159-167 and 252-255). So treat the LilyLoops download templates as M2 background only, not as M1 selector truth.

## 5. Port Notes For Agent 4B

1. In [AnnaArchiveScraper.cpp](/C:/Users/Suprabha/Desktop/Tankoban%202/src/core/book/AnnaArchiveScraper.cpp:21), change `kAaBase` from `https://annas-archive.li` to `https://annas-archive.io`.
2. Replace `kExtractJs` entirely with the block in Section 3. Do not keep the old `/md5|item|record/` anchor heuristics.
3. Keep your current `loadFinished`/settle/runJavaScript flow in [AnnaArchiveScraper.cpp](/C:/Users/Suprabha/Desktop/Tankoban%202/src/core/book/AnnaArchiveScraper.cpp:239) through [AnnaArchiveScraper.cpp](/C:/Users/Suprabha/Desktop/Tankoban%202/src/core/book/AnnaArchiveScraper.cpp:281). No C++ flow rewrite is required for M1.
4. Keep `extractResults()` parse logic unchanged. The replacement JS still returns `rows[i].md5`, `title`, `author`, `format`, `year`, `fileSize`, `language`, `coverUrl`, `detailUrl`.
5. Accept that `rows[i].md5` is now a misnamed source-id carrier. On the live site it will contain the `/books/<route-key>` tail, not a real MD5 hash. That is okay for M1 because your C++ currently just needs a stable non-empty `sourceId`.
6. Leave the 1.5s settle timer in place for now. It is unnecessary on `.io` from my observation, but harmless and lower-risk than touching lifecycle code during M1 closeout.
7. Keep the diagnostic fields if you want one more smoke pass of visibility; strip them after the smoke if you want the payload clean. They are not required for correctness.
8. Do not spend time reviving `.li` interstitial handling. Current official traffic is on `.io`; the live `.li` host is the wrong target.
9. M2 planning note only: re-audit anonymous download resolution before implementing `resolveDownload()`. Current live `.io` detail pages are login-gated and do not expose the old anonymous partner-link surface directly.

## 6. Definition Of Done

On a post-port smoke dated 2026-04-21 or later, with `kAaBase = https://annas-archive.io`, Agent 4B should see this in the live Tankoban app for query `orwell 1984`:

- `Done: 1 result(s) from Anna's Archive.` or higher.
- At least one row with title `ORWELL 1984`.
- Non-empty `detailUrl` pointing at `/books/36143020-orwell-1984`.
- Non-empty source-id carrier in the current `md5` field: `36143020-orwell-1984`.
- `author = unknown author`, `fileSize = 1 B`, and best-effort detail-enriched `language = English`, `format = file`.
- `year` may legitimately come through as `0` on this exact record, because that is what the current detail page exposes in its technical fields on 2026-04-21. That is not an extraction failure.

If the page instead errors with `Redirecting...`, `Category Search`, or a ParkLogic-looking landing page, the base-domain change did not land.
