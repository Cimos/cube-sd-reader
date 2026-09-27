/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * USB and SD startup adapted from ArduPilot USB_MSD.cpp (GPL-3.0-or-later).
 * CDC configuration follows ArduPilot usbcfg.c / ChibiOS (Apache-2.0).
 */
#include <AP_Common/AP_Common.h>
#include <AP_HAL/AP_HAL.h>
#include <AP_HAL_ChibiOS/hwdef/common/bouncebuffer.h>

#include <AP_HAL_ChibiOS/hwdef/common/stm32_util.h>
#include <AP_HAL_ChibiOS/hwdef/common/watchdog.h>
#include <hal.h>
#include <string.h>
#include <stdio.h>
#include "reader_policy.h"
#include "reader_hooks.h"

#if !defined(STM32H7) || HAL_USE_SDC != TRUE || HAL_USE_USB_MSD != TRUE
#error This target requires CubeOrangePlus SDMMC and USB MSD
#endif
#if !AP_FASTBOOT_ENABLED
#error Bootloader hold support must be enabled
#endif


static Reader::State state;
static volatile uint32_t epoch;
static uint32_t capacity;
static SerialUSBDriver control;
static USBMassStorageDriver storage;
static uint8_t *io_buffers;
alignas(32) static uint8_t fallback_buffers[1024];
static Reader::LineParser parser;
static char usb_serial[25];
static constexpr size_t IO_SIZE = 4096;
static constexpr uint8_t MSC_EP = 1, CDC_EP = 2, NOTIFY_EP = 3;
#if STM32_OTG2_IS_OTG1
static USBDriver *const usb = &USBD2;
#else
static USBDriver *const usb = &USBD1;
#endif

extern "C" bool reader_begin_command()
{
    chSysLock();
    const bool result = state.begin();
    chSysUnlock();
    return result;
}

extern "C" void reader_end_command()
{
    chSysLock();
    state.active = false;
    chSysUnlock();
}

extern "C" bool reader_media_ready()
{
    chSysLock();
    const bool result = state.media == Reader::Media::Ready && !state.quiescing;
    chSysUnlock();
    return result;
}

extern "C" void reader_record_io(bool write, uint32_t sectors, bool success)
{
    chSysLock();
    if (!success) {
        capacity = 0;
        state.media = Reader::Media::Error;
        state.errors++;
    } else if (write) {
        state.written_sectors += sectors;
    } else {
        state.read_sectors += sectors;
    }
    chSysUnlock();
}

extern "C" bool reader_sync_media()
{
    if (!reader_media_ready()) {
        return false;
    }
    const bool ok = blkSync(reinterpret_cast<BaseBlockDevice *>(&SDCD1)) == HAL_SUCCESS;
    if (!ok) {
        reader_record_io(false, 0, false);
    }
    return ok;
}

extern "C" bool reader_eject()
{
    chSysLock();
    const bool prevented = state.prevent_removal;
    const auto media = state.media;
    chSysUnlock();
    if (prevented || media == Reader::Media::Initializing) {
        return false;
    }
    if (media == Reader::Media::Ready && !reader_sync_media()) {
        return false;
    }
    chSysLock();
    state.media = Reader::Media::Ejected;
    chSysUnlock();
    return true;
}

extern "C" bool reader_load()
{
    // Do not reconnect a failed/removed card while the host is using it.
    chSysLock();
    if (state.media == Reader::Media::Ejected && capacity != 0 && !state.quiescing) {
        state.media = Reader::Media::Ready;
    }
    const bool ok = state.media == Reader::Media::Ready && !state.quiescing;
    chSysUnlock();
    return ok;
}

extern "C" void reader_prevent(bool prevent)
{
    chSysLock();
    state.prevent_removal = prevent;
    chSysUnlock();
}

extern "C" uint32_t reader_usb_epoch()
{
    return epoch; // Single-core aligned 32-bit ISR counter; also callable under the kernel lock.
}

// Reader-only USB identity. The CubeOrange+ PID 0x1058 is claimed by CubePilot's
// Windows serial INF (MI_00 -> usbser), which hides the MSC interface. This PID is
// in no CubePilot INF, so Windows binds the in-box USBSTOR and usbser class drivers.
#ifndef READER_USB_VENDOR_ID
#define READER_USB_VENDOR_ID 0x2DAE
#endif
#ifndef READER_USB_PRODUCT_ID
#define READER_USB_PRODUCT_ID 0x1158
#endif
static_assert(READER_USB_PRODUCT_ID != HAL_USB_PRODUCT_ID,
              "Reader must not share the flight/bootloader USB PID");

static const uint8_t device_data[] = {
    USB_DESC_DEVICE(0x0200, 0xEF, 0x02, 0x01, 64,
                    READER_USB_VENDOR_ID, READER_USB_PRODUCT_ID, 0x0300, 1, 2, 3, 1)
};
static const uint8_t configuration_data[] = {
    USB_DESC_CONFIGURATION(98, 3, 1, 0, 0xC0, 50),
    USB_DESC_INTERFACE(0, 0, 2, 0x08, 0x06, 0x50, 0),
    USB_DESC_ENDPOINT(0x01, 2, 64, 0),
    USB_DESC_ENDPOINT(0x81, 2, 64, 0),
    // CDC association: control interface 1, data interface 2.
    8, 11, 1, 2, 2, 2, 1, 0,
    USB_DESC_INTERFACE(1, 0, 1, 2, 2, 1, 0),
    5, 0x24, 0, 0x10, 0x01,
    5, 0x24, 1, 0, 2,
    4, 0x24, 2, 2,
    5, 0x24, 6, 1, 2,
    USB_DESC_ENDPOINT(0x83, 3, 8, 16),
    USB_DESC_INTERFACE(2, 0, 2, 0x0A, 0, 0, 0),
    USB_DESC_ENDPOINT(0x02, 2, 64, 0),
    USB_DESC_ENDPOINT(0x82, 2, 64, 0)
};
static_assert(sizeof(configuration_data) == 98, "USB configuration length");
static const USBDescriptor device_descriptor {sizeof(device_data), device_data};
static const USBDescriptor configuration_descriptor {sizeof(configuration_data), configuration_data};
static const uint8_t language[] = {4, USB_DESCRIPTOR_STRING, 0x09, 0x04};
static uint8_t string_data[3][66];
static USBDescriptor strings[] = {{sizeof(language), language}, {}, {}, {}};

static void make_string(unsigned index, const char *text)
{
    const size_t len = strnlen(text, 32);
    auto *buffer = string_data[index - 1];
    buffer[0] = 2 + 2 * len;
    buffer[1] = USB_DESCRIPTOR_STRING;
    for (size_t i = 0; i < len; i++) {
        buffer[2 + 2 * i] = text[i];
        buffer[3 + 2 * i] = 0;
    }
    strings[index] = {size_t(buffer[0]), buffer};
}

static const USBDescriptor *descriptor(USBDriver *, uint8_t type, uint8_t index, uint16_t)
{
    switch (type) {
    case USB_DESCRIPTOR_DEVICE: return &device_descriptor;
    case USB_DESCRIPTOR_CONFIGURATION: return &configuration_descriptor;
    case USB_DESCRIPTOR_STRING: return index < 4 ? &strings[index] : nullptr;
    default: return nullptr;
    }
}

static USBInEndpointState msc_in, cdc_in, notify_in;
static USBOutEndpointState msc_out, cdc_out;
static const USBEndpointConfig msc_config = {
    USB_EP_MODE_TYPE_BULK, nullptr, nullptr, nullptr, 64, 64,
    &msc_in, &msc_out, 2, nullptr
};
static const USBEndpointConfig cdc_config = {
    USB_EP_MODE_TYPE_BULK, nullptr, sduDataTransmitted, sduDataReceived, 64, 64,
    &cdc_in, &cdc_out, 2, nullptr
};
static const USBEndpointConfig notify_config = {
    USB_EP_MODE_TYPE_INTR, nullptr, sduInterruptTransmitted, nullptr, 8, 0,
    &notify_in, nullptr, 1, nullptr
};

static void configure_endpoints(USBDriver *usbp)
{
    usbInitEndpointI(usbp, MSC_EP, &msc_config);
    usbInitEndpointI(usbp, CDC_EP, &cdc_config);
    usbInitEndpointI(usbp, NOTIFY_EP, &notify_config);
    sduConfigureHookI(&control);
}

static void usb_event(USBDriver *usbp, usbevent_t event)
{
    chSysLockFromISR();
    switch (event) {
    case USB_EVENT_CONFIGURED:
        epoch++;
        if (state.media == Reader::Media::Ejected && capacity && !state.quiescing) {
            state.media = Reader::Media::Ready;
        }
        state.prevent_removal = false;
        configure_endpoints(usbp);
        break;
    case USB_EVENT_RESET:
    case USB_EVENT_UNCONFIGURED:
        epoch++;
        sduSuspendHookI(&control);
        break;
    case USB_EVENT_SUSPEND:
        epoch++;
        sduSuspendHookI(&control);
        break;
    case USB_EVENT_WAKEUP:
        sduWakeupHookI(&control);
        break;
    default: break;
    }
    chSysUnlockFromISR();
}

static bool request(USBDriver *usbp)
{
    const auto *s = usbp->setup;
    if (s[4] == 0 && s[5] == 0) {
        if (s[0] == 0x21 && s[1] == 0xFF &&
            s[2] == 0 && s[3] == 0 && s[6] == 0 && s[7] == 0) {
            // Abort waits so a reset cannot leave a stale CSW in the next session.
            chSysLockFromISR();
            epoch++;
            if (usbGetDriverStateI(usbp) == USB_ACTIVE) {
                usbDisableEndpointsI(usbp);
                configure_endpoints(usbp);
            }
            chSysUnlockFromISR();
            usbSetupTransfer(usbp, nullptr, 0, nullptr);
            return true;
        }
        if (s[0] == 0xA1 && s[1] == 0xFE &&
            s[2] == 0 && s[3] == 0 && s[6] == 1 && s[7] == 0) {
            static uint8_t lun = 0;
            usbSetupTransfer(usbp, &lun, 1, nullptr);
            return true;
        }
    }
    if (s[4] == 1 && s[5] == 0 &&
        ((s[0] == 0xA1 && s[1] == CDC_GET_LINE_CODING && s[6] == 7 && s[7] == 0) ||
         (s[0] == 0x21 && s[1] == CDC_SET_LINE_CODING && s[6] == 7 && s[7] == 0) ||
         (s[0] == 0x21 && s[1] == CDC_SET_CONTROL_LINE_STATE && s[6] == 0 && s[7] == 0))) {
        return sduRequestsHook(usbp);
    }
    return false;
}

static void sof(USBDriver *)
{
    chSysLockFromISR();
    sduSOFHookI(&control);
    chSysUnlockFromISR();
}

static const USBConfig usb_config = {usb_event, descriptor, request, sof};
static const SerialUSBConfig serial_config = {usb, CDC_EP, CDC_EP, NOTIFY_EP};

static void reply(const char *text)
{
    chnWriteTimeout(&control, reinterpret_cast<const uint8_t *>(text), strlen(text), TIME_MS2I(200));
}

static void handle_line(const char *line)
{
    if (strcmp(line, "info") == 0) {
        char response[150];
        snprintf(response, sizeof(response),
                 "OK protocol=1 firmware=0.1.0 board=CubeOrangePlus upstream=4c98c9221a serial=%s\r\n",
                 usb_serial);
        reply(response);
    } else if (strcmp(line, "status") == 0) {
        chSysLock();
        const Reader::State snapshot = state;
        const uint32_t sectors = capacity;
        chSysUnlock();
        static const char *const names[] = {"initializing", "ready", "ejected", "absent", "error"};
        char response[180];
        snprintf(response, sizeof(response),
                 "OK media=%s sectors=%lu active=%u reads=%lu writes=%lu errors=%lu\r\n",
                 names[unsigned(snapshot.media)], (unsigned long)sectors,
                 unsigned(snapshot.active), (unsigned long)snapshot.read_sectors,
                 (unsigned long)snapshot.written_sectors, (unsigned long)snapshot.errors);
        reply(response);
    } else if (strcmp(line, "bootloader") == 0 || strcmp(line, "reboot") == 0) {
        chSysLock();
        const bool allowed = state.request_reboot();
        chSysUnlock();
        if (!allowed) {
            reply("BUSY eject_media_first_or_wait_for_io\r\n");
            return;
        }
        reply("OK rebooting\r\n");
        chThdSleepMilliseconds(250);
        usbDisconnectBus(usb);
        // No SD operation can start after quiescing was set with active=false.
        set_fast_reboot(strcmp(line, "bootloader") == 0 ? RTC_BOOT_HOLD : RTC_BOOT_OFF);
        __DSB();
        __disable_irq();
        NVIC_SystemReset();
    } else {
        reply("ERR unknown_command\r\n");
    }
}

static THD_WORKING_AREA(sd_init_stack, 2048);
static THD_FUNCTION(init_sd, arg)
{
    (void)arg;
    // Raw SDMMC setup from ArduPilot sdcard_init_raw, without filesystem linkage.
    static const SDCConfig config = {SDC_MODE_4BIT, 0};
    bouncebuffer_init(&SDCD1.bouncebuffer, IO_SIZE, true);
    bool connected = false;
    if (SDCD1.bouncebuffer != nullptr && SDCD1.bouncebuffer->dma_buf != nullptr) {
        for (unsigned attempt = 0; attempt < 3 && !connected; attempt++) {
            sdcStart(&SDCD1, &config);
            connected = sdcConnect(&SDCD1) == HAL_SUCCESS;
            if (!connected) {
                sdcStop(&SDCD1);
            }
        }
    }
    BlockDeviceInfo info {};
    const bool ready = connected &&
        blkGetInfo(reinterpret_cast<BaseBlockDevice *>(&SDCD1), &info) == HAL_SUCCESS &&
        info.blk_size == 512 && info.blk_num != 0;
    chSysLock();
    capacity = ready ? info.blk_num : 0;
    state.media = ready ? Reader::Media::Ready : Reader::Media::Absent;
    chSysUnlock();
}

extern "C" int main(int argc, char *const argv[]);
extern "C" int main(int argc, char *const argv[])
{
    (void)argc;
    (void)argv;
    // __late_init already initialized ChibiOS, H757 power and MPU/cache.
    // Deliberately bypass HAL::run: no flight scheduler, filesystem or IO updater.

    peripheral_power_enable();
    make_string(1, "Cube SD Reader Dev");
    make_string(2, "Cube USB Drive");

    const auto *uid = reinterpret_cast<const uint8_t *>(UDID_START);
    for (unsigned i = 0; i < 12; i++) {
        snprintf(usb_serial + i * 2, 3, "%02X", uid[i]);
    }
    make_string(3, usb_serial);

    io_buffers = static_cast<uint8_t *>(malloc_axi_sram(IO_SIZE * 2));
    sduObjectInit(&control);
    sduStart(&control, &serial_config);
    usbDisconnectBus(usb);
    chThdSleepMilliseconds(20);
    usbStart(usb, &usb_config);
    size_t buffer_size = IO_SIZE;
    if (io_buffers == nullptr) {
        // Keep MSC responsive even on low memory; SD DMA uses its bounce buffer.
        io_buffers = fallback_buffers;
        buffer_size = sizeof(fallback_buffers) / 2;
        state.errors++;
    }
    msdObjectInit(&storage);
    msdStart(&storage, usb, reinterpret_cast<BaseBlockDevice *>(&SDCD1), io_buffers,
             io_buffers + buffer_size, buffer_size, nullptr, nullptr, nullptr, nullptr);
    chThdCreateStatic(sd_init_stack, sizeof(sd_init_stack), NORMALPRIO - 2, init_sd, nullptr);
    usbConnectBus(usb);
    stm32_watchdog_init();
    uint32_t last_epoch = reader_usb_epoch();
    while (true) {
        stm32_watchdog_pat();
        const uint32_t current_epoch = reader_usb_epoch();
        if (current_epoch != last_epoch) {
            parser.reset();
            last_epoch = current_epoch;
        }
        const msg_t byte = chnGetTimeout(&control, TIME_MS2I(10));
        const uint32_t received_epoch = reader_usb_epoch();
        if (received_epoch != last_epoch) {
            parser.reset();
            last_epoch = received_epoch;
        }
        if (byte >= 0) {
            switch (parser.push(uint8_t(byte))) {
            case Reader::LineParser::Result::Line: handle_line(parser.line()); break;
            case Reader::LineParser::Result::Invalid: reply("ERR invalid_line\r\n"); break;
            case Reader::LineParser::Result::None: break;
            }
        }
    }
}
