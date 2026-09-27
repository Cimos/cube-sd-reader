# AP_FLAKE8_CLEAN
import base64
import importlib.util
import json
import re
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


class UsbIdentityTests(unittest.TestCase):
    def test_firmware_pid_matches_validator_and_avoids_cubepilot_drivers(self):
        source = (ROOT / "firmware/CubeSDCardReader/CubeSDCardReader.cpp").read_text()
        vid = int(re.search(r"define READER_USB_VENDOR_ID (0x[0-9A-Fa-f]+)", source).group(1), 16)
        pid = int(re.search(r"define READER_USB_PRODUCT_ID (0x[0-9A-Fa-f]+)", source).group(1), 16)
        self.assertEqual((vid, pid), MODULE.READER_USB_ID)
        self.assertNotIn(pid, MODULE.CUBEPILOT_DRIVER_PIDS)


@unittest.skipUnless(ELF.exists() and APJ.exists(), "Build artifacts not present yet")
class ArtifactTests(unittest.TestCase):
    def test_current_package(self):
        result = MODULE.validate(ELF, APJ, BIN)
        self.assertEqual(result["application_descriptor_crc"], "passed")
        self.assertEqual(result["usb_id"], "2DAE:1158")
        self.assertEqual(result["disk_name"], "Cube USB Drive")

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
