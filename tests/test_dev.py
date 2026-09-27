# AP_FLAKE8_CLEAN
"""Check that overlay updates preserve independent developer edits."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("dev", Path(__file__).resolve().parents[1] / "tools/dev.py")
DEV = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEV)


class SyncTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "firmware/CubeSDCardReader"
        self.source.mkdir(parents=True)
        self.file = self.source / "sample.cpp"
        self.file.write_text("original")
        self.target = self.root / "upstream" / DEV.OVERLAY / "sample.cpp"
        for name, value in (("ROOT", self.root), ("UPSTREAM", self.root / "upstream")):
            patcher = patch.object(DEV, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = patch.object(DEV, "verify")
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_updates_previously_generated_copy(self):
        DEV.sync()
        self.file.write_text("updated source")
        DEV.sync()
        self.assertEqual(self.target.read_text(), "updated source")

    def test_preserves_independent_developer_edit(self):
        DEV.sync()
        self.target.write_text("independent edit")
        self.file.write_text("updated source")
        with self.assertRaisesRegex(RuntimeError, "Refusing to overwrite"):
            DEV.sync()
        self.assertEqual(self.target.read_text(), "independent edit")

    def test_preserves_preexisting_file_without_state(self):
        self.target.parent.mkdir(parents=True)
        self.target.write_text("preexisting")
        with self.assertRaisesRegex(RuntimeError, "Refusing to overwrite"):
            DEV.sync()
        self.assertEqual(self.target.read_text(), "preexisting")


if __name__ == "__main__":
    unittest.main()
