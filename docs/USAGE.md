# Use and recovery

Use a CubeOrange+ bench device with outputs disconnected and backed-up or expendable media. The reader is a dedicated application; normal flight operation returns after reflashing ArduPilot.

## Install

Build as described in README.md, then upload `artifacts/reader-dev/CubeSDCardReader.apj` through the existing bootloader using the normal ArduPilot uploader. Do not replace the bootloader or flash the application at address zero.

Expected USB functions are a removable SD disk and CDC control port. A missing card should leave control available; this case still needs hardware acceptance testing.

### Windows driver binding

An installed CubePilot driver may label the storage interface as a Mavlink COM port. Check the interface's compatible ID: storage is class 08, subclass 06, protocol 50. On the tested Windows host, switching that interface to Microsoft's **USB Mass Storage Device** driver enabled the disk.

Select only the reader's storage interface. Do not remove the CubePilot driver package or change unrelated devices. Windows requires administrator rights for the driver switch. Product naming can remain cached from a previous firmware.

## Control

Install Python 3 and the host requirements on the computer physically connected to the Cube:

```sh
python -m pip install -r requirements-host.txt
python tools/readerctl.py --port COM23 info
python tools/readerctl.py --port COM23 status
```

Replace COM23 with the actual port; on Linux use its /dev/ttyACM device. Commands are ASCII with LF or CRLF:

- `info`: version, board and hardware identity.
- `status`: media state, capacity and transfer/error counters.
- `bootloader`: enter the existing ArduPilot bootloader after eject.
- `reboot`: restart the reader under the same eject rules.

Mounted/ready media or active IO causes BUSY. Opening a serial port, changing baud or toggling DTR does not request a reset.

## Windows eject and return to ArduPilot

Run this helper in an elevated PowerShell session:

```powershell
.\tools\Return-ToArduPilot.ps1 -Port COM23 -Drive F:
```

Substitute the actual reader port and disk. It verifies matching USB container identities, requires a single-partition USB disk, and rejects an SD `ardupilot.abin` auto-flash image. It keeps CDC open, locks/flushes/dismounts the volume, sends SCSI cache-sync and logical eject, requests bootloader mode before releasing the lock, then verifies the bootloader handshake.

Busy volumes or identity mismatches stop the operation. Actual firmware upload stays with the normal uploader. The helper needs elevation for raw volume access; the ordinary serial uploader does not.

For manual recovery, eject through the OS and run:

```sh
python tools/readerctl.py --port COM23 bootloader
```

The OS must send logical eject and leave CDC available. The initial Windows EJECT_MEDIA-based helper caused a reset/reload; use the revised combined helper above on Windows.

## Power-cycle fallback

Start the normal uploader targeting the device's known **bootloader port** with the correct CubeOrange+ ArduPilot APJ, then unplug and reconnect USB. On the tested host the bootloader and reader used different COM ports. The waiting uploader caught startup and restored ArduPilot without a reader command.

This does not recover a damaged bootloader. Avoid disconnecting during writes; no firmware can flush data still in the host cache after power disappears.

## Limits

- USB Full Speed; initial sequential throughput is about 0.8 MB/s write and 1.1 MB/s read.
- One Windows host/card tested; Linux interoperability and fault cases remain open.
- Failed/removed-card reinsertion requires reconnect/reboot.
- No flight stack, heater management, local filesystem or autonomous formatting.
- The separate IO MCU is not updated by the reader; electrical output state is not yet measured.
