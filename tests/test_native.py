# AP_FLAKE8_CLEAN
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class NativeTests(unittest.TestCase):
    def test_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "policy")
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/test_policy.cpp"), "-o", binary], check=True)
            subprocess.run([binary], check=True)

    def test_actual_scsi_and_transfer_code(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "scsi")
            firmware = ROOT / "firmware/CubeSDCardReader"
            include = ROOT / "upstream/ardupilot/modules/ChibiOS/os/various/scsi_bindings"
            subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-I" + str(ROOT / "tests/fakes"), "-I" + str(include), "-I" + str(firmware),
                            str(ROOT / "tests/test_scsi.c"), str(firmware / "reader_scsi.c"),
                            str(firmware / "reader_transfer.c"), "-o", binary], check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
