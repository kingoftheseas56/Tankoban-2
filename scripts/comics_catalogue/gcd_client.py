"""GCD (Grand Comics Database) client for the Western catalogue brain.

Pure selection/parsing logic (no network) + a thin throttled live-fetch wrapper.
The brain knows nothing about GetComics — it only turns GCD into forward-ordered
TPB volumes with ISBNs. See docs/superpowers/plans/2026-06-05-comics-western-
gcd-catalogue-and-volume-aware-download.md.

Ground truth (verified live 2026-06-05):
- A series' collected-TPB line is a SEPARATE GCD series whose name EXACTLY equals
  the canonical title (en/us), publishing_format contains "Collected" (or binding
  is softcover/trade paperback, never saddle-stitched). Exact-name kills noise
  ("Invincible Iron Man", "Chakra the Invincible").
- Each TPB issue's descriptor is "N - Title" -> (volumeNumber, title).
- GCD throttles hard (429) after a burst -> sleep + back off. Offline build only.
"""
import json
import re
import time
import urllib.error
import urllib.request
import urllib.parse

_UA = "Tankoban/1.0 (dev catalogue builder; contact dev@tankoban.local)"
_THROTTLE_S = 5.0   # GCD throttles hard; offline dev build, so be patient/polite


# ── ISBN ─────────────────────────────────────────────────────────────────────

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


# ── TPB-line selection ───────────────────────────────────────────────────────

def _is_collected_line(r: dict) -> bool:
    fmt = (r.get("publishing_format") or "").lower()
    binding = (r.get("binding") or "").lower()
    if "saddle" in binding:
        return False
    return ("collect" in fmt) or ("trade paperback" in binding) or ("softcover" in binding)


def pick_tpb_line(rows: list, canonical_title: str):
    """From GCD series results, return the collected/TPB line whose name EXACTLY
    equals the canonical title (en/us), preferring the one with the most issues.
    None if no exact-name collected line exists."""
    exact = [r for r in rows
             if (r.get("name") or "").strip().lower() == canonical_title.strip().lower()
             and r.get("language") == "en" and r.get("country") == "us"]
    cands = [r for r in exact if _is_collected_line(r)]
    cands.sort(key=lambda r: len(r.get("active_issues") or []), reverse=True)
    return cands[0] if cands else None


# ── descriptor / volume assembly ─────────────────────────────────────────────

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


def build_volumes_from_issues(issues: list) -> list:
    """Map TPB-line issues -> forward-ordered, deduped volume dicts. Keeps the
    earliest printing per volume number (prefers one carrying an ISBN on ties)."""
    by_num = {}
    for iss in issues:
        # Primary: descriptor "N - Title" (Invincible shape). Fallback: a bare
        # numeric `number`/`descriptor` with the subtitle in a separate `title`
        # field (Saga shape) -> use number as the volume, title as the subtitle.
        num, title = parse_descriptor(iss.get("descriptor") or "")
        if num is None:
            nstr = str(iss.get("number") or iss.get("descriptor") or "").strip()
            if nstr.isdigit():
                num = int(nstr)
                title = (iss.get("title") or "").strip()
        if num is None:
            continue
        year = _year_of(iss.get("publication_date", ""))
        isbn = normalize_isbn13(iss.get("isbn") or "")
        cur = by_num.get(num)
        if cur is None \
                or (year and (cur["year"] == 0 or year < cur["year"])) \
                or (not cur["isbn"] and isbn):
            by_num[num] = {"volumeNumber": num, "title": title, "isbn": isbn, "year": year}
    return [by_num[n] for n in sorted(by_num)]


# ── live fetch (throttled, 429-backoff) ──────────────────────────────────────

def _get_json(url: str, tries: int = 6) -> dict:
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": _UA})
            with urllib.request.urlopen(req, timeout=40) as r:
                return json.loads(r.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as e:
            if e.code == 429 and i < tries - 1:
                time.sleep(15 * (i + 1))
                continue
            raise


def fetch_all_series(name: str, max_pages: int = 12) -> list:
    out, pages = [], 0
    url = f"https://www.comics.org/api/series/name/{urllib.parse.quote(name)}/?format=json"
    while url and pages < max_pages:
        data = _get_json(url)
        out.extend(data.get("results", []))
        url = data.get("next")
        pages += 1
        if url:
            time.sleep(_THROTTLE_S)
    return out


def fetch_issue(api_url: str) -> dict:
    time.sleep(_THROTTLE_S)
    return _get_json(api_url)
