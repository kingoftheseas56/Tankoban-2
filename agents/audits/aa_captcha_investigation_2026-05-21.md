# Anna's Archive captcha investigation + approach decision

**Date:** 2026-05-21
**Author:** Agent 2 (Book Reader + TankoLibrary)
**Arc:** BOOKS_STREMIO_PIVOT Phase 4 Task 4.1
**Plan ref:** [docs/superpowers/plans/2026-05-20-books-stremio-pivot.md](../../docs/superpowers/plans/2026-05-20-books-stremio-pivot.md) §4.1
**Spec ref:** [docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md](../../docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md) §3.6

## TL;DR

**Picked: Path C — defer AA to v1.1.** Path A is closed (AA has no public API per current sources). Path B doesn't map onto AA's actual blocker (Cloudflare Turnstile widget on `/ads.php` + `/slow_download/` URLs, not `cf_clearance`-style JS interstitial). v1 ships LibGen + Tankorent only. AA re-enable revisits at v1.1 with one of: paid anti-captcha service integration (2Captcha/CapMonster), visible-webview modal flow (Hemanth-approved user-interaction path), or a watch-and-wait if AA softens their Turnstile config.

## §1 — API token path (Path A probe)

**Asked:** does AA expose a documented programmatic API key / token that bypasses the captcha-gated `/slow_download/` interstitial?

**Findings:**

- Direct probe of `annas-archive.org/donate` + `annas-archive.org/faq` blocked at WebFetch layer (Anthropic content policy; not DNS). Cannot read AA's own membership-tier page from this tab.
- Wikipedia article on Anna's Archive (en.wikipedia.org/wiki/Anna's_Archive, fetched 2026-05-21) is the authoritative third-party summary. Key quotes:
    > "Non-members may download files through shared servers at reduced speeds, subject to browser-based verification intended to prevent automated abuse."
    > "Paid members receive high-speed downloads, with the number of fast downloads permitted per 24-hour period varying by membership tier; multiple memberships can be combined additively."
    > "[High-speed SFTP access] for organizations training large language models in exchange for large contributions of money or data."
- The Wikipedia summary states AA's published architecture includes Flask + MariaDB + Elasticsearch — but explicitly **does not describe an API layer**. The membership system is built around tiered download SPEED, not around bypassing CAPTCHA / browser-verification.
- Codebase grep for `aa_token`, `AA_TOKEN`, `/api/`, `fast_download`, `annas.token` — zero hits in `src/`. No prior Tankoban work identified an AA API path.
- 4B's Track-B note in `src/ui/pages/TankoLibraryPage.cpp:241-248` (committed 2026-04-22) confirms the same state: AA search works captcha-free, but `/books/` detail + `/ads.php` download paths are Turnstile-blocked. No API alternative discovered at the time.

**Conclusion §1:** Path A is closed for v1. AA does not offer a documented public API that would bypass the captcha. The SFTP-access path is gated on a large monetary/data contribution (LLM training partners), not a practical channel for Tankoban.

## §2 — Cookie harvest path (Path B probe)

**Asked:** can `CloudflareCookieHarvester` ([src/core/indexers/CloudflareCookieHarvester.{h,cpp}](../../src/core/indexers/CloudflareCookieHarvester.h)) be extended to handle AA's challenge stage(s)?

**Findings:**

- `CloudflareCookieHarvester` is built around the `cf_clearance` cookie name specifically. CloudflareCookieHarvester.cpp:138 — `if (cookie.name() != QByteArrayLiteral("cf_clearance")) return;`. The harvester succeeds when Cloudflare's JS challenge issues that cookie; the cookie is then reused for subsequent raw-HTTP requests against the same origin.
- AA's actual blocker, per 4B's M2.2 finding (`TankoLibraryPage.cpp:243`), is described as **"Turnstile-class captcha"** — Cloudflare Turnstile, a CAPTCHA challenge widget that gates the `/ads.php` + `/slow_download/` endpoints.
- Turnstile is architecturally distinct from `cf_clearance`:
    - `cf_clearance` = JS-only challenge cookie issued when Cloudflare's interstitial JS executes successfully. The existing `AnnaArchiveScraper` flow + `AaSlowDownloadWaitHandler` already passes this gate via QWebEngineView execution. The wait-handler's polling predicate looks for "just a moment / checking your browser / verify you are human / browser verification" text — that's the cf_clearance interstitial, and it already clears.
    - Turnstile widget = CAPTCHA challenge requiring either (a) actual user click on the widget, (b) browser execution with sufficiently realistic timing/behavior to auto-solve, or (c) integration with a paid anti-captcha service (2Captcha, CapMonster, anti-captcha.com) that returns a token via API.
- The existing `AnnaArchiveScraper` already executes in QWebEngineView — i.e., the most realistic browser-execution path Tankoban has available. AA's Turnstile config still does NOT auto-solve in this environment (per M2.2 finding). The cookie harvester pattern, which depends on a passive `cookieAdded` signal for a specific cookie name, cannot bridge the gap.
- Extending the harvester to a different cookie name (e.g., a hypothetical Turnstile-success cookie) would not help — the harvester pattern requires the challenge to auto-clear in a hidden off-the-record profile. Turnstile's design point IS to not auto-clear under those conditions.

**Conclusion §2:** Path B does not map onto the real blocker. The CloudflareCookieHarvester pattern addresses `cf_clearance` JS-challenge cookies; AA's blocker is a Turnstile CAPTCHA widget that requires either user interaction or paid third-party solver integration. Pattern reuse is not feasible.

## §3 — Decision

**Picked: Path C — defer AA to v1.1.**

**Rationale:**

1. Path A is closed: AA has no public API (Wikipedia summary + codebase grep + Tankoban historical record all converge).
2. Path B does not solve the actual problem: cookie harvester pattern targets `cf_clearance`, AA's blocker is Turnstile.
3. The spec's hinted alternative (Playwright MCP) was **removed from Tankoban 2026-05-20** during VS Code lag triage (per CLAUDE.md, "windows-mcp deprecated 2026-05-15, Playwright deprecated 2026-05-20"). So even the spec's preferred path is gone.
4. v1 ships honestly with 2 of 3 sources (LibGen + Tankorent). LibGen alone has been the working primary source since 4B's TankoLibrary M1–M2.4 work; it remains the cheapest-hit path. Tankorent fills the long-tail rare-book gap.
5. Hemanth's standing call from M2.2 era ("rules out captcha-solving") has not been revisited in the BOOKS_STREMIO_PIVOT brainstorm. Picking Path C honors that call instead of silently flipping it.
6. v1.1 follow-on can revisit AA re-enable with one of:
    - **(a) Paid anti-captcha service** — integrate 2Captcha or CapMonster, ~$1.50 per 1000 solves. Requires Hemanth's strategic OK (recurring cost + third-party dependency).
    - **(b) Visible-webview modal flow** — pop a modal QWebEngineView when the user clicks an AA row, let the user solve the Turnstile manually, harvest the resulting cookies, proceed with download. Aligned with Hemanth's M2.2-era allowance "when Hemanth OKs a visible-webview modal flow OR when AA drops their captcha."
    - **(c) Watch-and-wait** — periodically re-probe whether AA's Turnstile config has softened (e.g., reduced challenge frequency on `/ads.php`).

**Affected downstream tasks:**

- **Task 4.3** (Re-enable AA per Path picked) — **DROPPED** for v1. Replaced by §8 deferred-table entry. No code edit to `AnnaArchiveScraper.{h,cpp}`, `TankoLibraryPage.cpp:254` stays commented-out.
- **Task 4.6** (BookSearchAggregator) — now fans out to **2 sources** (LibGen + Tankorent), not 3. Aggregator's parallel-section UI in Phase 8 picker only renders 2 sections in v1.
- **Spec §8 deferred table** — needs entry: "AA re-enable deferred to v1.1. Blocker: Turnstile-class CAPTCHA on `/ads.php` + `/slow_download/`. Re-investigate with paid anti-captcha service OR visible-webview modal flow OR watch-and-wait per audit `agents/audits/aa_captcha_investigation_2026-05-21.md`."
- **Picker copy** (Phase 8 mockup): no "Anna's Archive" section header in v1. If user asks why AA is missing, Sources Panel could surface a "Anna's Archive — re-enable planned for v1.1 (captcha blocker)" pill, but this is v1.x polish not v1 blocker.

**Reversibility:** Reversible at any v1.x point. The `AnnaArchiveScraper` class stays compiled + registered in `CMakeLists.txt` — re-enable is a one-line uncomment in `TankoLibraryPage.cpp:254` (plus whatever shape v1.1 picks for Turnstile-solving). No architectural debt incurred by this deferral.

## References

- [src/ui/pages/TankoLibraryPage.cpp:241-254](../../src/ui/pages/TankoLibraryPage.cpp) — 4B's Track-B disable rationale.
- [src/core/book/AnnaArchiveScraper.{h,cpp}](../../src/core/book/AnnaArchiveScraper.h) — fully implemented scraper, gated only at page-construction time.
- [src/core/indexers/CloudflareCookieHarvester.{h,cpp}](../../src/core/indexers/CloudflareCookieHarvester.h) — `cf_clearance`-specific harvester pattern.
- [agents/audits/tankolibrary_2026-04-21.md](tankolibrary_2026-04-21.md) — original TankoLibrary audit (Agent 7).
- [CLAUDE.md](../../CLAUDE.md) — Playwright MCP removal note (2026-05-20).
- Wikipedia article on Anna's Archive — third-party summary of membership tiers and architecture.
