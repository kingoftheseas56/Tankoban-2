"""Open Library cover-by-ISBN for the Western catalogue brain.

GCD ships metadata only (its own cover CDN is Cloudflare-blocked to us); Open
Library turns an ISBN into a real cover image, no signup. `?default=false` makes
OL return 404 when it has no real cover (instead of a 1x1 placeholder), so we can
leave coverUrl empty and let the app render a title-card.
"""
import urllib.request

_UA = "Tankoban/1.0 (dev catalogue builder; contact dev@tankoban.local)"


def cover_url(isbn13: str) -> str:
    return f"https://covers.openlibrary.org/b/isbn/{isbn13}-L.jpg" if isbn13 else ""


def _head_ok(url: str) -> bool:
    probe = url + "?default=false"
    try:
        req = urllib.request.Request(probe, headers={"User-Agent": _UA}, method="GET")
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status == 200 and r.headers.get("Content-Type", "").startswith("image")
    except Exception:
        return False


def verified_cover_url(isbn13: str) -> str:
    """Cover URL only if OL actually has a real cover for this ISBN, else ''."""
    u = cover_url(isbn13)
    return u if (u and _head_ok(u)) else ""
