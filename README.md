# Cube SD Reader

Dedicated USB SD-card reader firmware for **CubeOrange+**. It exposes the onboard microSD card as USB mass storage and provides a serial control interface for returning to the existing ArduPilot bootloader.

Built from pinned ArduPilot and ChibiOS sources. The application uses raw SDMMC access without running the flight stack or mounting a filesystem locally.

## Bench results

Initial Windows testing on one CubeOrange+ and a 64 GB FAT32 card:

- **0.81 MB/s write**, including flush; **1.13 MB/s read**.
- File hashes matched after reboot; Windows CHKDSK found no errors.
- Safe eject, bootloader hold, and restoration of ArduPlane passed.
- A waiting uploader recovered through USB power cycling without a reader command.

These are initial development results. See [hardware tests](docs/TESTING.md) for limits and outstanding work.

## Build

Use Linux or WSL with Python 3, Git, make, gcc/g++, and GNU Arm Embedded **10-2020-q4-major** on PATH.

```sh
git clone https://github.com/Cimos/cube-sd-reader.git
cd cube-sd-reader
python3 tools/dev.py bootstrap
python3 tools/dev.py build --jobs 8
```

The build runs tests and validates the image, then writes the APJ, BIN, ELF, map, and checksum manifests to `artifacts/reader-dev/`. It never flashes a device.

## Use

Upload `CubeSDCardReader.apj` through the existing CubeOrange+ bootloader. The reader replaces the flight application until ArduPilot is reflashed. Use a bench device with outputs disconnected.

**Windows driver caveat:** installed CubePilot drivers can bind the storage interface as a serial port. The tested host needed that reader interface switched to Microsoft's USB Mass Storage driver. A production USB identity and automatic driver migration are not yet resolved.

See [usage and recovery](docs/USAGE.md), [development](docs/DEVELOPMENT.md), and [test coverage](docs/TESTING.md).

## Source and license

GPL-3.0-or-later, with retained third-party notices. See [LICENSE](LICENSE) and [third-party sources](docs/THIRD_PARTY.md). This project is not an official CubePilot or ArduPilot release.
