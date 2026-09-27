// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool reader_begin_command(void);
void reader_end_command(void);
bool reader_media_ready(void);
bool reader_eject(void);
bool reader_load(void);
void reader_prevent(bool prevent);
bool reader_removal_prevented(void);
void reader_require_reset(uint32_t command_epoch);
void reader_record_io(bool write, uint32_t sectors, bool success);
bool reader_sync_media(void);
uint32_t reader_usb_epoch(void);
bool reader_valid_transfer(const msd_cbw_t *cbw);
uint32_t reader_allocation_length(const uint8_t *cmd);
#ifdef __cplusplus
}
#endif
