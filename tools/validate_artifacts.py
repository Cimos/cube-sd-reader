#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Validate CubeOrange+ application packaging and linked USB descriptors."""
import base64
import json
import struct
import subprocess
import zlib
from pathlib import Path

FLASH_START = 0x08020000
FLASH_END = 0x08200000


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def elf_symbol(data, suffix):
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    sections = [struct.unpack_from("<IIIIIIIIII", data, header[6] + i * header[11])
                for i in range(header[12])]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, _, _, index = struct.unpack_from("<IIIBBH", data, offset)
            label = names[name:names.find(b"\0", name)].decode()
            if label.endswith(suffix):
                target = sections[index]
                start = target[4] + value - target[3]
                return data[start:start + size]
    raise RuntimeError("Required ELF symbol missing: " + suffix)


def validate(elf, apj, binary):
    data = Path(elf).read_bytes()
    check(data[:6] == b"\x7fELF\x01\x01", "Expected little-endian ELF32")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    check(header[2] == 40, "Expected ARM ELF")
    check(FLASH_START <= (header[4] & ~1) < FLASH_END, "Entry outside application flash")
    segments = []
    for i in range(header[10]):
        item = struct.unpack_from("<IIIIIIII", data, header[5] + i * header[9])
        kind, _, _, physical, length, _, _, _ = item
        if kind == 1 and length and 0x08000000 <= physical < FLASH_END:
            check(FLASH_START <= physical and physical + length <= FLASH_END, "Reserved flash overlap")
            segments.append((physical, length))
    check(segments and min(s[0] for s in segments) == FLASH_START, "Incorrect image origin")
    package = json.loads(Path(apj).read_text())
    raw = Path(binary).read_bytes()
    image = zlib.decompress(base64.b64decode(package["image"]))
    check(package["magic"] == "APJFWv1" and package["board_id"] == 1063, "Wrong APJ board or format")
    check(image == raw and package["image_size"] == len(raw), "APJ/bin payload mismatch")
    check(8 <= len(raw) <= FLASH_END - FLASH_START, "Invalid application size")

    signature = bytes.fromhex("40a2e4f164689106")
    offset = raw.find(signature)
    check(offset >= 0 and offset + 36 <= len(raw), "Missing application descriptor")
    crc1, crc2, size, _ = struct.unpack_from("<IIII", raw, offset + 8)
    board_id = struct.unpack_from("<H", raw, offset + 26)[0]
    crc = lambda payload: (zlib.crc32(payload, 0xFFFFFFFF) ^ 0xFFFFFFFF) & 0xFFFFFFFF
    check(size == len(raw) and board_id == 1063, "Invalid application descriptor identity/size")
    check(crc1 == crc(raw[:offset + 8]) and crc2 == crc(raw[offset + 24:]),
          "Bootloader application CRC mismatch")

    stack, reset = struct.unpack_from("<II", raw)
    check(reset == header[4] and reset & 1, "Reset vector/entry mismatch")
    check(stack % 8 == 0 and 0x20000000 <= stack <= 0x30050000, "Invalid stack vector")

    config = elf_symbol(data, "configuration_data")
    check(len(config) == 98 and int.from_bytes(config[2:4], "little") == len(config),
          "Bad USB configuration size")
    interfaces, endpoints, associations = [], [], []
    pos = 0
    while pos < len(config):
        length, kind = config[pos:pos + 2]
        check(length >= 2 and pos + length <= len(config), "Malformed USB descriptor")
        descriptor = config[pos:pos + length]
        if kind == 4:
            interfaces.append(descriptor[2])
        elif kind == 5:
            endpoints.append(descriptor[2])
        elif kind == 11:
            associations.append(tuple(descriptor[2:4]))
        pos += length
    check(interfaces == [0, 1, 2], "Wrong MSC/CDC interface numbering")
    check(len(endpoints) == len(set(endpoints)) and set(endpoints) == {1, 0x81, 2, 0x82, 0x83},
          "USB endpoint collision")
    check(associations == [(1, 2)], "CDC association mismatch")
    device = elf_symbol(data, "device_data")
    check(device[4:7] == bytes([0xEF, 2, 1]) and device[16] == 3, "Composite identity mismatch")
    symbols = subprocess.check_output(["arm-none-eabi-nm", "-C", str(elf)], text=True)
    prohibited = ("stm32_flash_write", "stm32_flash_erasepage", "f_mount", "f_write",
                  "HAL_ChibiOS::run", "AP_IOMCU::init", "AP_Param::save")
    for symbol in prohibited:
        check(symbol not in symbols, "Unexpected flight/filesystem/flash dependency: " + symbol)
    return {"board_id": 1063, "origin": hex(FLASH_START), "entry": hex(header[4]),
            "image_bytes": len(raw), "usb_interfaces": interfaces, "usb_endpoints": endpoints,
            "reserved_flash_check": "passed", "prohibited_symbols": "absent",
            "application_descriptor_crc": "passed", "hardware_tested": False}
