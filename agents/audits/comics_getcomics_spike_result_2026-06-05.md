# GetComics Acquisition Spike — Result (Saga)
**Date:** 2026-06-05 · **Agent:** 1 · **Plan:** `docs/superpowers/plans/2026-06-05-comics-western-phase1-datapath-spike.md` (Task-9 verdict, run ahead of scaffold per the de-risk goal).

> ## ⚠️ VERDICT CORRECTED 2026-06-05 — CLEAN HTTP WORKS (multi-agent re-verify, workflow `w5t37cyco`)
> The "clean HTTP NOT feasible" conclusion below was a **solo-spike bug.** I tested the **bare** `/dls/<token>/` URL — which *is* the ad-gate. But the real "DOWNLOAD NOW" button href carries an **encrypted payload + signature suffix**: `/dls/<token>/<payload>:<sig>==`. That **full href** returns a clean **302 → `https://fs1.comicfiles.ru/<date>/<file>.cbr`** — a real 223 MB RAR/CBR, **no ad-gate, no Cloudflare, no JS countdown, fully resumable (Range → 206).** 3 of 4 live tests pulled real archive bytes over plain HTTP; the 4th (Pixeldrain) failed only because `pixeldrain.com:443` is ISP-unreachable from this machine — a network block, not a gate.
>
> **Corrected working method (clean HTTP, no browser):**
> 1. `requests.Session` + Chrome UA → GET the post page (200, no CF, seeds cookies).
> 2. Parse the post; grab the **FULL** "DOWNLOAD NOW" / MAIN SERVER href **including the `<payload>:<sig>==` suffix** (NOT the bare `/dls/<token>/`).
> 3. GET it (Referer `getcomics.org`) → **302 → `fs1.comicfiles.ru/<date>/<file>.cbr`** (native GetComics CDN).
> 4. GET that → 200, `application/octet-stream`, ~223 MB, `accept-ranges: bytes` → stream with resume. First bytes `Rar!` = valid CBR.
>
> **Corrected architecture:** **PRIMARY = clean HTTP** (full DOWNLOAD-NOW href → comicfiles.ru; `requests.Session`, one redirect, streamed/resumable; no QWebEngine, no Cloudflare handling). **FALLBACK = the invisible QWebEngine** (already built — `CloudflareCookieHarvester` + `AaSlowDownloadWaitHandler`), only for posts that expose *no* native button (Pixeldrain/Mega/Terabox-only).
>
> **Caveat:** post-shape-dependent — works when the post has a native MAIN-SERVER button (most do). Mirror-only posts fall back to QWebEngine. Looted-tool survey agrees: tools using the native `/dls/` + comicfiles path work; browser use elsewhere is general CF-hardening, not because the comicfiles fetch needs JS.
>
> The body below is the original (now-superseded) solo finding, kept for the record.

## What was tested (live, on Saga)
Control: `https://getcomics.org/other-comics/saga-vol-12-tpb-2025/` (real Saga TPB).

| Step | Result |
|---|---|
| GetComics search (`/?s=Saga+Vol`) | ✅ plain HTTP, **no Cloudflare**, finds real Saga TPBs |
| Post page + download buttons | ✅ plain HTTP; DOWNLOAD NOW + Pixeldrain/Mega/Terabox, all → `getcomics.org/dls/<token>/` |
| `/dls/` safelink via plain GET | ⚠️ returns an **HTML ad-gate page**, not the file; no Set-Cookie |
| File link in `/dls/` HTML / inline JS / buttons / forms | ❌ **not present anywhere** — not static, not extractable |
| `admin-ajax` download action | ❌ none (theme.min.js only has `process_simple_like` + pagination) |
| Edge headless `--dump-dom` (+ virtual-time 25s) | ❌ did not reveal the link |
| Edge headless **net-log** capture (321 URLs, 33 MB) | ❌ **no file-host/.cbz request appeared**; instead saw a **Cloudflare JS challenge** (`/cdn-cgi/challenge-platform/...`) + a swarm of aggressive ad networks (pubadx.one, adskeeper, onclckinp/onclcktg) + a `&dv=desktop` self-reload |

## VERDICT: clean HTTP is NOT feasible. Acquisition requires a full real browser.
The real download URL is **never in the page**. It is produced only after a **Cloudflare bot-challenge + ad-network load + countdown** all execute at runtime. There is no clean request to replicate, and a thin headless snapshot does not reach the file. This is exactly the ad-redirect/file-host risk Codex flagged (spec §9) — and it is the dominant one.

## DECISION (Hemanth, 2026-06-05): build a browser mode INSIDE Tankoban.
A real embedded **Qt WebEngine (Chromium)** view is the right answer, not a consolation:
- It natively passes Cloudflare, runs the ads + countdown like a person, and we intercept the file via `QWebEngineProfile::downloadRequested` the instant it triggers.
- **Future-proof:** it's a real browser — whatever a human can download by hand, the app can. Gate changes (CF, ad-gate, scramble) don't break it.
- Can run **invisible/background** for the happy path; surface **visibly** only if a gate ever needs a human click (captcha).
- **General capability:** the same embedded browser serves the RCO read-online fallback and any future source. Weakness → feature.
- Likely **already available**: the book reader (foliate-js) runs in a web view, so Qt WebEngine may already be linked — to confirm in Phase 2.

## Impact on the plan
- **Metadata path stays clean HTTP** — GCD API + Open Library covers work perfectly (verified). No browser needed there.
- **Only file ACQUISITION changes:** Phase 2's `SourceAdapter.download` becomes an embedded-browser navigate + download-intercept, not an HTTP fetch. Validation/matching/identity (Tasks 1,4,7,8) are unaffected.
- The Python spike package (plan Tasks 1-9) is **superseded for the download step**; GCD/OL/validation/matching logic still ports.

## Next
Re-plan Phase 2 around: (1) confirm Qt WebEngine availability; (2) embedded browser download-intercept on a Saga post → land + validate the `.cbz`; (3) wire to `MangaDownloadIndex` + existing UI/reader.
