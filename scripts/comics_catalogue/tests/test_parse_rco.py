import pathlib
from parse_rco import parse_series, parse_series_cover

FIX = pathlib.Path(__file__).parent / "fixtures" / "rco_series_invincible.html"


def _items():
    return parse_series(FIX.read_text(encoding="utf-8", errors="replace"))


def test_parses_unique_items_with_label_and_href():
    items = _items()
    # Invincible fixture: 145 issues (0-144) + 25 TPBs = 170 unique item slugs.
    # Each appears twice on the page (cover link + text link) -> parser dedupes.
    assert len(items) == 170
    assert all("label" in it and "href" in it for it in items)


def test_slug_becomes_human_label():
    by_href = {it["href"]: it for it in _items()}
    assert by_href["/Comic/Invincible/Issue-1"]["label"] == "Issue 1"
    assert by_href["/Comic/Invincible/TPB-1-Family-matters"]["label"] == \
        "TPB 1 Family matters"
    assert by_href["/Comic/Invincible/TPB-25-The-End-of-All-Things-Part-Two"]["label"] == \
        "TPB 25 The End of All Things Part Two"


def test_surfaces_collected_editions():
    # This series page's collected editions are its 25 TPBs (the Compendium is a
    # SEPARATE series page, /Comic/Invincible-Universe-Compendium, not an item here).
    labels = [it["label"] for it in _items()]
    assert sum(1 for l in labels if l.lower().startswith("tpb")) == 25


def test_no_duplicate_hrefs():
    hrefs = [it["href"] for it in _items()]
    assert len(hrefs) == len(set(hrefs))


def test_only_item_links_not_series_links():
    # Only two-segment item links (/Comic/<series>/<item>); bare series links
    # like /Comic/Invincible-Universe-Compendium must be excluded.
    for it in _items():
        assert it["href"].count("/") >= 3


def test_parse_series_cover_extracts_image_src():
    # RCO series page exposes ONE series-hero cover via <link rel="image_src">.
    html = FIX.read_text(encoding="utf-8", errors="replace")
    cover = parse_series_cover(html)
    assert cover == "/Uploads/Etc/3-25-2016/42392826.jpg"


def test_parse_series_cover_empty_when_absent():
    assert parse_series_cover("<html><head></head></html>") == ""
