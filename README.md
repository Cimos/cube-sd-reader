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

The reader enumerates as USB `2DAE:1158`, separate from the CubeOrange+ `2DAE:1058`, so installed CubePilot serial drivers no longer claim the disk. The PID is a development allocation until CubePilot confirms it.

See [usage and recovery](docs/USAGE.md), [development](docs/DEVELOPMENT.md), and [test coverage](docs/TESTING.md).

## Source and license

GPL-3.0-or-later, with retained third-party notices. See [LICENSE](LICENSE) and [third-party sources](docs/THIRD_PARTY.md). This project is not an official CubePilot or ArduPilot release.
