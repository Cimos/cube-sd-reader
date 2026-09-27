# Cube SD Reader

Turn a **CubeOrange+** flight controller into a USB SD card reader. Flash this firmware through the normal ArduPilot bootloader and the onboard microSD card shows up on your computer as a removable disk called **Cube USB Drive**, with no card removal and no extra drivers. A serial control port returns the board to the bootloader so ArduPilot can be reflashed.

Built from pinned ArduPilot and ChibiOS sources. The application uses raw SDMMC access without running the flight stack or mounting a filesystem locally.

## Download

Get `CubeSDCardReader.apj` from the [latest release](https://github.com/Cimos/cube-sd-reader/releases/latest) and upload it with your usual ArduPilot uploader (Mission Planner, QGroundControl or `uploader.py`). The bootloader is kept, so ArduPilot can be reflashed at any time. See [usage and recovery](docs/USAGE.md).

## Bench results

Windows testing on one CubeOrange+ and a 64 GB FAT32 card:

- About **0.8-0.9 MB/s write** including flush, **1.13 MB/s read** (USB Full Speed).
- File hashes matched after reboot and across firmware versions; CHKDSK found no errors.
- Eject, return to the bootloader, and ArduPlane restore passed. v0.1.0 was flashed over the previous reader with eject and `bootloader`, no replug.
- Windows binds its own disk and serial drivers, even with CubePilot drivers installed.

See [hardware tests](docs/TESTING.md) for limits and outstanding work.

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

The reader enumerates as USB `2DAE:1158`, separate from the CubeOrange+ `2DAE:1058`, so installed CubePilot serial drivers do not claim the disk.

See [usage and recovery](docs/USAGE.md), [development](docs/DEVELOPMENT.md), and [test coverage](docs/TESTING.md).

## Source and license

GPL-3.0-or-later, with retained third-party notices. See [LICENSE](LICENSE) and [third-party sources](docs/THIRD_PARTY.md). This project is not an official CubePilot or ArduPilot release.
