# Synopsis Harvester

Offline Python tool that harvests official per-volume English synopses for manga
series and writes them into the app's existing enrichment overlay, so volume rows
show real plot blurbs in the Tankoban comics UI.

## Pipeline

1. **Wikipedia parser** — reads a manga article's standardized "Graphic novel list"
   volume table, extracts each volume's English-edition ISBN (the keying resolver).
2. **Synopsis source** — looks up the English ISBN on Barnes & Noble (primary,
   keyless) and extracts the official per-volume synopsis. Google Books is an
   optional fallback when `GOOGLE_BOOKS_API_KEY` is set.
3. **Enrichment writer** — assembles and writes `data/manga_enrichment/<seriesId>.volumes.json`
   in the exact schema the C++ loader already consumes.

**Zero app changes needed.** The `LocalMangaCatalogLoader` already merges
`volumes[].synopsis` into `MangaVolume.synopsis` by integer `volumeNumber`, and
`VolumeTile` already renders it.

## Setup

```bash
pip install -r requirements.txt
```

## Run

```bash
# From repo root:
python scripts/synopsis_harvester/harvest_synopsis.py --series grand-blue-dreaming

# If the Wikipedia article title differs from the seriesTitle in the catalog:
python scripts/synopsis_harvester/harvest_synopsis.py --series 20th-century-boys --wiki "20th Century Boys"

# Adjust rate-limit delay (default 0.7s between B&N requests):
python scripts/synopsis_harvester/harvest_synopsis.py --series bleach --delay 1.0
```

## Output

`data/manga_enrichment/<seriesId>.volumes.json` — the app auto-merges this
overlay on next load (no rebuild needed).

## Optional: Google Books fallback

Set `GOOGLE_BOOKS_API_KEY` to enable Google Books as a fallback when B&N
returns nothing:

```bash
set GOOGLE_BOOKS_API_KEY=your_key_here  # Windows
export GOOGLE_BOOKS_API_KEY=your_key_here  # Unix
```

## Tests

```bash
cd scripts && python -m pytest synopsis_harvester/tests/ -v
```

## Design

See the locked-decision memory `project_manga_synopsis_source_decision` for why
this pipeline (Wikipedia ISBN → B&N synopsis) was chosen over alternatives
(back-cover OCR, Google Books primary, MAL scraping, etc.).
