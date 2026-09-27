# Third-party sources

ArduPilot is pinned at 4c98c9221ad6933ab1c70fba8a3900ba2ff02f3b.
Its ChibiOS submodule is pinned at 9aebaf4a40442277d01bd9dd38c13713fc45ef41.

The project is GPL-3.0-or-later; see the root LICENSE. Original notices are retained in adapted files.

- Reader USB/SD startup: adapted from ArduPilot USB_MSD.cpp and sdcard.cpp, GPL-3.0-or-later.
- CDC descriptors/callback arrangement: follows ArduPilot usbcfg.c and ChibiOS serial USB support.
- reader_msd.c: adapted from ChibiOS os/hal/src/hal_usb_msd.c, Apache-2.0 header retained.
- reader_scsi.c: adapted from ChibiOS os/various/scsi_bindings/lib_scsi.c, Apache-2.0 header retained.
- reader_sdc.c: adapted from ChibiOS os/hal/src/hal_sdc.c; original license header retained.
- Standard application descriptor: ArduPilot AP_CheckFirmware layout.

firmware/vendor-sources.json records original paths and hashes. tools/prepare_drivers.py records the local changes. Upstream source trees are unmodified. The build manifest hashes the final local source files.

This development was AI-assisted.
