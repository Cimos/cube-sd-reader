# Roadmap

## Done

- Reader firmware: composite USB (MSC + CDC control), raw SDMMC, keeps the installed bootloader. Build validates flash layout, board ID, descriptors and CRCs.
- First bench session on CubeOrange+: 0.81 MB/s write, 1.13 MB/s read, hashes and CHKDSK clean, eject and bootloader return, ArduPlane restore.
- Own USB ID `2DAE:1158`: Windows binds the in-box disk and serial drivers with CubePilot drivers installed.
- v0.1.0: disk named "Cube USB Drive"; Bulk-Only reset leaves the control port alone; clear-halt resets the data toggle; invalid CBW keeps endpoints halted until reset; 16-bit INQUIRY length with replies capped at the allocation length; START STOP UNIT IMMED/LoEj/sense fixes; card IO retries; eject keeps failure state; `readerctl reboot`; stale overlay cleanup. Hardware retested.

## Next

- Forced USB error recovery on hardware (Bulk-Only reset and clear-halt paths).
- Linux and exFAT hosts, more cards, external-reader hash check.

## Later

- Fault cases: no card, card removal, power loss, long transfers, reconnect loops.
- Round-trip endurance: ArduPilot/reader cycles, copy/eject cycles, USB reconnects.
- Throughput beyond Full Speed limits.
