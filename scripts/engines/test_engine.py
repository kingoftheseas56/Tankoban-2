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

if __name__ == "__main__":
    unittest.main()
