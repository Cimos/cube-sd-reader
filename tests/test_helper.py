# AP_FLAKE8_CLEAN
import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("readerctl", Path(__file__).resolve().parents[1] / "tools/readerctl.py")
CTL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CTL)


class Port:
    def __init__(self, response):
        self.response = response
        self.sent = []
    def reset_input_buffer(self):
        pass
    def write(self, data):
        self.sent.append(data)
    def flush(self):
        pass
    def readline(self):
        return self.response


class HelperTests(unittest.TestCase):
    def test_rejects_wrong_device_before_other_commands(self):
        port = Port(b"OK board=Other protocol=1\r\n")
        with self.assertRaises(RuntimeError):
            CTL.identify(port)
        self.assertEqual(port.sent, [b"info\n"])

    def test_requires_hardware_identity(self):
        port = Port(b"OK board=CubeOrangePlus protocol=1 serial=broken\r\n")
        with self.assertRaises(RuntimeError):
            CTL.identify(port)

    def test_accepts_verified_reader(self):
        port = Port(b"OK board=CubeOrangePlus protocol=1 serial=0123456789ABCDEF01234567\r\n")
        self.assertEqual(CTL.identify(port)["board"], "CubeOrangePlus")
