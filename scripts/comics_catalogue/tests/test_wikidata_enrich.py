import json
import pathlib

from wikidata_enrich import pick_comic_entity, extract_fields


def _fix(name):
    return json.loads((pathlib.Path(__file__).parent / "fixtures" / name)
                      .read_text(encoding="utf-8"))


def test_pick_comic_entity_prefers_comic_instance():
    cands = [
        {"id": "Q999", "instance_of": ["Q35", "Q5"]},        # not a comic
        {"id": "Q1426726", "instance_of": ["Q14406742"]},    # comic book series
    ]
    assert pick_comic_entity(cands) == "Q1426726"


def test_pick_comic_entity_none_when_no_comic():
    cands = [{"id": "Q999", "instance_of": ["Q5"]}]
    assert pick_comic_entity(cands) is None


def test_pick_comic_entity_weak_type_needs_signals():
    # "limited series" (Q3297186) is WEAK: only qualifies with a comic signal.
    no_signal = [{"id": "QtvShow", "instance_of": ["Q3297186"], "signals": 0}]
    assert pick_comic_entity(no_signal) is None
    with_signal = [{"id": "QwatchmenComic", "instance_of": ["Q3297186"], "signals": 3}]
    assert pick_comic_entity(with_signal) == "QwatchmenComic"


def test_pick_comic_entity_strong_beats_weak():
    cands = [
        {"id": "Qweak", "instance_of": ["Q3297186"], "signals": 5},   # weak, rich
        {"id": "Qstrong", "instance_of": ["Q14406742"], "signals": 0},  # strong, sparse
    ]
    assert pick_comic_entity(cands) == "Qstrong"  # tier wins over signals


def test_pick_comic_entity_richest_among_same_tier():
    cands = [
        {"id": "Qsparse", "instance_of": ["Q14406742"], "signals": 0},
        {"id": "Qrich", "instance_of": ["Q14406742"], "signals": 4},
    ]
    assert pick_comic_entity(cands) == "Qrich"  # more signals wins within a tier


def test_extract_fields_maps_claims_to_labels():
    entity = _fix("wikidata_invincible.json")
    labels = {"Q357331": "Robert Kirkman", "Q5176580": "Cory Walker",
              "Q1057217": "Image Comics", "Q1535153": "superhero comics"}
    out = extract_fields(entity, labels)
    assert out["author"] == "Robert Kirkman, Cory Walker"   # P50 + P110, joined
    assert out["publisher"] == "Image Comics"               # P123
    assert out["genres"] == ["superhero comics"]            # P136
    assert out["yearStart"] == 2003                         # P577 year


def test_extract_fields_empty_entity_safe():
    out = extract_fields({"claims": {}}, {})
    assert out == {"author": "", "publisher": "", "genres": [], "yearStart": 0}
