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
