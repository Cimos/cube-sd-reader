// SPDX-License-Identifier: GPL-3.0-or-later
// Standard unsigned ArduPilot application descriptor, patched by Waf after link.
#define AP_CHECK_FIRMWARE_ENABLED 1
#include <AP_CheckFirmware/AP_CheckFirmware.h>

const app_descriptor_unsigned reader_app_descriptor
    __attribute__((used, section(".app_descriptor"))) = {
    AP_APP_DESCRIPTOR_SIGNATURE_UNSIGNED,
    0, 0, 0, 0,
    0, 1, APJ_BOARD_ID,
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
};
