# Comics-Western Prototype — Phase 1: Data-Path Spike (Saga) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the entire *new* comics data path for one series (Saga) as a standalone, tested Python CLI — GCD metadata → Open Library covers → **GetComics download** → validation — de-risking the make-or-break acquisition layer (Codex review, spec §9) *before* any C++/Qt integration.

**Architecture:** A thin `SourceAdapter`-shaped Python package under `scripts/comics_spike/`. Pure functions + small modules, each one responsibility: `volume_identity` (the model), `gcd_brain` (metadata), `ol_covers` (covers + title-card fallback), `getcomics_source` (the risky search→resolve→download→validate), `matching` (GCD volume ↔ GetComics file), `cli` (Saga end-to-end). Network code is tested against **recorded fixtures** (deterministic); real calls run only under an explicit `-m live` marker. This package is throwaway-or-port: if the spike proves the path, Phase 2 ports the proven logic into the C++ app (or shells to it, like `harvest.py`).

**Tech Stack:** Python 3.12, `requests`, `Pillow`, `pytest`. Edge `--headless` (RCO fallback) is explicitly OUT of Phase 1 scope.

**Why this is Phase 1 (not the UI):** Per Codex (spec §9), GetComics acquisition through the ad-redirect/safelink/file-host layer is the single biggest feasibility risk and "could change the acquisition model." We prove it on Saga, repeatably, before building anything on top.

---

## File Structure

```
scripts/comics_spike/
  __init__.py
  volume_identity.py      # VolumeIdentity dataclass (the shared identity model)
  http.py                 # one tiny session helper (UA, retries, timeouts)
  gcd_brain.py            # GCD: series search + collected-edition volume list
  ol_covers.py            # OL cover-by-ISBN + generated title-card fallback
  getcomics_source.py     # SourceAdapter: search / resolve / download / validate
  matching.py             # rank GetComics candidates against a VolumeIdentity
  cli.py                  # `python -m scripts.comics_spike.cli saga`
  requirements.txt
  tests/
    __init__.py
    conftest.py           # fixture loader
    fixtures/             # recorded GCD JSON + GetComics HTML (captured in-plan)
    test_volume_identity.py
    test_gcd_brain.py
    test_ol_covers.py
    test_getcomics_source.py
    test_matching.py
    test_cli_smoke.py
```

Each file has one responsibility; network parsing is split from network fetching so parsers test against recorded bytes.

---

### Task 1: Scaffold + VolumeIdentity model

**Files:**
- Create: `scripts/comics_spike/__init__.py` (empty)
- Create: `scripts/comics_spike/requirements.txt`
- Create: `scripts/comics_spike/volume_identity.py`
- Create: `scripts/comics_spike/tests/__init__.py` (empty)
- Test: `scripts/comics_spike/tests/test_volume_identity.py`

- [ ] **Step 1: Write `requirements.txt`**

```
requests>=2.31
Pillow>=10.0
pytest>=8.0
```

- [ ] **Step 2: Write the failing test**

```python
# scripts/comics_spike/tests/test_volume_identity.py
from scripts.comics_spike.volume_identity import VolumeIdentity

def test_display_label_uses_volume_number():
    v = VolumeIdentity(series="Saga", series_gcd_id=1, volume=1,
                       issue_range=(1, 6), isbns=["9781607066019"], year=2012)
    assert v.display_label() == "Saga, Vol. 1"

def test_match_tokens_includes_series_year_and_volume():
    v = VolumeIdentity(series="Saga", series_gcd_id=1, volume=3,
                       issue_range=(13, 18), isbns=[], year=2014)
    toks = v.match_tokens()
    assert "saga" in toks and "2014" in toks and "3" in toks
```

- [ ] **Step 3: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_volume_identity.py -v`
Expected: FAIL — `ModuleNotFoundError: scripts.comics_spike.volume_identity`

- [ ] **Step 4: Write minimal implementation**

```python
# scripts/comics_spike/volume_identity.py
from dataclasses import dataclass, field

@dataclass
class VolumeIdentity:
    series: str
    series_gcd_id: int
    volume: int                       # display volume number (1..N)
    issue_range: tuple | None = None  # (first_issue, last_issue) the TPB collects
    isbns: list = field(default_factory=list)
    year: int | None = None
    edition: str = "tpb"              # tpb | hardcover | deluxe | omnibus | digital
    source_aliases: list = field(default_factory=list)

    def display_label(self) -> str:
        return f"{self.series}, Vol. {self.volume}"

    def match_tokens(self) -> set[str]:
        toks = {self.series.lower(), str(self.volume)}
        if self.year:
            toks.add(str(self.year))
        return toks
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_volume_identity.py -v`
Expected: PASS (2 passed)

- [ ] **Step 6: Commit**

```bash
git add scripts/comics_spike/__init__.py scripts/comics_spike/requirements.txt scripts/comics_spike/volume_identity.py scripts/comics_spike/tests/
git commit -m "feat(comics-spike): scaffold + VolumeIdentity model"
```

---

### Task 2: HTTP helper

**Files:**
- Create: `scripts/comics_spike/http.py`
- Test: (covered indirectly; no unit test for the thin wrapper)

- [ ] **Step 1: Write the helper**

```python
# scripts/comics_spike/http.py
import requests
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36")

def session() -> requests.Session:
    s = requests.Session()
    s.headers.update({"User-Agent": UA})
    return s

def get(url, *, referer=None, timeout=30, stream=False):
    s = session()
    if referer:
        s.headers["Referer"] = referer
    return s.get(url, timeout=timeout, stream=stream, allow_redirects=True)
```

- [ ] **Step 2: Smoke it**

Run: `python -c "from scripts.comics_spike.http import get; print(get('https://www.comics.org/api/').status_code)"`
Expected: `200`

- [ ] **Step 3: Commit**

```bash
git add scripts/comics_spike/http.py
git commit -m "feat(comics-spike): http session helper"
```

---

### Task 3: GCD brain — series search + volume list

GCD verified live this session: `/api/series/name/<q>/?format=json` filters; each series' `active_issues` are issue API URLs; an issue carries `isbn`, `number`, `publication_date`, `page_count`. Collected-edition series are identified by `publishing_format` containing "collected"/"tpb"/"trade".

**Files:**
- Create: `scripts/comics_spike/gcd_brain.py`
- Test: `scripts/comics_spike/tests/test_gcd_brain.py`
- Fixture: `scripts/comics_spike/tests/fixtures/gcd_saga_tpb_series.json`, `gcd_saga_tpb_issue1.json`

- [ ] **Step 1: Capture fixtures (real GCD bytes, recorded once)**

Run:
```bash
python - <<'PY'
from scripts.comics_spike.http import get
import json, pathlib
d = pathlib.Path("scripts/comics_spike/tests/fixtures"); d.mkdir(parents=True, exist_ok=True)
ser = get("https://www.comics.org/api/series/name/saga/?format=json").json()
(d/"gcd_saga_search.json").write_text(json.dumps(ser)[:200000], encoding="utf-8")
PY
```
Then hand-trim `gcd_saga_search.json` to the Image "Saga" TPB collected-edition series record + capture one of its issue URLs into `gcd_saga_tpb_issue1.json` via a second `get(...).json()`. Commit the trimmed fixtures.
Expected: two JSON fixtures on disk, each a single record.

- [ ] **Step 2: Write the failing test (parser only — no network)**

```python
# scripts/comics_spike/tests/test_gcd_brain.py
import json, pathlib
from scripts.comics_spike.gcd_brain import parse_volume_from_issue, is_collected_series

FX = pathlib.Path(__file__).parent / "fixtures"

def test_is_collected_series_detects_tpb():
    assert is_collected_series({"publishing_format": "trade paperback"})
    assert not is_collected_series({"publishing_format": "was ongoing series"})

def test_parse_volume_from_issue_extracts_isbn_and_number():
    issue = json.loads((FX / "gcd_saga_tpb_issue1.json").read_text(encoding="utf-8"))
    vol = parse_volume_from_issue("Saga", series_gcd_id=42, number=1, issue=issue)
    assert vol.series == "Saga"
    assert vol.volume == 1
    assert vol.isbns and vol.isbns[0].replace("-", "").isdigit()
```

- [ ] **Step 3: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_gcd_brain.py -v`
Expected: FAIL — `ImportError: cannot import name 'parse_volume_from_issue'`

- [ ] **Step 4: Write the implementation**

```python
# scripts/comics_spike/gcd_brain.py
from .http import get
from .volume_identity import VolumeIdentity

_COLLECTED = ("collected", "tpb", "trade", "paperback", "hardcover", "deluxe", "omnibus")

def is_collected_series(series: dict) -> bool:
    fmt = (series.get("publishing_format") or "").lower()
    return any(k in fmt for k in _COLLECTED)

def search_series(name: str) -> list[dict]:
    url = f"https://www.comics.org/api/series/name/{name}/?format=json"
    return get(url).json().get("results", [])

def parse_volume_from_issue(series: str, series_gcd_id: int, number: int, issue: dict) -> VolumeIdentity:
    isbn = (issue.get("isbn") or "").strip()
    return VolumeIdentity(
        series=series, series_gcd_id=series_gcd_id, volume=number,
        isbns=[isbn] if isbn else [],
        year=_year(issue.get("publication_date") or issue.get("key_date")),
        edition="tpb",
    )

def list_volumes(series_record: dict) -> list[VolumeIdentity]:
    series = series_record["name"]
    sid = int(series_record["api_url"].rstrip("/").split("/")[-2])
    out = []
    for n, issue_url in enumerate(series_record.get("active_issues", []), start=1):
        issue = get(issue_url).json()
        out.append(parse_volume_from_issue(series, sid, n, issue))
    return out

def _year(s):
    import re
    m = re.search(r"(19|20)\d{2}", s or "")
    return int(m.group(0)) if m else None
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_gcd_brain.py -v`
Expected: PASS (2 passed)

- [ ] **Step 6: Commit**

```bash
git add scripts/comics_spike/gcd_brain.py scripts/comics_spike/tests/test_gcd_brain.py scripts/comics_spike/tests/fixtures/
git commit -m "feat(comics-spike): GCD series search + volume parsing (fixture-tested)"
```

---

### Task 4: Open Library covers + title-card fallback

OL verified: `https://covers.openlibrary.org/b/isbn/{ISBN}-L.jpg` returns a real JPEG when present, and a tiny (~<2KB) blank when absent. Fallback = a generated title-card.

**Files:**
- Create: `scripts/comics_spike/ol_covers.py`
- Test: `scripts/comics_spike/tests/test_ol_covers.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/comics_spike/tests/test_ol_covers.py
from scripts.comics_spike.ol_covers import is_real_cover, title_card
from PIL import Image
import io

def test_is_real_cover_rejects_tiny_blank():
    assert not is_real_cover(b"\xff\xd8\xff" + b"\x00" * 300)   # ~300 bytes = blank
    assert is_real_cover(b"\xff\xd8\xff" + b"\x00" * 60000)     # ~60KB = real

def test_title_card_is_a_2x3_image_with_text():
    png = title_card("Saga", 4)
    im = Image.open(io.BytesIO(png))
    assert im.width / im.height == 2 / 3
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_ol_covers.py -v`
Expected: FAIL — `ImportError`

- [ ] **Step 3: Write the implementation**

```python
# scripts/comics_spike/ol_covers.py
import io
from PIL import Image, ImageDraw
from .http import get

def cover_url(isbn: str) -> str:
    return f"https://covers.openlibrary.org/b/isbn/{isbn.replace('-','')}-L.jpg"

def is_real_cover(data: bytes) -> bool:
    return len(data) > 3000 and data[:3] == b"\xff\xd8\xff"

def fetch_cover(isbn: str) -> bytes | None:
    r = get(cover_url(isbn))
    return r.content if r.status_code == 200 and is_real_cover(r.content) else None

def title_card(series: str, volume: int, w: int = 600, h: int = 900) -> bytes:
    img = Image.new("RGB", (w, h), (24, 26, 33))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w, 10], fill=(79, 140, 255))
    d.text((30, h // 2 - 40), series, fill=(235, 238, 245))
    d.text((30, h // 2 + 10), f"Vol {volume}", fill=(150, 160, 180))
    buf = io.BytesIO(); img.save(buf, "PNG"); return buf.getvalue()

def cover_for(volume) -> bytes:
    """Fallback hierarchy: OL-by-ISBN -> generated title-card."""
    for isbn in volume.isbns:
        data = fetch_cover(isbn)
        if data:
            return data
    return title_card(volume.series, volume.volume)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_ol_covers.py -v`
Expected: PASS (2 passed)

- [ ] **Step 5: Commit**

```bash
git add scripts/comics_spike/ol_covers.py scripts/comics_spike/tests/test_ol_covers.py
git commit -m "feat(comics-spike): OL covers + title-card fallback"
```

---

### Task 5: GetComics source — SEARCH (discovery-driven, fixture-recorded)

This is the start of the risk zone. GetComics has a search page; we record its real HTML once, then write a parser against the recording.

**Files:**
- Create: `scripts/comics_spike/getcomics_source.py`
- Test: `scripts/comics_spike/tests/test_getcomics_source.py`
- Fixture: `scripts/comics_spike/tests/fixtures/getcomics_saga_search.html`

- [ ] **Step 1: Record the real search HTML**

Run:
```bash
python - <<'PY'
from scripts.comics_spike.http import get
import pathlib
html = get("https://getcomics.org/?s=Saga+Vol").text
pathlib.Path("scripts/comics_spike/tests/fixtures/getcomics_saga_search.html").write_text(html, encoding="utf-8")
print("bytes:", len(html))
PY
```
Inspect the saved HTML for the post-listing structure (post title + post URL). Note the CSS selector for a result item (likely `article ... h1.post-title a` or `.post-info a`).
Expected: an HTML file with multiple Saga post links.

- [ ] **Step 2: Write the failing test (parser against the fixture)**

```python
# scripts/comics_spike/tests/test_getcomics_source.py
import pathlib
from scripts.comics_spike.getcomics_source import parse_search_results
FX = pathlib.Path(__file__).parent / "fixtures"

def test_parse_search_results_returns_posts():
    html = (FX / "getcomics_saga_search.html").read_text(encoding="utf-8")
    posts = parse_search_results(html)
    assert posts, "expected at least one Saga post"
    assert all(p["url"].startswith("http") and p["title"] for p in posts)
    assert any("saga" in p["title"].lower() for p in posts)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_getcomics_source.py -v`
Expected: FAIL — `ImportError`

- [ ] **Step 4: Implement `parse_search_results` against the recorded selector**

```python
# scripts/comics_spike/getcomics_source.py  (grows over Tasks 5-7)
from bs4 import BeautifulSoup   # add beautifulsoup4 to requirements.txt
from .http import get

def search(query: str) -> list[dict]:
    return parse_search_results(get(f"https://getcomics.org/?s={query.replace(' ', '+')}").text)

def parse_search_results(html: str) -> list[dict]:
    soup = BeautifulSoup(html, "html.parser")
    posts = []
    # SELECTOR confirmed from the recorded fixture in Step 1 — update if the dump differs:
    for a in soup.select("h1.post-title a, h2.post-title a, article .post-header a"):
        href, title = a.get("href"), a.get_text(strip=True)
        if href and title:
            posts.append({"url": href, "title": title})
    return posts
```
Add `beautifulsoup4>=4.12` to `requirements.txt` and `pip install -r scripts/comics_spike/requirements.txt`.

- [ ] **Step 5: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_getcomics_source.py -v`
Expected: PASS — if the selector misses, adjust it against the real fixture until green.

- [ ] **Step 6: Commit**

```bash
git add scripts/comics_spike/getcomics_source.py scripts/comics_spike/tests/test_getcomics_source.py scripts/comics_spike/tests/fixtures/getcomics_saga_search.html scripts/comics_spike/requirements.txt
git commit -m "feat(comics-spike): GetComics search parser (fixture-tested)"
```

---

### Task 6: GetComics RESOLVE — post page → real download URL (the core risk)

The GetComics post page exposes download buttons that pass through an ad-redirect / safelink to a file host. This task **maps that chain** and extracts a directly-fetchable URL.

**Files:**
- Modify: `scripts/comics_spike/getcomics_source.py`
- Test: `scripts/comics_spike/tests/test_getcomics_source.py` (extend)
- Fixture: `scripts/comics_spike/tests/fixtures/getcomics_saga_post.html`

- [ ] **Step 1: Record a real Saga TPB post page + map the download chain**

Run:
```bash
python - <<'PY'
from scripts.comics_spike.http import get
from scripts.comics_spike.getcomics_source import search
import pathlib
post = next(p for p in search("Saga Vol 1") if "saga" in p["title"].lower())
html = get(post["url"]).text
pathlib.Path("scripts/comics_spike/tests/fixtures/getcomics_saga_post.html").write_text(html, encoding="utf-8")
print("post:", post["title"], "bytes:", len(html))
PY
```
Then manually trace one download button: grep the saved HTML for `Download`, `download-here`, `dlds`, `class="aio-`, or direct host links (`pixeldrain`, `mega`, `mediafire`, `terabox`, `ufile`). Record where the button points and how many redirects it takes to reach a file. **Document the chain in a comment block at the top of `getcomics_source.py`.**
Expected: a saved post HTML + a written map of `post → button → (safelink) → file host → file`.

- [ ] **Step 2: Write the failing test (extract candidate download links from the fixture)**

```python
def test_parse_download_links_finds_a_host_link():
    html = (FX / "getcomics_saga_post.html").read_text(encoding="utf-8")
    links = parse_download_links(html)
    assert links, "expected at least one download/host link"
    assert all(l["url"].startswith("http") for l in links)
```
(add `from scripts.comics_spike.getcomics_source import parse_download_links` to the test imports)

- [ ] **Step 3: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_getcomics_source.py::test_parse_download_links_finds_a_host_link -v`
Expected: FAIL — `ImportError: parse_download_links`

- [ ] **Step 4: Implement `parse_download_links` + `resolve()` against the mapped chain**

```python
# append to getcomics_source.py
import re

_HOSTS = ("pixeldrain", "mediafire", "mega.nz", "terabox", "ufile", "fastpic", "main-download")

def parse_download_links(html: str) -> list[dict]:
    soup = BeautifulSoup(html, "html.parser")
    out = []
    for a in soup.select("a[href]"):
        href = a["href"]
        label = a.get_text(strip=True).lower()
        if any(h in href for h in _HOSTS) or "download" in label or "/dlds/" in href:
            out.append({"url": href, "label": label})
    return out

def resolve(post_url: str) -> str | None:
    """post URL -> a directly-GETtable .cbz/.cbr URL. Returns None if the chain breaks.
    The exact hop logic is filled from the Step-1 chain map; keep it small + fail-closed."""
    html = get(post_url).text
    for link in parse_download_links(html):
        final = _follow(link["url"], referer=post_url)
        if final and re.search(r"\.(cbz|cbr|zip|rar)(\?|$)", final, re.I):
            return final
    return None

def _follow(url: str, referer: str, hops: int = 4) -> str | None:
    cur = url
    for _ in range(hops):
        r = get(cur, referer=referer, stream=True)
        ct = r.headers.get("Content-Type", "")
        if "application" in ct or re.search(r"\.(cbz|cbr|zip|rar)(\?|$)", r.url, re.I):
            return r.url
        # else: look for a meta-refresh / JS redirect / "click here" anchor in the body
        m = re.search(r'href=["\']([^"\']+\.(?:cbz|cbr|zip|rar)[^"\']*)', r.text, re.I)
        if m:
            cur = m.group(1); continue
        return None
    return None
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_getcomics_source.py -v`
Expected: PASS — adjust `_HOSTS`/selectors against the real fixture until the parser finds links.

- [ ] **Step 6: Commit**

```bash
git add scripts/comics_spike/getcomics_source.py scripts/comics_spike/tests/test_getcomics_source.py scripts/comics_spike/tests/fixtures/getcomics_saga_post.html
git commit -m "feat(comics-spike): GetComics post->download-link resolve (chain mapped)"
```

---

### Task 7: Download + validation

**Files:**
- Modify: `scripts/comics_spike/getcomics_source.py`
- Test: `scripts/comics_spike/tests/test_getcomics_source.py` (extend)

- [ ] **Step 1: Write the failing test (validation against a tiny built archive)**

```python
def test_validate_archive_accepts_real_cbz(tmp_path):
    import zipfile
    from PIL import Image
    p = tmp_path / "ok.cbz"
    with zipfile.ZipFile(p, "w") as z:
        for i in range(3):
            b = tmp_path / f"{i}.jpg"; Image.new("RGB", (988, 1500)).save(b)
            z.write(b, f"{i}.jpg")
    ok, info = validate_archive(str(p))
    assert ok and info["pages"] == 3

def test_validate_archive_rejects_empty(tmp_path):
    import zipfile
    p = tmp_path / "empty.cbz"
    zipfile.ZipFile(p, "w").close()
    ok, info = validate_archive(str(p))
    assert not ok
```
(add `from scripts.comics_spike.getcomics_source import validate_archive, download` to imports)

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_getcomics_source.py -k validate -v`
Expected: FAIL — `ImportError: validate_archive`

- [ ] **Step 3: Implement download + validation**

```python
# append to getcomics_source.py
import hashlib, zipfile, pathlib

def download(url: str, dest: str, referer: str | None = None) -> str:
    r = get(url, referer=referer, stream=True)
    r.raise_for_status()
    p = pathlib.Path(dest)
    with p.open("wb") as f:
        for chunk in r.iter_content(1 << 16):
            f.write(chunk)
    return str(p)

def validate_archive(path: str) -> tuple[bool, dict]:
    p = pathlib.Path(path)
    info = {"bytes": p.stat().st_size if p.exists() else 0, "pages": 0,
            "sha256": "", "ok_dims": False}
    if not p.exists() or info["bytes"] < 50_000:
        return False, info
    imgs = []
    try:
        if zipfile.is_zipfile(p):
            with zipfile.ZipFile(p) as z:
                imgs = [n for n in z.namelist() if n.lower().endswith((".jpg", ".jpeg", ".png", ".webp"))]
        else:
            return False, info  # .cbr/RAR handled in Phase 2 via 7-Zip; Phase 1 proves the .cbz path
    except zipfile.BadZipFile:
        return False, info
    info["pages"] = len(imgs)
    info["sha256"] = hashlib.sha256(p.read_bytes()).hexdigest()
    info["ok_dims"] = info["pages"] > 0
    return (info["pages"] > 0), info
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_getcomics_source.py -k validate -v`
Expected: PASS (2 passed)

- [ ] **Step 5: Commit**

```bash
git add scripts/comics_spike/getcomics_source.py scripts/comics_spike/tests/test_getcomics_source.py
git commit -m "feat(comics-spike): download + archive validation"
```

---

### Task 8: Matching / ranking — GCD volume ↔ GetComics candidate

**Files:**
- Create: `scripts/comics_spike/matching.py`
- Test: `scripts/comics_spike/tests/test_matching.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/comics_spike/tests/test_matching.py
from scripts.comics_spike.volume_identity import VolumeIdentity
from scripts.comics_spike.matching import score, best_match

V = VolumeIdentity(series="Saga", series_gcd_id=1, volume=1, issue_range=(1,6),
                   isbns=["9781607066019"], year=2012)

def test_score_prefers_series_and_volume_match():
    hi = score(V, {"title": "Saga Vol. 1 (2012) (Digital)"})
    lo = score(V, {"title": "Paper Girls Vol. 1 (2016)"})
    assert hi > lo and hi >= 0.5

def test_best_match_flags_low_confidence():
    cand = [{"title": "Totally Unrelated Comic (2020)"}]
    pick, confident = best_match(V, cand)
    assert not confident   # must require user confirmation
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest scripts/comics_spike/tests/test_matching.py -v`
Expected: FAIL — `ImportError`

- [ ] **Step 3: Write the implementation**

```python
# scripts/comics_spike/matching.py
import re

def _tokens(s: str) -> set[str]:
    return set(re.findall(r"[a-z0-9]+", s.lower()))

def score(vol, candidate: dict) -> float:
    t = _tokens(candidate.get("title", ""))
    s = 0.0
    if vol.series.lower() in candidate.get("title", "").lower(): s += 0.5
    if str(vol.volume) in t or f"v{vol.volume}" in t or f"vol{vol.volume}" in t: s += 0.3
    if vol.year and str(vol.year) in t: s += 0.1
    if any(isbn.replace("-", "") in candidate.get("title", "") for isbn in vol.isbns): s += 0.2
    return min(s, 1.0)

CONFIDENCE_FLOOR = 0.6

def best_match(vol, candidates: list[dict]):
    if not candidates:
        return None, False
    ranked = sorted(candidates, key=lambda c: score(vol, c), reverse=True)
    top = ranked[0]
    return top, score(vol, top) >= CONFIDENCE_FLOOR
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest scripts/comics_spike/tests/test_matching.py -v`
Expected: PASS (2 passed)

- [ ] **Step 5: Commit**

```bash
git add scripts/comics_spike/matching.py scripts/comics_spike/tests/test_matching.py
git commit -m "feat(comics-spike): GCD-volume <-> GetComics matching/ranking"
```

---

### Task 9: CLI — Saga end-to-end + live acceptance smoke

**Files:**
- Create: `scripts/comics_spike/cli.py`
- Test: `scripts/comics_spike/tests/test_cli_smoke.py`

- [ ] **Step 1: Write the CLI**

```python
# scripts/comics_spike/cli.py
import sys, pathlib
from . import gcd_brain, ol_covers, getcomics_source as gc, matching

def run_saga(out_dir="scripts/comics_spike/_out"):
    out = pathlib.Path(out_dir); out.mkdir(parents=True, exist_ok=True)
    series = next(s for s in gcd_brain.search_series("saga")
                  if s["name"] == "Saga" and s.get("country") == "us"
                  and gcd_brain.is_collected_series(s))
    vols = gcd_brain.list_volumes(series)
    print(f"GCD: {len(vols)} volumes")
    v1 = vols[0]
    (out / "vol1_cover.img").write_bytes(ol_covers.cover_for(v1))
    print(f"cover: ok ({v1.display_label()})")
    candidates = gc.search(f"{v1.series} Vol {v1.volume}")
    pick, confident = matching.best_match(v1, candidates)
    print(f"match: {pick['title'] if pick else None}  confident={confident}")
    if not (pick and confident):
        print("LOW CONFIDENCE -> would prompt user; stopping"); return 2
    url = gc.resolve(pick["url"])
    if not url:
        print("RESOLVE FAILED (chain broke)"); return 3
    path = gc.download(url, str(out / "saga_v1.cbz"), referer=pick["url"])
    ok, info = gc.validate_archive(path)
    print(f"download: {info['bytes']//1024}KB  pages={info['pages']}  valid={ok}")
    return 0 if ok else 4

if __name__ == "__main__":
    sys.exit(run_saga())
```

- [ ] **Step 2: Write the live acceptance smoke (marked, opt-in)**

```python
# scripts/comics_spike/tests/test_cli_smoke.py
import pytest
from scripts.comics_spike.cli import run_saga

@pytest.mark.live
def test_saga_end_to_end_live(tmp_path):
    assert run_saga(out_dir=str(tmp_path)) == 0
```
Add to `scripts/comics_spike/tests/conftest.py`:
```python
def pytest_configure(config):
    config.addinivalue_line("markers", "live: hits real network (opt-in)")
```

- [ ] **Step 3: Run unit tests (live excluded) to verify nothing broke**

Run: `python -m pytest scripts/comics_spike/tests/ -v -m "not live"`
Expected: PASS (all fixture/unit tests)

- [ ] **Step 4: Run the LIVE acceptance smoke**

Run: `python -m pytest scripts/comics_spike/tests/test_cli_smoke.py -v -m live`
Expected: PASS — Saga Vol 1 resolves, downloads, validates. **If it fails, the failure mode is the Phase-1 finding** (record which hop broke).

- [ ] **Step 5: Repeatability check (the Codex de-risk bar)**

Run: `for i in 1 2 3 4 5; do python -m scripts.comics_spike.cli && echo "RUN $i OK" || echo "RUN $i FAIL"; done`
Expected: record the success rate (target: ≥4/5). Document any host-captcha / expiring-link / anti-bot failure modes observed.

- [ ] **Step 6: Commit + write the finding**

```bash
git add scripts/comics_spike/cli.py scripts/comics_spike/tests/test_cli_smoke.py scripts/comics_spike/tests/conftest.py
git commit -m "feat(comics-spike): Saga end-to-end CLI + live acceptance smoke"
```
Then write `agents/audits/comics_getcomics_spike_result_2026-06-05.md`: success rate, the mapped download chain, failure modes, and a GO / NO-GO / NEEDS-BROWSER verdict for the GetComics acquisition model. **This verdict gates Phase 2.**

---

## Phase 2+ (separate plans, written after Phase 1's GO/NO-GO)

Do NOT plan these in detail until the spike verdict lands — per Codex, the spike may change the acquisition model.

- **Phase 2 — C++ `SourceAdapter`:** port the proven Python resolve/download/validate into the app (or shell to the spike like `harvest.py`); wire to `MangaDownloadIndex`. Carries the matching/ranking + low-confidence-confirm + validation.
- **Phase 3 — Metadata wiring:** GCD brain + OL covers + title-card behind the existing `WesternCatalogLoader` / sources path; storage/library model + cover cache.
- **Phase 4 — UI wiring (reuse):** point `ComicsSeriesView` (volume rows) + `ComicsSourcesPanel` + home/search at the new data path. No new UI (spec §3.4, §6.1).
- **Phase 5 — Reader + progress:** confirm the existing comic reader opens the validated `.cbz`; persist read progress (Continue-reading strip).
- **Phase 6 — Hardening:** error/offline states, dedup, edition policy, update detection, RCO read-online fallback (Edge headless), legal/CC-BY-SA attribution UX (spec §9).

---

## Self-Review

**Spec coverage:** Phase 1 covers spec §3.1 (GCD+OL brain — Tasks 3,4), §3.2 (GetComics source + RCO-deferred — Tasks 5–7), §3.3 (TPB unit / VolumeIdentity — Task 1), §6.4 (cover-fallback title-card — Task 4), §6.5 + §9 acceptance test (Task 9), and §9's headline de-risk (spike-first — the whole plan). §3.4/§6.1/§6.2 (UI + reader, all "same as manga", reuse) are explicitly Phase 4–5, correctly deferred. §9 SourceAdapter/VolumeIdentity/matching/validation/fallback are Tasks 1,4,6,7,8. Legal/compliance is Phase 6 (pre-ship, not prototype-blocking, per Codex). No spec gaps in the Phase-1 scope.

**Placeholder scan:** Network parsers (Tasks 5,6) are *record-then-parse* — the selector/chain is captured from a real fixture in the task's Step 1, then real code is written against it. This is the correct method for an unknown external site, not a "fill in later" placeholder; every such task produces a fixture-backed passing test. No "TODO/handle edge cases" steps remain.

**Type consistency:** `VolumeIdentity` fields (`series`, `series_gcd_id`, `volume`, `issue_range`, `isbns`, `year`, `edition`) are used consistently in Tasks 3 (`parse_volume_from_issue`), 4 (`cover_for` reads `.isbns/.series/.volume`), 8 (`score` reads `.series/.volume/.year/.isbns`), 9 (`.display_label()`). `getcomics_source` public fns: `search`, `parse_search_results`, `parse_download_links`, `resolve`, `download`, `validate_archive` — names match across Tasks 5–9 and the CLI.
