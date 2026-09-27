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
 * @file    hal_usb_msd.c
 * @brief   USM mass storage device code.
 *
 * @addtogroup usb_msd
 * @{
 */

#include "hal.h"
#include "reader_hooks.h"
static uint32_t command_epoch;

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
}

#if (HAL_USE_USB_MSD == TRUE) || defined(__DOXYGEN__)

#include <string.h>

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

#define MSD_REQ_RESET                   0xFF
#define MSD_REQ_GET_MAX_LUN             0xFE

#define MSD_CBW_SIGNATURE               0x43425355
#define MSD_CSW_SIGNATURE               0x53425355

#define CBW_FLAGS_RESERVED_MASK         0b01111111
#define CBW_LUN_RESERVED_MASK           0b11110000
#define CBW_CMD_LEN_RESERVED_MASK       0b11000000

#define CSW_STATUS_PASSED               0x00
#define CSW_STATUS_FAILED               0x01
#define CSW_STATUS_PHASE_ERROR          0x02

#define MSD_SETUP_WORD(setup, index) (uint16_t)(((uint16_t)setup[index+1] << 8)\
                                                | (setup[index] & 0x00FF))

#define MSD_SETUP_VALUE(setup)  MSD_SETUP_WORD(setup, 2)
#define MSD_SETUP_INDEX(setup)  MSD_SETUP_WORD(setup, 4)
#define MSD_SETUP_LENGTH(setup) MSD_SETUP_WORD(setup, 6)

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/
/**
 * @brief   USB mass storage driver identifier.
 */
USBMassStorageDriver USBMSD1;

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/**
 * @brief   Hardcoded default SCSI inquiry response structure.
 */
static const scsi_inquiry_response_t default_scsi_inquiry_response = {
    0x00,           /* direct access block device     */
    0x80,           /* removable                      */
    0x04,           /* SPC-2                          */
    0x02,           /* response data format           */
    0x20,           /* response has 0x20 + 4 bytes    */
    0x00,
    0x00,
    0x00,
    "Chibios",
    "Mass Storage",
    {'v',CH_KERNEL_MAJOR+'0','.',CH_KERNEL_MINOR+'0'}
};

/**
 * @brief   Hardcoded default SCSI unit serial number inquiry response structure.
 */
static const scsi_unit_serial_number_inquiry_response_t default_scsi_unit_serial_number_inquiry_response =
{
    0x00,
    0x80,
    0x00,
    0x08,
    "00000000"
};


/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Checks validity of CBW content.
 * @details The device shall consider the CBW valid when:
 *          • The CBW was received after the device had sent a CSW or after a reset,
 *          • the CBW is 31 (1Fh) bytes in length,
 *          • and the dCBWSignature is equal to 43425355h.
 *
 * @param[in] cbw       pointer to the @p msd_cbw_t object
 * @param[in] recvd     number of received bytes
 *
 * @return              Operation status.
 * @retval true         CBW is meaningful.
 * @retval false        CBW is bad.
 *
 * @notapi
 */
static bool cbw_valid(const msd_cbw_t *cbw, msg_t recvd) {
  if ((sizeof(msd_cbw_t) != recvd) || (cbw->signature != MSD_CBW_SIGNATURE)) {
    return false;
  }
  else {
    return true;
  }
}

/**
 * @brief   Checks meaningfulness of CBW content.
 * @details The device shall consider the contents of a valid CBW meaningful when:
 *          • no reserved bits are set,
 *          • the bCBWLUN contains a valid LUN supported by the device,
 *          • and both bCBWCBLength and the content of the CBWCB are in
 *            accordance with bInterfaceSubClass.
 *
 * @param[in] cbw       pointer to the @p msd_cbw_t object
 *
 * @return              Operation status.
 * @retval true         CBW is meaningful.
 * @retval false        CBW is bad.
 *
 * @notapi
 */
static bool cbw_meaningful(const msd_cbw_t *cbw) {
  if ((cbw->cmd_len == 0 || cbw->cmd_len > 16)
      || ((cbw->cmd_len & CBW_CMD_LEN_RESERVED_MASK) != 0)
      || ((cbw->flags & CBW_FLAGS_RESERVED_MASK) != 0)
      || (cbw->lun != 0)) {
    return false;
  }
  else {
    return true;
  }
}

/**
 * @brief   SCSI transport transmit function.
 *
 * @param[in] transport pointer to the @p SCSITransport object
 * @param[in] data      payload
 * @param[in] len       number of bytes to be transmitted
 *
 * @return              Number of successfully transmitted bytes.
 * @notapi
 */
static uint32_t scsi_transport_transmit(const SCSITransport *transport,
                                        const uint8_t *data, size_t len) {

  usb_scsi_transport_handler_t *trp = transport->handler;
  osalMutexLock(&trp->txmtx);
  msg_t status = command_epoch == reader_usb_epoch() ? guarded_transmit(trp->usbp, trp->ep, data, len) : MSG_RESET;
  osalMutexUnlock(&trp->txmtx);
  if (MSG_OK == status)
    return len;
  else
    return 0;
}

/**
 * @brief   SCSI transport receive function.
 *
 * @param[in] transport pointer to the @p SCSITransport object
 * @param[in] data      payload
 * @param[in] len       number bytes to be received
 *
 * @return              Number of successfully received bytes.
 * @notapi
 */
static uint32_t scsi_transport_receive(const SCSITransport *transport,
                                       uint8_t *data, size_t len) {

  usb_scsi_transport_handler_t *trp = transport->handler;
  osalMutexLock(&trp->txmtx);
  msg_t status = command_epoch == reader_usb_epoch() ? guarded_receive(trp->usbp, trp->ep, data, len, false) : MSG_RESET;
  osalMutexUnlock(&trp->txmtx);
  if (MSG_RESET != status)
    return status;
  else
    return 0;
}

/**
 * @brief   Starts an asynchronous SCSI transport operation.
 *
 * @param[in] transport pointer to the @p SCSITransport object
 * @param[in] data      payload buffer
 * @param[in] len       number of bytes to transfer
 * @param[in] transmit  true for transmit, false for receive
 *
 * @return              Number of bytes accepted for transfer.
 * @notapi
 */
static uint32_t scsi_transport_start(const SCSITransport *transport,
                                     uint8_t *data, size_t len,
                                     bool transmit) {

  usb_scsi_transport_handler_t *trp = transport->handler;
  if (trp->io_pending || command_epoch != reader_usb_epoch()) {
    return 0;
  }

  trp->iobuf = data;
  trp->iolen = len;
  trp->io_is_transmit = transmit;
  trp->io_pending = true;
  chBSemSignal(&trp->io_start);

  return len;
}

/**
 * @brief   Starts an asynchronous SCSI transport transmit.
 *
 * @param[in] transport pointer to the @p SCSITransport object
 * @param[in] data      payload
 * @param[in] len       number of bytes to transmit
 *
 * @return              Number of bytes accepted for transfer.
 * @notapi
 */
static uint32_t scsi_transport_transmit_start(const SCSITransport *transport,
                                              const uint8_t *data,
                                              size_t len) {

  return scsi_transport_start(transport, (uint8_t *)data, len, true);
}

/**
 * @brief   Starts an asynchronous SCSI transport receive.
 *
 * @param[in] transport pointer to the @p SCSITransport object
 * @param[out] data     payload buffer
 * @param[in] len       number of bytes to receive
 *
 * @return              Number of bytes accepted for transfer.
 * @notapi
 */
static uint32_t scsi_transport_receive_start(const SCSITransport *transport,
                                             uint8_t *data, size_t len) {

  return scsi_transport_start(transport, data, len, false);
}

/**
 * @brief   Waits for an asynchronous SCSI transport operation.
 *
 * @param[in] transport pointer to the @p SCSITransport object
 *
 * @return              Number of bytes transferred.
 * @notapi
 */
static uint32_t scsi_transport_wait(const SCSITransport *transport) {

  usb_scsi_transport_handler_t *trp = transport->handler;
  if (!trp->io_pending) {
    return 0;
  }

  chBSemWait(&trp->io_done);
  trp->io_pending = false;
  return trp->io_result;
}

/**
 * @brief   Fills and sends CSW message.
 *
 * @param[in] msdp      pointer to the @p USBMassStorageDriver object
 * @param[in] status    status returned by SCSI layer
 * @param[in] residue   number of residue bytes in case of failed transaction
 *
 * @notapi
 */
static void send_csw(USBMassStorageDriver *msdp, uint8_t status,
                     uint32_t residue) {

  msdp->csw.signature = MSD_CSW_SIGNATURE;
  msdp->csw.data_residue = residue;
  msdp->csw.tag = msdp->cbw.tag;
  msdp->csw.status = status;

  osalMutexLock(&msdp->usb_scsi_transport_handler.txmtx);
  guarded_transmit(msdp->usbp, USB_MSD_DATA_EP, (uint8_t *)&msdp->csw,
      sizeof(msd_csw_t));
  osalMutexUnlock(&msdp->usb_scsi_transport_handler.txmtx);
}

/**
 * @brief   Mass storage worker thread.
 *
 * @param[in] arg     pointer to the @p USBMassStorageDriver object
 *
 * @notapi
 */
static void stall_data(USBMassStorageDriver *msdp, bool both) {
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

    const msg_t received = guarded_receive(msdp->usbp, USB_MSD_DATA_EP,
                                     (uint8_t *)&msdp->cbw, sizeof(msd_cbw_t), true);
    if (received == MSG_RESET || command_epoch != reader_usb_epoch()) {
      osalThreadSleepMilliseconds(10);
      continue;
    }

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

/**
 * @brief USB asynchronous I/O worker thread.
 *
 * @param[in] arg     pointer to the @p USBMassStorageDriver object
 *
 * @notapi
 */
static THD_FUNCTION(usb_msd_io_worker, arg) {
  USBMassStorageDriver *msdp = arg;
  usb_scsi_transport_handler_t *trp = &msdp->usb_scsi_transport_handler;
  chRegSetThreadName("usb_msd_io_worker");

  while(! chThdShouldTerminateX()) {
    chBSemWait(&trp->io_start);
    if (chThdShouldTerminateX()) {
      break;
    }

    osalMutexLock(&trp->txmtx);
    if (trp->io_is_transmit) {
      msg_t status = command_epoch == reader_usb_epoch() ? guarded_transmit(trp->usbp, trp->ep, trp->iobuf, trp->iolen) : MSG_RESET;
      trp->io_result = status == MSG_OK ? trp->iolen : 0;
    }
    else {
      msg_t status = command_epoch == reader_usb_epoch() ? guarded_receive(trp->usbp, trp->ep, trp->iobuf, trp->iolen, false) : MSG_RESET;
      trp->io_result = status == MSG_RESET ? 0 : status;
    }
    osalMutexUnlock(&trp->txmtx);
    chBSemSignal(&trp->io_done);
  }

  chThdExit(MSG_OK);
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Mass storage specific request hook for USB.
 *
 * @param[in] usbp     pointer to the @p USBDriver object
 *
 * @notapi
 */
bool msd_request_hook(USBDriver *usbp) {
  /* check that the request is for interface 0.*/
  if (MSD_SETUP_INDEX(usbp->setup) != 0)
    return false;

  if (usbp->setup[0] == (USB_RTYPE_TYPE_CLASS | USB_RTYPE_RECIPIENT_INTERFACE | USB_RTYPE_DIR_HOST2DEV)
    && usbp->setup[1] == MSD_REQ_RESET) {
    /* Bulk-Only Mass Storage Reset (class-specific request)
    This request is used to reset the mass storage device and its associated interface.
    This class-specific request shall ready the device for the next CBW from the host. */
    /* Do any special reset code here. */
    /* The device shall NAK the status stage of the device request until
     * the Bulk-Only Mass Storage Reset is complete.
     * NAK EP1 in and out */
    // usbp->otg->ie[1].DIEPCTL = DIEPCTL_SNAK;
    // usbp->otg->oe[1].DOEPCTL = DOEPCTL_SNAK;
    /* response to this request using EP0 */
    usbSetupTransfer(usbp, 0, 0, NULL);
    return true;
  } else if (usbp->setup[0] == (USB_RTYPE_TYPE_CLASS | USB_RTYPE_RECIPIENT_INTERFACE | USB_RTYPE_DIR_DEV2HOST)
    && usbp->setup[1] == MSD_REQ_GET_MAX_LUN) {
    /* Return the maximum supported LUN. */
    static uint8_t zero = 0;
    usbSetupTransfer(usbp, &zero, 1, NULL);
    return true;
    /* OR */
    /* Return false to stall to indicate that we don't support LUN */
    // return false;
  }

  return false;
}

/**
 * @brief   Initializes the standard part of a @p USBMassStorageDriver structure.
 *
 * @param[out] msdp     pointer to the @p USBMassStorageDriver object
 *
 * @init
 */
void msdObjectInit(USBMassStorageDriver *msdp) {

  memset(msdp, 0x55, sizeof(USBMassStorageDriver));
  msdp->state = USB_MSD_STOP;
  msdp->usbp = NULL;
  msdp->worker = NULL;
  msdp->usb_scsi_transport_handler.ioworker = NULL;

  scsiObjectInit(&msdp->scsi_target);
}

/**
 * @brief   Stops the USB mass storage driver.
 *
 * @param[in] msdp      pointer to the @p USBMassStorageDriver object
 *
 * @api
 */
void msdStop(USBMassStorageDriver *msdp) {

  osalDbgCheck(msdp != NULL);
  osalDbgAssert((msdp->state == USB_MSD_READY), "invalid state");

  chThdTerminate(msdp->worker);
  chThdWait(msdp->worker);

  chThdTerminate(msdp->usb_scsi_transport_handler.ioworker);
  chBSemSignal(&msdp->usb_scsi_transport_handler.io_start);
  chThdWait(msdp->usb_scsi_transport_handler.ioworker);

  scsiStop(&msdp->scsi_target);

  msdp->worker = NULL;
  msdp->usb_scsi_transport_handler.ioworker = NULL;
  msdp->state = USB_MSD_STOP;
  msdp->usbp = NULL;
}

/**
 * @brief   Configures and activates the USB mass storage driver.
 *
 * @param[in] msdp      pointer to the @p USBMassStorageDriver object
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] blkdev    pointer to the @p BaseBlockDevice object
 * @param[in] blkbuf_a  pointer to the first working area buffer
 * @param[in] blkbuf_b  pointer to the second working area buffer
 * @param[in] blkbuf_size size of the working area buffer in bytes
 * @param[in] inquiry   pointer to the SCSI inquiry response structure,
 *                      set it to @p NULL to use default hardcoded value.
 *
 * @api
 */
void msdStart(USBMassStorageDriver *msdp, USBDriver *usbp,
              BaseBlockDevice *blkdev, uint8_t *blkbuf_a, uint8_t *blkbuf_b,
              size_t blkbuf_size,
              const scsi_inquiry_response_t *inquiry,
              const scsi_unit_serial_number_inquiry_response_t *serialInquiry,
              scsi_block_filesystem_access_t blockFilesystemAccess,
              scsi_free_filesystem_access_t freeFilesystemAccess) {

  osalDbgCheck((msdp != NULL) && (usbp != NULL)
              && (blkdev != NULL) && (blkbuf_a != NULL) && (blkbuf_b != NULL)
              && (blkbuf_size > 0U));
  osalDbgAssert((msdp->state == USB_MSD_STOP), "invalid state");

  msdp->usbp = usbp;

  msdp->usb_scsi_transport_handler.usbp = msdp->usbp;
  msdp->usb_scsi_transport_handler.ep   = USB_MSD_DATA_EP;
  osalMutexObjectInit(&msdp->usb_scsi_transport_handler.txmtx);
  chBSemObjectInit(&msdp->usb_scsi_transport_handler.io_start, true);
  chBSemObjectInit(&msdp->usb_scsi_transport_handler.io_done, true);
  msdp->usb_scsi_transport_handler.io_pending = false;
  msdp->usb_scsi_transport_handler.ioworker = chThdCreateStatic(
      msdp->usb_scsi_transport_handler.waMSDIOWorker,
      sizeof(msdp->usb_scsi_transport_handler.waMSDIOWorker), MSD_THD_PRIO,
      usb_msd_io_worker, msdp);

  msdp->scsi_transport.handler  = &msdp->usb_scsi_transport_handler;
  msdp->scsi_transport.transmit = scsi_transport_transmit;
  msdp->scsi_transport.receive  = scsi_transport_receive;
  msdp->scsi_transport.transmit_start = scsi_transport_transmit_start;
  msdp->scsi_transport.receive_start = scsi_transport_receive_start;
  msdp->scsi_transport.wait = scsi_transport_wait;

  msdp->scsi_transport.block_filesystem_access = blockFilesystemAccess;
  msdp->scsi_transport.free_filesystem_access = freeFilesystemAccess;

  if (NULL == inquiry) {
    msdp->scsi_config.inquiry_response = &default_scsi_inquiry_response;
  }
  else {
    msdp->scsi_config.inquiry_response = inquiry;
  }
  if (NULL == serialInquiry) {
    msdp->scsi_config.unit_serial_number_inquiry_response = &default_scsi_unit_serial_number_inquiry_response;
  }
  else {
    msdp->scsi_config.unit_serial_number_inquiry_response = serialInquiry;
  }
  msdp->scsi_config.blkbuf[0] = blkbuf_a;
  msdp->scsi_config.blkbuf[1] = blkbuf_b;
  msdp->scsi_config.blkbuf_size = blkbuf_size;
  msdp->scsi_config.blkdev = blkdev;
  msdp->scsi_config.transport = &msdp->scsi_transport;

  scsiStart(&msdp->scsi_target, &msdp->scsi_config);

  msdp->state = USB_MSD_READY;
  msdp->worker = chThdCreateStatic(msdp->waMSDWorker, sizeof(msdp->waMSDWorker),
                                   MSD_THD_PRIO-1, usb_msd_worker, msdp);
}

#endif /* HAL_USE_USB_MSD */

/** @} */
