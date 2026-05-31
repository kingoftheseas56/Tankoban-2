from harvest import build_record


def test_keeps_collected_editions_tier_sorted():
    items = [
        {"label": "Issue 1", "href": "/Comic/Invincible/Issue-1"},
        {"label": "TPB 1 Family matters", "href": "/Comic/Invincible/TPB-1-Family-matters"},
        {"label": "Invincible Compendium 1", "href": "/Comic/Invincible/Compendium-1"},
    ]
    rec = build_record("invincible", "Invincible", items)
    # Compendium (tier 0) sorts before TPB (tier 2); the single issue is excluded.
    assert [e["label"] for e in rec["editions"]] == [
        "Invincible Compendium 1",
        "TPB 1 Family matters",
    ]
    assert [e["formatTier"] for e in rec["editions"]] == [0, 2]


def test_single_issues_excluded_from_editions():
    items = [
        {"label": "Issue 1", "href": "/Comic/X/Issue-1"},
        {"label": "Issue 2", "href": "/Comic/X/Issue-2"},
    ]
    rec = build_record("x", "X", items)
    assert rec["editions"] == []


def test_record_shape_and_identity():
    items = [{"label": "Omnibus 1", "href": "/Comic/Y/Omnibus-1"}]
    rec = build_record("y", "Y", items)
    assert rec["seriesId"] == "y"
    assert rec["seriesTitle"] == "Y"
    assert rec["source"] == "rco"
    e = rec["editions"][0]
    assert e == {"label": "Omnibus 1", "href": "/Comic/Y/Omnibus-1", "formatTier": 1}


def test_stable_order_within_same_tier():
    # Two TPBs (same tier) keep their input order (stable sort).
    items = [
        {"label": "TPB 2 Eight is Enough", "href": "/Comic/Z/TPB-2"},
        {"label": "TPB 1 Family matters", "href": "/Comic/Z/TPB-1"},
    ]
    rec = build_record("z", "Z", items)
    assert [e["label"] for e in rec["editions"]] == [
        "TPB 2 Eight is Enough",
        "TPB 1 Family matters",
    ]


def test_empty_items_gives_empty_editions():
    rec = build_record("e", "E", [])
    assert rec["editions"] == []
    assert rec["seriesId"] == "e"
