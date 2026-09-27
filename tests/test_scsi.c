#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "hal.h"
#include "reader_hooks.h"

static uint8_t media[32 * 512], host[32 * 512], buffers[2][512];
static size_t position, pending;
static unsigned reads, writes, syncs;
static bool ready, prevented, fail_sync;
static BaseBlockDevice device;
static SCSITarget target;
static const scsi_inquiry_response_t inquiry = {.vendorID = "TEST"};
static const scsi_unit_serial_number_inquiry_response_t serial = {.page_code = 0x80};
static SCSITransport transport;
static SCSITargetConfig config;

bool blkGetInfo(BaseBlockDevice *d, BlockDeviceInfo *info)
{
    (void)d; info->blk_num = 32; info->blk_size = 512; return HAL_SUCCESS;
}
bool blkIsInserted(BaseBlockDevice *d) { (void)d; return ready; }
bool blkIsWriteProtected(BaseBlockDevice *d) { (void)d; return false; }
bool blkRead(BaseBlockDevice *d, uint32_t first, uint8_t *out, size_t count)
{
    (void)d; assert(first <= 32 && count <= 32 - first);
    reads++; memcpy(out, media + first * 512, count * 512); return HAL_SUCCESS;
}
bool blkWrite(BaseBlockDevice *d, uint32_t first, const uint8_t *in, size_t count)
{
    (void)d; assert(first <= 32 && count <= 32 - first);
    writes++; memcpy(media + first * 512, in, count * 512); return HAL_SUCCESS;
}
bool reader_media_ready(void) { return ready; }
bool reader_sync_media(void) { syncs++; return ready && !fail_sync; }
bool reader_eject(void)
{
    if (prevented || !reader_sync_media()) { return false; }
    ready = false; return true;
}
bool reader_load(void) { ready = true; return true; }
void reader_prevent(bool value) { prevented = value; }
void reader_record_io(bool write, uint32_t sectors, bool success)
{ (void)write; (void)sectors; if (!success) { ready = false; } }

static uint32_t transmit(const SCSITransport *t, const uint8_t *data, size_t len)
{
    (void)t; assert(position + len <= sizeof(host));
    memcpy(host + position, data, len); position += len; return len;
}
static uint32_t receive(const SCSITransport *t, uint8_t *data, size_t len)
{
    (void)t; assert(position + len <= sizeof(host));
    memcpy(data, host + position, len); position += len; return len;
}
static uint32_t tx_start(const SCSITransport *t, const uint8_t *data, size_t len)
{ pending = transmit(t, data, len); return pending; }
static uint32_t rx_start(const SCSITransport *t, uint8_t *data, size_t len)
{ pending = receive(t, data, len); return pending; }
static uint32_t wait_io(const SCSITransport *t) { (void)t; return pending; }

static bool execute(uint8_t *cmd, uint32_t bytes)
{
    position = 0; target.residue = bytes;
    return scsiExecCmd(&target, cmd);
}
static void reset(void)
{
    ready = true; prevented = fail_sync = false;
    reads = writes = syncs = 0;
    memset(host, 0xCC, sizeof(host));
    memset(media, 0x5A, sizeof(media));
    scsiObjectInit(&target); scsiStart(&target, &config);
}
int main(void)
{
    transport = (SCSITransport){.transmit = transmit, .receive = receive,
        .transmit_start = tx_start, .receive_start = rx_start, .wait = wait_io};
    config = (SCSITargetConfig){.transport = &transport, .blkdev = &device,
        .blkbuf = {buffers[0], buffers[1]}, .blkbuf_size = 512,
        .inquiry_response = &inquiry, .unit_serial_number_inquiry_response = &serial};
    uint8_t cmd[16] = {0};
    reset();
    cmd[0] = 0x12; cmd[4] = 4;
    assert(execute(cmd, 4) == SCSI_SUCCESS && position == 4 && target.residue == 0);
    cmd[0] = 0x03; cmd[4] = 8;
    assert(execute(cmd, 8) == SCSI_SUCCESS && position == 8);
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0x23; cmd[8] = 12;
    assert(execute(cmd, 12) == SCSI_SUCCESS && host[0] == 0 && host[1] == 0 && host[2] == 0);
    reset();
    memset(cmd, 0, sizeof(cmd)); cmd[0] = 0x28;
    memset(cmd + 2, 0xFF, 4); cmd[8] = 2;
    assert(execute(cmd, 1024) == SCSI_FAILED && reads == 0 && target.residue == 1024);
    memset(cmd + 2, 0, 4);
    assert(execute(cmd, 1024) == SCSI_SUCCESS && reads == 2 && position == 1024);
    assert(memcmp(host, media, 1024) == 0);
    reset();
    cmd[0] = 0x2A;
    assert(execute(cmd, 1024) == SCSI_SUCCESS && writes == 2 && syncs == 1);
    assert(memcmp(host, media, 1024) == 0);
    fail_sync = true;
    assert(execute(cmd, 1024) == SCSI_FAILED);
    reset();
    memset(cmd, 0, sizeof(cmd)); cmd[0] = 0x35;
    assert(execute(cmd, 0) == SCSI_SUCCESS && syncs == 1);
    cmd[0] = 0x1E; cmd[4] = 1;
    assert(execute(cmd, 0) == SCSI_SUCCESS);
    cmd[0] = 0x1B; cmd[4] = 2;
    assert(execute(cmd, 0) == SCSI_FAILED && ready);
    cmd[0] = 0x1E; cmd[4] = 0;
    assert(execute(cmd, 0) == SCSI_SUCCESS);
    cmd[0] = 0x1B; cmd[4] = 2;
    assert(execute(cmd, 0) == SCSI_SUCCESS && !ready);
    cmd[0] = 0x25; cmd[4] = 0;
    assert(execute(cmd, 8) == SCSI_FAILED && target.sense.byte[12] == 0x3A);
    msd_cbw_t cbw = {.cmd_len = 10, .data_len = 512, .flags = 0x80,
                    .cmd_data = {0x28, 0, 0, 0, 0, 0, 0, 0, 1}};
    assert(reader_valid_transfer(&cbw));
    cbw.data_len = 511; assert(!reader_valid_transfer(&cbw));
    cbw.data_len = 512; cbw.flags = 0; assert(!reader_valid_transfer(&cbw));
    cbw.flags = 0x80; cbw.cmd_len = 6; assert(!reader_valid_transfer(&cbw));
    puts("SCSI/transfer tests passed");
    return 0;
}
