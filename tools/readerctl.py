#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Inspect the reader or reboot an already-ejected reader into its retained bootloader."""
import argparse
import time


def exchange(port, command):
    port.reset_input_buffer()
    port.write((command + "\n").encode("ascii"))
    port.flush()
    response = port.readline().decode("ascii", errors="replace").strip()
    if not response:
        raise RuntimeError("No reader response")
    return response


def identify(port):
    response = exchange(port, "info")
    fields = dict(part.split("=", 1) for part in response.split()[1:] if "=" in part)
    if not response.startswith("OK ") or fields.get("board") != "CubeOrangePlus" or fields.get("protocol") != "1":
        raise RuntimeError("Selected port is not a compatible Cube SD Card Reader")
    serial = fields.get("serial", "")
    if len(serial) != 24 or any(c not in "0123456789ABCDEF" for c in serial):
        raise RuntimeError("Reader did not supply a valid hardware serial")
    return fields


def await_bootloader(serial_number, seconds=20):
    import serial
    from serial.tools import list_ports
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        matches = [p for p in list_ports.comports()
                   if p.vid == 0x2DAE and p.pid == 0x1058
                   and (p.serial_number or "").upper() == serial_number]
        if len(matches) == 1:
            try:
                with serial.Serial(matches[0].device, 115200, timeout=0.3, write_timeout=0.3) as port:
                    port.reset_input_buffer()
                    # AP/PX4 bootloader GET_SYNC + EOC; does not erase or program.
                    port.write(bytes([0x21, 0x20]))
                    port.flush()
                    if port.read(2) == bytes([0x12, 0x10]):
                        return matches[0].device
            except (OSError, serial.SerialException):
                pass
        time.sleep(0.5)
    raise RuntimeError("Bootloader handshake not confirmed; inspect USB enumeration before uploading")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit COM port or /dev/ttyACM device")
    parser.add_argument("command", choices=("info", "status", "bootloader"))
    args = parser.parse_args()
    import serial
    try:
        with serial.Serial(args.port, 115200, timeout=2, write_timeout=2) as port:
            identity = identify(port)
            if args.command == "info":
                print(identity)
                return
            response = exchange(port, args.command)
            print(response)
            if not response.startswith("OK "):
                raise RuntimeError("Reader refused command; safely eject the card before retrying")
        if args.command == "bootloader":
            time.sleep(0.5)
            name = await_bootloader(identity["serial"])
            print(f"Bootloader confirmed on {name}. Use your normal CubeOrange+ ArduPilot uploader.")
    except (OSError, serial.SerialException, RuntimeError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
