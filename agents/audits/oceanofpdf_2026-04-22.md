# Audit - oceanofpdf - 2026-04-22

By Agent 7 (Codex). For Agent 4B (TankoLibrary).
Reference comparison: live `oceanofpdf.com`, reachable adjacent fronts `theoceanofpdf.com` and `allepub.com`, historical press/legal coverage around OceanOfPDF.
Scope: Read-only Trigger C audit answering Agent 4B's 7-question M4 brief: reachability, gate classification, domain/mirror census, three page-shape snapshots, download-flow decomposition, pattern-fit recommendation, coverage delta probe, and a short legal-risk read. Out of scope: prescribing fixes as fact, implementing a scraper, or asserting operator continuity between current lookalike domains and the original `oceanofpdf.com` brand without independent proof.

## Observed behavior (in our codebase)

- Tankoban's book-scraper base already supports the two families relevant to this audit: plain `QNetworkAccessManager` scrapers and JS-executing `QWebEngineView` scrapers. `BookScraper` explicitly notes that some sources need browser execution rather than raw `QNetworkRequest`, while still exposing the same `search`, `fetchDetail`, and `resolveDownload` contract. Citations: `src/core/book/BookScraper.h:19-21`, `src/core/book/BookScraper.h:37-41`.
- Anna's Archive is our current example of the browser-executing path. `AnnaArchiveScraper` is `QWebEngineView`-backed, loads `/search` and `/books/<id>` in the embedded browser, and extracts slow-download links via DOM JS. Citations: `src/core/book/AnnaArchiveScraper.h:13-24`, `src/core/book/AnnaArchiveScraper.h:60-62`, `src/core/book/AnnaArchiveScraper.cpp:720-722`, `src/core/book/AnnaArchiveScraper.cpp:841-842`, `src/core/book/AnnaArchiveScraper.cpp:1005-1012`.
- Anna's current implementation already treats browser-verification pages as blocking conditions. Its JS extraction returns `"Anna's Archive browser verification blocked extraction"` when it sees challenge text or challenge DOM. Citations: `src/core/book/AnnaArchiveScraper.cpp:63-67`, `src/core/book/AnnaArchiveScraper.cpp:206-208`, `src/core/book/AnnaArchiveScraper.cpp:492-494`.
- LibGen is our example of the plain stateless path. It uses raw `QNetworkRequest`, then resolves direct mirror URLs from HTML such as `get.php` and `library.lol`. Citations: `src/core/book/LibGenScraper.cpp:127-131`, `src/core/book/LibGenScraper.cpp:389-414`, `src/core/book/LibGenScraper.cpp:488-490`.
- TankoLibrary's UI pipeline expects each source to end in resolved direct URLs, not a custom browser handoff. The page triggers `scraper->resolveDownload(...)`, then `BookDownloader` starts from the returned URL list. Citations: `src/ui/pages/TankoLibraryPage.cpp:912-915`, `src/ui/pages/TankoLibraryPage.cpp:979-983`.
- Our current Cloudflare helper is narrower than a full captcha solver. `CloudflareCookieHarvester` is a hidden `QWebEngineView` flow that waits for a `cf_clearance` cookie and emits success when that cookie appears. It does not model active Turnstile interaction. Citations: `src/core/indexers/CloudflareCookieHarvester.h:11-15`, `src/core/indexers/CloudflareCookieHarvester.cpp:138-156`.
- TankoLibrary currently keeps Anna disabled because a Turnstile-class captcha made the visible source noisy without a reliable download path. Citations: `src/ui/pages/TankoLibraryPage.cpp:168-175`.

## Reference behavior

### 1. Reachability and gate classification

- Raw CLI probe to `https://oceanofpdf.com/?s=sapiens` returns HTTP 403 from this environment. Repro: `Invoke-WebRequest` returned `403` on 2026-04-22.
- Real Chromium-class probing does not reach content cleanly either. Headless Edge against `https://oceanofpdf.com/?s=sapiens` returned a Cloudflare challenge page with `Just a moment...`, `Performing security verification`, hidden `cf-turnstile-response`, a Turnstile API script under `https://challenges.cloudflare.com/turnstile/...`, and `_cf_chl_opt` carrying `cType: 'managed'`.
- Operational classification for the real browser path is `C` - active Turnstile-managed challenge. There is also a CLI-only `403`, so the precise shape is "compound: CLI 403 + browser Turnstile challenge", but for Tankoban's allowed integration surface the correct go/no-go outcome is still `C`, not `A` or `B`.

### 2. Domain and mirror census

Observed on 2026-04-22:

| Domain | Status | Notes |
| --- | --- | --- |
| `oceanofpdf.com` | Live but gated | CLI returns 403; Chromium receives active Cloudflare Turnstile challenge. |
| `theoceanofpdf.com` | Reachable | WordPress-style site with working search, detail pages, and direct file links under `/wp-content/uploads/...`. |
| `allepub.com` | Reachable | WordPress-style site with search, detail pages, and external file-host redirects (`zerolibrary.xyz`). |
| `oceanofpdf.site` | Reachable wrapper | Informational page claiming the old domain moved and pointing toward `oceanofpdf.com.co` / `allepub.com`; not itself a searchable source. |
| `oceanofpdf.co` | Do not use | Prior Agent 4B probe classified it as an adware cloaker. I did not get a stable clean fetch this pass. |
| `oceanofpdf.net` | Dead/stub | Returns only a generic error page, not a usable library surface. |
| `oceanofpdf.xyz` | Dead/stub | Tiny JS redirect page sending the browser to `/lander`. |

Historical/legal rotation surfaced trivially from press and industry sources:

- The Guardian reported the original OceanofPDF shutdown on August 8, 2018 after publisher takedowns. Source: [The Guardian, 2018-08-08](https://www.theguardian.com/books/2018/aug/08/elitist-angry-book-pirates-ocean-of-pdf-author-campaign-website).
- The Authors Guild wrote on November 22, 2024 that the site had been taken offline on prior occasions but kept returning through new hosts and service providers. Source: [Authors Guild, 2024-11-22](https://authorsguild.org/news/oceanofpdf-piracy-site-actions-authors-can-take/).
- The Brussels Times reported on July 18, 2025 that Belgian courts ordered blocking against OceanofPDF together with LibGen and Z-Library. Source: [The Brussels Times, 2025-07-18](https://www.brusselstimes.com/belgium-news/1669291/belgian-courts-block-websites-providing-free-pirated-books).

### 3. HTML shape - 3 page types

Saved snapshots:

- `agents/audits/snapshots/oceanofpdf_search_2026-04-22.html`
- `agents/audits/snapshots/oceanofpdf_detail_2026-04-22.html`
- `agents/audits/snapshots/oceanofpdf_download_2026-04-22.html`

Important note: the primary `oceanofpdf.com` front never yielded searchable site HTML because of the challenge wall. The saved search/detail snapshots are therefore from the reachable `theoceanofpdf.com` front, and the saved download-page snapshot is from `allepub.com` because `theoceanofpdf.com` does not expose an intermediate HTML download page at all; its detail page links directly to the file.

#### Search page (`https://theoceanofpdf.com/search/fourth+wing/`)

- Title/detail selector: `h2.wp-block-post-title a[href]`
- Cover/detail selector: `figure.wp-block-post-featured-image a[href] img[src]`
- Uploader selector: `div.wp-block-post-author-name a`
- Excerpt selector: `div.wp-block-post-excerpt p`
- No explicit format or size field was present on the result card.

Concrete snapshot evidence:

```html
<figure class="wp-block-post-featured-image">
  <a href="https://theoceanofpdf.com/fourth-wing-the-empyrean-1/">
    <img src="https://theoceanofpdf.com/wp-content/uploads/2024/03/PDF-EPUB-Fourth-Wing-The-Empyrea.png">
  </a>
</figure>
<h2 class="wp-block-post-title">
  <a href="https://theoceanofpdf.com/fourth-wing-the-empyrean-1/">[PDF] [EPUB] Fourth Wing (The Empyrean, #1) Download</a>
</h2>
<div class="wp-block-post-author-name"><a ...>Mattie</a></div>
<div class="wp-block-post-excerpt"><p>Download Rebecca Yarros ... PDF EPUB ...</p></div>
```

Citations: `agents/audits/snapshots/oceanofpdf_search_2026-04-22.html:1284-1316`.

#### Detail page (`https://theoceanofpdf.com/fourth-wing-the-empyrean-1/`)

- Human-facing title selector: `h3.elementor-heading-title`
- Cover selector: main detail image under the first Elementor image block; the concrete image URL is present in `img[src]`
- Description/body: page paragraphs under the main content container
- Direct download selector: `figure.wp-block-image a[href$=".pdf"]`
- Format is signaled in title/body text (`[PDF] [EPUB]`), but I did not find a clean structured "size" field on the page.

Concrete snapshot evidence:

```html
<h3 class="elementor-heading-title elementor-size-default">
  [PDF] [EPUB] Fourth Wing (The Empyrean, #1) Download
</h3>
<img src="https://theoceanofpdf.com/wp-content/uploads/2024/03/PDF-EPUB-Fourth-Wing-The-Empyrea.png">
<a href="https://theoceanofpdf.com/wp-content/uploads/2024/03/OceanofPDF.com_Fourth_Wing_Special_Edition_-_Rebecca_Yarros.pdf" target="_blank">
  <img src="https://theoceanofpdf.com/wp-content/uploads/2024/03/pdf-button-1.jpg">
</a>
```

Citations: `agents/audits/snapshots/oceanofpdf_detail_2026-04-22.html:1163-1233`.

Live HEAD verification on the direct file URL returned `200 OK`, `Content-Type: application/pdf`, `Content-Length: 2047175`.

#### Download page (`https://allepub.com/fourth-wing-by-rebecca-yarros-epub-pdf/`)

- Title selector: `h1.entry-title`
- Cover selector: `.post-thumbnail img[data-lazy-src]` or its noscript `img[src]`
- Status selector: list item containing `Status:`
- Format selector: list item containing `Format:`
- Download-link selectors: HTML forms whose `action` points at `https://zerolibrary.xyz/...`; secondary button opens `https://301alldomain.xyz`

Concrete snapshot evidence:

```html
<h1 class="entry-title">Fourth Wing by Rebecca Yarros EPUB & PDF</h1>
<li><strong>Status: Available For Free Download</strong></li>
<li>Format: <strong>PDF / EPUB</strong></li>
<form method="post" target="_blank" action="https://zerolibrary.xyz/ea92cb1f89c8bfd9" />
<form method="post" target="_blank" action="https://zerolibrary.xyz/1e4dada6c2c74079" />
<a href="javascript:void(0)" onclick="window.open('https://301alldomain.xyz', '_blank')">
```

Citations: `agents/audits/snapshots/oceanofpdf_download_2026-04-22.html:247-335`.

### 4. Download flow decomposition

The historical "search -> detail -> intermediate OceanOfPDF download page -> external mirrors" pattern is not what the cleanest reachable `theoceanofpdf.com` front does today.

Observed current reachable flows:

1. `theoceanofpdf.com`
   - Search: `https://theoceanofpdf.com/search/<query>/`
   - Detail: post permalink such as `https://theoceanofpdf.com/fourth-wing-the-empyrean-1/`
   - Download: direct file link embedded on the detail page, e.g. `/wp-content/uploads/2024/03/OceanofPDF.com_Fourth_Wing_Special_Edition_-_Rebecca_Yarros.pdf`
   - No intermediate HTML download page. No extra mirror-button page on the sampled title.

2. `allepub.com`
   - Search: ordinary WordPress `?s=<query>` flow
   - Detail: post permalink such as `https://allepub.com/fourth-wing-by-rebecca-yarros-epub-pdf/`
   - Download page: the detail page itself contains the download buttons
   - Final hops:
     - `https://zerolibrary.xyz/ea92cb1f89c8bfd9`
     - `302` to `https://ww1.zerolibrary.xyz/.../Allepub.com___Fourth-Wing.epub?download_token=...`
     - final `200 OK` binary response with `Content-Type: application/epub+zip` and `Content-Disposition: attachment; filename="Allepub.com _ Fourth-Wing.epub"`

So the current reachable ecosystem is already split:

- one front (`theoceanofpdf.com`) is detail-page-direct-download
- one front (`allepub.com`) is detail-page-to-external-file-host redirect
- the original `oceanofpdf.com` brand front is challenge-gated and was not observed serving parseable site content

### 5. Pattern-fit answer to the brief

Observed pattern fit by target:

- Exact `oceanofpdf.com`: `Hard park`. The browser path hits active Turnstile, and the current harvester only models passive `cf_clearance`, not active captcha interaction.
- `theoceanofpdf.com`: closest to `QNetworkAccessManager` + HTML parse. The sample pages are server-rendered WordPress HTML and the download is a direct file URL. I did not observe a need for a JS-rendering pass.
- `allepub.com`: also closest to raw HTTP parsing, but with a more fragile external-host redirect chain. Again, I did not observe a need for a QWebEngine DOM pass on the sampled title.

### 6. Coverage delta probe

Probe set used:

- `fourth wing`
- `a court of thorns and roses`
- `leviathan wakes`

Observed:

- LibGen returned substantial result counts on all three probes during this run:
  - `fourth wing`: about 55
  - `a court of thorns and roses`: about 75
  - `leviathan wakes`: about 66
- `theoceanofpdf.com` clearly had `fourth wing`, but I did not get corresponding positive hits for the other two probes through the sampled surfaces.
- `allepub.com` search was noisy. It surfaced the exact `Fourth Wing` page, but the other two probes did not produce convincing exact-title wins during this pass.

Net observation: I found evidence that the reachable OceanOfPDF-adjacent fronts carry some recent-commercial-fiction material, but I did not find evidence in this pass that they materially outperform LibGen on the suggested sample set. A meaningful unique-coverage percentage over LibGen is therefore not demonstrated by this audit.

### 7. DMCA / seizure risk read

- The legal-risk profile is high, not hypothetical. OceanofPDF was publicly takedown-targeted in 2018, the Authors Guild was still publishing current anti-piracy action guidance against `OceanofPDF.com` in November 2024, and Belgian courts reportedly ordered blocking in July 2025.
- That history does not prove imminent disappearance on a specific day, but it does show sustained enforcement pressure, active complaints against registrars/DNS/hosting, and a long-running domain-rotation pattern rather than a stable-clearnet-source profile.

## Gaps (ranked P0 / P1 / P2)

**P0 (user-blocking or severely degrading):**

- Exact-domain integration for `oceanofpdf.com` is blocked by an active Cloudflare Turnstile-managed challenge. Observed: Chromium-class probing hit a verification page with hidden `cf-turnstile-response` and `_cf_chl_opt` `cType: 'managed'`, while CLI probing returned HTTP 403. Our codebase only has a passive `cf_clearance` harvester and already disables Anna when captcha blocks the path. Reference: `src/core/indexers/CloudflareCookieHarvester.h:11-15`, `src/core/indexers/CloudflareCookieHarvester.cpp:138-156`, `src/ui/pages/TankoLibraryPage.cpp:168-175`. Impact: under Hemanth's no-captcha rule, M4 cannot ship `oceanofpdf.com` as an exact-source integration.

**P1 (user-visible shortfall):**

- The reachable fronts are not a clean drop-in for the exact-domain target because they are structurally different from one another. Observed: `theoceanofpdf.com` is direct-download-on-detail-page, while `allepub.com` pushes users to `zerolibrary.xyz` token redirects. Impact: "OceanOfPDF support" is not one stable parser contract right now; it is at least two distinct source contracts if the domain scope is broadened.
- Coverage extension beyond LibGen is unproven by the sampled queries. Observed: LibGen returned large hit counts for all three probes, while the reachable OceanOfPDF-adjacent fronts only clearly won on `fourth wing`. Impact: the stated rationale for adding this source family - meaningful recent-fiction coverage beyond AA+LibGen - was not evidenced strongly enough in this pass.

**P2 (polish):**

- The reachable fronts do not surface a strong structured metadata contract. Observed: result cards and detail pages exposed title, cover, descriptive text, and in some cases format words, but not a clean size field or a reliable book-author field on the search card. Impact: even if a reachable alias front were adopted later, its user-visible metadata quality would likely be thinner than LibGen unless augmented by heuristics.

## Hypothesized root causes (Agent 4B to validate)

- **Hypothesis -** If M4 is defined narrowly as "wire the exact `oceanofpdf.com` domain into TankoLibrary", the work should remain parked because the active managed Turnstile wall is functionally the same blocker class that already removed Anna from default dispatch. **Agent 4B to validate.**
- **Hypothesis -** If Hemanth is willing to treat reachable successor/lookalike fronts as acceptable substitutes, `theoceanofpdf.com` is the cleaner technical target than `allepub.com` because its sampled flow is just search -> detail -> direct file, without an external-host redirect chain. **Agent 4B to validate.**
- **Hypothesis -** The value proposition of this source family is weaker than originally hoped unless broader query sampling proves a real recent-fiction delta over LibGen. The current audit found reachability on adjacent fronts, but not strong differential corpus evidence. **Agent 4B to validate.**

## Recommended follow-ups (advisory)

- Consider whether M4's scope is the exact `oceanofpdf.com` domain or the broader OceanOfPDF-adjacent content family. This audit only supports progress on the latter, not the former.
- Investigate whether the roadmap should pivot immediately if the product requirement is exact-domain support, because the exact-domain verdict from this run is a hard no-go under the current captcha policy.
- Consider whether `theoceanofpdf.com` should be treated as a separate new-source candidate, with its own acceptance decision, rather than being described as a transparent fallback for `oceanofpdf.com`.
- Investigate whether a wider query sample changes the corpus story. If it does not, the integration case remains mostly "another scrappable site exists" rather than "this meaningfully fills a LibGen/AA coverage gap."
