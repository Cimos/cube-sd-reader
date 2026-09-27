# Development

## Environment

Linux or Ubuntu under WSL is the supported build environment. Install Python 3 with venv support, Git, make, native gcc/g++, and GNU Arm Embedded 10-2020-q4-major (10.2.1). Put the Arm compiler's bin directory on PATH. Python 3.12 was used for the initial build.

From the repository root:

```sh
python3 tools/dev.py bootstrap
python3 tools/dev.py doctor
python3 tools/dev.py build --jobs 8
python3 -m unittest discover -s tests -v
```

Bootstrap fetches the pinned ArduPilot submodule and its recursive dependencies and installs requirements-dev.txt into a project-local virtual environment. It does not install system packages. Allow several GB for upstream sources and build products.

## Layout

- `firmware/CubeSDCardReader/`: dedicated entry point, composite USB, control policy, adapted MSD/SCSI/SD drivers.
- `firmware/reader.hwdef`: board overrides disabling filesystem/flash crash dumping and persistent parameter saves.
- `tools/dev.py`: dependency checks, protected source overlay, build and package validation.
- `tools/prepare_drivers.py`: reproduces adaptations of pinned ChibiOS sources.
- `tools/readerctl.py`: portable serial control and bootloader identity check.
- `tools/windows_eject.py`, `tools/Return-ToArduPilot.ps1`: Windows disk/COM association, flush, eject and bootloader transition.
- `tests/`: native policy and SCSI tests, helper identity, overlay protection and packaging checks.

The build copies editable firmware into ArduPilot's examples tree, refusing to overwrite independent developer changes. Upstream tracked files stay unchanged. The reader has its own main function and does not invoke the flight HAL.

The driver regeneration script overwrites local adapted driver copies. Run it only deliberately after reviewing local changes. Original source hashes are in firmware/vendor-sources.json.

## Firmware and packaging

The reader reuses CubeOrange+ startup, raw SDMMC, DMA bounce buffers, ChibiOS USB transport, and the ArduPilot RTC bootloader-hold mechanism. USB includes MSC and CDC; the card is exclusively owned by the host. Reset is refused until logical eject or an inactive absent/error state.

The first 128 KiB of flash remain reserved for the existing bootloader. Artifact validation checks flash placement, CubeOrange+ board ID 1063, vectors, USB descriptors, APJ payload and application descriptor CRCs. It rejects linked flight-HAL, filesystem and flash-writing symbols.

Output is an unsigned development application. Boards requiring signatures need their legitimate signing process. No build command uploads firmware.

## USB identity

The reader uses VID:PID 2DAE:1158 (`READER_USB_VENDOR_ID`/`READER_USB_PRODUCT_ID` in CubeSDCardReader.cpp), with a reader product string and MCU-derived serial. It must not reuse the CubeOrange+ PID 2DAE:1058: CubePilot's Windows INF binds `PID_1058&MI_00` to usbser, which is the reader's MSC interface. With an unlisted PID, Windows loads its in-box USBSTOR and usbser class drivers.

The artifact validator rejects any PID matched by a known CubePilot INF or Cube bootloader (list in tools/validate_artifacts.py). 0x1158 is a development allocation; confirm it with CubePilot before distributing a product. Board packaging ID and USB PID are separate.

## Validation

Host tests compile the actual SCSI and transfer code with address/undefined-behavior sanitizers. They do not prove electrical operation, DMA/cache behavior on hardware, or interoperability with every host. Build manifests report automated checks only; dated hardware evidence is summarized in TESTING.md.
