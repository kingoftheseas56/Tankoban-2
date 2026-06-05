# Comics Western — GCD+OL Catalogue Brain + Volume-Aware GetComics Download — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the wrong RCO-harvest Western catalogue with a correct GCD+Open Library brain (forward-ordered TPB volumes, real titles, real covers), and make the GetComics download volume-aware so tapping "Vol N" fetches that specific volume's file.

**Architecture:** Two halves. (1) An **offline Python builder** (`scripts/comics_catalogue/gcd_*.py`) that queries the Grand Comics Database for a series' trade-paperback line, reads it forward (Vol 1 = first TPB), attaches Open-Library cover URLs by ISBN, and emits schema-v3 JSON into `data/western_catalogue/`. (2) A **volume-aware GetComics resolver** (C++) that, given a series + volume number, browses GetComics' clean per-series tag page, finds the post that carries that volume (standalone post or range bundle), extracts that one volume's clean-HTTP download link, and saves it as its own file. The C++ catalogue loader is extended to read the per-volume schema. The brain (builder) and the source (resolver) stay decoupled — exactly the manga model.

**Tech Stack:** Python 3.12 + `urllib` + pytest (builder, mirrors existing `scripts/comics_catalogue/`). C++17 + Qt 6 + GoogleTest (loader + resolver, mirrors existing `src/core/manga/` + `tests/core/manga/`).

---

## Ground truth (verified live 2026-06-05, do not re-litigate)

- **GCD API, no key:** `https://www.comics.org/api/series/name/<URL-name>/?format=json` (paginated via `next`); `https://www.comics.org/api/issue/<id>/?format=json`. GCD throttles hard (HTTP 429) after a burst → the builder MUST sleep ~2s between calls and back off on 429. This is an *offline dev-time* build, never run at user runtime.
- **TPB line selection (proven on Invincible):** A series' collected-TPB line is a SEPARATE GCD series whose `name` exactly equals the canonical title, `language=="en"`, `country=="us"`, `publishing_format` contains "Collected" (or `binding` is `softcover`/`trade paperback`, never `saddle-stitched`). For Invincible that is series **65547** ("Invincible", 34 issues, "Collected Editions; Was Ongoing Series", softcover) — NOT the floppy run 17010. Exact-name match kills the "Invincible Iron Man" / "Chakra the Invincible" noise.
- **Volume number + title in one field:** each TPB issue's `descriptor` is `"1 - Family Matters"`, `"2 - Eight Is Enough"`, … → split on the first `" - "` → (volumeNumber, title). Multiple printings repeat a volume → DEDUPE by volumeNumber.
- **ISBN:** issue `isbn` field, formats vary (`1-58240-320-1`, `9781582407111`, `978-1-58240-347-2`, sometimes `a; b`). Strip non-digits/X, prefer/【convert to】ISBN-13.
- **OL cover by ISBN:** `https://covers.openlibrary.org/b/isbn/<isbn13>-L.jpg?default=false` → 200 image (real cover) or 404 (no cover → leave coverUrl empty → C++ renders title-card).
- **GetComics inventory (from its tag page `getcomics.org/tag/<slug>/`, the clean canonical listing — NOT `/?s=` which is noisy):** standalone TPB posts exist for some volumes (Saga 4,5,6,8,9,11,12); gap volumes (Saga 1,2,3,7,10) live only inside range posts ("Saga Vol. 1 – 10 + Book 1 – 3 (TPB)"); the range post carries a SEPARATE per-volume download link for each volume. All issues (#26–72) also present. Tag pages paginate at `…/tag/<slug>/page/N/`.
- **Clean-HTTP download (proven last wake):** the post's "DOWNLOAD NOW" anchor href is `getcomics.org/dls/<token>...:<sig>==` — the FULL signed href 302s straight to `comicfiles.ru` and streams the `.cbr/.cbz`. Truncating the signature suffix breaks it. `GetComicsParse::extractDownloads` already captures the full href.

## Current-state contracts (what the plan targets)

- `scripts/comics_catalogue/harvest.py::build_record(...)` emits schema-v2 `{seriesId, seriesTitle, source:"rco", seriesCover, schemaVersion:2, synopsis, author, publisher, genres, yearStart, yearEnd, status, editions:[{label,href,formatTier}]}`. Editions are reverse-ordered RCO scrapes — THE BUG.
- `src/core/manga/MangaCatalogTypes.h`: `MangaVolume` already has `volumeNumber`, `titleEnglish`, `isbnEn`, `releaseDateEn` (QDate), `coverUrlJapanese`, `coverUrlEdition`, `groupingLabel`, `sourceHref`. **No struct change needed.** `MangaCatalog` has `seriesTitle`, `seriesCover`, `author`, `studio` (publisher reuses this), `genres`, `publishedYearStart/End`, `status`, `source`.
- `src/core/manga/WesternCatalogLoader.cpp::loadFromJsonObject` maps editions→volumes: position→volumeNumber, label→titleEnglish, formatTier→groupingLabel, href→sourceHref, shared seriesCover→every coverUrlJapanese. Tier map: 0 Compendium / 1 Omnibus / 2 TPB / 3 Deluxe / 4 Vol.
- `GetComicsResolver::resolve(seriesTitle, year, tierLabel)` — series-level, volume-blind. `pickBestCollectedEdition` returns ONE collected edition per series, empty on tie. `GetComicsParse` pure fns: `parseSearchResults`, `isCollectedEditionOf`, `pickBestCollectedEdition`, `extractDownloads`, `pickBest`, `parsePostCover`. Tests in `tests/core/manga/GetComicsParseTest.cpp`.
- `WesternVolumeDownloader::requestVolume(seriesId, volumeNumber, seriesTitle, year, tierLabel, destinationPath)` — stashes volumeNumber in ReqKey but calls `m_resolver.resolve(seriesTitle, year, tierLabel)` WITHOUT volumeNumber; names the DDL file `sanitiseName(seriesTitle)+".cbz"` (series-level, every volume overwrites). Both are the volume-blindness to fix.
- `ComicsPage.cpp:1098-1105`: `year = seriesJson["yearStart"]` (series-level); calls `requestVolume(seriesId, volumeNumber, seriesTitle, year, tierLabel, destPath)`.

---

## Phase 1 — GCD client (Python, pure logic, TDD)

New file `scripts/comics_catalogue/gcd_client.py`. Pure parsing/selection functions first (no network), then a thin live-fetch wrapper. Tests in `scripts/comics_catalogue/tests/test_gcd_client.py`.

### Task 1: ISBN normalization

**Files:**
- Create: `scripts/comics_catalogue/gcd_client.py`
- Test: `scripts/comics_catalogue/tests/test_gcd_client.py`

- [ ] **Step 1: Write the failing test**

```python
# tests/test_gcd_client.py
from gcd_client import normalize_isbn13

def test_isbn10_converts_to_isbn13():
    assert normalize_isbn13("1-58240-320-1") == "9781582403201"

def test_isbn13_passthrough_strips_hyphens():
    assert normalize_isbn13("978-1-58240-347-2") == "9781582403472"

def test_compound_isbn_takes_first_valid():
    assert normalize_isbn13("1-58240-778-9; 978-1-58240-778-4") == "9781582407784"

def test_empty_or_junk_returns_empty():
    assert normalize_isbn13("") == ""
    assert normalize_isbn13("n/a") == ""
```

- [ ] **Step 2: Run it, verify it fails** — `cd scripts/comics_catalogue && python -m pytest tests/test_gcd_client.py -v` → FAIL (no module / function).

- [ ] **Step 3: Implement**

```python
# gcd_client.py
import re

def _isbn10_to_13(isbn10: str) -> str:
    core = "978" + isbn10[:9]
    s = sum((1 if i % 2 == 0 else 3) * int(d) for i, d in enumerate(core))
    return core + str((10 - s % 10) % 10)

def normalize_isbn13(raw: str) -> str:
    """Return a clean 13-digit ISBN, or '' if none parseable. Handles ISBN-10,
    ISBN-13, hyphens, and 'a; b' compounds (prefers the first 13-digit, else
    converts the first valid 10-digit)."""
    if not raw:
        return ""
    candidates = re.split(r"[;,/]", raw)
    tens = []
    for c in candidates:
        digits = re.sub(r"[^0-9Xx]", "", c)
        if len(digits) == 13 and digits.isdigit():
            return digits
        if len(digits) == 10:
            tens.append(digits)
    for t in tens:
        return _isbn10_to_13(t)
    return ""
```

- [ ] **Step 4: Run tests, verify PASS.**
- [ ] **Step 5: Commit** — `git add scripts/comics_catalogue/gcd_client.py scripts/comics_catalogue/tests/test_gcd_client.py && git commit -m "feat(comics): GCD ISBN-13 normalization (agent1)"`

### Task 2: TPB-line selection from a series result set

**Files:** Modify `scripts/comics_catalogue/gcd_client.py`; Test `tests/test_gcd_client.py`

- [ ] **Step 1: Write the failing test**

```python
from gcd_client import pick_tpb_line

def _series(name, fmt, binding, niss):
    return {"api_url": f"https://www.comics.org/api/series/{niss}/?format=json",
            "name": name, "language": "en", "country": "us",
            "publishing_format": fmt, "binding": binding,
            "active_issues": ["x"] * niss}

def test_picks_collected_softcover_line_over_floppy():
    rows = [
        _series("Invincible", "was ongoing series", "Saddle-stitched", 170),
        _series("Invincible", "Collected Editions; Was Ongoing Series", "softcover", 34),
        _series("Invincible Iron Man", "was ongoing series", "Saddle-stitched", 186),
        _series("Invincible Compendium", "Was Ongoing Series", "Squarebound", 3),
    ]
    line = pick_tpb_line(rows, "Invincible")
    assert line["binding"] == "softcover"
    assert len(line["active_issues"]) == 34

def test_returns_none_when_no_collected_line():
    rows = [_series("Saga", "was ongoing series", "Saddle-stitched", 72)]
    assert pick_tpb_line(rows, "Saga") is None
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement**

```python
def _is_collected_line(r: dict) -> bool:
    fmt = (r.get("publishing_format") or "").lower()
    binding = (r.get("binding") or "").lower()
    if "saddle" in binding:
        return False
    return ("collect" in fmt) or ("trade paperback" in binding) or ("softcover" in binding)

def pick_tpb_line(rows: list[dict], canonical_title: str) -> dict | None:
    """From GCD series results, return the collected/TPB line whose name EXACTLY
    equals the canonical title (en/us), preferring the one with the most issues.
    None if no exact-name collected line exists."""
    exact = [r for r in rows
             if (r.get("name") or "").strip().lower() == canonical_title.strip().lower()
             and r.get("language") == "en" and r.get("country") == "us"]
    cands = [r for r in exact if _is_collected_line(r)]
    cands.sort(key=lambda r: len(r.get("active_issues") or []), reverse=True)
    return cands[0] if cands else None
```

- [ ] **Step 4: Run, verify PASS.**
- [ ] **Step 5: Commit** — `git commit -am "feat(comics): GCD TPB-line selection by exact name + collected format (agent1)"`

### Task 3: descriptor → (volumeNumber, title) + dedupe/forward-order

**Files:** Modify `gcd_client.py`; Test `tests/test_gcd_client.py`

- [ ] **Step 1: Write the failing test**

```python
from gcd_client import parse_descriptor, build_volumes_from_issues

def test_parse_descriptor_splits_number_and_title():
    assert parse_descriptor("1 - Family Matters") == (1, "Family Matters")
    assert parse_descriptor("12 - The Untitled") == (12, "The Untitled")
    assert parse_descriptor("Family Matters") == (None, "Family Matters")

def test_build_volumes_dedupes_and_forward_sorts():
    # issues = dicts already fetched (descriptor/isbn/publication_date)
    issues = [
        {"descriptor": "2 - Eight Is Enough", "isbn": "978-1-58240-347-2", "publication_date": "2005-03"},
        {"descriptor": "1 - Family Matters", "isbn": "1-58240-320-1", "publication_date": "2003-05"},
        {"descriptor": "1 - Family Matters", "isbn": "9781582407111", "publication_date": "2008"},  # reprint
    ]
    vols = build_volumes_from_issues(issues)
    assert [v["volumeNumber"] for v in vols] == [1, 2]
    assert vols[0]["title"] == "Family Matters"
    assert vols[0]["isbn"] == "9781582403201"   # first printing's ISBN, normalized
    assert vols[0]["year"] == 2003
    assert vols[1]["volumeNumber"] == 2
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement**

```python
def parse_descriptor(descriptor: str):
    """'1 - Family Matters' -> (1, 'Family Matters'); no leading int -> (None, text)."""
    s = (descriptor or "").strip()
    m = re.match(r"^(\d+)\s*[-–]\s*(.*)$", s)
    if m:
        return int(m.group(1)), m.group(2).strip()
    return None, s

def _year_of(pub: str) -> int:
    m = re.match(r"\s*(\d{4})", pub or "")
    return int(m.group(1)) if m else 0

def build_volumes_from_issues(issues: list[dict]) -> list[dict]:
    """Map TPB-line issues -> forward-ordered, deduped volume dicts. Keeps the
    FIRST printing seen per volume number (earliest by publication date)."""
    by_num: dict[int, dict] = {}
    for iss in issues:
        num, title = parse_descriptor(iss.get("descriptor") or iss.get("number") or "")
        if num is None:
            continue
        year = _year_of(iss.get("publication_date", ""))
        isbn = normalize_isbn13(iss.get("isbn") or "")
        cur = by_num.get(num)
        # prefer the earliest printing; if tie, prefer one that has an ISBN
        if cur is None or (year and (cur["year"] == 0 or year < cur["year"])) \
                or (not cur["isbn"] and isbn):
            by_num[num] = {"volumeNumber": num, "title": title, "isbn": isbn, "year": year}
    return [by_num[n] for n in sorted(by_num)]
```

- [ ] **Step 4: Run, verify PASS.** **Step 5: Commit** — `git commit -am "feat(comics): GCD descriptor parse + forward-ordered dedupe (agent1)"`

### Task 4: live GCD fetch wrapper (throttled, 429-backoff)

**Files:** Modify `gcd_client.py`; Test `tests/test_gcd_client.py` (monkeypatch the fetcher, no real network in tests)

- [ ] **Step 1: Write the failing test**

```python
import gcd_client

def test_fetch_all_series_paginates(monkeypatch):
    pages = {
        "https://www.comics.org/api/series/name/X/?format=json":
            {"results": [{"name": "X"}], "next": "PAGE2"},
        "PAGE2": {"results": [{"name": "X2"}], "next": None},
    }
    monkeypatch.setattr(gcd_client, "_get_json", lambda url: pages[url])
    rows = gcd_client.fetch_all_series("X")
    assert [r["name"] for r in rows] == ["X", "X2"]
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement**

```python
import json, time, urllib.request, urllib.parse

_UA = "Tankoban/1.0 (dev catalogue builder; contact dev@tankoban.local)"
_THROTTLE_S = 2.0

def _get_json(url: str, tries: int = 6) -> dict:
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": _UA})
            with urllib.request.urlopen(req, timeout=40) as r:
                return json.loads(r.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as e:
            if e.code == 429 and i < tries - 1:
                time.sleep(15 * (i + 1)); continue
            raise

def fetch_all_series(name: str, max_pages: int = 12) -> list[dict]:
    out, pages = [], 0
    url = f"https://www.comics.org/api/series/name/{urllib.parse.quote(name)}/?format=json"
    while url and pages < max_pages:
        data = _get_json(url); out.extend(data.get("results", []))
        url = data.get("next"); pages += 1
        if url:
            time.sleep(_THROTTLE_S)
    return out

def fetch_issue(api_url: str) -> dict:
    time.sleep(_THROTTLE_S)
    return _get_json(api_url)
```

- [ ] **Step 4: Run, verify PASS.** **Step 5: Commit** — `git commit -am "feat(comics): GCD throttled fetch + pagination (agent1)"`

---

## Phase 2 — OL covers + record assembly + builder driver

### Task 5: Open-Library cover URL (verified-present)

**Files:** Create `scripts/comics_catalogue/ol_covers.py`; Test `scripts/comics_catalogue/tests/test_ol_covers.py`

- [ ] **Step 1: Write the failing test**

```python
import ol_covers

def test_cover_url_built_from_isbn():
    assert ol_covers.cover_url("9781582403201") == \
        "https://covers.openlibrary.org/b/isbn/9781582403201-L.jpg"

def test_cover_url_empty_for_empty_isbn():
    assert ol_covers.cover_url("") == ""

def test_verified_cover_returns_url_when_present(monkeypatch):
    monkeypatch.setattr(ol_covers, "_head_ok", lambda url: True)
    assert ol_covers.verified_cover_url("9781582403201").endswith("-L.jpg")

def test_verified_cover_empty_when_absent(monkeypatch):
    monkeypatch.setattr(ol_covers, "_head_ok", lambda url: False)
    assert ol_covers.verified_cover_url("9781582403201") == ""
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement**

```python
# ol_covers.py
import urllib.request

_UA = "Tankoban/1.0 (dev catalogue builder; contact dev@tankoban.local)"

def cover_url(isbn13: str) -> str:
    return f"https://covers.openlibrary.org/b/isbn/{isbn13}-L.jpg" if isbn13 else ""

def _head_ok(url: str) -> bool:
    # ?default=false => 404 when OL has no real cover (avoids the 1x1 placeholder).
    probe = url + "?default=false"
    try:
        req = urllib.request.Request(probe, headers={"User-Agent": _UA}, method="GET")
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status == 200 and (r.headers.get("Content-Type", "").startswith("image"))
    except Exception:
        return False

def verified_cover_url(isbn13: str) -> str:
    """Cover URL only if OL actually has a real cover for this ISBN, else ''."""
    u = cover_url(isbn13)
    return u if (u and _head_ok(u)) else ""
```

- [ ] **Step 4: Run, verify PASS.** **Step 5: Commit** — `git commit -am "feat(comics): Open Library verified cover-by-ISBN (agent1)"`

### Task 6: schema-v3 record assembly

**Files:** Create `scripts/comics_catalogue/gcd_record.py`; Test `scripts/comics_catalogue/tests/test_gcd_record.py`

- [ ] **Step 1: Write the failing test**

```python
from gcd_record import build_gcd_record

def test_record_shape_v3_forward_editions():
    vols = [
        {"volumeNumber": 1, "title": "Family Matters", "isbn": "9781582403201", "year": 2003,
         "coverUrl": "https://covers.openlibrary.org/b/isbn/9781582403201-L.jpg"},
        {"volumeNumber": 2, "title": "Eight Is Enough", "isbn": "9781582403472", "year": 2004,
         "coverUrl": ""},
    ]
    rec = build_gcd_record("invincible", "Invincible", vols, gcd_series_id=65547,
                           synopsis="...", author="Robert Kirkman", publisher="Image Comics",
                           genres=["superhero comics"], year_start=2003)
    assert rec["source"] == "gcd"
    assert rec["schemaVersion"] == 3
    assert rec["seriesTitle"] == "Invincible"
    assert rec["gcdSeriesId"] == 65547
    assert [e["volumeNumber"] for e in rec["editions"]] == [1, 2]
    e0 = rec["editions"][0]
    assert e0 == {"volumeNumber": 1, "title": "Family Matters", "isbn": "9781582403201",
                  "coverUrl": "https://covers.openlibrary.org/b/isbn/9781582403201-L.jpg",
                  "year": 2003, "formatTier": 2, "tierLabel": "TPB"}
    # series hero cover = first volume's cover (first non-empty)
    assert rec["seriesCover"].endswith("9781582403201-L.jpg")
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement**

```python
# gcd_record.py
def build_gcd_record(series_id, series_title, volumes, gcd_series_id=0,
                     synopsis="", author="", publisher="", genres=None,
                     year_start=0, year_end=0, status=""):
    """Assemble a schema-v3 GCD catalogue record. `volumes` are forward-ordered
    dicts each with volumeNumber/title/isbn/year/coverUrl. Every edition is a TPB
    (formatTier 2). seriesCover = first volume with a non-empty cover."""
    editions = [{
        "volumeNumber": v["volumeNumber"],
        "title": v["title"],
        "isbn": v["isbn"],
        "coverUrl": v.get("coverUrl", ""),
        "year": v.get("year", 0),
        "formatTier": 2,
        "tierLabel": "TPB",
    } for v in volumes]
    hero = next((e["coverUrl"] for e in editions if e["coverUrl"]), "")
    return {
        "seriesId": series_id,
        "seriesTitle": series_title,
        "source": "gcd",
        "schemaVersion": 3,
        "gcdSeriesId": gcd_series_id,
        "seriesCover": hero,
        "synopsis": synopsis,
        "author": author,
        "publisher": publisher,
        "genres": genres or [],
        "yearStart": year_start,
        "yearEnd": year_end,
        "status": status,
        "editions": editions,
    }
```

- [ ] **Step 4: Run, verify PASS.** **Step 5: Commit** — `git commit -am "feat(comics): schema-v3 GCD record assembly (agent1)"`

### Task 7: builder driver (live, seed Saga + Invincible)

**Files:** Create `scripts/comics_catalogue/gcd_harvest.py` (driver, mirrors `harvest.py` shape). No unit test (it's the live orchestrator); validated by the Phase-5 run.

- [ ] **Step 1: Implement the driver**

```python
# gcd_harvest.py
"""Build Western catalogue records from GCD + Open Library (no signup).
Per series: GCD search -> pick TPB line -> fetch its issues (throttled) ->
forward-ordered deduped volumes -> OL cover by ISBN -> schema-v3 JSON in
data/western_catalogue/<seriesId>.json. Replaces the RCO harvest (harvest.py).
Throttled + idempotent; safe to re-run.
"""
import json, pathlib, sys
import gcd_client, ol_covers
from gcd_record import build_gcd_record

_OUT = pathlib.Path(__file__).resolve().parents[2] / "data" / "western_catalogue"

# (seriesId, canonical GCD name, display title)
_SEED = [
    ("saga", "Saga", "Saga"),
    ("invincible", "Invincible", "Invincible"),
]

def harvest_one(series_id, gcd_name, title):
    rows = gcd_client.fetch_all_series(gcd_name)
    line = gcd_client.pick_tpb_line(rows, gcd_name)
    if not line:
        raise RuntimeError(f"no TPB line for {gcd_name}")
    gcd_sid = int([p for p in line["api_url"].split("/") if p.isdigit()][-1])
    issues = [gcd_client.fetch_issue(u) for u in (line.get("active_issues") or [])]
    vols = gcd_client.build_volumes_from_issues(issues)
    for v in vols:
        v["coverUrl"] = ol_covers.verified_cover_url(v["isbn"])
    year_start = min((v["year"] for v in vols if v["year"]), default=0)
    return build_gcd_record(series_id, title, vols, gcd_series_id=gcd_sid,
                            year_start=year_start)

def main():
    _OUT.mkdir(parents=True, exist_ok=True)
    for series_id, gcd_name, title in _SEED:
        try:
            rec = harvest_one(series_id, gcd_name, title)
            (_OUT / f"{series_id}.json").write_text(
                json.dumps(rec, indent=2, ensure_ascii=False), encoding="utf-8")
            print(f"{series_id}: {len(rec['editions'])} TPB volumes")
        except Exception as e:
            print(f"{series_id}: FAILED {type(e).__name__}: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Commit** — `git commit -am "feat(comics): GCD+OL catalogue builder driver (Saga+Invincible seed) (agent1)"`
- [ ] **Step 3:** (Run is deferred to Phase 5 so the C++ loader can consume the output in the same smoke.)

---

## Phase 3 — C++ loader reads schema-v3 (per-volume covers, forward order, ISBN, year)

### Task 8: WesternCatalogLoader v3 branch

**Files:** Modify `src/core/manga/WesternCatalogLoader.cpp:38-108`; Test `tests/core/manga/WesternCatalogLoaderTest.cpp`

- [ ] **Step 1: Write the failing test** (append to `WesternCatalogLoaderTest.cpp`)

```cpp
TEST(WesternCatalogLoaderTest, V3GcdSchemaForwardVolumesPerVolumeCovers) {
    const QString json = R"({
      "seriesId":"saga","seriesTitle":"Saga","source":"gcd","schemaVersion":3,
      "seriesCover":"https://covers.openlibrary.org/b/isbn/9781607066019-L.jpg",
      "editions":[
        {"volumeNumber":1,"title":"Volume One","isbn":"9781607066019",
         "coverUrl":"https://covers.openlibrary.org/b/isbn/9781607066019-L.jpg",
         "year":2012,"formatTier":2,"tierLabel":"TPB"},
        {"volumeNumber":2,"title":"Volume Two","isbn":"9781607066927",
         "coverUrl":"","year":2013,"formatTier":2,"tierLabel":"TPB"}
      ]})";
    const auto obj = QJsonDocument::fromJson(json.toUtf8()).object();
    const auto cat = tankoban::manga::WesternCatalogLoader::loadFromJsonObject(obj);
    ASSERT_EQ(cat.volumes.size(), 2);
    EXPECT_EQ(cat.volumes[0].volumeNumber, 1);                       // forward
    EXPECT_EQ(cat.volumes[0].titleEnglish, QStringLiteral("Volume One"));
    EXPECT_EQ(cat.volumes[0].isbnEn, QStringLiteral("9781607066019"));
    EXPECT_TRUE(cat.volumes[0].coverUrlJapanese.endsWith("9781607066019-L.jpg")); // per-vol cover
    EXPECT_EQ(cat.volumes[0].releaseDateEn.year(), 2012);
    EXPECT_EQ(cat.volumes[0].groupingLabel, QStringLiteral("TPB"));
    EXPECT_TRUE(cat.volumes[1].coverUrlJapanese.isEmpty());          // empty cover => title-card
}
```

- [ ] **Step 2: Run, verify fail** — build + run `tankoban_tests --gtest_filter=WesternCatalogLoaderTest.V3GcdSchemaForwardVolumesPerVolumeCovers` (see Build note below). Expected FAIL (loader ignores volumeNumber/isbn/year/per-vol cover).

- [ ] **Step 3: Implement** — branch on `schemaVersion >= 3` (or `source == "gcd"`) inside `loadFromJsonObject`, BEFORE the existing v2 loop. Keep the v2/rco path intact for not-yet-regenerated files.

```cpp
    const int schemaVer = obj.value(QStringLiteral("schemaVersion")).toInt();
    const bool isGcd = schemaVer >= 3
        || obj.value(QStringLiteral("source")).toString() == QLatin1String("gcd");

    if (isGcd) {
        // v3 GCD schema: per-volume forward editions with their own ISBN/cover/year.
        cat.source = obj.value(QStringLiteral("source")).toString();
        QString hero = obj.value(QStringLiteral("seriesCover")).toString().trimmed();
        cat.seriesCover = hero;   // already an absolute OL URL
        cat.volumes.reserve(editionsArr.size());
        for (const auto& v : editionsArr) {
            const QJsonObject eo = v.toObject();
            MangaVolume vol;
            vol.volumeNumber  = eo.value(QStringLiteral("volumeNumber")).toInt();
            vol.titleEnglish  = eo.value(QStringLiteral("title")).toString();
            vol.isbnEn        = eo.value(QStringLiteral("isbn")).toString();
            vol.groupingLabel = tierLabel(eo.value(QStringLiteral("formatTier")).toInt(2));
            const int y = eo.value(QStringLiteral("year")).toInt();
            if (y > 0) vol.releaseDateEn = QDate(y, 1, 1);
            const QString cov = eo.value(QStringLiteral("coverUrl")).toString().trimmed();
            vol.coverUrlJapanese = cov;   // per-volume OL cover (may be empty => title-card)
            vol.coverUrlEdition  = cov;
            cat.volumes.append(std::move(vol));
        }
        return cat;   // skip the v2/rco loop
    }
    // ...existing v2/rco mapping unchanged below...
```

- [ ] **Step 4: Run test, verify PASS.**
- [ ] **Step 5: Commit** — `git commit -am "feat(comics): WesternCatalogLoader reads schema-v3 GCD catalogue (agent1)"`

---

## Phase 4 — Volume-aware GetComics resolution

Pure parse functions first (TDD in `GetComicsParseTest.cpp`), then resolver/downloader wiring.

### Task 9: tag-slug derivation + post-for-volume selection (pure)

**Files:** Modify `src/core/manga/GetComicsParse.h` + `.cpp`; Test `tests/core/manga/GetComicsParseTest.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
using namespace tankoban::manga::getcomics;

TEST(GetComicsParseTest, TagSlug) {
    EXPECT_EQ(tagSlug("Saga"), QStringLiteral("saga"));
    EXPECT_EQ(tagSlug("The Wicked + The Divine"), QStringLiteral("the-wicked-the-divine"));
    EXPECT_EQ(tagSlug("Invincible"), QStringLiteral("invincible"));
}

TEST(GetComicsParseTest, PickPostForVolume_PrefersStandalone) {
    QList<SearchResult> r = {
        {"Saga Vol. 1 – 10 + Book 1 – 3 (TPB) (2012-2022)", "u-range"},
        {"Saga Vol. 12 (TPB) (2025)", "u-12"},
        {"Saga #72 (2025)", "u-iss"},
        {"Elektra Saga #1 (1984)", "u-noise"},
    };
    EXPECT_EQ(pickPostForVolume("Saga", 12, r).postUrl, QStringLiteral("u-12"));   // standalone wins
}

TEST(GetComicsParseTest, PickPostForVolume_FallsBackToRange) {
    QList<SearchResult> r = {
        {"Saga Vol. 1 – 10 + Book 1 – 3 (TPB) (2012-2022)", "u-range"},
        {"Saga Vol. 12 (TPB) (2025)", "u-12"},
    };
    EXPECT_EQ(pickPostForVolume("Saga", 3, r).postUrl, QStringLiteral("u-range")); // 3 in 1-10
}

TEST(GetComicsParseTest, PickPostForVolume_NoneWhenUncovered) {
    QList<SearchResult> r = {{"Saga Vol. 12 (TPB) (2025)", "u-12"}};
    EXPECT_TRUE(pickPostForVolume("Saga", 3, r).postUrl.isEmpty());
}
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement** in `GetComicsParse.{h,cpp}`:

```cpp
// .h — declarations
QString tagSlug(const QString& seriesTitle);
// Pick the post that carries volumeNumber: a standalone "<series> Vol. N"
// post (preferred), else a range post "<series> Vol. A - B" with A<=N<=B.
// Series identity gated as in isCollectedEditionOf. Empty postUrl if none.
SearchResult pickPostForVolume(const QString& seriesTitle, int volumeNumber,
                               const QList<SearchResult>& results);
```

```cpp
// .cpp
QString tagSlug(const QString& seriesTitle) {
    QString s = seriesTitle.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    return s.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
}

namespace {
// Extract the volume coverage of a post title. Returns (lo,hi); a standalone
// "Vol. 12" -> (12,12); a range "Vol. 1 - 10" -> (1,10); none -> (0,0).
QPair<int,int> volumeCoverage(const QString& title) {
    static const QRegularExpression range(
        QStringLiteral("vol\\.?\\s*(\\d+)\\s*[\\-\\x{2013}\\x{2014}]\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = range.match(title);
    if (m.hasMatch()) return {m.captured(1).toInt(), m.captured(2).toInt()};
    static const QRegularExpression single(
        QStringLiteral("vol\\.?\\s*(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    m = single.match(title);
    if (m.hasMatch()) { const int n = m.captured(1).toInt(); return {n, n}; }
    return {0, 0};
}
} // namespace

SearchResult pickPostForVolume(const QString& seriesTitle, int volumeNumber,
                               const QList<SearchResult>& results) {
    SearchResult standalone, range;
    for (const auto& r : results) {
        // identity gate: series tokens must be a subset (rejects "Elektra Saga",
        // "Saga of the Swamp Thing") — reuse identityTokens.
        const QStringList series = identityTokens(seriesTitle);
        const QStringList cand   = identityTokens(r.title);
        const QSet<QString> seriesSet(series.begin(), series.end());
        const QSet<QString> candSet(cand.begin(), cand.end());
        if (!seriesSet.isEmpty() && !candSet.contains(*seriesSet.begin())) {
            // require ALL series tokens present (subset), not exact (range posts
            // carry extra tokens like "book"): check every series token in cand.
        }
        bool identityOk = true;
        for (const QString& t : series) if (!candSet.contains(t)) { identityOk = false; break; }
        if (!identityOk || series.isEmpty()) continue;
        const auto cov = volumeCoverage(r.title);
        if (cov.first == 0) continue;
        if (cov.first == volumeNumber && cov.second == volumeNumber) {
            if (standalone.postUrl.isEmpty()) standalone = r;
        } else if (cov.first <= volumeNumber && volumeNumber <= cov.second) {
            if (range.postUrl.isEmpty()) range = r;
        }
    }
    return !standalone.postUrl.isEmpty() ? standalone : range;
}
```

- [ ] **Step 4: Run tests, verify PASS.** **Step 5: Commit** — `git commit -am "feat(comics): GetComics tag-slug + volume-post selection (agent1)"`

### Task 10: extract a specific volume's download from a post (pure)

**Files:** Modify `GetComicsParse.{h,cpp}`; Test `GetComicsParseTest.cpp`

- [ ] **Step 1: Write the failing test** (use a trimmed real-post fixture — standalone has one link group; range has per-volume `<h3>Series Vol. N ...</h3>` sections each followed by their DOWNLOAD-NOW anchor).

```cpp
TEST(GetComicsParseTest, ExtractVolumeDownload_Standalone) {
    const QString html =
        R"(<h3>Saga Vol. 12 (2025)</h3>)"
        R"(<a href="https://getcomics.org/dls/AAA:sig==">DOWNLOAD NOW</a>)";
    const DownloadLink d = extractVolumeDownload(html, "Saga", 12);
    EXPECT_EQ(d.url, QStringLiteral("https://getcomics.org/dls/AAA:sig=="));
}

TEST(GetComicsParseTest, ExtractVolumeDownload_RangePicksRightSection) {
    const QString html =
        R"(<h3>Saga Vol. 1 (2012)</h3><a href="https://getcomics.org/dls/V1:s==">DOWNLOAD NOW</a>)"
        R"(<h3>Saga Vol. 3 (2014)</h3><a href="https://getcomics.org/dls/V3:s==">DOWNLOAD NOW</a>)"
        R"(<h3>Saga Vol. 10 (2022)</h3><a href="https://getcomics.org/dls/V10:s==">DOWNLOAD NOW</a>)";
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 3).url,
              QStringLiteral("https://getcomics.org/dls/V3:s=="));
}
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement**

```cpp
// .h
DownloadLink extractVolumeDownload(const QString& postHtml,
                                   const QString& seriesTitle, int volumeNumber);
```

```cpp
// .cpp — find the section heading for "Vol. N" and take the FIRST download anchor
// after it (before the next "Vol. M" heading). If no per-volume heading exists
// (true standalone post), fall back to pickBest over the whole post.
DownloadLink extractVolumeDownload(const QString& postHtml,
                                   const QString& seriesTitle, int volumeNumber) {
    static const QRegularExpression head(
        QStringLiteral("vol\\.?\\s*(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    // Find char offset of the heading for this exact volume, and the next heading.
    int start = -1, end = postHtml.size();
    auto it = head.globalMatch(postHtml);
    QList<QPair<int,int>> heads;   // (offset, volNum)
    while (it.hasNext()) { const auto m = it.next(); heads.append({m.capturedStart(), m.captured(1).toInt()}); }
    for (int i = 0; i < heads.size(); ++i) {
        if (heads[i].second == volumeNumber) {
            start = heads[i].first;
            if (i + 1 < heads.size()) end = heads[i + 1].first;
            break;
        }
    }
    if (start < 0) {
        // No per-volume sectioning -> treat as standalone post.
        return pickBest(extractDownloads(postHtml));
    }
    const QString section = postHtml.mid(start, end - start);
    return pickBest(extractDownloads(section));
}
```

- [ ] **Step 4: Run tests, verify PASS.** **Step 5: Commit** — `git commit -am "feat(comics): extract per-volume GetComics download from a post (agent1)"`

### Task 11: resolver becomes volume-aware (tag page → post → volume link)

**Files:** Modify `src/core/manga/GetComicsResolver.{h,cpp}`

- [ ] **Step 1:** Change the signature and flow. `resolve(seriesTitle, volumeNumber, year, tierLabel)`. New flow: fetch `getcomics.org/tag/<tagSlug(seriesTitle)>/` (page 1; if `pickPostForVolume` finds nothing, fetch `/page/2..N` up to 5, then fall back to the existing `/?s=` query loop). On a chosen post, fetch it and assemble `EditionDownload` with `dl.best = extractVolumeDownload(postHtml, seriesTitle, volumeNumber)`.

```cpp
// .h
void resolve(const QString& seriesTitle, int volumeNumber, int year, const QString& tierLabel);
```

```cpp
// .cpp — tag-page first
void GetComicsResolver::resolve(const QString& seriesTitle, int volumeNumber,
                                int year, const QString& tierLabel) {
    const QString slug = getcomics::tagSlug(seriesTitle);
    fetchTagPage(seriesTitle, volumeNumber, slug, 1);   // new helper, see below
}
```

- [ ] **Step 2:** Implement `fetchTagPage(seriesTitle, volumeNumber, slug, page)`:
  - GET `GC_BASE + "/tag/" + slug + (page==1 ? "/" : "/page/" + N + "/")`.
  - `parseSearchResults(html)` → `pickPostForVolume(seriesTitle, volumeNumber, results)`.
  - Match → `fetchPostForVolume(match, seriesTitle, volumeNumber)`; no match & page<5 & results non-empty → next page; else fall back to legacy `tryQuery` (still volume-blind, last-resort) or `resolveFailed`.
- [ ] **Step 3:** `fetchPostForVolume` mirrors `fetchPost` but sets `dl.best = extractVolumeDownload(...)`; emit `resolveFailed("volume not found in post")` if `dl.best.url.isEmpty()`.
- [ ] **Step 4:** Build the test target to ensure it compiles (no new unit test — logic is exercised by the pure fns in Tasks 9–10 + the Phase-5 smoke).
- [ ] **Step 5: Commit** — `git commit -am "feat(comics): volume-aware GetComics resolver via tag page (agent1)"`

### Task 12: downloader passes volumeNumber + names file per-volume

**Files:** Modify `src/core/manga/WesternVolumeDownloader.cpp`

- [ ] **Step 1:** In `pumpResolveQueue()` change `m_resolver.resolve(st.seriesTitle, st.year, st.tierLabel)` → `m_resolver.resolve(st.seriesTitle, rk.volumeNumber, st.year, st.tierLabel)`.
- [ ] **Step 2:** In `tryNextDdlLink`, change the DDL filename from `sanitiseName(seriesTitle) + ".cbz"` to per-volume:

```cpp
    const QString destFile = QDir(destFolder).absoluteFilePath(
        sanitiseName(seriesTitle + QStringLiteral(" Vol ")
                     + QString::number(rk.volumeNumber)) + QStringLiteral(".cbz"));
```

- [ ] **Step 3:** Build the test/app target to confirm it compiles.
- [ ] **Step 4: Commit** — `git commit -am "feat(comics): per-volume GetComics resolve + filename (agent1)"`

---

## Phase 5 — Wire, build, run builder, smoke

### Task 13: ComicsPage passes the volume's own year

**Files:** Modify `src/ui/pages/ComicsPage.cpp:1098-1105`

- [ ] **Step 1:** The lambda already has the volume row context. Source `year` from the clicked volume's record (the schema-v3 per-volume `year`, surfaced via the MangaVolume/row) instead of the series-level `seriesJson["yearStart"]`. If the per-volume year isn't readily available at this call site, pass the series `yearStart` as the fallback (resolver treats year as a hint only). Keep the existing `requestVolume(...)` call shape — only the `year` argument's source changes.
- [ ] **Step 2:** Build-verify (compile) via `build_check.bat` (absolute path, see Build note).
- [ ] **Step 3: Commit** — `git commit -am "feat(comics): pass per-volume year to Western download (agent1)"`

### Task 14: run the builder, regenerate Saga + Invincible

- [ ] **Step 1:** `cd scripts/comics_catalogue && python gcd_harvest.py` (throttled; ~1–2 min/series due to GCD rate-limit). Expect `saga: N TPB volumes`, `invincible: M TPB volumes`.
- [ ] **Step 2:** Eyeball `data/western_catalogue/saga.json` + `invincible.json`: `source:"gcd"`, `schemaVersion:3`, editions FORWARD (volumeNumber 1,2,3…), real titles, ISBNs, OL `coverUrl`s (some may be `""` — expected, title-card).
- [ ] **Step 3: Commit** — `git add data/western_catalogue/saga.json data/western_catalogue/invincible.json && git commit -m "data(comics): GCD+OL catalogue for Saga + Invincible (agent1)"`

### Task 15: full build + smoke

- [ ] **Step 1:** Kill any running instance (`taskkill //F //IM Tankoban.exe`), then `build_and_run.bat` (absolute path via PowerShell call operator; verify `out/Tankoban.exe` mtime advanced — Rule 1 / verify-exe-mtime).
- [ ] **Step 2:** Drive the smoke via tankoctl: `comics-open-western-series` (or open via dev bridge) Invincible → confirm volume rows read **Vol 1 = the first TPB**, real covers paint. Dispatch volume 1 → watch `requestVolume` → tag-page resolve → `comicfiles.ru` stream → a `.cbz` named `Invincible Vol 1.cbz` lands; tile flips to Read.
- [ ] **Step 3:** Repeat for Saga: a standalone-post volume (e.g. Vol 12) AND a range-only volume (e.g. Vol 3) to exercise both paths.
- [ ] **Step 4:** Capture evidence (tankoctl state + the landed file) per `/smoke-package`. `scripts/stop-tankoban.ps1` after.

---

## Build note (C++ tests + app, Windows shared tree)

- Tests: configure with `-DTANKOBAN_BUILD_TESTS=ON`, build target `tankoban_tests`, run `out/tankoban_tests.exe --gtest_filter=<Suite.Case>`. Use a per-lane build dir (`TANKOBAN_BUILD_LANE=agent1`) to avoid the shared-`out/` lock (Rules 19+22).
- App compile check: `build_check.bat` invoked by ABSOLUTE path via PowerShell call operator (`& "C:\...\build_check.bat"`) — bare-name invocation fails in backgrounded shells (verified 2026-06-05). Link step dominates (~915s); batch edits before verifying. Verify `out/Tankoban.exe` mtime advanced — "BUILD OK" lies if the app was running (Rule 1).

## Self-review notes (done)

- **Spec coverage:** GCD brain (Tasks 1–7), OL covers + cover-fallback empty→title-card (Tasks 5,6,8), forward TPB unit (Tasks 3,6,8), volume-aware download incl. range bundles (Tasks 9–12), per-volume file (Task 12), wiring+smoke (13–15). The Saga first-slice end-to-end loop is Task 15.
- **Decoupling:** builder = GCD+OL only (no GetComics); resolver = GetComics only (no GCD) — mirrors manga brain/source split.
- **Back-compat:** loader keeps the v2/rco path; only `source:"gcd"`/`schemaVersion>=3` files take the new branch, so the 11 not-yet-regenerated series don't break mid-transition.
- **Type consistency:** `normalize_isbn13`, `pick_tpb_line`, `parse_descriptor`, `build_volumes_from_issues`, `build_gcd_record`, `verified_cover_url`, `tagSlug`, `pickPostForVolume`, `extractVolumeDownload`, `resolve(seriesTitle,volumeNumber,year,tierLabel)` — names consistent across tasks.
- **Open risk (flagged, not blocking):** the range-post per-volume HTML sectioning (Task 10) is the one piece keyed on live markup; it's TDD'd against a fixture and falls back to `pickBest(whole post)` when no per-volume heading is found. If live Saga range markup differs, adjust the Task-10 fixture from a captured post.
