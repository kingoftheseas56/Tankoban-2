"""Resolve an English-ISBN to its official per-volume synopsis text.

Primary: Barnes & Noble product page (keyless). We read the JSON-LD `description`
(complete text) and fall back to the visible overview block / meta description.
Optional: Google Books (needs GOOGLE_BOOKS_API_KEY) returns a usually-complete
description and is used only when B&N yields nothing.
"""
import json
import os
import re
import time
import requests
from bs4 import BeautifulSoup

_BN_HEADERS = {
    "User-Agent": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                   "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"),
    "Accept-Language": "en-US,en;q=0.9",
}


def _clean(text):
    return re.sub(r"\s+", " ", (text or "").replace("\xa0", " ")).strip()


def _jsonld_objects(data):
    """Yield every dict object in a JSON-LD graph, recursing into @graph."""
    if isinstance(data, list):
        for item in data:
            yield from _jsonld_objects(item)
    elif isinstance(data, dict):
        yield data
        for key in ("@graph", "hasPart", "itemListElement"):
            if key in data:
                yield from _jsonld_objects(data[key])


def extract_bn_synopsis(html):
    """Pure: pull the complete synopsis from a Barnes & Noble product page HTML."""
    soup = BeautifulSoup(html, "html.parser")
    # 1) JSON-LD description (most complete).
    for tag in soup.find_all("script", type="application/ld+json"):
        try:
            data = json.loads(tag.string or "")
        except (ValueError, TypeError):
            continue
        for obj in _jsonld_objects(data):
            if isinstance(obj, dict) and obj.get("description"):
                desc = _clean(BeautifulSoup(obj["description"], "html.parser").get_text(" "))
                if len(desc) > 40:
                    return desc
    # 2) Visible overview block (B&N accordion DOM, selector-adjusted to real page).
    for sel in ("#accordion-content-overview", ".product-description",
                "#productInfoOverview", "#overview", "div.overview-content"):
        node = soup.select_one(sel)
        if node:
            desc = _clean(node.get_text(" "))
            if len(desc) > 40:
                return desc
    # 3) Meta description (last resort, may be short).
    meta = soup.find("meta", attrs={"name": "description"})
    if meta and meta.get("content"):
        desc = _clean(meta["content"])
        if len(desc) > 40:
            return desc
    return ""


def fetch_bn_synopsis(isbn13, session=None, delay=0.0):
    """Network: fetch the B&N page for an ISBN-13 and extract its synopsis."""
    if delay:
        time.sleep(delay)
    sess = session or requests.Session()
    resp = sess.get(f"https://www.barnesandnoble.com/w/?ean={isbn13}",
                    headers=_BN_HEADERS, timeout=30, allow_redirects=True)
    if resp.status_code != 200:
        return ""
    return extract_bn_synopsis(resp.text)


def fetch_google_books_synopsis(isbn13, session=None):
    """Optional fallback. Requires env GOOGLE_BOOKS_API_KEY (keyless quota is 0)."""
    key = os.environ.get("GOOGLE_BOOKS_API_KEY")
    if not key:
        return ""
    sess = session or requests.Session()
    resp = sess.get("https://www.googleapis.com/books/v1/volumes",
                    params={"q": f"isbn:{isbn13}", "key": key}, timeout=30)
    if resp.status_code != 200:
        return ""
    items = resp.json().get("items") or []
    if not items:
        return ""
    return _clean(items[0].get("volumeInfo", {}).get("description", ""))


def resolve_synopsis(isbn13, session=None, delay=0.0):
    """B&N first; Google Books only if B&N is empty and a key is configured."""
    text = fetch_bn_synopsis(isbn13, session=session, delay=delay)
    return text or fetch_google_books_synopsis(isbn13, session=session)
