# Testing

## Automated checks

`python3 tools/dev.py build --jobs 8` runs the host suite and validates the linked firmware package. Tests cover parser/reboot policy, actual SCSI and transfer code under sanitizers, short responses, range/length checks, helper identity, source overlay preservation and corrupt/wrong-board packages.

The ELF/APJ validator checks the reserved flash region, board ID, vectors, composite USB descriptors and application descriptor CRC. A successful build is not hardware qualification.

## Initial bench session — 27 September 2026

CubeOrange+, existing bootloader protocol 5, one 64 GB FAT32 card, Windows host. Reader image: 45,348 bytes. Original and restored firmware: ArduPlane 4.3.7, source revision 2d1a8a28. Board/carrier and card model were not recorded.

| Test | Result |
|---|---|
| Reader flash | Accepted by existing bootloader; uploader verification passed |
| SD and CDC startup | Correct capacity, responsive control |
| 32 MiB write including flush | 41.266 s, **0.813 MB/s** |
| 32 MiB readback | 29.639 s, **1.132 MB/s**, SHA-256 matched |
| Readback after reboot/reflash | 29.596 s, **1.134 MB/s**, SHA-256 matched |
| Empty and small file persistence | Hashes matched |
| Windows CHKDSK | No problems, exit 0 |
| Reset while media ready | Correctly refused |
| Malformed/overlong serial commands | Rejected; subsequent command succeeded |
| Serial open/close and DTR | Five cycles without reset |
| Revised Windows eject helper | Eject and bootloader handshake passed |
| Bootloader hold | Responsive after more than 90 seconds; subsequent reflash passed |
| ArduPilot restoration | Upload verified; correct version and heartbeat observed |
| Power-cycle recovery | Waiting bootloader-port uploader restored ArduPilot without a reader reboot command |
| Parameters after restoration | 983 present; 968 unchanged, 15 changes limited to reset counter and startup calibration values |

No reader SD IO errors were reported during the workload. Decimal MB/s includes host Python/file processing; writes include fsync. Read-after-reboot used a fresh volume instance, but no external-reader verification was performed. This is a small benchmark, not endurance qualification.

## Findings

- Installed CubePilot serial drivers outranked the MSC class driver. A targeted switch to Microsoft's storage driver worked; restored ArduPilot subsequently bound correctly. Automatic driver migration/product USB identity remains unresolved.
- **Current Windows regression:** after reinstalling the reader, Windows again binds the storage interface (`MI_00`) to CubePilot's `oem75.inf`/`usbser` driver because the firmware inherits CubeOrange+ VID:PID `2DAE:1058`. The interface appears as `Cube Orange+ Mavlink (COM21)` and no disk is created, while CDC remains available on `COM23`. Device Manager reports service `usbser` and driver `oem75.inf`; the firmware reports `media=ready` and the SD card is initialized. Switching only `MI_00` to Microsoft's USB Mass Storage driver restores the disk. A follow-up agent should fix the USB identity/descriptor or Windows driver matching without breaking the existing bootloader interface.
- The original Windows EJECT_MEDIA/reopen-CDC helper failed. Keeping CDC and the locked volume open while issuing explicit SCSI cache-sync/eject and then reboot fixed the tested sequence.
- Power-cycle capture proves recovery independent of the reader control command. Deliberate application crash/corruption and damaged-bootloader recovery were not tested.

## Remaining acceptance work

- Additional cards/hosts, including Linux and exFAT; independent hashes through an external reader.
- No-card startup, card removal/errors, power interruption, long-duration transfers.
- Ten ArduPilot/reader round trips, twenty copy/eject cycles, fifty USB reconnects.
- Busy-file helper refusal and multiple-device identity tests on hardware.
- Raw final-sector and invalid-LBA checks; malformed USB/BOT reset/stall recovery.
- Bootloader checksum comparison, stronger FRAM preservation checks and electrical output measurement.
- Deliberate unresponsive-application recovery testing.

Use expendable media for destructive fault tests. Preserve logs locally; do not publish parameter dumps or unique device identifiers.
