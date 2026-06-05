# scripts/agents/smoke/test_oracle.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oracle import evaluate

def test_equals():
    assert evaluate({"equals": "playing"}, "playing")[0] is True
    assert evaluate({"equals": "playing"}, "paused")[0] is False

def test_approx_tolerance():
    assert evaluate({"approx": 600, "tolerance": 5}, 603)[0] is True
    assert evaluate({"approx": 600, "tolerance": 5}, 590)[0] is False

def test_gt_gte_lt():
    assert evaluate({"gt": 0}, 5)[0] is True
    assert evaluate({"gt": 0}, 0)[0] is False
    assert evaluate({"gte": 0}, 0)[0] is True
    assert evaluate({"lt": 100}, 50)[0] is True

def test_contains_regex():
    assert evaluate({"contains": "One Piece"}, "[SubsPlease] One Piece 1133")[0] is True
    assert evaluate({"regex": r"\d+~\d+"}, "1123~1133")[0] is True
    assert evaluate({"regex": r"\d+~\d+"}, "1133")[0] is False

def test_exists_nonempty_len():
    assert evaluate({"exists": True}, 0)[0] is True
    assert evaluate({"exists": True}, None)[0] is False
    assert evaluate({"nonempty": True}, [1])[0] is True
    assert evaluate({"nonempty": True}, [])[0] is False
    assert evaluate({"len_gte": 1}, ["a"])[0] is True
    assert evaluate({"len_gte": 2}, ["a"])[0] is False

def test_reason_is_human():
    ok, reason = evaluate({"approx": 600, "tolerance": 5}, 590)
    assert ok is False and "590" in reason and "600" in reason
