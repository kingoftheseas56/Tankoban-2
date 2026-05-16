# Tankoyomi-Premium catalog helper

Dev-only tool. Drafts one catalog entry per series from a `.torrent` file plus a chapter-to-volume mapping CSV. The draft is reviewed by hand before it lands in the bundled catalog at `resources/manga_premium_catalogs/`.

## Why this tool exists

Per Codex's section 25 review of the Phase 1 brainstorm, hand-curating 30 series end-to-end was estimated at 30-50 hours when done entirely manually. The dominant cost was per-volume file-mapping (capturing `fileIndex`, `fileSizeBytes`, `pieceStart`, `pieceEnd` for every cbz in the torrent). This tool eliminates that data-entry portion in seconds per series, while preserving the human verification step.

## Per-series workflow

For each series in the v1 catalog corpus (see brainstorm section 6 for the list of 30):

1. **Download the `.torrent` file** from nyaa.si for your trusted uploader (1r0n, VIZ Digital, Hox, Danke, etc). Save it locally.
2. **Build the chapter-to-volume mapping CSV.** Source: mangareader.to volume index pages, AniList volumes endpoint, MangaUpdates, or community wikis. Validate that the chapter range listed on the mangareader.to vol N page matches the volume's table of contents.
3. **Run the helper.** Example for Berserk:
   ```
   python premium_catalog_draft.py \
     --torrent-file "C:/curation/berserk_1r0n.torrent" \
     --series-id berserk \
     --title "Berserk" \
     --status completed \
     --uploader 1r0n \
     --release-edition "VIZ Digital" \
     --mapping berserk_mapping.csv \
     --out drafts/berserk.draft.json
   ```
4. **Open the draft and review.** Confirm:
   - Every `vol` was guessed correctly (look for `_needs_review: true` flags).
   - Non-cbz entries (rar samples, scanlator credits txt) are excluded or commented out.
   - The `chapters` array under each volume matches the volume's actual chapter range.
5. **Add optional fields if available:** `alternateTitles`, `anilistId`, `coverPageHint`.
6. **Set `postCoverageFallback`** for ongoing series only:
   ```json
   "postCoverageFallback": {
     "weebcentralSlug": "berserk",
     "startsAfterVolume": 42
   }
   ```
7. **Merge into the bundled catalog file** at `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json`.
8. **Run the app and watch the loader's diagnostic output** (Phase 1 logs to `qDebug` and to the app's ring buffer accessible via `tankoctl logs`). Any validator rejection means the entry needs fixing.

## Two operating modes

**`.torrent` mode** (preferred):
- Resolves `infoHash` deterministically from the bencoded info dict.
- Computes per-file piece ranges precisely.
- Guesses volume numbers from filename patterns (`v01`, `Vol 1`, etc).
- Emits a complete draft entry.

**Magnet-only mode**:
- Extracts `infoHash` from the magnet URI's `xt=urn:btih:` parameter.
- Emits a partial draft with `_TODO_resolve_files: true`. Volumes must be filled in by hand OR by re-running with `--torrent-file` once the .torrent is obtained.

## Why no runtime mangareader scraper?

Per mangareader.to's terms of use (https://mangareader.to/terms): access is for temporary personal non-commercial viewing; copying and modifying materials is prohibited. The helper is local, rate-unlimited because it doesn't even talk to mangareader.to, and consumes a hand-prepared CSV. The Tankoban app itself never talks to mangareader.to at runtime. The catalog is the only artifact that ships.

## Why no live nyaa.si search either?

v1 is bundled-only. Live nyaa search is explicitly out-of-scope per brainstorm section 8. The catalog's `expectedInfoHash` field is the trust identity (per Codex section 24); the uploader name is informational only.
