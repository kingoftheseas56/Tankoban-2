# scripts/synopsis_harvester/tests/test_synopsis_sources.py
from synopsis_harvester.synopsis_sources import extract_bn_synopsis

def test_extract_bn_grand_blue_v4(fixtures_dir):
    html = (fixtures_dir / "bn_9781632367402.html").read_text(encoding="utf-8")
    text = extract_bn_synopsis(html)
    assert text  # non-empty
    assert "Swimsuits! Ramen!" in text
    # Must capture the FULL blurb, not just the truncated preview.
    assert "the bane of all morons: an exam" in text
