from gcd_record import build_gcd_record


def test_record_shape_v3_forward_editions():
    vols = [
        {"volumeNumber": 1, "title": "Family Matters", "isbn": "9781582403205", "year": 2003,
         "coverUrl": "https://covers.openlibrary.org/b/isbn/9781582403205-L.jpg"},
        {"volumeNumber": 2, "title": "Eight Is Enough", "isbn": "9781582403472", "year": 2004,
         "coverUrl": ""},
    ]
    rec = build_gcd_record("invincible", "Invincible", vols, gcd_series_id=65547,
                           synopsis="...", author="Robert Kirkman", publisher="Image Comics",
                           genres=["superhero comics"], year_start=2003)
    assert rec["source"] == "gcd"
    assert rec["schemaVersion"] == 3
    assert rec["seriesTitle"] == "Invincible"
    assert rec["gcdSeriesId"] == 65547
    assert [e["volumeNumber"] for e in rec["editions"]] == [1, 2]
    e0 = rec["editions"][0]
    assert e0 == {"volumeNumber": 1, "title": "Family Matters", "isbn": "9781582403205",
                  "coverUrl": "https://covers.openlibrary.org/b/isbn/9781582403205-L.jpg",
                  "year": 2003, "formatTier": 2, "tierLabel": "TPB"}
    assert rec["seriesCover"].endswith("9781582403205-L.jpg")


def test_series_cover_skips_empty_cover_volumes():
    vols = [
        {"volumeNumber": 1, "title": "A", "isbn": "x", "year": 2003, "coverUrl": ""},
        {"volumeNumber": 2, "title": "B", "isbn": "y", "year": 2004,
         "coverUrl": "https://covers.openlibrary.org/b/isbn/y-L.jpg"},
    ]
    rec = build_gcd_record("s", "S", vols)
    assert rec["seriesCover"].endswith("y-L.jpg")


def test_empty_volumes_gives_empty_editions_and_cover():
    rec = build_gcd_record("s", "S", [])
    assert rec["editions"] == []
    assert rec["seriesCover"] == ""
