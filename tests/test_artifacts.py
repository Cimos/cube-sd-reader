# AP_FLAKE8_CLEAN
import base64
import importlib.util
import json
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("validate_artifacts", ROOT / "tools/validate_artifacts.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
BUILD = ROOT / "upstream/ardupilot/build/CubeOrangePlus"
ELF = BUILD / "examples/CubeSDCardReader"
APJ = BUILD / "bin/CubeSDCardReader.apj"
BIN = BUILD / "bin/CubeSDCardReader.bin"


@unittest.skipUnless(ELF.exists() and APJ.exists(), "Build artifacts not present yet")
class ArtifactTests(unittest.TestCase):
    def test_current_package(self):
        self.assertEqual(MODULE.validate(ELF, APJ, BIN)["application_descriptor_crc"], "passed")

    def test_wrong_board_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            package = json.loads(APJ.read_text())
            package["board_id"] = 1062
            path = Path(directory) / "wrong.apj"
            path.write_text(json.dumps(package))
            with self.assertRaisesRegex(RuntimeError, "Wrong APJ board"):
                MODULE.validate(ELF, path, BIN)

    def test_corruption_rejected_even_with_matching_apj_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = bytearray(BIN.read_bytes())
            raw[-1] ^= 1
            binary = Path(directory) / "bad.bin"
            binary.write_bytes(raw)
            package = json.loads(APJ.read_text())
            package["image"] = base64.b64encode(zlib.compress(raw)).decode()
            apj = Path(directory) / "bad.apj"
            apj.write_text(json.dumps(package))
            with self.assertRaisesRegex(RuntimeError, "CRC mismatch"):
                MODULE.validate(ELF, apj, binary)
