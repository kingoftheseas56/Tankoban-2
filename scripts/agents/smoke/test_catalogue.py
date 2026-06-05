# scripts/agents/smoke/test_catalogue.py
import os, sys, json, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from catalogue import load, ValidationError
import pytest

GOOD = {
  "journeys": [
    {"id": "J1", "name": "One Piece", "steps": [
      {"id": "op.search", "action": ["search", "One Piece"],
       "asserts": [
         {"id": "op.search.returns", "lane": "SELF",
          "probe": ["search", "One Piece"], "path": "results[*].imdb",
          "expect": {"contains": "tt0388629"}, "timeoutSec": 15},
         {"id": "op.detail.renders", "lane": "VISUAL",
          "gemini_prompt": "Is the hero art and episode list painted?"}
       ]}
    ]}
  ]
}

def _write(obj):
    fd, p = tempfile.mkstemp(suffix=".json"); os.close(fd)
    with open(p, "w", encoding="utf-8") as f: json.dump(obj, f)
    return p

def test_loads_good():
    cat = load(_write(GOOD))
    assert cat.journeys[0].id == "J1"
    assert cat.journeys[0].steps[0].asserts[0].lane == "SELF"

def test_self_requires_probe_and_expect():
    bad = json.loads(json.dumps(GOOD))
    del bad["journeys"][0]["steps"][0]["asserts"][0]["expect"]
    with pytest.raises(ValidationError):
        load(_write(bad))

def test_visual_requires_prompt():
    bad = json.loads(json.dumps(GOOD))
    del bad["journeys"][0]["steps"][0]["asserts"][1]["gemini_prompt"]
    with pytest.raises(ValidationError):
        load(_write(bad))

def test_duplicate_assert_id_rejected():
    bad = json.loads(json.dumps(GOOD))
    bad["journeys"][0]["steps"][0]["asserts"][1]["id"] = "op.search.returns"
    with pytest.raises(ValidationError):
        load(_write(bad))
