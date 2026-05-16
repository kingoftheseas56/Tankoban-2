# Tankoyomi-Premium catalog: per-series curation handoff

This doc walks a curator (Hemanth or any future contributor) through adding one series to the bundled Premium catalog at `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json`. It also lists the 29 series still pending after the v1 shipping cut (which carries only Death Note as a worked example).

The infrastructure side (loader, helper tool, runtime providers, detail view, search-widget integration) is complete end-to-end across Phases 1-11. What remains is wall-clock curation work that requires human judgment per series.

## Per-series time budget

Per Codex section 25 of the brainstorm doc (`docs/superpowers/specs/2026-05-15-tankoyomi-premium-brainstorm.md`):

- Completed short series (8-15 vols): 1-2 hours.
- Completed medium series (16-30 vols): 2-3 hours.
- Long ongoing series (One Piece, Kingdom, Bleach+TYBW, etc): 2-4 hours.
- Realistic total for the remaining 29: 30-50 hours over multiple sessions.

Pacing recommendation: 2-3 series per session, smallest-first to build momentum.

## Trusted uploader allowlist

Pick the highest-seeder release from one of these handles on nyaa.si. They have a track record of clean, properly-paginated digital releases:

- **1r0n** -- VIZ digital rips, generally lossless cbz.
- **VIZ Digital** -- branded uploads, mixed quality but usually properly tagged.
- **KG Manga** -- consistent cbz packaging, used for the v1 Death Note worked-example.
- **Hox** -- preferred for niche / older series.
- **Danke** -- volume-pack consolidations.
- **DarkZone-Empire** -- cbr (not cbz) and so unsuitable for v1 since the validator rejects non-cbz.

Always cross-check the uploader's profile on nyaa.si before downloading; flagged accounts will not have the trusted-uploader red name.

## Per-series workflow

### Step 1: Acquire the .torrent

1. Visit nyaa.si.
2. Search by series title plus the uploader handle (e.g. `Berserk 1r0n` or `Vinland Saga KG Manga`).
3. Sort by seeders descending.
4. Pick the top result that:
   - Is a single torrent containing all volumes (one cbz per volume).
   - Has 5+ seeders.
   - Uses `.cbz` extension (not `.cbr`).
5. Click the download link (the floppy-disk icon next to the magnet icon). Save to `tools/premium_catalog_helper/_curation/<series_id>.torrent`. (Underscore-prefix the dir so loader skips it if a stray catalog file leaks in.)

### Step 2: Build the chapter-to-volume mapping CSV

For each volume, transcribe the chapter list. Sources, in order of trust:

1. The back-of-volume table of contents (gold standard but you need the physical or digital book).
2. AniList volumes endpoint (e.g. https://anilist.co/manga/30005/Death-Note -- look for the per-volume chapter breakdown if available).
3. mangaupdates.com series page.
4. mangareader.to volume index pages.

Cross-check at least two sources. Pick verifiable, English chapter titles. If a volume's chapter titles cannot be verified, use `Chapter N` placeholders and add a top-of-file comment noting which volumes are placeholders (see `_worked_example_death_note_mapping.csv` for the Death Note pattern, where vols 4-12 used placeholders).

Save as `tools/premium_catalog_helper/_curation/<series_id>_mapping.csv`. Format (commas, double-quotes around titles with commas inside):

```csv
# Comments OK on lines starting with #.
1,1,"Chapter 1 Title"
1,2,"Chapter 2 Title"
2,3,"Chapter 3 Title"
```

### Step 3: Run the helper

From the repo root:

```cmd
python tools\premium_catalog_helper\premium_catalog_draft.py ^
    --torrent-file tools\premium_catalog_helper\_curation\<series_id>.torrent ^
    --series-id <series_id> ^
    --title "<Display Title>" ^
    --status completed ^
    --uploader "<uploader_handle>" ^
    --release-edition "<edition name, e.g. VIZ Digital>" ^
    --mapping tools\premium_catalog_helper\_curation\<series_id>_mapping.csv ^
    --out tools\premium_catalog_helper\_curation\<series_id>.draft.json
```

For ongoing series with WeebCentral fallback coverage:

```cmd
python tools\premium_catalog_helper\premium_catalog_draft.py ^
    --torrent-file ... ^
    --series-id one_piece ^
    --title "One Piece" ^
    --status ongoing ^
    --uploader 1r0n ^
    --release-edition "VIZ Digital" ^
    --post-coverage-slug one-piece ^
    --post-coverage-after-vol 109 ^
    --mapping ... ^
    --out ...
```

### Step 4: Review the draft

Open `<series_id>.draft.json` in an editor:

1. Volume count matches the canonical volume count.
2. Each volume has non-zero `fileIndex` / `fileSizeBytes` / `pieceStart` / `pieceEnd` / `cbzFileName`.
3. Any volume marked `"_needs_review": true` -- the helper couldn't guess the volume number from the filename. Fix by hand.
4. Any `"_warning"` entry -- a non-cbz file is in the torrent (rar samples, txt credits, etc). Either drop the entry or leave it and the validator will reject it as `invalid_cbzFileName`.
5. Each volume's `chapters` array matches the CSV section.

### Step 5: Hand-add the optional metadata

The helper does not (and cannot) auto-source these:

- `anilistId` -- look up on anilist.co (the URL slug after /manga/ is the ID, e.g. `30005` for Death Note).
- `alternateTitles` -- common dedup variants. For Japanese works, add the JP capitalization variant (e.g. `["DEATH NOTE", "Desu Noto"]`).
- `coverPageHint` (per volume, optional) -- if the cbz's first image is a back-cover or credit page rather than the front cover, override here with the entryName preference.
- `pageCount` (per volume, optional) -- if known. Otherwise 0 means runtime probe.

### Step 6: Merge into the bundled catalog

1. Open `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json`.
2. The `series[]` array is alphabetical by `seriesId`. Insert the new entry in the right position.
3. Strip the helper's `_needs_review` / `_warning` keys (the validator does not error on extra keys but they pollute the diff).
4. Save.

### Step 7: Loader-verify

Kill any running Tankoban + sidecar, then:

```cmd
build_and_run.bat
out\tankoctl.exe ping
out\tankoctl.exe get-state
```

The catalog log line emits to stderr (qDebug), not to the structured ring buffer. To see it directly, launch via the shipped Phase 11 stderr-capture flow:

```powershell
$env:QT_FORCE_STDERR_LOGGING='1'
Start-Process -FilePath 'out\Tankoban.exe' -ArgumentList '--dev-control' `
  -RedirectStandardError '_curation_stderr.log' -RedirectStandardOutput '_curation_stdout.log' `
  -NoNewWindow -PassThru
# wait a few seconds, then:
Stop-Process -Name Tankoban -Force
Select-String -Path _curation_stderr.log -Pattern 'PremiumCatalog'
```

Expected line after N series total are curated:

```
[PremiumCatalog] loaded N series across 1 file(s), with 0 diagnostic(s)
```

If you see `with K diagnostic(s)` where K > 0, search the same stderr file for `severity=2` lines (RejectSeries) and `severity=3` lines (RejectFile). Each will have a `code` field naming the rejection class (`invalid_expected_infohash`, `infohash_mismatch`, `missing_magnet_uri`, `invalid_cbzFileName`, `vol_out_of_range`, `unsupported_format`, etc). Fix the JSON and re-run.

When verified, kill Tankoban + sidecar:

```cmd
taskkill /F /IM Tankoban.exe
taskkill /F /IM ffmpeg_sidecar.exe
```

### Step 8: Commit

The catalog is the only persistent artifact. The `_curation/` scratch dir (torrent + CSV + draft.json) can be deleted; everything is reproducible from the .torrent files which can be re-downloaded.

If keeping per-series provenance is desirable, archive each `_curation/<series_id>.draft.json` under `tools/premium_catalog_helper/_curation_archive/` instead of deleting. Not required.

## The 29-series pending list

Curate in this order (smallest first, finishing with the largest). Source: brainstorm section 6.

1. Death Note (12 vols) -- DONE (v1 worked example, KG Manga edition).
2. Pluto (8)
3. Blame! (10)
4. Goodnight Punpun (13)
5. Made in Abyss (13) -- ongoing; needs `postCoverageFallback`.
6. Frieren (14) -- ongoing.
7. Monster (18)
8. The Promised Neverland (20)
9. Vinland Saga (~28 published in English; ongoing in JP) -- ongoing.
10. Vagabond (37, on indefinite hiatus) -- treat as ongoing without post-coverage.
11. Berserk (~42 published; ongoing under new author) -- ongoing.
12. Akira (6)
13. Nausicaa of the Valley of the Wind (7)
14. 20th Century Boys (22)
15. Pluto -- already in pos 2.
16. Oyasumi Punpun -- already in pos 4.
17. Chainsaw Man (~18; ongoing Part 2) -- ongoing.
18. Jujutsu Kaisen (30; completed 2024) -- completed.
19. Spy x Family (~15; ongoing) -- ongoing.
20. Mob Psycho 100 (16) -- completed.
21. Tokyo Ghoul (14)
22. Tokyo Ghoul:re (16)
23. Attack on Titan (34) -- completed.
24. Naruto (72) -- completed.
25. Bleach + TYBW (~74 inc. final arc; long).
26. Kingdom (70+; ongoing) -- ongoing; large.
27. Slam Dunk (31).
28. Hunter x Hunter (37+ on indefinite hiatus) -- treat as ongoing without post-coverage.
29. Vagabond -- already in pos 10. Drop dupe.
30. One Piece (110+; ongoing) -- ongoing; LARGEST. Save for last.

(The above is from the plan's Task 11.2.1 order; the brainstorm section 6 list may differ slightly. Curator should reconcile against section 6 at curation time. Two duplicate entries (Pluto, Punpun, Vagabond) listed; consolidate at curation.)

## Known issues

### Helper -- non-cbz file detection

The helper flags `_warning` for any non-cbz file in the torrent (rar samples, scanlator credits txt). Currently the curator must manually drop those entries before merge. Suggested follow-up: a `--strict-cbz` flag that drops non-cbz entries from the draft entirely.

### Helper -- volume-number guess from non-standard filenames

The helper's `guess_volume()` regex covers `v01` / `vol 1` / `volume 1` / ` - 01 - ` patterns. It does not handle:
- `Berserk - Deluxe Edition Vol I` (Roman numerals).
- Filenames where the volume number appears after a hyphen separator with no spaces (`Naruto-v01.cbz` works; `Naruto-01.cbz` doesn't).
- Multi-volume omnibus packs (`Naruto v01-03.cbz`).

For these edge cases the curator must set `vol` by hand. Suggested follow-up: extend `_VOL_PATTERNS` in `premium_catalog_draft.py` to include Roman numerals and bare-number-after-hyphen.

### Helper -- magnet-only mode

If a `.torrent` cannot be downloaded (e.g. nyaa is temporarily Cloudflare-gated), `--magnet` mode still emits a partial draft with `infoHash` extracted from the URI's `xt=urn:btih:...` parameter. The `volumes[]` array will be a single placeholder asking the curator to populate by hand. Re-run with `--torrent-file` once the .torrent is available.

### Catalog loader -- stderr-only emission

The PremiumCatalog loader's "loaded N series" status line emits via `qDebug()` which goes to stderr (or OutputDebugString on Windows). The structured log ring buffer surfaced via `tankoctl logs` does NOT capture it. To verify the line:
- Launch via `Start-Process -RedirectStandardError`.
- Pass `QT_FORCE_STDERR_LOGGING=1` so Qt forces stderr instead of OutputDebugString.

Suggested follow-up: route the PremiumCatalog ctor's load-result summary through the structured ring buffer (one info-level entry on success, one warn-level entry per diagnostic). Adds 5-10 LOC in `PremiumCatalog.cpp` plus the include for whatever the ring-buffer API is.

### Adopt-existing-folder mitigation (Phase 9)

If a curator manually folder-imports a series before adding it to the catalog, the Premium download path will detect the conflict and adopt the existing folder rather than collide. This was shipped in Phase 9 and validated in smoke 5. No curator action required, but be aware that re-curating a series someone already imported will use the existing tile in the library rather than creating a new one.

## Next steps after all 30 series land

Once `[PremiumCatalog] loaded 30 series across 1 file(s), with 0 diagnostic(s)` is observed:

1. Execute the 21-case smoke matrix from the plan's Task 11.3.
2. The Smoke 1 visual-quality A/B (Berserk Premium vs WeebCentral) requires real downloads + Hemanth weighing in on fidelity.
3. Pass / fail per smoke is logged one-liner to chat.md.
4. v1.x followups: vcpkg / version bumps / community-catalog gating (signing + curator review) per brainstorm section 24.

End of handoff.
