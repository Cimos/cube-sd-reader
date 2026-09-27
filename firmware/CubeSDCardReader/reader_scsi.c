/*
    ChibiOS/HAL - Copyright (C) 2016 Uladzimir Pylinsky aka barthess

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    lib_scsi.c
 * @brief   SCSI target driver source code.
 *
 * @addtogroup SCSI
 * @{
 */

#include <string.h>

#include "hal.h"

#include "lib_scsi.h"
#include "reader_hooks.h"

/* Allocation length of the command being executed; replies never exceed it. */
static uint32_t allocation_limit = UINT32_MAX;

#define DEBUG_TRACE_PRINT     FALSE
#define DEBUG_TRACE_WARNING   FALSE
#define DEBUG_TRACE_ERROR     FALSE
// #include "dbgtrace.h"

#define ARCH_LITTLE_ENDIAN
#include "bswap.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

typedef struct {
  uint32_t first_lba;
  uint16_t blk_cnt;
} data_request_t;

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Combines data request from byte array.
 *
 * @notapi
 */
static data_request_t decode_data_request(const uint8_t *cmd) {

  data_request_t req;
  uint32_t lba;
  uint16_t blk;

  memcpy(&lba, &cmd[2], sizeof(lba));
  memcpy(&blk, &cmd[7], sizeof(blk));

  req.first_lba = be32_to_cpu(lba);
  req.blk_cnt = be16_to_cpu(blk);

  return req;
}

/**
 * @brief   Fills sense structure.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] key     SCSI sense key
 * @param[in] code    SCSI sense code
 * @param[in] qual    SCSI sense qualifier
 *
 * @notapi
 */
static void set_sense(SCSITarget *scsip, uint8_t key,
                      uint8_t code, uint8_t qual) {

  scsi_sense_response_t *sense = &scsip->sense;
  memset(sense, 0 , sizeof(scsi_sense_response_t));

  sense->byte[0]  = 0x70;
  sense->byte[2]  = key;
  sense->byte[7]  = 8;
  sense->byte[12] = code;
  sense->byte[13] = qual;
}

/**
 * @brief   Sets all values in sense data to 'success' condition.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 *
 * @notapi
 */
static void set_sense_ok(SCSITarget *scsip) {
  set_sense(scsip, SCSI_SENSE_KEY_GOOD,
                   SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                   SCSI_ASENSEQ_NO_QUALIFIER);
}

/**
 * @brief   Transmits data via transport channel.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] data    pointer to data buffer
 * @param[in] len     number of bytes to be transmitted
 *
 * @return            The operation status.
 *
 * @notapi
 */
static bool transmit_data(SCSITarget *scsip, const uint8_t *data, uint32_t len) {

  const SCSITransport *trp = scsip->config->transport;
  if (len > allocation_limit) {
    len = allocation_limit;
  }
  if (len > scsip->residue) {
    len = scsip->residue;
  }
  uint32_t sent = trp->transmit(trp, data, len);
  if (sent > len) {
    sent = len;
  }
  scsip->residue -= sent;
  return sent == len ? SCSI_SUCCESS : SCSI_FAILED;
}

/**
 * @brief   Stub for unhandled SCSI commands.
 * @details Sets error flags in sense data structure and returns error error.
 */
static bool cmd_unhandled(SCSITarget *scsip, const uint8_t *cmd) {
  (void)cmd;

  set_sense(scsip, SCSI_SENSE_KEY_ILLEGAL_REQUEST,
                   SCSI_ASENSE_INVALID_COMMAND,
                   SCSI_ASENSEQ_NO_QUALIFIER);
  return SCSI_FAILED;
}

/**
 * @brief   Stub for unrealized but required SCSI commands.
 * @details Sets sense data in 'all OK' condition and returns success status.
 */

/**
 * @brief   SCSI inquiry command handler.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @notapi
 */
static bool inquiry(SCSITarget *scsip, const uint8_t *cmd) {

  if ((cmd[1] & 0b1) && cmd[2] == 0x80) {
    /* Unit serial number page */
    return transmit_data(scsip, (const uint8_t *)scsip->config->unit_serial_number_inquiry_response,
                                sizeof(scsi_unit_serial_number_inquiry_response_t));
  }
  else if ((cmd[1] & 0b11) || cmd[2] != 0) {
    set_sense(scsip, SCSI_SENSE_KEY_ILLEGAL_REQUEST,
                     SCSI_ASENSE_INVALID_FIELD_IN_CDB,
                     SCSI_ASENSEQ_NO_QUALIFIER);
    return SCSI_FAILED;
  }
  else {
    return transmit_data(scsip, (const uint8_t *)scsip->config->inquiry_response,
                                sizeof(scsi_inquiry_response_t));
  }
}

/**
 * @brief   SCSI request sense command handler.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @notapi
 */
static bool request_sense(SCSITarget *scsip, const uint8_t *cmd) {

  if (((cmd[1] & 0x01) != 0)) {
    set_sense(scsip, SCSI_SENSE_KEY_ILLEGAL_REQUEST,
                     SCSI_ASENSE_INVALID_FIELD_IN_CDB,
                     SCSI_ASENSEQ_NO_QUALIFIER);
    return SCSI_FAILED;
  }
  else {
    return transmit_data(scsip, (uint8_t *)&scsip->sense,
                                 sizeof(scsi_sense_response_t));
  }
}

/**
 * @brief   SCSI mode sense (6) command handler.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @notapi
 */
static bool mode_sense6(SCSITarget *scsip, const uint8_t *cmd) {
  (void)cmd;

  scsip->mode_sense.byte[0] = sizeof(scsi_mode_sense6_response_t) - 1;
  scsip->mode_sense.byte[1] = 0;
  if (blkIsWriteProtected(scsip->config->blkdev)) {
    scsip->mode_sense.byte[2] = 0x01 << 7;
  }
  else {
    scsip->mode_sense.byte[2] = 0;
  }
  scsip->mode_sense.byte[3] = 0;

  return transmit_data(scsip, (uint8_t *)&scsip->mode_sense,
                              sizeof(scsi_mode_sense6_response_t));
}

/**
 * @brief   SCSI read format capacities command handler.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @notapi
 */
static bool read_format_capacities(SCSITarget *scsip, const uint8_t *cmd) {

  /* An Allocation Length of zero indicates that no data shall be transferred.
     This condition shall not be considered as an error. The Logical Unit
     shall terminate the data transfer when Allocation Length bytes have
     been transferred or when all available data have been transferred to
     the Initiator, whatever is less. */

  uint16_t len = cmd[7] << 8 | cmd[8];

  if (0 == len) {
    return SCSI_SUCCESS;
  }
  else {
    scsi_read_format_capacities_response_t ret = {{0}, {0}, {0}};
    BlockDeviceInfo bdi;
    blkGetInfo(scsip->config->blkdev, &bdi);

    uint32_t tmp = cpu_to_be32(bdi.blk_num);
    memcpy(ret.blocknum, &tmp, 4);

    uint8_t formatted_media = 0b10;
    uint16_t blocklen = bdi.blk_size;
    ret.blocklen[0] = formatted_media;
    ret.blocklen[1] = 0;
    ret.blocklen[2] = blocklen >> 8;
    ret.blocklen[3] = blocklen & 0xFF;

    ret.header[3] = 1 * 8;

    return transmit_data(scsip, (uint8_t *)&ret,
                         sizeof(scsi_read_format_capacities_response_t));
  }
}

/**
 * @brief   SCSI read capacity (10) command handler.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @notapi
 */
static bool read_capacity10(SCSITarget *scsip, const uint8_t *cmd) {

  (void)cmd;

  BlockDeviceInfo bdi;
  blkGetInfo(scsip->config->blkdev, &bdi);
  scsi_read_capacity10_response_t ret;
  ret.block_size      = cpu_to_be32(bdi.blk_size);
  ret.last_block_addr = cpu_to_be32(bdi.blk_num - 1);

  return transmit_data(scsip, (uint8_t *)&ret,
                        sizeof(scsi_read_capacity10_response_t));
}

/**
 * @brief   Checks data request for media overflow.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 * @retval true       When media overflow detected.
 * @retval false      Otherwise.
 *
 * @notapi
 */
static bool data_overflow(SCSITarget *scsip, const data_request_t *req) {

  BlockDeviceInfo bdi;
  blkGetInfo(scsip->config->blkdev, &bdi);

  if (req->first_lba > bdi.blk_num || req->blk_cnt > bdi.blk_num - req->first_lba) {
    set_sense(scsip, SCSI_SENSE_KEY_ILLEGAL_REQUEST,
                     SCSI_ASENSE_LBA_OUT_OF_RANGE,
                     SCSI_ASENSEQ_NO_QUALIFIER);
    return true;
  }
  else {
    return false;
  }
}

/**
 * @brief   SCSI read/write (10) command handler.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @notapi
 */
/* Retry transient card errors before failing a command; block IO is idempotent. */
#define READER_IO_ATTEMPTS 3U

static bool read_blocks(BaseBlockDevice *dev, uint32_t lba, uint8_t *buf, size_t n) {
  for (unsigned attempt = 0; attempt < READER_IO_ATTEMPTS; attempt++) {
    if (blkRead(dev, lba, buf, n) == HAL_SUCCESS) {
      return true;
    }
  }
  return false;
}

static bool write_blocks(BaseBlockDevice *dev, uint32_t lba, const uint8_t *buf, size_t n) {
  for (unsigned attempt = 0; attempt < READER_IO_ATTEMPTS; attempt++) {
    if (blkWrite(dev, lba, buf, n) == HAL_SUCCESS) {
      return true;
    }
  }
  return false;
}

/* NOT READY: medium not present once ejected or failed, otherwise becoming ready. */
static bool set_not_ready(SCSITarget *scsip) {
  set_sense(scsip, SCSI_SENSE_KEY_NOT_READY,
            reader_media_ready() ? SCSI_ASENSE_LOGICAL_UNIT_NOT_READY
                                 : SCSI_ASENSE_MEDIUM_NOT_PRESENT, 0);
  return SCSI_FAILED;
}

static bool data_read_write10(SCSITarget *scsip, const uint8_t *cmd) {

  data_request_t req = decode_data_request(cmd);
  /* CBW residue is initialized by the transport. */

  if (data_overflow(scsip, &req)) {
    return SCSI_FAILED;
  }
  else {
    const SCSITransport *tr = scsip->config->transport;
    BaseBlockDevice *blkdev = scsip->config->blkdev;
    BlockDeviceInfo bdi;
    blkGetInfo(blkdev, &bdi);
    size_t bs = bdi.blk_size;
    size_t max_blocks = bs > 0U ? scsip->config->blkbuf_size / bs : 0U;
    uint32_t total_len = req.blk_cnt * bs;

    if (max_blocks == 0U) {
      set_sense(scsip, SCSI_SENSE_KEY_HARDWARE_ERROR,
                       SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                       SCSI_ASENSEQ_NO_QUALIFIER);
      scsip->residue = total_len;
      return SCSI_FAILED;
    }

    if (cmd[0] == SCSI_CMD_READ_10) {
      size_t i = 0;
      size_t pending_len = 0;
      unsigned buf_idx = 0;
      uint32_t transferred = 0;

      while (i < req.blk_cnt) {
        size_t n = req.blk_cnt - i;
        if (n > max_blocks) {
          n = max_blocks;
        }
        size_t len = n * bs;
        uint8_t *buf = scsip->config->blkbuf[buf_idx];

        if (!read_blocks(blkdev, req.first_lba + i, buf, n)) {
          reader_record_io(false, 0, false);
          if (pending_len > 0U) {
            uint32_t sent = tr->wait(tr);
            transferred += sent < pending_len ? sent : pending_len;
          }
          set_sense(scsip, SCSI_SENSE_KEY_MEDIUM_ERROR,
                           SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                           SCSI_ASENSEQ_NO_QUALIFIER);
          scsip->residue = total_len - transferred;
          return SCSI_FAILED;
        }

        if (pending_len > 0U) {
          uint32_t sent = tr->wait(tr);
          transferred += sent < pending_len ? sent : pending_len;
          if (sent != pending_len) {
            set_sense(scsip, SCSI_SENSE_KEY_ABORTED_COMMAND,
                             SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                             SCSI_ASENSEQ_NO_QUALIFIER);
            scsip->residue = total_len - transferred;
            return SCSI_FAILED;
          }
        }

        if (tr->transmit_start(tr, buf, len) != len) {
          set_sense(scsip, SCSI_SENSE_KEY_ABORTED_COMMAND,
                           SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                           SCSI_ASENSEQ_NO_QUALIFIER);
          scsip->residue = total_len - transferred;
          return SCSI_FAILED;
        }
        pending_len = len;
        i += n;
        buf_idx ^= 1U;
      }

      if (pending_len > 0U) {
        uint32_t sent = tr->wait(tr);
        transferred += sent < pending_len ? sent : pending_len;
        if (sent != pending_len) {
          set_sense(scsip, SCSI_SENSE_KEY_ABORTED_COMMAND,
                           SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                           SCSI_ASENSEQ_NO_QUALIFIER);
          scsip->residue = total_len - transferred;
          return SCSI_FAILED;
        }
      }
    }
    else if (req.blk_cnt > 0U) {
      size_t i = 0;
      size_t n = req.blk_cnt > max_blocks ? max_blocks : req.blk_cnt;
      size_t pending_len = n * bs;
      unsigned buf_idx = 0;
      uint32_t transferred = 0;

      if (tr->receive_start(tr, scsip->config->blkbuf[buf_idx],
                            pending_len) != pending_len) {
        set_sense(scsip, SCSI_SENSE_KEY_ABORTED_COMMAND,
                         SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                         SCSI_ASENSEQ_NO_QUALIFIER);
        scsip->residue = total_len;
        return SCSI_FAILED;
      }

      while (i < req.blk_cnt) {
        uint32_t received = tr->wait(tr);
        transferred += received < pending_len ? received : pending_len;
        if (received != pending_len) {
          set_sense(scsip, SCSI_SENSE_KEY_ABORTED_COMMAND,
                           SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                           SCSI_ASENSEQ_NO_QUALIFIER);
          scsip->residue = total_len - transferred;
          return SCSI_FAILED;
        }

        size_t next_i = i + n;
        bool next_pending = false;
        size_t next_n = 0;
        size_t next_len = 0;
        if (next_i < req.blk_cnt) {
          next_n = req.blk_cnt - next_i;
          if (next_n > max_blocks) {
            next_n = max_blocks;
          }
          next_len = next_n * bs;
          if (tr->receive_start(tr, scsip->config->blkbuf[buf_idx ^ 1U],
                                next_len) == next_len) {
            next_pending = true;
          }
        }

        if (!write_blocks(blkdev, req.first_lba + i,
                     scsip->config->blkbuf[buf_idx], n)) {
          reader_record_io(true, 0, false);
          if (next_pending) {
            received = tr->wait(tr);
            transferred += received < next_len ? received : next_len;
          }
          set_sense(scsip, SCSI_SENSE_KEY_MEDIUM_ERROR,
                           SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                           SCSI_ASENSEQ_NO_QUALIFIER);
          scsip->residue = total_len - transferred;
          return SCSI_FAILED;
        }

        if (next_i < req.blk_cnt && !next_pending) {
          set_sense(scsip, SCSI_SENSE_KEY_ABORTED_COMMAND,
                           SCSI_ASENSE_NO_ADDITIONAL_INFORMATION,
                           SCSI_ASENSEQ_NO_QUALIFIER);
          scsip->residue = total_len - transferred;
          return SCSI_FAILED;
        }

        i = next_i;
        n = next_n;
        pending_len = next_len;
        buf_idx ^= 1U;
      }
    }
  }
  if (cmd[0] == SCSI_CMD_WRITE_10 && !reader_sync_media()) {
    set_sense(scsip, SCSI_SENSE_KEY_MEDIUM_ERROR, 0, 0);
    return SCSI_FAILED;
  }
  reader_record_io(cmd[0] == SCSI_CMD_WRITE_10, req.blk_cnt, true);
  scsip->residue = 0;
  return SCSI_SUCCESS;
}

/**
 * @brief   SCSI test unit ready command handler
 * @details If block device is inserted, sets sense data in 'all OK' condition
 *          and returns success status.
 *          If block device is not inserted, sets sense data to 'Medium not present' considion,
 *          and returns check condition status.
 */
static bool test_unit_ready(SCSITarget *scsip, const uint8_t *cmd) {
  (void)cmd;

  if (blkIsInserted(scsip->config->blkdev)) {
    return SCSI_SUCCESS;
  }
  else {
    // warnprintf("SCSI Medium is not inserted.\r\n");
    set_sense(scsip, SCSI_SENSE_KEY_NOT_READY,
                     SCSI_ASENSE_MEDIUM_NOT_PRESENT,
                     SCSI_ASENSEQ_NO_QUALIFIER);
    return SCSI_FAILED;
  }

}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Executes SCSI command encoded in byte array.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] cmd     pointer to SCSI command data
 *
 * @return            The operation status.
 *
 * @api
 */
bool scsiExecCmd(SCSITarget *scsip, const uint8_t *cmd) {


  bool ret = SCSI_SUCCESS;
  allocation_limit = reader_allocation_length(cmd);
  if (cmd[0] != SCSI_CMD_INQUIRY && cmd[0] != SCSI_CMD_REQUEST_SENSE &&
      cmd[0] != SCSI_CMD_START_STOP_UNIT &&
      cmd[0] != SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL && !reader_media_ready()) {
    set_sense(scsip, SCSI_SENSE_KEY_NOT_READY, SCSI_ASENSE_MEDIUM_NOT_PRESENT, 0);
    return SCSI_FAILED;
  }

  switch (cmd[0]) {
  case SCSI_CMD_INQUIRY:
    // dbgprintf("SCSI_CMD_INQUIRY\r\n");
    ret = inquiry(scsip, cmd);
    break;

  case SCSI_CMD_REQUEST_SENSE:
    // dbgprintf("SCSI_CMD_REQUEST_SENSE\r\n");
    ret = request_sense(scsip, cmd);
    break;

  case SCSI_CMD_READ_CAPACITY_10:
    // dbgprintf("SCSI_CMD_READ_CAPACITY_10\r\n");
    ret = read_capacity10(scsip, cmd);
    break;

  case SCSI_CMD_READ_10:
    // dbgprintf("SCSI_CMD_READ_10\r\n");
    ret = data_read_write10(scsip, cmd);
    break;

  case SCSI_CMD_WRITE_10:
    // dbgprintf("SCSI_CMD_WRITE_10\r\n");
    ret = data_read_write10(scsip, cmd);
    break;

  case SCSI_CMD_TEST_UNIT_READY:
    // dbgprintf("SCSI_CMD_TEST_UNIT_READY\r\n");
    ret = test_unit_ready(scsip, cmd);
    break;

  case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
    // dbgprintf("SCSI_CMD_ALLOW_MEDIUM_REMOVAL\r\n");
    if ((cmd[4] & ~1U) != 0) {
      ret = cmd_unhandled(scsip, cmd);
    } else {
      reader_prevent(cmd[4] != 0);
    }
    break;

  case SCSI_CMD_START_STOP_UNIT:
    /* IMMED is accepted: every action completes before status is returned. */
    if ((cmd[1] & ~1U) != 0 || (cmd[4] & ~3U) != 0) {
      ret = cmd_unhandled(scsip, cmd);
    } else if ((cmd[4] & 2U) != 0) {
      /* LoEj: Start=1 loads, Start=0 ejects. */
      if ((cmd[4] & 1U) == 0 && reader_removal_prevented()) {
        set_sense(scsip, SCSI_SENSE_KEY_ILLEGAL_REQUEST, 0x53, 0x02); /* Removal prevented. */
        ret = SCSI_FAILED;
      } else if (!((cmd[4] & 1U) != 0 ? reader_load() : reader_eject())) {
        ret = set_not_ready(scsip);
      }
    } else if ((cmd[4] & 1U) != 0) {
      /* Start without LoEj never reloads ejected media. */
      if (!reader_media_ready()) {
        ret = set_not_ready(scsip);
      }
    } else if (!reader_sync_media()) {
      ret = set_not_ready(scsip);
    }
    break;

  case 0x35: /* SYNCHRONIZE CACHE(10), synchronous, no volatile write cache. */
    if ((cmd[1] & ~0x06U) != 0) { /* IMMED and SYNC_NV are harmless here. */
      ret = cmd_unhandled(scsip, cmd);
    } else if (!reader_sync_media()) {
      set_sense(scsip, SCSI_SENSE_KEY_MEDIUM_ERROR, 0, 0);
      ret = SCSI_FAILED;
    }
    break;

  case SCSI_CMD_MODE_SENSE_6:
    // dbgprintf("SCSI_CMD_MODE_SENSE_6\r\n");
    ret = mode_sense6(scsip, cmd);
    break;

  case SCSI_CMD_READ_FORMAT_CAPACITIES:
    // dbgprintf("SCSI_CMD_READ_FORMAT_CAPACITIES\r\n");
    ret = read_format_capacities(scsip, cmd);
    break;

  case SCSI_CMD_VERIFY_10:
    // dbgprintf("SCSI_CMD_VERIFY_10\r\n");
    ret = cmd_unhandled(scsip, cmd);
    break;

  default:
    // warnprintf("SCSI unhandled command: %X\r\n", cmd[0]);
    ret = cmd_unhandled(scsip, cmd);
    break;
  }

  if (ret == SCSI_SUCCESS)
    set_sense_ok(scsip);

  return ret;
}

/**
 * @brief   Driver structure initialization.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 *
 * @api
 */
void scsiObjectInit(SCSITarget *scsip) {

  scsip->config = NULL;
  scsip->residue = 0;
  memset(&scsip->sense, 0 , sizeof(scsi_sense_response_t));
  scsip->state = SCSI_TRGT_STOP;
}

/**
 * @brief   Starts SCSITarget driver.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 * @param[in] config  pointer to @p SCSITargetConfig structure
 *
 * @api
 */
void scsiStart(SCSITarget *scsip, const SCSITargetConfig *config) {

  scsip->config = config;
  scsip->state = SCSI_TRGT_READY;
}

/**
 * @brief   Stops SCSITarget driver.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 *
 * @api
 */
void scsiStop(SCSITarget *scsip) {

  scsip->config = NULL;
  scsip->state = SCSI_TRGT_STOP;
}

/**
 * @brief   Retrieves residue bytes.
 *
 * @param[in] scsip   pointer to @p SCSITarget structure
 *
 * @return            Residue bytes.
 *
 * @api
 */
uint32_t scsiResidue(const SCSITarget *scsip) {

  return scsip->residue;
}

/** @} */
