# scripts/agents/smoke/test_probes.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probes import extract, MISSING

REPLY = {
    "snapshot": {"detail": {"currentImdb": "tt0388629", "episodeRows": [1, 2, 3]}},
    "entries": [{"progress": 0.42, "imdb": "tt0388629"}],
    "results": [{"imdb": "tt0388629"}, {"imdb": "tt11737520"}],
}

def test_dotted_path():
    assert extract(REPLY, "snapshot.detail.currentImdb") == "tt0388629"

def test_indexed_path():
    assert extract(REPLY, "entries[0].progress") == 0.42

def test_list_len_via_path():
    assert extract(REPLY, "snapshot.detail.episodeRows") == [1, 2, 3]

def test_missing_returns_sentinel():
    assert extract(REPLY, "snapshot.nope.x") is MISSING
    assert extract(REPLY, "entries[9].progress") is MISSING

def test_wildcard_collects_field():
    # "results[*].imdb" -> list of all imdb values
    assert extract(REPLY, "results[*].imdb") == ["tt0388629", "tt11737520"]
