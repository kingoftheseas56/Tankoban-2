"""Parse a Wikipedia manga article's volume table into per-volume records.

Wikipedia renders the standardized "Graphic novel list" template as table rows;
each volume row carries one or two Special:BookSources/<isbn> links (Japanese
and/or English edition). We classify each ISBN by prefix and keep the English one.
"""
import re
from datetime import datetime
from bs4 import BeautifulSoup

# Mirrors src/core/manga/wikipedia/WikipediaParser.cpp:37 (Special:BookSources regex).
_ISBN_HREF_RE = re.compile(r"/wiki/Special:BookSources/([0-9Xx\-]+)")
_VOL_ID_RE = re.compile(r"^vol(\d+)$")
_DATE_RE = re.compile(r"([A-Z][a-z]+ \d{1,2}, \d{4})")


def _normalize_isbn(raw):
    digits = raw.replace("-", "").upper()
    return digits if len(digits) in (10, 13) else None


def _is_japanese(isbn13):
    # JP group: ISBN-13 starting 9784, or ISBN-10 starting 4.
    return isbn13.startswith("9784") or (len(isbn13) == 10 and isbn13.startswith("4"))


def _to_isbn13(isbn):
    # Best-effort: callers only need a stable key; B&N/Google accept ISBN-13.
    if len(isbn) == 13:
        return isbn
    if len(isbn) == 10:
        core = "978" + isbn[:-1]
        total = sum((1 if i % 2 == 0 else 3) * int(d) for i, d in enumerate(core))
        return core + str((10 - total % 10) % 10)
    return isbn


def _iso_date(text):
    m = _DATE_RE.search(text or "")
    if not m:
        return ""
    try:
        return datetime.strptime(m.group(1), "%B %d, %Y").date().isoformat()
    except ValueError:
        return ""


def _row_volume_number(row):
    th = row.find("th", id=_VOL_ID_RE)
    if th:
        return int(_VOL_ID_RE.match(th["id"]).group(1))
    first = row.find(["th", "td"])
    if first:
        m = re.match(r"\s*(\d+)\s*$", first.get_text())
        if m:
            return int(m.group(1))
    return None


def parse_volume_table(html):
    """Return a list of {volumeNumber, isbnEn, isbnJp, englishTitle, englishReleaseDate}."""
    soup = BeautifulSoup(html, "html.parser")
    out = {}
    for table in soup.find_all("table"):
        if "Special:BookSources" not in str(table):
            continue
        for row in table.find_all("tr"):
            num = _row_volume_number(row)
            if num is None:
                continue
            isbn_en = isbn_jp = None
            for a in row.find_all("a", href=_ISBN_HREF_RE):
                m = _ISBN_HREF_RE.search(a["href"])
                norm = _normalize_isbn(m.group(1)) if m else None
                if not norm:
                    continue
                isbn13 = _to_isbn13(norm)
                if _is_japanese(isbn13):
                    isbn_jp = isbn_jp or isbn13
                else:
                    isbn_en = isbn_en or isbn13
            if isbn_en is None and isbn_jp is None:
                continue
            out[num] = {
                "volumeNumber": num,
                "isbnEn": isbn_en,
                "isbnJp": isbn_jp,
                "englishTitle": "",
                "englishReleaseDate": _iso_date(row.get_text(" ")),
            }
    return [out[k] for k in sorted(out)]
