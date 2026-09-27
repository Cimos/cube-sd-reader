#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Materialize reviewed local driver adaptations from pinned upstream (one-time tool)."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
UP = ROOT / "upstream/ardupilot/modules/ChibiOS"
DEST = ROOT / "firmware/CubeSDCardReader"
records = []


def save(name, source, transform):
    original = (UP / source).read_text()
    result = "\n".join(line.rstrip() for line in transform(original).splitlines()) + "\n"
    (DEST / name).write_text(result)
    records.append({"file": name, "upstream_path": source,
                    "upstream_sha256": hashlib.sha256(original.encode()).hexdigest()})


def replace(text, old, new):
    if old not in text:
        raise RuntimeError("Upstream fragment missing: " + old[:100])
    return text.replace(old, new, 1)


def scsi(s):
    s = replace(s, '#include "lib_scsi.h"', '#include "lib_scsi.h"\n#include "reader_hooks.h"')
    start = s.index('  const SCSITransport *trp = scsip->config->transport;', s.index('static bool transmit_data'))
    end = s.index('\n}\n', start)
    s = s[:start] + """  const SCSITransport *trp = scsip->config->transport;
  if (len > scsip->residue) {
    len = scsip->residue;
  }
  uint32_t sent = trp->transmit(trp, data, len);
  if (sent > len) {
    sent = len;
  }
  scsip->residue -= sent;
  return sent == len ? SCSI_SUCCESS : SCSI_FAILED;""" + s[end:]
    # Allocation length is constrained by the validated CBW, including zero.
    s = replace(s, '((cmd[1] & 0x01) != 0) || (cmd[4] != sizeof(scsi_sense_response_t))',
                '((cmd[1] & 0x01) != 0)')
    s = replace(s, 'scsi_read_format_capacities_response_t ret;', 'scsi_read_format_capacities_response_t ret = {{0}, {0}, {0}};')
    s = replace(s, 'if (req->first_lba + req->blk_cnt > bdi.blk_num)',
                'if (req->first_lba > bdi.blk_num || req->blk_cnt > bdi.blk_num - req->first_lba)')
    s = replace(s, '  scsip->residue = 0;\n\n  if (data_overflow', '  /* CBW residue is initialized by the transport. */\n\n  if (data_overflow')
    s = replace(s, 'if (blkRead(blkdev, req.first_lba + i, buf, n) != HAL_SUCCESS) {',
                'if (blkRead(blkdev, req.first_lba + i, buf, n) != HAL_SUCCESS) {\n          reader_record_io(false, 0, false);')
    s = replace(s, 'scsip->config->blkbuf[buf_idx], n) != HAL_SUCCESS) {',
                'scsip->config->blkbuf[buf_idx], n) != HAL_SUCCESS) {\n          reader_record_io(true, 0, false);')
    # Ensure final card programming completes before CSW success.
    marker = '\n  return SCSI_SUCCESS;\n}\n\n/**\n * @brief   SCSI test unit ready'
    s = replace(s, marker, """
  if (cmd[0] == SCSI_CMD_WRITE_10 && !reader_sync_media()) {
    set_sense(scsip, SCSI_SENSE_KEY_MEDIUM_ERROR, 0, 0);
    return SCSI_FAILED;
  }
  reader_record_io(cmd[0] == SCSI_CMD_WRITE_10, req.blk_cnt, true);
  scsip->residue = 0;
  return SCSI_SUCCESS;
}

/**
 * @brief   SCSI test unit ready""")
    s = replace(s, '  bool ret = SCSI_SUCCESS;\n\n  switch (cmd[0])', """
  bool ret = SCSI_SUCCESS;
  if (cmd[0] != SCSI_CMD_INQUIRY && cmd[0] != SCSI_CMD_REQUEST_SENSE &&
      cmd[0] != SCSI_CMD_START_STOP_UNIT &&
      cmd[0] != SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL && !reader_media_ready()) {
    set_sense(scsip, SCSI_SENSE_KEY_NOT_READY, SCSI_ASENSE_MEDIUM_NOT_PRESENT, 0);
    return SCSI_FAILED;
  }

  switch (cmd[0])""")
    s = replace(s, '    ret = cmd_ignored(scsip, cmd);\n    break;\n\n  case SCSI_CMD_MODE_SENSE_6:',
                """    if ((cmd[4] & ~1U) != 0) {
      ret = cmd_unhandled(scsip, cmd);
    } else {
      reader_prevent(cmd[4] != 0);
    }
    break;

  case SCSI_CMD_START_STOP_UNIT:
    if (cmd[1] != 0 || (cmd[4] & ~3U) != 0) {
      ret = cmd_unhandled(scsip, cmd);
    } else if ((cmd[4] & 1U) != 0) {
      ret = reader_load() ? SCSI_SUCCESS : SCSI_FAILED;
    } else if ((cmd[4] & 2U) != 0) {
      ret = reader_eject() ? SCSI_SUCCESS : SCSI_FAILED;
    } else {
      ret = reader_sync_media() ? SCSI_SUCCESS : SCSI_FAILED;
    }
    if (ret != SCSI_SUCCESS) {
      set_sense(scsip, SCSI_SENSE_KEY_NOT_READY, SCSI_ASENSE_LOGICAL_UNIT_NOT_READY, 0);
    }
    break;

  case 0x35: /* SYNCHRONIZE CACHE(10), synchronous, no volatile write cache. */
    if (cmd[1] != 0) {
      ret = cmd_unhandled(scsip, cmd);
    } else if (!reader_sync_media()) {
      set_sense(scsip, SCSI_SENSE_KEY_MEDIUM_ERROR, 0, 0);
      ret = SCSI_FAILED;
    }
    break;

  case SCSI_CMD_MODE_SENSE_6:""")
    s = replace(s, '    ret = cmd_ignored(scsip, cmd);', '    ret = cmd_unhandled(scsip, cmd);')
    a = s.index('static bool cmd_ignored(')
    b = s.index('\n}\n', a)+3
    s = s[:a] + s[b:]
    return s


def msd(s):
    # Transport reuse with reset epochs, CBW validation and controlled admission.
    s = replace(s, '#include "hal.h"', '#include "hal.h"\n#include "reader_hooks.h"\nstatic uint32_t command_epoch;')
    s = replace(s, 'if (((cbw->cmd_len & CBW_CMD_LEN_RESERVED_MASK) != 0)',
                'if ((cbw->cmd_len == 0 || cbw->cmd_len > 16)\n      || ((cbw->cmd_len & CBW_CMD_LEN_RESERVED_MASK) != 0)')
    # Original calls inside IO worker and blocking transport functions.
    s = s.replace('msg_t status = usbTransmit(trp->usbp, trp->ep, data, len);',
                  'msg_t status = command_epoch == reader_usb_epoch() ? usbTransmit(trp->usbp, trp->ep, data, len) : MSG_RESET;')
    s = s.replace('msg_t status = usbReceive(trp->usbp, trp->ep, data, len);',
                  'msg_t status = command_epoch == reader_usb_epoch() ? usbReceive(trp->usbp, trp->ep, data, len) : MSG_RESET;')
    s = s.replace('if (trp->io_pending) {', 'if (trp->io_pending || command_epoch != reader_usb_epoch()) {')
    s = s.replace('msg_t status = usbTransmit(trp->usbp, trp->ep, trp->iobuf, trp->iolen);',
                  'msg_t status = command_epoch == reader_usb_epoch() ? usbTransmit(trp->usbp, trp->ep, trp->iobuf, trp->iolen) : MSG_RESET;')
    s = s.replace('msg_t status = usbReceive(trp->usbp, trp->ep, trp->iobuf, trp->iolen);',
                  'msg_t status = command_epoch == reader_usb_epoch() ? usbReceive(trp->usbp, trp->ep, trp->iobuf, trp->iolen) : MSG_RESET;')
    a = s.index('static THD_FUNCTION(usb_msd_worker, arg)')
    b = s.index('\n/**', a)
    s = s[:a] + """static void stall_data(USBMassStorageDriver *msdp, bool both) {
  osalSysLock();
  if (usbGetDriverStateI(msdp->usbp) == USB_ACTIVE && command_epoch == reader_usb_epoch()) {
    if (both || (msdp->cbw.flags & 0x80)) {
      usbStallTransmitI(msdp->usbp, USB_MSD_DATA_EP);
    }
    if (both || !(msdp->cbw.flags & 0x80)) {
      usbStallReceiveI(msdp->usbp, USB_MSD_DATA_EP);
    }
  }
  osalSysUnlock();
}

static THD_FUNCTION(usb_msd_worker, arg) {
  USBMassStorageDriver *msdp = arg;
  chRegSetThreadName("reader_msd");

  while (!chThdShouldTerminateX()) {
    const uint32_t received_epoch = reader_usb_epoch();
    const msg_t received = usbReceive(msdp->usbp, USB_MSD_DATA_EP,
                                     (uint8_t *)&msdp->cbw, sizeof(msd_cbw_t));
    if (received == MSG_RESET || received_epoch != reader_usb_epoch()) {
      osalThreadSleepMilliseconds(10);
      continue;
    }
    command_epoch = received_epoch;
    if (!cbw_valid(&msdp->cbw, received) || !cbw_meaningful(&msdp->cbw)) {
      stall_data(msdp, true);
      while (command_epoch == reader_usb_epoch()) {
        osalThreadSleepMilliseconds(10);
      }
      continue;
    }
    msdp->scsi_target.residue = msdp->cbw.data_len;
    if (!reader_valid_transfer(&msdp->cbw)) {
      stall_data(msdp, msdp->cbw.data_len == 0);
      send_csw(msdp, CSW_STATUS_PHASE_ERROR, msdp->cbw.data_len);
      while (command_epoch == reader_usb_epoch()) {
        osalThreadSleepMilliseconds(10);
      }
      continue;
    }
    if (!reader_begin_command()) {
      // A reset is imminent. Admit no new access to the card.
      osalThreadSleepMilliseconds(10);
      continue;
    }
    const bool result = scsiExecCmd(&msdp->scsi_target, msdp->cbw.cmd_data);
    if (command_epoch == reader_usb_epoch()) {
      if (result != SCSI_SUCCESS && scsiResidue(&msdp->scsi_target) != 0) {
        stall_data(msdp, false);
      }
      send_csw(msdp, result == SCSI_SUCCESS ? CSW_STATUS_PASSED : CSW_STATUS_FAILED,
               scsiResidue(&msdp->scsi_target));
    }
    reader_end_command();
  }
  chThdExit(MSG_OK);
}
""" + s[b:]

    # Check the reset generation and start each transfer under the same lock.
    # Otherwise a reset between the check and usbReceive could admit stale IO.
    marker = "static uint32_t command_epoch;"
    guarded = """static uint32_t command_epoch;

static msg_t guarded_receive(USBDriver *usbp, usbep_t ep, uint8_t *buf,
                             size_t length, bool command) {
  osalSysLock();
  if (usbGetDriverStateI(usbp) != USB_ACTIVE ||
      (!command && command_epoch != reader_usb_epoch())) {
    osalSysUnlock();
    return MSG_RESET;
  }
  if (command) {
    command_epoch = reader_usb_epoch();
  }
  usbStartReceiveI(usbp, ep, buf, length);
  msg_t result = osalThreadSuspendS(&usbp->epc[ep]->out_state->thread);
  osalSysUnlock();
  return result;
}

static msg_t guarded_transmit(USBDriver *usbp, usbep_t ep,
                              const uint8_t *buf, size_t length) {
  osalSysLock();
  if (usbGetDriverStateI(usbp) != USB_ACTIVE ||
      command_epoch != reader_usb_epoch()) {
    osalSysUnlock();
    return MSG_RESET;
  }
  usbStartTransmitI(usbp, ep, buf, length);
  msg_t result = osalThreadSuspendS(&usbp->epc[ep]->in_state->thread);
  osalSysUnlock();
  return result;
}"""
    s = replace(s, marker, guarded)
    s = s.replace("usbTransmit(trp->usbp, trp->ep, data, len)",
                  "guarded_transmit(trp->usbp, trp->ep, data, len)")
    s = s.replace("usbReceive(trp->usbp, trp->ep, data, len)",
                  "guarded_receive(trp->usbp, trp->ep, data, len, false)")
    s = s.replace("usbTransmit(trp->usbp, trp->ep, trp->iobuf, trp->iolen)",
                  "guarded_transmit(trp->usbp, trp->ep, trp->iobuf, trp->iolen)")
    s = s.replace("usbReceive(trp->usbp, trp->ep, trp->iobuf, trp->iolen)",
                  "guarded_receive(trp->usbp, trp->ep, trp->iobuf, trp->iolen, false)")
    s = s.replace("usbTransmit(msdp->usbp, USB_MSD_DATA_EP, (uint8_t *)&msdp->csw,",
                  "guarded_transmit(msdp->usbp, USB_MSD_DATA_EP, (uint8_t *)&msdp->csw,")
    s = s.replace("    const uint32_t received_epoch = reader_usb_epoch();", "")
    s = s.replace("const msg_t received = usbReceive(msdp->usbp, USB_MSD_DATA_EP,",
                  "const msg_t received = guarded_receive(msdp->usbp, USB_MSD_DATA_EP,")
    s = s.replace("(uint8_t *)&msdp->cbw, sizeof(msd_cbw_t));",
                  "(uint8_t *)&msdp->cbw, sizeof(msd_cbw_t), true);")
    s = s.replace("received_epoch != reader_usb_epoch()", "command_epoch != reader_usb_epoch()")
    s = s.replace("    command_epoch = received_epoch;", "")

    return s


def sdc(s):
    s = replace(s, '  bool cmd_fail;\n\n  while (true) {',
                '  bool cmd_fail;\n  const systime_t started = chVTGetSystemTimeX();\n\n  while (true) {\n    if (chVTTimeElapsedSinceX(started) >= TIME_MS2I(2000)) {\n      return HAL_FAILED;\n    }')
    s = replace(s, '  result = sdc_lld_sync(sdcp);', '  result = _sdc_wait_for_transfer_state(sdcp);')
    return s


save("reader_scsi.c", "os/various/scsi_bindings/lib_scsi.c", scsi)
save("reader_msd.c", "os/hal/src/hal_usb_msd.c", msd)
save("reader_sdc.c", "os/hal/src/hal_sdc.c", sdc)
(ROOT / "firmware/vendor-sources.json").write_text(json.dumps(records, indent=2) + "\n")
