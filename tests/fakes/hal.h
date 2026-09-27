#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CC_PACK __attribute__((packed))
#define HAL_SUCCESS false
#define HAL_FAILED true
typedef struct { uint32_t blk_num; uint32_t blk_size; } BlockDeviceInfo;
typedef struct { int unused; } BaseBlockDevice;
typedef struct {
    uint32_t signature, tag, data_len;
    uint8_t flags, lun, cmd_len, cmd_data[16];
} msd_cbw_t;
bool blkGetInfo(BaseBlockDevice *, BlockDeviceInfo *);
bool blkIsInserted(BaseBlockDevice *);
bool blkIsWriteProtected(BaseBlockDevice *);
bool blkRead(BaseBlockDevice *, uint32_t, uint8_t *, size_t);
bool blkWrite(BaseBlockDevice *, uint32_t, const uint8_t *, size_t);
#include "lib_scsi.h"
