"""Resolve a GetComics post into the best download link.

Recon (RECON_FINDINGS.md): GetComics posts carry a normalized download block
per book — labelled anchors for MAGNET / Main Server / Mega / Mediafire /
Pixeldrain. Magnets are `magnet:?...`; the DDL options route through
`getcomics.org/dls/<token>`. Footer ad links (craveu/crushon/etc.) are filtered
by requiring a magnet: scheme or a getcomics.org/dls/ host.

Download priority (spec §4): magnet -> our libtorrent client (preferred,
resumable), then Main Server DDL, then the other file hosts.
"""
import re

_ANCHOR = re.compile(r'<a\s+[^>]*?href="([^"]+)"[^>]*>(.*?)</a>', re.S)
_PRIORITY = ["magnet", "main_server", "pixeldrain", "mediafire", "mega"]


def _kind(href: str, text: str) -> str | None:
    t = re.sub(r"<[^>]+>", "", text).strip().lower()
    if href.startswith("magnet:"):
        return "magnet"
    if "main server" in t:
        return "main_server"
    if "pixeldrain" in t:
        return "pixeldrain"
    if "mediafire" in t:
        return "mediafire"
    if "mega" in t:
        return "mega"
    return None


def extract_downloads(html: str) -> list[dict]:
    """Every real download anchor on the post, as {kind, url}. Ad links and
    non-download anchors are dropped."""
    out = []
    for href, text in _ANCHOR.findall(html):
        kind = _kind(href, text)
        if not kind:
            continue
        if href.startswith("magnet:") or "getcomics.org/dls/" in href:
            out.append({"kind": kind, "url": href})
    return out


def pick_best(downloads: list[dict]):
    """The preferred download by priority (magnet first), or None if empty."""
    for kind in _PRIORITY:
        for d in downloads:
            if d["kind"] == kind:
                return d
    return None
