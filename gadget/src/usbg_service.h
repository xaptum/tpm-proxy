/**
 * @brief Xaptum mPCIe offload card USB service
 *
 * @file  usbg_service.h
 *
 * Header for USB Gadget service
 */

#ifndef USBG_SERVICE_H_
#define USBG_SERVICE_H_

#define FETCH(_var_)                            \
    memcpy(cp, &_var_, _var_.bLength);          \
    cp += _var_.bLength;

#define CONFIG_VALUE 2

#ifdef DEBUG
#define usbsg_debug printf
#else
#define usbsg_debug(fmt, ...)
#endif

// Specific to controller
#define USB_DEV_NAME      "/home/root/usbg/atmel_usba_udc"
#define USB_DEV_EPIN      "/home/root/usbg/ep1"
#define USB_DEV_EPOUT     "/home/root/usbg/ep2"

#define USB_DEV_TUN_EPIN  "/home/root/usbg/ep3"
#define USB_DEV_TUN_EPOUT "/home/root/usbg/ep4"

#define USBG_VID     0x2FE0
#define USBG_PID     0x8B01

enum {
    STRINGID_MANUFACTURER = 1,
    STRINGID_PRODUCT,
    STRINGID_SERIAL,
    STRINGID_CONFIG_HS,
    STRINGID_CONFIG_LS,
    STRINGID_INTERFACE,
    STRINGID_MAX
};


/**
 * Bringup GadgetFS
 */
void gadgetfs_usb_mount(void);

/**
 * Init USB gadget device
 */
void gadgetfs_usb_init(void);

/**
 * Stop USB gadget device
 */
void gadgetfs_usb_stop(void);

/**
 * USB Gadget IO write
 *
 * @param pdata - Data to be sent
 *
 * @param len   - Length in bytes to write
 *
 * @return 0 - success, <0 - error
 */
int  gadgetfs_io_write(void * pdata, int len, int * pexit_req);

/**
 * USB Gadget IO read
 *
 * @param pdata - Data to be filled
 *
 * @param len   - Length in bytes to be readed
 *
 * @return 0 - success, <0 - error
 */
int  gadgetfs_io_read(void * pdata, int len, int * pexit_req);

/**
 * USB Gadget IO write TUN
 *
 * @param pdata - Data to be sent
 *
 * @param len   - Length in bytes to write
 *
 * @return 0 - success, <0 - error
 */
int  gadgetfs_io_tun_write(void * pdata, int len, int * pexit_req);

/**
 * USB Gadget IO read TUN
 *
 * @param pdata - Data to be filled
 *
 * @param len   - Length in bytes to be readed
 *
 * @return 0 - success, <0 - error
 */
int  gadgetfs_io_tun_read(void * pdata, int len, int * pexit_req);

/**
 * USB Gadget IO read TUN Async
 *
 * @param pdata - Data to be filled
 *
 * @param len   - Length in bytes to be readed
 *
 * @return 0 - success, <0 - error
 */
int  gadgetfs_io_tun_read_async(void * pdata, int len, int * pexit_req);

/**
 * Return USB Gadget IO status
 *
 * @return 0 - Not ready, 1 - Ready to communication
 */
int gadgetfs_io_is_ready(void);


#endif /* USBG_SERVICE_H_ */
