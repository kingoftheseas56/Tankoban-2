# ReadAllComics as the Western comic page-source — proven recipe (2026-06-03)

**Why:** rcostation (readcomicsonline) reader is browser-only obfuscation (18% descramble,
dead end — see `project_comics_western_downloads_arc` memory). readallcomics.com serves the
**same blogspot CDN images as plain `<img>` URLs** — no descramble, no JS, no Cloudflare on
plain curl. PROVEN end-to-end 2026-06-03: Invincible #144 → 55/55 pages → valid 33MB cbz,
~1s/page. Script: `agents/audits/rco_run_their_js2.js` (the rcostation dead-end probe) +
the readallcomics proof inline below.

## The scrape recipe (all plain HTTP GET, UA = a normal Chrome UA, Referer https://readallcomics.com/)

1. **Search:** `GET https://readallcomics.com/?story=<query>&s=&type=comic`
   → results are `<a href="https://readallcomics.com/category/<series-slug>/" class="cat-title">Series (Publisher: X)</a>`.
   Pick the right publisher (e.g. "Invincible (Publisher: Image Comics)" → `category/invincible-image-comics/`).

2. **Issue list (fetchChapters):** `GET https://readallcomics.com/category/<series-slug>/`
   → issue links are `<a href="https://readallcomics.com/<issue-slug>/">` where issue-slug is
   like `invincible-144-2018` (`<series>-<number>-<year>`). Filter out nav/utility links
   (category/page/report/request/vip/comment/privacy/dmca/contact/tag/author/wp-/feed/terms/about).
   Category pages paginate (`/page/2/` etc.) for long runs.

3. **Pages (fetchPages):** `GET https://readallcomics.com/<issue-slug>/`
   → every page is `<img ... src="https://<N>.bp.blogspot.com/...=s0?rhlupa=...&#038;rnvuka=...">`
   directly in raw HTML. Extract with `<img[^>]+src="(https://\d\.bp\.blogspot\.com/[^"]+)"`,
   HTML-decode `&#038;`→`&`. URLs fetch directly as JPEG/PNG (with or without Referer).

4. **Pack:** download each image → MangaDownloader's existing page->cbz packer. DONE.

## C++ integration (next session — small, reuses the whole pipeline)

- New scraper `ReadAllComicsScraper : MangaScraper` (sourceId "readallcomics") implementing
  search / fetchChapters / fetchPages per the recipe. Mirror ReadComicsScraper's QNAM shape.
  Register in `MangaSourceRegistry` + it auto-wires into `MangaDownloader` via setScraper.
- `MangaDownloader.cpp` image-fetch: add an `else if (source=="readallcomics") Referer
  https://readallcomics.com/`. (Already has the setTransferTimeout from 5cbc864.)
- The download TRIGGER + Sources-panel status UX (07f7b15) are source-agnostic — reuse as-is;
  just feed source="readallcomics" + the issue-slug as chapterId.
- **Catalogue decision (Hemanth):** readallcomics has SINGLE ISSUES (not rcostation's TPBs).
  Either (a) make readallcomics the full Western source (its search + issue list drive the
  catalogue — issues, not TPBs), or (b) keep rcostation catalogue/metadata + cross-map a
  clicked TPB to its readallcomics issues (harder: TPB->issue-range mapping). Lean (a) — it's
  the clean, robust path; the catalogue just becomes issue-granular.
- Caveat: ChatGPT deep-research flagged intermittent Cloudflare IUAM on readallcomics; plain
  curl got through clean all session, but if it 403s under load, add TLS-impersonation headers
  (Sec-Ch-Ua etc.) or a cloudscraper-style client. Refs: Nighmared/RAD, lucascunha rad.py.

## Status of the rcostation work (keep / dead)
- KEEP (source-agnostic, reused): MangaDownloader page->cbz, the download trigger, Sources UX.
- DEAD (rcostation-specific): ReadComicsPageParse descramble (18%), the fixture test. Leave in
  tree; a future cleanup retires it once readallcomics ships.
