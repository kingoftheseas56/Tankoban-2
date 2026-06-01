"""Wikipedia REST summary as a synopsis fallback when RCO's 'Summary:' block is
thin or missing. needs_fallback() is pure (unit-tested); fetch_extract() is
network glue (exercised by the harvester run).

See docs/superpowers/specs/2026-06-01-comics-western-richness-design.md (D2).
"""
import json
import urllib.parse
import urllib.request

_REST = "https://en.wikipedia.org/api/rest_v1/page/summary/"
_UA = "TankobanCatalogue/1.0 (comics catalogue; contact: local)"
_MIN_CHARS = 120  # below this, treat the RCO summary as too thin
# Disambiguation suffixes (comic articles), tried in order; bare title last.
_VARIANTS = [" (comics)", " (comic book)", " (comic strip)", ""]


def needs_fallback(rco_summary) -> bool:
    return len((rco_summary or "").strip()) < _MIN_CHARS


def _fetch_one(full_title: str) -> str:
    try:
        slug = urllib.parse.quote(full_title.replace(" ", "_"))
        req = urllib.request.Request(_REST + slug, headers={"User-Agent": _UA})
        with urllib.request.urlopen(req, timeout=15) as r:
            d = json.loads(r.read().decode("utf-8"))
        # Skip disambiguation stubs; we want a real article extract.
        if d.get("type") == "disambiguation":
            return ""
        return d.get("extract", "") or ""
    except Exception:
        return ""


def fetch_extract(title: str) -> str:
    """Best-effort Wikipedia overview for a comic `title`, preferring the
    disambiguated article ('Saga (comics)') over the bare title. '' on miss."""
    for suffix in _VARIANTS:
        text = _fetch_one(title + suffix)
        if text:
            return text
    return ""
