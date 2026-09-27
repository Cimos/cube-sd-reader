/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <hal.h>
#include "reader_hooks.h"

bool reader_valid_transfer(const msd_cbw_t *cbw)
{
    const uint8_t *cmd = cbw->cmd_data;
    uint32_t bytes = 0;
    uint8_t length = 6;
    bool input = true;
    bool allocation = false;
    switch (cmd[0]) {
    case 0x00: case 0x1B: case 0x1E: bytes = 0; break;
    case 0x03: case 0x12: case 0x1A:
        bytes = cmd[4]; allocation = true; break;
    case 0x23:
        length = 10; bytes = ((uint32_t)cmd[7] << 8) | cmd[8]; allocation = true; break;
    case 0x25: length = 10; bytes = 8; allocation = true; break;
    case 0x28: case 0x2A:
        length = 10;
        bytes = (((uint32_t)cmd[7] << 8) | cmd[8]) * 512U;
        input = cmd[0] == 0x28;
        break;
    case 0x35: length = 10; bytes = 0; break;
    default:
        // Unknown commands are rejected by SCSI; no payload is consumed.
        return cbw->cmd_len >= 1 && cbw->cmd_len <= 16;
    }
    if (cbw->cmd_len != length) {
        return false;
    }
    if (cbw->data_len != 0 && (((cbw->flags & 0x80) != 0) != input)) {
        return false;
    }
    return allocation ? cbw->data_len <= bytes : cbw->data_len == bytes;
}
