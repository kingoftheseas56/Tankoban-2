import gcd_client
from gcd_client import (
    normalize_isbn13, pick_tpb_line, parse_descriptor, build_volumes_from_issues,
)


# ── ISBN normalization ───────────────────────────────────────────────────────

def test_isbn10_converts_to_isbn13():
    # ISBN-13 recomputes the check digit (978-prefixed): 1-58240-320-1 -> ...205
    assert normalize_isbn13("1-58240-320-1") == "9781582403205"


def test_isbn13_passthrough_strips_hyphens():
    assert normalize_isbn13("978-1-58240-347-2") == "9781582403472"


def test_compound_isbn_takes_first_valid():
    assert normalize_isbn13("1-58240-778-9; 978-1-58240-778-4") == "9781582407784"


def test_empty_or_junk_returns_empty():
    assert normalize_isbn13("") == ""
    assert normalize_isbn13("n/a") == ""


# ── TPB-line selection ───────────────────────────────────────────────────────

def _series(name, fmt, binding, niss):
    return {"api_url": f"https://www.comics.org/api/series/{niss}/?format=json",
            "name": name, "language": "en", "country": "us",
            "publishing_format": fmt, "binding": binding,
            "active_issues": ["x"] * niss}


def test_picks_collected_softcover_line_over_floppy():
    rows = [
        _series("Invincible", "was ongoing series", "Saddle-stitched", 170),
        _series("Invincible", "Collected Editions; Was Ongoing Series", "softcover", 34),
        _series("Invincible Iron Man", "was ongoing series", "Saddle-stitched", 186),
        _series("Invincible Compendium", "Was Ongoing Series", "Squarebound", 3),
    ]
    line = pick_tpb_line(rows, "Invincible")
    assert line["binding"] == "softcover"
    assert len(line["active_issues"]) == 34


def test_returns_none_when_no_collected_line():
    rows = [_series("Saga", "was ongoing series", "Saddle-stitched", 72)]
    assert pick_tpb_line(rows, "Saga") is None


# ── descriptor / volume assembly ─────────────────────────────────────────────

def test_parse_descriptor_splits_number_and_title():
    assert parse_descriptor("1 - Family Matters") == (1, "Family Matters")
    assert parse_descriptor("12 - The Untitled") == (12, "The Untitled")
    assert parse_descriptor("Family Matters") == (None, "Family Matters")


def test_build_volumes_dedupes_and_forward_sorts():
    issues = [
        {"descriptor": "2 - Eight Is Enough", "isbn": "978-1-58240-347-2", "publication_date": "2005-03"},
        {"descriptor": "1 - Family Matters", "isbn": "1-58240-320-1", "publication_date": "2003-05"},
        {"descriptor": "1 - Family Matters", "isbn": "9781582407111", "publication_date": "2008"},  # reprint
    ]
    vols = build_volumes_from_issues(issues)
    assert [v["volumeNumber"] for v in vols] == [1, 2]
    assert vols[0]["title"] == "Family Matters"
    assert vols[0]["isbn"] == "9781582403205"   # first printing's ISBN-10, converted to 13
    assert vols[0]["year"] == 2003
    assert vols[1]["volumeNumber"] == 2


# ── live fetch pagination (no real network) ──────────────────────────────────

def test_fetch_all_series_paginates(monkeypatch):
    pages = {
        "https://www.comics.org/api/series/name/X/?format=json":
            {"results": [{"name": "X"}], "next": "PAGE2"},
        "PAGE2": {"results": [{"name": "X2"}], "next": None},
    }
    monkeypatch.setattr(gcd_client, "_get_json", lambda url: pages[url])
    monkeypatch.setattr(gcd_client.time, "sleep", lambda s: None)
    rows = gcd_client.fetch_all_series("X")
    assert [r["name"] for r in rows] == ["X", "X2"]
