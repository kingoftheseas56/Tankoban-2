from synopsis_harvester.clean import normalize_text, strip_series_boilerplate


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


def test_normalize_inserts_spacing_and_subtitle_colon():
    assert normalize_text("nudity!INTO THE BLUEAfter graduating") == \
        "nudity! INTO THE BLUE: After graduating"
