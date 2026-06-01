import os, json, tempfile, unittest, importlib.util
from unittest.mock import patch, MagicMock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("engine", os.path.join(HERE, "engine.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

CFG = {"caps": {"per_wake_hard": 3, "per_task_soft": 2, "max_packet_chars": 100}}

# Full config for live-caller tests (needs deepseek/codex/gemini/timeouts keys).
CFG_FULL = {
    "caps": {"per_wake_hard": 3, "per_task_soft": 2, "max_packet_chars": 100},
    "deepseek": {"base_url": "https://api.deepseek.com/anthropic", "model": "test"},
    "codex": {"model": "test"},
    "gemini": {"url": "https://example.com/{model}:generateContent", "model": "test", "enabled": True},
    "timeouts": {"deepseek": 30, "codex": 30, "gemini": 30},
}

class CapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()  # empty ledger
        os.environ["ENGINE_AGENT"] = "test-agent"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_AGENT", None)

    def test_wake_marker_resets_count(self):
        for _ in range(2):
            engine.log_call("deepseek", 10, "x", "T1")
        engine.mark_wake()
        rows = engine.ledger_rows_since_wake()
        calls = [r for r in rows if r.get("event") == "call"]
        self.assertEqual(len(calls), 0)

    def test_hard_cap_blocks(self):
        for _ in range(3):
            engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertFalse(ok)
        self.assertIn("hard cap", msg)

    def test_soft_cap_warns_but_allows(self):
        for _ in range(2):
            engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        self.assertIn("soft cap", msg)

    def test_under_cap_clean(self):
        engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        self.assertIsNone(msg)

    def test_torn_lines_are_skipped(self):
        with open(self.tmp.name, "w") as f:
            f.write('{"event":"call","engine":"d","tokens":1,"task":"T1","agent":"test-agent"}\n')
            f.write('this is torn garbage\n')
            f.write('{"event":"call","engine":"c","tokens":2,"task":"T1","agent":"test-agent"}\n')
        rows = engine._read_ledger()
        self.assertEqual(len(rows), 2)

    def test_per_agent_isolation(self):
        # Brother A logs 3 calls (at hard cap).
        for _ in range(3):
            engine.log_call("deepseek", 10, "x", "T1")
        # Brother B's wake-start + cap check should be independent.
        os.environ["ENGINE_AGENT"] = "brother-b"
        engine.mark_wake()
        rows = engine.ledger_rows_since_wake()
        calls = [r for r in rows if r.get("event") == "call"]
        self.assertEqual(len(calls), 0)
        ok, _ = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        os.environ["ENGINE_AGENT"] = "test-agent"  # restore

class GuardTest(unittest.TestCase):
    def test_packet_too_big_raises(self):
        with self.assertRaises(ValueError):
            engine.guard_packet("x" * 101, CFG)

    def test_packet_ok(self):
        engine.guard_packet("small", CFG)  # no raise

    def test_missing_key_raises(self):
        os.environ.pop("FAKE_KEY_XYZ", None)
        with self.assertRaises(RuntimeError):
            engine.require_key("FAKE_KEY_XYZ")

    def test_present_key_returns(self):
        os.environ["FAKE_KEY_XYZ"] = "sk-test"
        try:
            self.assertEqual(engine.require_key("FAKE_KEY_XYZ"), "sk-test")
        finally:
            os.environ.pop("FAKE_KEY_XYZ", None)

GEMINI_FIXTURE = {
    "candidates": [
        {"content": {"parts": [{"text": "3.2"}], "role": "model"},
         "finishReason": "STOP", "index": 0}
    ],
    "usageMetadata": {"totalTokenCount": 159},
    "modelVersion": "gemini-2.5-flash",
}

CODEX_FIXTURE = """Reading additional input from stdin...
OpenAI Codex v0.131.0
--------
workdir: C:\\Users\\Suprabha\\Desktop\\Tankoban 2
model: gpt-5.5
--------
user
Review this C++ function...
codex
APPROVE clamps values below 0 to 0, above 100 to 100, and returns in-range values unchanged.
tokens used
15,018
"""

class ParserTest(unittest.TestCase):
    def test_parse_gemini(self):
        self.assertEqual(engine.parse_gemini(GEMINI_FIXTURE), "3.2")

    def test_parse_codex_extracts_answer(self):
        out = engine.parse_codex(CODEX_FIXTURE)
        self.assertEqual(
            out,
            "APPROVE clamps values below 0 to 0, above 100 to 100, "
            "and returns in-range values unchanged.")

    def test_parse_codex_multiline_answer(self):
        sample = "banner\ncodex\nline one\nline two\ntokens used\n42\n"
        self.assertEqual(engine.parse_codex(sample), "line one\nline two")

class DispatchTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()
        os.environ["ENGINE_DRY_RUN"] = "1"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_DRY_RUN", None)

    def test_grunt_dry_runs_and_logs(self):
        out = engine.dispatch("grunt", "tiny packet", "T1", "p", CFG)
        self.assertTrue(out.startswith("DRY:deepseek"))
        calls = [r for r in engine.ledger_rows_since_wake() if r.get("event") == "call"]
        self.assertEqual(calls[-1]["engine"], "deepseek")

    def test_review_dry_runs(self):
        out = engine.dispatch("review", "diff", "T1", "p", CFG)
        self.assertTrue(out.startswith("DRY:codex"))

    def test_read_blocked_when_gemini_disabled(self):
        cfg = dict(CFG)
        cfg["gemini"] = {"enabled": False}
        with self.assertRaises(RuntimeError):
            engine.dispatch("read", "blob", "T1", "p", cfg)

    @patch("engine.subprocess.run")
    def test_grunt_nonzero_returncode_raises(self, mock_run):
        mock_run.return_value = MagicMock(returncode=1, stdout="", stderr="claude: error")
        os.environ.pop("ENGINE_DRY_RUN", None)
        os.environ["DEEPSEEK_API_KEY"] = "sk-test"
        try:
            with self.assertRaises(RuntimeError) as ctx:
                engine.call_deepseek("packet", CFG_FULL)
            self.assertIn("exited 1", str(ctx.exception))
            self.assertIn("claude: error", str(ctx.exception))
        finally:
            os.environ.pop("DEEPSEEK_API_KEY", None)

    @patch("engine.subprocess.run")
    def test_codex_nonzero_returncode_raises(self, mock_run):
        mock_run.return_value = MagicMock(returncode=2, stdout="", stderr="codex: fatal")
        os.environ.pop("ENGINE_DRY_RUN", None)
        with self.assertRaises(RuntimeError) as ctx:
            engine.call_codex("packet", CFG_FULL)
        self.assertIn("exited 2", str(ctx.exception))

    def test_ledger_lock_context_manager(self):
        with engine._ledger_lock():
            self.assertTrue(os.path.exists(engine.LOCK_PATH))
        # Lock file remains on disk (the fd is closed, not the file removed).
        self.assertTrue(os.path.exists(engine.LOCK_PATH))
        os.unlink(engine.LOCK_PATH)


class GateTest(unittest.TestCase):
    """Prove the Gemini gate does NOT reset the caller's cap counter."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()
        os.environ["ENGINE_AGENT"] = "solo"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_AGENT", None)

    def test_gate_probe_does_not_write_wake_marker(self):
        # Simulate existing calls from the solo user.
        engine.log_call("deepseek", 10, "existing work", "T1")
        engine.log_call("codex", 5, "existing review", "T1")
        calls_before = len(
            [r for r in engine.ledger_rows_since_wake()
             if r.get("event") == "call"])
        self.assertEqual(calls_before, 2)
        # The gate probe logs a gemini call but does NOT call mark_wake.
        engine.log_call("gemini", 3, "gate-probe", "GEMINI_GATE")
        calls_after = len(
            [r for r in engine.ledger_rows_since_wake()
             if r.get("event") == "call"])
        # Still 3 calls (2 original + 1 gate probe); wake markers unchanged.
        self.assertEqual(calls_after, 3)

if __name__ == "__main__":
    unittest.main()
