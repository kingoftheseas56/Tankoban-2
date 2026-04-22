# audiobookbay.lu reachability + DOM probe — 2026-04-22

**Probed by:** Agent 4B (Sources), ~5 min curl-only probe (no webview, no desktop MCP).
**Verdict:** GREEN — cleaner than LibGen to scrape. One-scraper-file plumbing, ~1 wake ship.

## 1. Reachability

- `HEAD https://audiobookbay.lu/` → HTTP 200, 0 redirects, ~540ms connect.
- No Cloudflare challenge, no Turnstile, no "Just a Moment", no CAPTCHA. Pure server-rendered HTML.
- No cookie wall, no login required for search or detail pages.
- Backend: WordPress + `simplebalance` theme.
- Default browser User-Agent accepted; not testing without UA (probably unnecessary).

## 2. Search contract

**URL shape:** `GET https://audiobookbay.lu/?s=<urlencoded_query>`

**Pagination:** `/page/N/?s=<urlencoded_query>` (1-indexed, standard WordPress pattern).
Observed 12 pages of results for "rhythm of war" — 10 results per page visible.

**Result block markup** (real rows, NOT honeypots):

```html
<div class="post">
  <div class="postTitle">
    <h2><a href="/abss/<slug>/" rel="bookmark">{{TITLE}}</a></h2>
  </div>
  <div class="postInfo">
    Category: {{CAT1}}&nbsp; {{CAT2}}&nbsp; <br />
    Language: {{LANG}}
    <span style="margin-left:100px;">Keywords: {{KW1}}&nbsp {{KW2}}&nbsp </span>
    <br />
  </div>
  <div class="postContent">
    <div class="center">
      <p class="center">Shared by:<a href="/member/users/...">{{UPLOADER}}</a></p>
      <p class="center"><a href="..."><img src="{{COVER_URL}}" alt="..." width="250" /></a></p>
    </div>
    <p style='text-align:center;'>
      Posted: {{DATE}}<br />
      Format: <span style='color:#a00;'>{{FORMAT}}</span> / Bitrate: <span style='color:#a00;'>{{BITRATE}}</span><br />
      File Size: <span style='color:#00f;'>{{SIZE_NUM}}</span> {{SIZE_UNIT}}
    </p>
  </div>
  <div class='postMeta'>
    <span class='postLink'><a href='...'>Audiobook Details</a></span>
    <span class='postComments'><a href='/dload-now?ll=...'>Direct Download</a></span>
  </div>
</div>
```

**Fields extractable from search row alone** (no detail-page round-trip needed for listing view):
- `title` — text of `postTitle > h2 > a`
- `detailUrl` — href of same anchor, rewritten absolute (`/abss/<slug>/` → `https://audiobookbay.lu/abss/<slug>/`)
- `category` — comma-split of first line in postInfo
- `language` — text after "Language:" in postInfo
- `keywords` — list after "Keywords:" in postInfo span (space-delimited by `&nbsp`)
- `postedDate` — free-text after "Posted:" (human-readable, "17 Nov 2020" shape)
- `format` — span text after "Format:" (`M4B` / `MP3`)
- `bitrate` — span text after "Bitrate:" (`64 Kbps`, `256 Kbps`, `?`)
- `fileSize` — concatenation of "File Size:" number + unit (`1.53 GBs`, `849.22 MBs`)
- `coverUrl` — src of first `<img>` in postContent
- `uploader` — text of "Shared by:" anchor

**This is richer than LibGen search rows** (LibGen gives us author/year/language/pages/size/ext/md5; ABB adds cover URL, bitrate, posted date, uploader directly).

## 3. Honeypot trap (anti-scraper)

Pages contain decoy rows shaped like:

```html
<div class="post re-ab" style="display:none;">{{BASE64_ENCODED_FAKE_HTML}}</div>
```

- Hidden with inline CSS `display:none;` — users never see them
- Contents are base64-encoded fake post markup
- If scraped, "Direct Download" links point to ad-redirect traps

**Filter contract:** match `class="post"` EXACTLY (not `class contains "post"`). In Qt Regex:
```cpp
QRegularExpression rx(R"RX(<div class="post">)RX");
```
NOT `class="post[^"]*"` — that would include honeypot rows.

## 4. Detail page contract

**URL shape:** `GET https://audiobookbay.lu/abss/<slug>/`

**Torrent metadata table** — plain HTML, deterministic parse targets:
- Announce URL: `<td>Tracker:</td>\s*<td>{{TRACKER_URL}}</td>`
- Combined File Size: `<td>Combined File Size:</td>\s*<td>[\s\S]*?>{{SIZE}}[\s\S]*?</td>`
- Piece Size: `<td>Piece Size:</td>\s*<td>[\s\S]*?>{{MB}}[\s\S]*?</td>`
- **Info Hash**: `<td>Info Hash:</td>\s*<td>({{40_HEX}})</td>` ← the load-bearing field
- File list: rows of `<tr><td>This is a Multifile Torrent</td></tr>` followed by filename+size rows

**File-list pattern for multifile torrents:**
```html
<tr><td>This is a Multifile Torrent</td></tr>
<tr><td>Rhythm of War The Stormlight Archive, Book 4.jpg 68.96 KBs</td></tr>
<tr><td>Rhythm of War The Stormlight Archive, Book 4.m4b 1.53 GBs</td></tr>
<tr><td>Rhythm of War The Stormlight Archive, Book 4.nfo 1.34 KBs</td></tr>
```

(Rhythm of War's torrent ships the audiobook as a single `.m4b` + cover `.jpg` + `.nfo` — confirms Agent 2's flag that post-download chapter extraction needs ffprobe-chapter-metadata support for this audiobook shape. But that's post-download, not our scope.)

## 5. Magnet URI construction

**Key finding — the "Magnet" button on the detail page is CLIENT-SIDE JS.**

Raw HTML from `curl` has NO `magnet:?` URI, NO `btih`, NO "Magnet" anchor text. The word "Magnet" shown in Hemanth's screenshot is injected by jQuery in `/js/main.js`:

```javascript
// From /js/main.js (verbatim excerpt):
torDown.after(' | <a id="magnetLink" href="javascript:void(0)">Magnet</a> ...');

$('body').on('click', '#magnetLink', function() {
    var data = "magnet:"
        + "?xt=urn:btih:" + $('td:contains("Info Hash:")').next().text()
        + "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.open-internet.nl%3A6969%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A69691337%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce"
        + "&tr=http%3A%2F%2Ftracker.baravik.org%3A6970%2Fannounce"
        + "&tr=http%3A%2F%2Fretracker.telecom.by%3A80%2Fannounce"
        + "&tr=http%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce";
    // ...
});
```

**We replicate this EXACTLY in our scraper:**
1. Fetch `/abss/<slug>/` HTML
2. Regex-extract info hash (40-hex) from `<td>Info Hash:</td>\s*<td>([0-9a-fA-F]{40})</td>`
3. Construct magnet URI with hash + verbatim 7-tracker list from main.js (keeping the `:69691337` typo; libtorrent will fail that one and move on)
4. Hand to `TorrentEngine::addMagnet(uri, savePath=booksRoot)`

**Fallback path** (handle gracefully if detail page doesn't have Info Hash in plain HTML):
- `/download-magnet.php?h=<hashCode>` — AJAX endpoint returning the info hash given some page-local `hashCode`. Not needed for the pages we've seen; implement only if a real-world URL surfaces without plain-HTML Info Hash.

## 6. Anti-scraper: URL slug misspellings

Observed in search results — many slugs have typos: `rhytuhm`, `sgtormlight`, `tdhe`, `sitormlight`, `theo`, `hthe`, `mrhythm`. These are deliberate — they make the URLs non-guessable from the title alone. **Does NOT affect scraping** since we always extract the `href` from the anchor directly rather than constructing URLs ourselves.

## 7. What we do NOT use (the "HTTP caveats" Hemanth flagged)

- **Torrent Free Downloads** (`/downld0?downfs=...`) → filehost ad-wall redirect
- **Direct Download** (`/dload-now?ll=...` / `/dedl4-now?teb=...`) → same
- **Secured Download** (`/hi-3dl?downf=...`) → same

All three go to filehosts (Rapidgator/DDownload/similar) with free-tier captcha + wait timer + throttled speeds, premium paywall for fast lane. **We never touch these paths.** Magnet-only.

## 8. Scope sketch for TANKOLIBRARY_ABB_FIX_TODO (shape only, not authoring)

**Track A (primary):**
1. NEW `src/core/book/AbbScraper.{h,cpp}` (~400-500 LOC, follows `LibGenScraper` pattern exactly). QNetworkAccessManager, no webview. Search + parse + detail-fetch + magnet-construct.
2. `BookResult` additive fields: `audioFormat`, `audioBitrate`, `audioPostedDate` (or just stuff them into existing `format`/`year` where reasonable — need to look at shape).
3. `TankoLibraryPage` tabs: segmented control at top ("Books" / "Audiobooks"); separate `m_scrapers` lists per tab; separate filter chips per tab (Books: existing "EPUB only"; Audiobooks: new "Unabridged only" and/or "M4B / MP3" format filter).
4. Download routing: when `BookResult.sourceId == "abb"`, skip `BookDownloader` entirely; call `m_bridge->torrentEngine()->addMagnet(magnetUri, booksRoot)` instead. Progress reporting flows through existing `TorrentEngine::torrentStateUpdated` signal, which already has a UI consumer in TransfersView.
5. Destination folder: same `rootFolders("books").first()` as EPUB downloads (matches where your existing audiobook library already lives per Agent 2's scanner).

**Track B (polish, later):**
- Cover image download + cache (search rows have URLs, detail pages have nicer covers).
- Optional "Mirror to Audiobooks/ root" config for users who want separate location.
- "Author this post" filter / uploader-rep lens (we have uploader usernames from search rows).

**Does NOT require:**
- Agent 2 domain work (his audiobook walker already handles single-M4B and multi-MP3 shapes).
- Agent 4 stream-engine work.
- Any new TorrentEngine API (`addMagnet` + `torrentStateUpdated` already exist and are battle-tested).
- HELP request to any other agent.

**Est. scope:** 1 wake to ship, similar to LibGen M2.3 + M2.4 combined. Same scaffold shape, simpler resolve (no ads.php parse, just info-hash regex).

## 9. Open Qs for Hemanth before TODO authoring

1. **Tabs vs unified feed** — I recommend tabs (cleaner, filter vocabulary diverges). Confirm?
2. **Destination folder** — same `Media\Books\` as EPUBs (simplest, matches existing), or new `Media\Audiobooks\`?
3. **Default sources on Audiobooks tab** — ABB only to start (same pattern as Books tab shipping LibGen-only first), or also search AA's audiobook corpus in parallel (likely blocked by same /books/ captcha we already punted on; recommend skip)?
4. **M4B chapter-metadata support** — belongs to Agent 2 (post-download reader-side concern). I don't need a decision from you on this to ship the scraper. Flag it in the TODO for Agent 2's next wake.

## Artifacts in this directory

- `home.html` — ABB homepage (468 lines)
- `search_rhythm.html` — search results for "rhythm of war" (451 lines)
- `detail_rhythm.html` — detail page for Rhythm of War (1103 lines)
- `main.js` — ABB's JS including magnet construction logic (658 lines)
- `FINDINGS.md` — this doc
