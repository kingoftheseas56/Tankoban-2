"""Parse an RCO (rcostation.xyz) series page into its list of items.

Recon (RECON_FINDINGS.md): RCO catalogue pages are reachable on plain HTTP at
`https://rcostation.xyz/Comic/<Series-Name>` (capital C). Each readable item is
an `<a href="/Comic/<Series>/<Item>?id=N">` link; every item appears twice on
the page (cover thumbnail link + text link), so we dedupe by href. The item's
human label is the last path segment with dashes -> spaces (RCO exposes no
structured per-item field — confirmed by two research reports).

This parser only extracts the item LIST. Reader-page scans are obfuscated
(21wiz.com/s.js) and are NOT used — downloads come from GetComics.
"""
import html as _html
import re

# Match item links: /Comic/<Series>/<Item> with optional ?id=... query.
_ITEM = re.compile(r'href="(/Comic/[^"/]+/[^"?]+)(?:\?[^"]*)?"')

# Any run of whitespace OR dash-family chars -> a single normal space. Covers
# regular '-', the non-breaking space \xa0 RCO slugs sometimes carry, en/em
# dashes, and stray unicode whitespace, so labels match cleanly against
# GetComics titles downstream.
_SEP = re.compile(r"[\s \-‐-―]+")


def slug_to_label(href: str) -> str:
    seg = href.rstrip("/").split("/")[-1]
    return _SEP.sub(" ", seg).strip()


def parse_series(html: str) -> list[dict]:
    """Return a deduped list of {label, href} for every item on the page,
    in first-seen order."""
    seen = set()
    items = []
    for href in _ITEM.findall(html):
        if href in seen:
            continue
        seen.add(href)
        items.append({"label": slug_to_label(href), "href": href})
    return items


# Series-hero cover: RCO exposes exactly ONE cover for the whole series via
# <link rel="image_src" href="/Uploads/Etc/<date>/<id>.jpg">. There are NO
# per-edition thumbnails on the series page (per-edition covers would need
# per-edition page fetches, which are obfuscated). One shared cover per series.
_COVER = re.compile(r'<link\s+rel="image_src"\s+href="([^"]+)"', re.IGNORECASE)


def parse_series_cover(html: str) -> str:
    """The series-hero cover path (one per series), or "" if absent."""
    m = _COVER.search(html)
    return m.group(1) if m else ""


# RCO series 'Summary:' block: <span class="info">Summary:</span> <p>...prose...</p>
_SUMMARY = re.compile(
    r'<span class="info">\s*Summary:\s*</span>\s*<p>(.*?)</p>',
    re.S | re.IGNORECASE)


def parse_series_summary(page_html: str) -> str:
    """Extract the RCO series 'Summary:' prose block (already present in the
    fetched series-page HTML), stripped + unescaped. "" if absent."""
    m = _SUMMARY.search(page_html)
    if not m:
        return ""
    text = re.sub(r"<[^>]+>", " ", m.group(1))
    return re.sub(r"\s+", " ", _html.unescape(text)).strip()
