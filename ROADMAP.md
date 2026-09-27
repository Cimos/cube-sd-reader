# Roadmap

## Done

- Reader firmware: composite USB (MSC + CDC control), raw SDMMC, keeps the installed bootloader. Build validates flash layout, board ID, descriptors and CRCs.
- First bench session on CubeOrange+: 0.81 MB/s write, 1.13 MB/s read, hashes and CHKDSK clean, eject and bootloader return, ArduPlane restore.
- Own USB ID `2DAE:1158`: Windows binds the in-box disk and serial drivers with CubePilot drivers installed. Hardware retested.

## Next

- Confirm PID 0x1158 with CubePilot (development pick today).
- USB protocol fixes from code review: mass-storage reset must leave the CDC port alone; reset the data toggle on clear-halt; START STOP UNIT IMMED and sense handling; 16-bit INQUIRY length.
- Add `reboot` to readerctl.py (documented, not implemented).

## Later

- Linux and exFAT hosts, more cards, external-reader hash check.
- Fault cases: no card, card removal, power loss, long transfers, reconnect loops.
- Media error policy: retry before marking the card failed; keep error state through eject.
- Throughput beyond Full Speed limits.
