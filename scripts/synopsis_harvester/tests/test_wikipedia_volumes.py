# scripts/synopsis_harvester/tests/test_wikipedia_volumes.py
from synopsis_harvester.wikipedia_volumes import parse_volume_table

def test_grand_blue_volume4_english_isbn(fixtures_dir):
    html = (fixtures_dir / "grand_blue_dreaming_wikipedia.html").read_text(encoding="utf-8")
    vols = parse_volume_table(html)
    by_num = {v["volumeNumber"]: v for v in vols}
    assert 4 in by_num
    # English ISBN must be selected over the Japanese one for the same volume.
    assert by_num[4]["isbnEn"] == "9781632367402"
    assert by_num[4]["isbnJp"] == "9784063880816"

def test_grand_blue_first_six_have_english_isbns(fixtures_dir):
    html = (fixtures_dir / "grand_blue_dreaming_wikipedia.html").read_text(encoding="utf-8")
    by_num = {v["volumeNumber"]: v for v in parse_volume_table(html)}
    expected_en = {
        1: "9781632366665", 2: "9781632366672", 3: "9781632366689",
        4: "9781632367402", 5: "9781632367242", 6: "9781632367259",
    }
    for num, isbn in expected_en.items():
        assert by_num[num]["isbnEn"] == isbn, f"vol {num}"
