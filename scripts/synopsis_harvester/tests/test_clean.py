from synopsis_harvester.clean import (
    normalize_text,
    strip_series_boilerplate,
    strip_shared_prefixes,
)


def test_strip_boilerplate_keeps_per_volume_body():
    a = "The hit comedy manga! Swimsuits! INTO THE BLUE: After graduating in town."
    b = "The hit comedy manga! Swimsuits! FIRST TIMES: Despite swimming poorly."
    out = strip_series_boilerplate([a, b])
    assert out[0].startswith("INTO THE BLUE")
    assert out[1].startswith("FIRST TIMES")
    assert "hit comedy manga" not in out[0]


def test_strip_boilerplate_noop_when_no_shared_intro():
    a = "Totally different opening one."
    b = "Another unrelated opening two."
    assert strip_series_boilerplate([a, b]) == [a, b]


def test_strip_shared_prefixes_handles_sub_majority_cluster():
    # Bleach 48-74 case: a generic series blurb prepended to only a SUB-MAJORITY
    # range (3 of 5 vols), each with a distinct per-volume tail. The >=60%
    # majority gate misses it; strip_shared_prefixes (>=3 vols) must strip it
    # from the carriers and leave the distinct volumes untouched.
    blurb = "Ichigo is a Soul Reaper guardian of the afterlife born with the gift. "
    a = blurb + "Aizen strolls through Karakura Town."
    b = blurb + "The final battle against Yhwach begins."
    c = blurb + "Isshin faces a mysterious Black Hollow."
    d = "A completely unrelated volume opening here that stands alone fine."
    e = "Yet another distinct standalone volume opening, nothing shared at all."
    out = strip_shared_prefixes([a, b, c, d, e])
    assert out[0].startswith("Aizen")
    assert out[1].startswith("The final battle")
    assert out[2].startswith("Isshin")
    assert "Soul Reaper guardian" not in out[0]
    assert out[3] == d  # distinct volume untouched
    assert out[4] == e  # distinct volume untouched


def test_normalize_inserts_spacing_and_subtitle_colon():
    assert normalize_text("nudity!INTO THE BLUEAfter graduating") == \
        "nudity! INTO THE BLUE: After graduating"
