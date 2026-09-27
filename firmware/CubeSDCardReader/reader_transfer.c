/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <hal.h>
#include "reader_hooks.h"

typedef struct {
    uint32_t bytes;
    uint8_t length;
    bool input;
    bool allocation;
    bool known;
} transfer_shape_t;

static transfer_shape_t shape(const uint8_t *cmd)
{
    transfer_shape_t t = {0, 6, true, false, true};
    switch (cmd[0]) {
    case 0x00: case 0x1B: case 0x1E: break;
    case 0x03: case 0x1A:
        t.bytes = cmd[4]; t.allocation = true; break;
    case 0x12: // INQUIRY allocation length is 16 bits (SPC-3).
        t.bytes = ((uint32_t)cmd[3] << 8) | cmd[4]; t.allocation = true; break;
    case 0x23:
        t.length = 10; t.bytes = ((uint32_t)cmd[7] << 8) | cmd[8]; t.allocation = true; break;
    case 0x25: t.length = 10; t.bytes = 8; t.allocation = true; break;
    case 0x28: case 0x2A:
        t.length = 10;
        t.bytes = (((uint32_t)cmd[7] << 8) | cmd[8]) * 512U;
        t.input = cmd[0] == 0x28;
        break;
    case 0x35: t.length = 10; break;
    default: t.known = false; break;
    }
    return t;
}

bool reader_valid_transfer(const msd_cbw_t *cbw)
{
    const transfer_shape_t t = shape(cbw->cmd_data);
    if (!t.known) {
        // Unknown commands are rejected by SCSI; no payload is consumed.
        return cbw->cmd_len >= 1 && cbw->cmd_len <= 16;
    }
    if (cbw->cmd_len != t.length) {
        return false;
    }
    if (cbw->data_len != 0 && (((cbw->flags & 0x80) != 0) != t.input)) {
        return false;
    }
    // A host transfer longer than the allocation length is legal (BOT cases 4/5):
    // the reply is capped by reader_allocation_length() and ends short with residue.
    return t.allocation || cbw->data_len == t.bytes;
}

uint32_t reader_allocation_length(const uint8_t *cmd)
{
    const transfer_shape_t t = shape(cmd);
    return t.known && t.allocation ? t.bytes : UINT32_MAX;
}
