import pathlib
from parse_rco import parse_series

FIX = pathlib.Path(__file__).parent / "fixtures" / "rco_series_invincible.html"


def _items():
    return parse_series(FIX.read_text(encoding="utf-8", errors="replace"))


def test_parses_unique_items_with_label_and_href():
    items = _items()
    # Fixture has 165 unique item slugs (each appears twice on the page: cover +
    # text link). Parser must dedupe.
    assert len(items) == 165
    assert all("label" in it and "href" in it for it in items)


def test_slug_becomes_human_label():
    items = _items()
    by_href = {it["href"]: it for it in items}
    assert by_href["/Comic/Invincible/Compendium-1"]["label"] == "Compendium 1"
    assert by_href["/Comic/Invincible/Issue-1"]["label"] == "Issue 1"
    assert by_href["/Comic/Invincible/Ultimate-Collection-Vol-1"]["label"] == \
        "Ultimate Collection Vol 1"


def test_surfaces_collected_editions():
    labels = [it["label"] for it in _items()]
    assert any("compendium" in l.lower() for l in labels)
    assert any("ultimate collection" in l.lower() for l in labels)


def test_no_duplicate_hrefs():
    items = _items()
    hrefs = [it["href"] for it in items]
    assert len(hrefs) == len(set(hrefs))
