# Testing

## Automated checks

`python3 tools/dev.py build --jobs 8` runs the host suite and validates the linked firmware package. Tests cover parser/reboot policy, actual SCSI and transfer code under sanitizers, short responses, allocation-length capping, START STOP UNIT and SYNCHRONIZE CACHE fields, card IO retries, range/length checks, helper identity, source overlay preservation and cleanup, USB identity and disk name, and corrupt/wrong-board packages.

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

## USB identity retest — 27 September 2026

Same CubeOrange+ and 64 GB FAT32 card, same Windows host with CubePilot drivers still installed. Reader image with USB ID `2DAE:1158`, 45,348 bytes, flashed through the existing bootloader by power-cycle capture (board type 1063, verify passed).

| Test | Result |
|---|---|
| Driver binding, no manual change | `MI_00` USB Mass Storage Device (USBSTOR), `MI_01` USB Serial Device (usbser, COM24), parent usbccgp |
| Disk | Online, 62,534,975,488 bytes, MBR, FAT32 volume mounted as D: |
| 32 MiB file hash read | 29.7 s, about 1.13 MB/s |
| Reader status after read | `media=ready`, 0 errors |

Windows reported the volume health as Warning after the unplug used for flashing, most likely the FAT dirty flag. CHKDSK was not rerun.

## v0.1.0 session — 27 September 2026

Same CubeOrange+ and card. v0.1.0 (46,152 bytes) adds the disk name and the Bulk-Only/SCSI fixes. A CubeOrange was also attached and was not touched.

| Test | Result |
|---|---|
| Windows eject of the mounted disk | Reader reported `media=ejected`, 0 errors |
| `readerctl bootloader` after eject | Accepted; waiting uploader found board type 1063, verify passed, no replug |
| Enumeration | `2DAE:1158`, disk named **Cube USB Drive**, USBSTOR + usbser, no driver change |
| 16 MiB write including flush | 18.1 s, about 0.93 MB/s; file hash matched, file removed |
| 32 MiB uncached read | 29.6 s, about 1.13 MB/s; SHA-256 matches the first session's manifest |
| Reader status afterwards | `media=ready`, 0 errors |

The Bulk-Only reset, clear-halt and START STOP changes are covered by host tests and this normal-use run; forced USB error recovery was not exercised on hardware.

## Findings

- Installed CubePilot serial drivers outranked the MSC class driver. A targeted switch to Microsoft's storage driver worked; restored ArduPilot subsequently bound correctly. Superseded by the distinct reader USB identity below.
- **Windows driver collision (fixed):** the first bench image used CubeOrange+ VID:PID `2DAE:1058`, so CubePilot's `oem75.inf` bound `MI_00` (the storage interface) to `usbser`. It showed as `Cube Orange+ Mavlink (COM21)` with no disk while the reader reported `media=ready`. The reader now uses `2DAE:1158`, which no installed CubePilot INF lists (checked against oem73/74/75 on the bench host); the build validator enforces this.
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
