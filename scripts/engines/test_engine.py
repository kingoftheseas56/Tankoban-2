import os, json, tempfile, unittest, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("engine", os.path.join(HERE, "engine.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

CFG = {"caps": {"per_wake_hard": 3, "per_task_soft": 2, "max_packet_chars": 100}}

class CapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()  # empty ledger

    def tearDown(self):
        os.unlink(self.tmp.name)

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

if __name__ == "__main__":
    unittest.main()
