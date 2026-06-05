# Comics-Western Prototype — Phase 2: GetComics Source (clean-HTTP, proven) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or superpowers:executing-plans. Steps tracked with `- [ ]`.

**Goal:** Build the in-app GetComics download path — proven live as clean HTTP — and wire it to the existing manga download/UI plumbing, with the invisible-QWebEngine fallback only for mirror-only posts.

**Architecture:** Clean-HTTP primary (no browser); invisible QWebEngine fallback (already shipped via `CloudflareCookieHarvester` / `AaSlowDownloadWaitHandler`) only when a post exposes no native main-server button. Metadata (GCD + Open Library) is separate clean HTTP. UI reuses the existing manga `ComicsSeriesView` / `VolumeTile` / `ComicsSourcesPanel` / `MangaDownloadIndex` / comic reader.

**Tech:** C++/Qt, `QNetworkAccessManager`. Supersedes the Phase-1 Python spike plan's download tasks (that plan's GCD/OL/validation/matching logic still informs).

---

## The PROVEN download method (verified live 2026-06-05, fresh token, real 212 MB CBR, file opens to 155 pages)

1. `QNetworkAccessManager` + Chrome UA → GET the GetComics post page (200, no Cloudflare; seeds cookies).
2. Parse the post; take the **FULL** "DOWNLOAD NOW" / MAIN SERVER href — it carries a signed suffix `/dls/<token>/<payload>:<sig>==`. **Do NOT reduce it to the bare `/dls/<token>/`** (the bare form is the ad-gate; the full signed href is not).
3. GET that full href (Referer `getcomics.org`) → **one 302 → `https://fs1.comicfiles.ru/<date>/<file>.cbr`** (native CDN; no ad-gate, no JS, no challenge).
4. GET the file URL → 200, `application/octet-stream`, `accept-ranges: bytes` (resumable) → stream to disk. First bytes `Rar!` (CBR) / `PK` (CBZ).

**Fallback (mirror-only posts):** no native main-server signed href → invoke the invisible QWebEngine resolver (adapter around `CloudflareCookieHarvester`). If it can't yield a direct archive URL → surface "manual/mirror unsupported", not a network error.

---

## Build blueprint
<!-- Codex (gpt-5.5) orthogonal-model build blueprint via scripts/engines (full-quota), 2026-06-05. Folded in by Agent 1. -->

### `GetComicsSource` (C++/Qt) — public interface
```cpp
class GetComicsSource final : public QObject {
  Q_OBJECT
public:
  struct SearchHit   { QString title; QUrl postUrl; QDate postDate; QStringList detected; double matchConfidence=0; };
  struct ResolvedFile{ QUrl postUrl, fileUrl; QString fileName; qint64 sizeBytes=-1; bool resumable=false; QString contentType; bool usedWebFallback=false; };
  struct ValidationResult { bool ok=false; int pageCount=0; QString archiveType; QString error; };
  explicit GetComicsSource(QNetworkAccessManager*, QObject* =nullptr);
  QFuture<QList<SearchHit>> search(const QString& series, const QString& volTitle, int volNumber);
  QFuture<ResolvedFile>     resolve(const QUrl& postUrl);              // post -> comicfiles.ru file URL (signed href, one 302)
  QFuture<void>             download(const QUrl& fileUrl, const QString& dest, bool resume);
  QFuture<ValidationResult> validate(const QString& path);
};
```
Stateless-ish source object (follow `AnnaArchiveScraper` shape): async Qt network, small DTOs, no UI ownership, no direct model mutation. HTML parsing stays inside; match semantic button/link patterns, not brittle DOM paths. QWebEngine fallback hooks in **only** when `!hasNativeMainServerSignedHref(html) && hasMirrorOnlyLinks(html)`.

### Integration
- **`MangaDownloadIndex`** = source of truth: states queued→resolving→downloading→paused→validating→complete→failed; store `postUrl, resolvedFileUrl, etag/lastModified, totalBytes, downloadedBytes, supportsResume, archiveType, pageCount, validationState`. Progress from `QNetworkReply::downloadProgress`; resume via `Range: bytes=<partial>-`. Completion runs `validate()` BEFORE marking complete.
- **`ComicsSourcesPanel`** = one row per candidate: source name · match label (exact/likely/manual) · post title+date · file status · action (download/resume/retry/choose-other). Row action enqueues into the index (same command path as manga).
- **`VolumeTile`** stays dumb: asks the comic download coordinator "download this volume". One high-confidence source → enqueue; multiple/weak → open `ComicsSourcesPanel`.

### Edge/error handling
- Mirror-only post → fallback; if fallback yields nothing → "manual/mirror unsupported" (not a network error).
- Signed-link expiry mid-download (403/404/short-HTML on the final URL) → re-resolve from the original post URL, resume against the new final URL if ranges supported + partial valid.
- Resume `.cbr`: check partial size, ranged GET, require compatible total, append only on `206`; on `200` restart unless index says complete.
- Corrupt/short archive: completion ≠ success — validate magic bytes + page enumeration; reject if far-below-expected size, open fails, or zero pages; quarantine to `.failed`, don't mark complete.
- `.cbr` vs `.cbz`: detect by **magic, not extension**; `.cbz`=ZIP (`QZipReader`/existing), `.cbr`=RAR (reuse the comic reader's RAR backend). **Validation shares the reader's page-discovery code** so "download valid" == "reader opens".
- GCD↔GetComics matching: auto-pick only on strong series+volume(+ISBN) match; ask user on omnibus/deluxe/TPB ambiguity or missing volume number; never auto-pick on loose title only; persist the user's source choice per series.

### Build sequencing (each step independently testable; start from the proven download)
- [ ] **1.** `resolve()` against the proven post URL → final `comicfiles.ru` URL, signed link preserved.
- [ ] **2.** Direct HTTP download w/ progress + resume, OUTSIDE the UI → interrupt a partial `.cbr`, resume via `Range`, validate byte count.
- [ ] **3.** Archive validation reusing the reader's archive code → valid CBR = 155 pages; truncated file rejected.
- [ ] **4.** Wire into `MangaDownloadIndex` w/ comic metadata → state transitions resolving→downloading→validating→complete.
- [ ] **5.** `ComicsSourcesPanel` rows from mocked GetComics hits → row action enqueues + reflects progress.
- [ ] **6.** `VolumeTile` action → high-confidence auto-downloads; ambiguous opens the panel.
- [ ] **7.** GCD→GetComics search/matching → test exact / ambiguous / no-match.
- [ ] **8.** QWebEngine fallback adapter for mirror-only posts → only after the clean path is stable; isolated behind `resolve()`.

### Top risks + mitigations
1. GetComics HTML changes → small parser, log resolve failures, semantic matching not DOM paths.
2. Signed URLs expire / header-bound → keep the post URL, re-resolve on expiry, consistent headers resolve↔download.
3. RAR validation diverges from reader → reuse the reader's archive/page enumeration for validation.
4. Bad auto-matches → confidence thresholds + visible source choice + persisted overrides.
5. Fallback scope-creep → fallback only for mirror-only/no-native posts, behind the resolver interface; never route the proven main-server path through a browser.

---

## Reference
- Proven download evidence + corrected verdict: `agents/audits/comics_getcomics_spike_result_2026-06-05.md`
- Clean-HTTP re-verify workflow: `w5t37cyco`
- Foundation spec: `docs/superpowers/specs/2026-06-04-comics-western-prototype-design.md`
