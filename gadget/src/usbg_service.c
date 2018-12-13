/**
 * @brief Xaptum mPCIe offload card USB service
 *
 * @file  usbg_service.c
 *
 * USB Gadget service body
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <aio.h>

#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadgetfs.h>

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include "usbg_service.h"
#include "usbstring.h"


static struct usb_string stringtab [] = {
    { STRINGID_MANUFACTURER, "Xaptum", },
    { STRINGID_PRODUCT,      "mPCIe OC", },
    { STRINGID_SERIAL,       "0001", },
    { STRINGID_CONFIG_HS,    "High speed configuration", },
    { STRINGID_CONFIG_LS,    "Low speed configuration", },
    { STRINGID_INTERFACE,    "Custom interface", },
    { STRINGID_MAX, NULL},
};

static struct usb_gadget_strings strings = {
    .language = 0x0409, /* en-us */
    .strings = stringtab,
};

static struct usb_endpoint_descriptor ep_descriptor_in;
static struct usb_endpoint_descriptor ep_descriptor_out;

static struct usb_endpoint_descriptor ep_descriptor_tun_in;
static struct usb_endpoint_descriptor ep_descriptor_tun_out;

struct io_thread_args {
    unsigned stop;
    int      fd_in,
             fd_out,
             fd_tun_in,
             fd_tun_out;
};

static struct io_thread_args g_usbg_io_thread_args = \
        { 1, -1, -1, -1, -1 };

static pthread_t g_thread_ep0_handler;
static int       g_fd_usb_gadget;
static int       g_aio_read_async = 0;
struct aiocb     g_aiocb_async_read;
/* static pthread_t g_usbg_io_thread; */

/**
 * Bringup GadgetFS
 */
void gadgetfs_usb_mount(void)
{
    int iret;

    printf("Prepare USB GadgetFS\n");

    iret = system("modprobe libcomposite");
    iret = system("modprobe gadgetfs");

    iret = system("mkdir /root/usbg");
    iret = system("mount -t gadgetfs gadgetfs /root/usbg");

    iret = iret;
}

/**
 * Stop GadgetFS
 */
void gadgetfs_usb_dismount(void)
{
    int iret;

    printf("Stop USB GadgetFS\n");

    iret = system("umount /root/usbg");

    iret = iret;
}

/**
 * Return USB Gadget IO status
 *
 * @return 0 - Not ready, 1 - Ready to communication
 */
int gadgetfs_io_is_ready(void)
{
    return g_usbg_io_thread_args.stop ? 0 : 1;
}

/**
 * USB Gadget IO write
 *
 * @param pdata - Data to be sent
 *
 * @param len   - Length in bytes to write
 *
 * @return 0 - success, <0 - error
 */
int gadgetfs_io_write(void * pdata, int len, int * pexit_req)
{
    int iret;
    struct aiocb aiocb;
    int err;

    if (g_usbg_io_thread_args.stop)
    {
        return -EPERM;
    }

    iret = write(g_usbg_io_thread_args.fd_in,
            pdata, len);

    return iret;
}

/**
 * USB Gadget IO write2
 *
 * @param pdata - Data to be sent
 *
 * @param len   - Length in bytes to write
 *
 * @return 0 - success, <0 - error
 */
int gadgetfs_io_write2(void * pdata, int len, int * pexit_req)
{
    int iret;
    struct aiocb aiocb;
    int err;

    if (g_usbg_io_thread_args.stop)
    {
        return -EPERM;
    }

    memset(&aiocb, 0, sizeof(struct aiocb));
    aiocb.aio_fildes     = g_usbg_io_thread_args.fd_in;
    aiocb.aio_buf        = pdata;
    aiocb.aio_nbytes     = len;
    aiocb.aio_lio_opcode = LIO_WRITE;

    if (aio_write(&aiocb) == -1) {
        return 0;
    }

    /* Wait until end of transaction */
    while (((err = aio_error (&aiocb)) == EINPROGRESS) && (*pexit_req == 0));

    /**
     * Exit requested
     */
    if (*pexit_req)
    {
        return -EPERM;
    }

    err = aio_error(&aiocb);
    iret = aio_return(&aiocb);

    if (err != 0) {
        return 0;
    }

    if (iret != len) {
        return 0;
    }

    return iret;
}

/**
 * USB Gadget IO write TUN
 *
 * @param pdata - Data to be sent
 *
 * @param len   - Length in bytes to write
 *
 * @return 0 - success, <0 - error
 */
int gadgetfs_io_tun_write(void * pdata, int len, int * pexit_req)
{
    int iret;
    struct aiocb aiocb;
    int err;

    if (g_usbg_io_thread_args.stop)
    {
        return -EPERM;
    }

    memset(&aiocb, 0, sizeof(struct aiocb));
    aiocb.aio_fildes     = g_usbg_io_thread_args.fd_tun_in;
    aiocb.aio_buf        = pdata;
    aiocb.aio_nbytes     = len;
    aiocb.aio_lio_opcode = LIO_WRITE;

    if (aio_write(&aiocb) == -1) {
        return 0;
    }

    /* Wait until end of transaction */
    while (((err = aio_error (&aiocb)) == EINPROGRESS) && (*pexit_req == 0));

    /**
     * Exit requested
     */
    if (*pexit_req)
    {
        return -EPERM;
    }

    err = aio_error(&aiocb);
    iret = aio_return(&aiocb);

    if (err != 0) {
        return 0;
    }

    if (iret != len) {
        return 0;
    }

    return iret;
}

/**
 * USB Gadget IO read
 *
 * @param pdata - Data to be filled
 *
 * @param len   - Length in bytes to be readed
 *
 * @return 0 - success, <0 - error
 */
int gadgetfs_io_read(void * pdata, int len, int * pexit_req)
{
    int iret;
    struct aiocb aiocb;
    int err;

    if (g_usbg_io_thread_args.stop)
    {
        return -EPERM;
    }

    memset(&aiocb, 0, sizeof(struct aiocb));
    aiocb.aio_fildes     = g_usbg_io_thread_args.fd_out;
    aiocb.aio_buf        = pdata;
    aiocb.aio_nbytes     = len;
    aiocb.aio_lio_opcode = LIO_READ;

    if (aio_read(&aiocb) == -1) {
        return 0;
    }

    /* Wait until end of transaction */
    while (((err = aio_error (&aiocb)) == EINPROGRESS) && (*pexit_req == 0));

    /**
     * Exit requested
     */
    if (*pexit_req)
    {
        return -EPERM;
    }

    err = aio_error(&aiocb);
    iret = aio_return(&aiocb);

    if (err != 0) {
        return 0;
    }

    if (iret != len) {
        return 0;
    }

    return iret;
}

/**
 * USB Gadget IO read TUN
 *
 * @param pdata - Data to be filled
 *
 * @param len   - Length in bytes to be readed
 *
 * @return 0 - success, <0 - error
 */
int gadgetfs_io_tun_read(void * pdata, int len, int * pexit_req)
{
    int iret;
    struct aiocb aiocb;
    int err;

    if (g_usbg_io_thread_args.stop)
    {
        return -EPERM;
    }

    if (!pexit_req)
    {
        return -EINVAL;
    }

    memset(&aiocb, 0, sizeof(struct aiocb));
    aiocb.aio_fildes     = g_usbg_io_thread_args.fd_tun_out;
    aiocb.aio_buf        = pdata;
    aiocb.aio_nbytes     = len;
    aiocb.aio_lio_opcode = LIO_READ;

    if (aio_read(&aiocb) == -1) {
        return 0;
    }

    /* Wait until end of transaction */
    while (((err = aio_error (&aiocb)) == EINPROGRESS) && (*pexit_req == 0));

    /**
     * Exit requested
     */
    if (*pexit_req)
    {
        return -EPERM;
    }

    err = aio_error(&aiocb);
    iret = aio_return(&aiocb);

    if (err != 0) {
        return 0;
    }

    return iret;
}

/**
 * USB Gadget IO read TUN async
 *
 * @param pdata - Data to be filled
 *
 * @param len   - Length in bytes to be readed
 *
 * @return 0 - success, <0 - error
 */
int gadgetfs_io_tun_read_async(void * pdata, int len, int * pexit_req)
{
    int          iret = 0;
    int          err;

    if (g_usbg_io_thread_args.stop)
    {
        return -EPERM;
    }

    if (!pexit_req)
    {
        return -EINVAL;
    }

    if (g_aio_read_async == 0)
    {
        memset(&g_aiocb_async_read, 0, sizeof(struct aiocb));
        g_aiocb_async_read.aio_fildes     = g_usbg_io_thread_args.fd_tun_out;
        g_aiocb_async_read.aio_buf        = pdata;
        g_aiocb_async_read.aio_nbytes     = len;
        g_aiocb_async_read.aio_lio_opcode = LIO_READ;

        if (aio_read(&g_aiocb_async_read) == -1) {
            return 0;
        }

        g_aio_read_async = 1;
    }

    if (g_aio_read_async == 1)
    {
        /* Wait until end of transaction */
        if (((err = aio_error (&g_aiocb_async_read)) == EINPROGRESS) &&
             (*pexit_req == 0))
        {
            return -EBUSY;
        }

        /**
         * Exit requested
         */
        if (*pexit_req)
        {
            g_aio_read_async = 0;
            aio_cancel(
                    g_usbg_io_thread_args.fd_tun_out,
                    &g_aiocb_async_read);

            return -EPERM;
        }

        g_aio_read_async = 2;
    }

    if (g_aio_read_async == 2)
    {
        /**
         * Exit requested
         */
        if (*pexit_req)
        {
            return -EPERM;
        }

        err = aio_error(&g_aiocb_async_read);
        iret = aio_return(&g_aiocb_async_read);

        g_aio_read_async = 0;

        if (err != 0) {
            return 0;
        }
    }

    return iret;
}

/**
 * Return USB read IO file descriptor
 *
 * @return fd value
 */
int  gadgetfs_io_get_read_fd(void)
{
    return g_usbg_io_thread_args.fd_out;
}

int  gadgetfs_io_get_write_fd(void)
{
    return g_usbg_io_thread_args.fd_in;
}

void gadgetfs_io_tun_read_async_cancel(void)
{

}


#if 0
/*
 * Respond to host requests
 */
static void* usbg_io_thread(void* arg)
{
    struct io_thread_args* thread_args = (struct io_thread_args*)arg;
    fd_set read_set, write_set;
    struct timeval timeout;
    int    ret, max_read_fd, max_write_fd;
    char   buffer[512];

    max_read_fd = max_write_fd = 0;

    if (thread_args->fd_in > max_write_fd) max_write_fd = thread_args->fd_in;
    if (thread_args->fd_out > max_read_fd) max_read_fd  = thread_args->fd_out;

    printf("usbg_io_thread+\n");

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        ret = read (thread_args->fd_out, buffer, sizeof(buffer));

        printf("USB Read stepi2\n");

        if (ret > 0)
            printf("Read %d bytes : %s\n", ret, buffer);
        else
            printf("Read error %d(%m)\n", ret);

#if 0
        memset(buffer, 0, sizeof(buffer));

        // Error
        if (ret < 0)
            break;

        strcpy(buffer, "My name is USBond !");

        ret = write (thread_args->fd_in, buffer, strlen(buffer)+1);

        printf("Write status %d (%m)\n", ret);

#endif
    }

    close (thread_args->fd_in);
    close (thread_args->fd_out);

    thread_args->fd_in = -1;
    thread_args->fd_out = -1;

    printf("usbg_io_thread-\n");

    return NULL;
}


static void usbg_io_prepare(struct io_thread_args* args_in)
{
    struct io_thread_args* thread_args = args_in;
    fd_set read_set, write_set;
    struct timeval timeout;
    int    ret, max_read_fd, max_write_fd;

    max_read_fd = max_write_fd = 0;

    if (thread_args->fd_in > max_write_fd) max_write_fd = thread_args->fd_in;
    if (thread_args->fd_out > max_read_fd) max_read_fd  = thread_args->fd_out;

    FD_ZERO(&read_set);
    FD_SET(thread_args->fd_out, &read_set);
    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec  = 0;
    timeout.tv_usec = 10000; // 10ms

    ret = select(max_read_fd + 1, &read_set, NULL, NULL, &timeout);

    // Timeout
    if (ret == 0)
    {
        printf("USB Read timeout\n");
    }

    // Error
    if (ret < 0)
    {
        printf("select error\n");
    }

    FD_ZERO(&write_set);
    FD_SET(thread_args->fd_in, &write_set);

    ret = select(max_write_fd + 1, NULL, &write_set, NULL, NULL);
}
#endif

static int init_ep(int* fd_in, int* fd_out, int* fd_tun_in, int* fd_tun_out)
{
    uint8_t init_config[2048];
    uint8_t* cp;
    int ret = -1;
    uint32_t send_size;

    // Configure ep1 (low/full speed + high speed)
    *fd_in = open(USB_DEV_EPIN, O_RDWR);

    if (*fd_in <= 0)
    {
        usbsg_debug("[EP1] Unable to open %s (%m)\n", USB_DEV_EPIN);
        goto end;
    }

    *(uint32_t*)init_config = 1;
    cp = &init_config[4];

    FETCH(ep_descriptor_in);
    FETCH(ep_descriptor_in);

    send_size = (uint32_t)cp-(uint32_t)init_config;
    ret = write(*fd_in, init_config, send_size);

    if (ret != send_size)
    {
        usbsg_debug("[EP1] Write error %d (%m)\n", ret);
        goto end;
    }

    usbsg_debug("ep1 configured\n");

    // Configure ep2 (low/full speed + high speed)
    *fd_out = open(USB_DEV_EPOUT, O_RDWR);

    if (*fd_out <= 0)
    {
        usbsg_debug("[EP2] Unable to open %s (%m)\n", USB_DEV_EPOUT);
        goto end;
    }

    *(uint32_t*)init_config = 1;
    cp = &init_config[4];

    FETCH(ep_descriptor_out);
    FETCH(ep_descriptor_out);

    send_size = (uint32_t)cp-(uint32_t)init_config;
    ret = write(*fd_out, init_config, send_size);

    if (ret != send_size)
    {
        usbsg_debug("[EP2] Write error %d (%m)\n", ret);
        goto end;
    }

    usbsg_debug("ep2 configured\n");

    // Configure ep3 (low/full speed + high speed)
    *fd_tun_in = open(USB_DEV_TUN_EPIN, O_RDWR);

    if (*fd_tun_in <= 0)
    {
        usbsg_debug("[EP3] Unable to open %s (%m)\n", USB_DEV_TUN_EPIN);
        goto end;
    }

    *(uint32_t*)init_config = 1;
    cp = &init_config[4];

    FETCH(ep_descriptor_tun_in);
    FETCH(ep_descriptor_tun_in);

    send_size = (uint32_t)cp-(uint32_t)init_config;
    ret = write(*fd_tun_in, init_config, send_size);

    if (ret != send_size)
    {
        usbsg_debug("[EP3] Write error %d (%m)\n", ret);
        goto end;
    }

    usbsg_debug("ep3 configured\n");

    // Configure ep4 (low/full speed + high speed)
    *fd_tun_out = open(USB_DEV_TUN_EPOUT, O_RDWR);

    if (*fd_tun_out <= 0)
    {
        usbsg_debug("[EP4] Unable to open %s (%m)\n", USB_DEV_TUN_EPOUT);
        goto end;
    }

    *(uint32_t*)init_config = 1;
    cp = &init_config[4];

    FETCH(ep_descriptor_tun_out);
    FETCH(ep_descriptor_tun_out);

    send_size = (uint32_t)cp-(uint32_t)init_config;
    ret = write(*fd_tun_out, init_config, send_size);

    if (ret != send_size)
    {
        usbsg_debug("[EP4] Write error %d (%m)\n", ret);
        goto end;
    }

    usbsg_debug("ep4 configured\n");

    ret = 0;

end:
    return ret;
}

static void handle_setup_request(int fd, struct usb_ctrlrequest* setup)
{
    int     status;
    uint8_t buffer[512];

    usbsg_debug("Setup request %d\n", setup->bRequest);

    switch (setup->bRequest)
    {
    case USB_REQ_GET_DESCRIPTOR:
        if (setup->bRequestType != USB_DIR_IN)
            goto stall;
        switch (setup->wValue >> 8)
        {
            case USB_DT_STRING:
                usbsg_debug("Get string id #%d (max length %d)\n", setup->wValue & 0xff,
                    setup->wLength);
                status = usb_gadget_get_string (&strings, setup->wValue & 0xff, buffer);
                // Error
                if (status < 0)
                {
                    usbsg_debug("String not found !!\n");
                    break;
                }
                else
                {
                    usbsg_debug("Found %d bytes\n", status);
                }
                write (fd, buffer, status);
                return;
        default:
            usbsg_debug("Cannot return descriptor %d\n", (setup->wValue >> 8));
        }
        break;
    case USB_REQ_SET_CONFIGURATION:
        if (setup->bRequestType != USB_DIR_OUT)
        {
            usbsg_debug("Bad dir\n");
            goto stall;
        }
        switch (setup->wValue) {
        case CONFIG_VALUE:
            printf("Set config value\n");

            if (g_usbg_io_thread_args.fd_in <= 0)
            {
                status = init_ep (
                        &g_usbg_io_thread_args.fd_in,
                        &g_usbg_io_thread_args.fd_out,
                        &g_usbg_io_thread_args.fd_tun_in,
                        &g_usbg_io_thread_args.fd_tun_out);
            }
            else
                status = 0;

            if ((!status) && (g_usbg_io_thread_args.stop))
            {
                /* usbg_io_prepare(&g_usbg_io_thread_args); */
                printf("usbg xcomm started\n");
                g_usbg_io_thread_args.stop = 0;
                /* pthread_create(&g_usbg_io_thread, NULL,
                        usbg_io_thread, &g_usbg_io_thread_args); */
            }

            break;
        case 0:
            usbsg_debug("Disable threads\n");
            g_usbg_io_thread_args.stop = 1;
            break;
        default:
            usbsg_debug("Unhandled configuration value %d\n", setup->wValue);
            break;
        }
        // Just ACK
        status = read (fd, &status, 0);
        return;
    case USB_REQ_GET_INTERFACE:
        usbsg_debug("GET_INTERFACE\n");
        buffer[0] = 0;
        write (fd, buffer, 1);
        return;
    case USB_REQ_SET_INTERFACE:
        usbsg_debug("SET_INTERFACE\n");
        ioctl (g_usbg_io_thread_args.fd_in, GADGETFS_CLEAR_HALT);
        ioctl (g_usbg_io_thread_args.fd_out, GADGETFS_CLEAR_HALT);
        // ACK
        status = read (fd, &status, 0);
        return;
    }

stall:
    usbsg_debug("Stalled\n");
    // Error
    if (setup->bRequestType & USB_DIR_IN)
        read (fd, &status, 0);
    else
        write (fd, &status, 0);
}

static void *handle_ep0_thread(void *arg)
{
    int    ret, nevents, i;
    fd_set read_set;
    struct usb_gadgetfs_event events[5];
    int    fd = 0;

    printf("handle_ep0_thread+\n");

    if (arg)
    {
        fd = *(int *)arg;
    } else {
        printf("Fail to get fd\n");
        return NULL;
    }

    while (1)
    {
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);

        select(fd + 1, &read_set, NULL, NULL, NULL);

        ret = read(fd, &events, sizeof(events));

        if (ret < 0)
        {
            usbsg_debug("Read error %d (%m)\n", ret);
            goto end;
        }
        nevents = ret / sizeof(events[0]);

        usbsg_debug("%d event(s)\n", nevents);

        for (i = 0; i < nevents; i++)
        {
            switch (events[i].type)
            {
            case GADGETFS_CONNECT:
                usbsg_debug("EP0 CONNECT\n");
                break;
            case GADGETFS_DISCONNECT:
                usbsg_debug("EP0 DISCONNECT\n");
                break;
            case GADGETFS_SETUP:
                usbsg_debug("EP0 SETUP\n");
                handle_setup_request(fd, &events[i].u.setup);
                break;
            case GADGETFS_NOP:
            case GADGETFS_SUSPEND:
                break;
            }
        }
    }

end:
    printf("handle_ep0_thread-\n");
    return NULL;
}


/**
 * Setup USB gadget device
 */
void gadgetfs_usb_init(void)
{
    int ret;
    uint32_t send_size;
    struct usb_config_descriptor    config;
    struct usb_config_descriptor    config_hs;
    struct usb_device_descriptor    device_descriptor;
    struct usb_interface_descriptor if_descriptor;
    uint8_t init_config[2048];
    uint8_t* cp;

    printf("USB device setup\n");

    memset(&g_aiocb_async_read, 0, sizeof(struct aiocb));

    g_aio_read_async  = 0;
    g_fd_usb_gadget   = -1;
    g_usbg_io_thread_args.fd_in  = -1;
    g_usbg_io_thread_args.fd_out = -1;
    g_usbg_io_thread_args.stop   = 1;

    g_fd_usb_gadget = open(USB_DEV_NAME, O_RDWR|O_SYNC);

    if (g_fd_usb_gadget <= 0)
    {
        printf("Unable to open %s (%m)\n", USB_DEV_NAME);
        return;
    }

    *(uint32_t*)init_config = 0;
    cp = &init_config[4];

    device_descriptor.bLength           = USB_DT_DEVICE_SIZE;
    device_descriptor.bDescriptorType   = USB_DT_DEVICE;
    device_descriptor.bDeviceClass      = USB_CLASS_VENDOR_SPEC;
    device_descriptor.bDeviceSubClass   = 0;
    device_descriptor.bDeviceProtocol   = 0;
    //device_descriptor.bMaxPacketSize0 = 255; Set by driver
    device_descriptor.idVendor          = USBG_VID; // My own id
    device_descriptor.idProduct         = USBG_PID; // My own id
    device_descriptor.bcdDevice         = 0x0200; // Version
    // Strings
    device_descriptor.iManufacturer         = STRINGID_MANUFACTURER;
    device_descriptor.iProduct              = STRINGID_PRODUCT;
    device_descriptor.iSerialNumber         = STRINGID_SERIAL;
    device_descriptor.bNumConfigurations    = 1; // Only one configuration

    ep_descriptor_in.bLength                = USB_DT_ENDPOINT_SIZE;
    ep_descriptor_in.bDescriptorType        = USB_DT_ENDPOINT;
    ep_descriptor_in.bEndpointAddress       = USB_DIR_IN | 1;
    ep_descriptor_in.bmAttributes           = USB_ENDPOINT_XFER_BULK;
    ep_descriptor_in.wMaxPacketSize         = 512; // HS size

    ep_descriptor_out.bLength               = USB_DT_ENDPOINT_SIZE;
    ep_descriptor_out.bDescriptorType       = USB_DT_ENDPOINT;
    ep_descriptor_out.bEndpointAddress      = USB_DIR_OUT | 2;
    ep_descriptor_out.bmAttributes          = USB_ENDPOINT_XFER_BULK;
    ep_descriptor_out.wMaxPacketSize        = 512; // HS size

    ep_descriptor_tun_in.bLength            = USB_DT_ENDPOINT_SIZE;
    ep_descriptor_tun_in.bDescriptorType    = USB_DT_ENDPOINT;
    ep_descriptor_tun_in.bEndpointAddress   = USB_DIR_IN | 3;
    ep_descriptor_tun_in.bmAttributes       = USB_ENDPOINT_XFER_BULK;
    ep_descriptor_tun_in.wMaxPacketSize     = 512; // HS size

    ep_descriptor_tun_out.bLength           = USB_DT_ENDPOINT_SIZE;
    ep_descriptor_tun_out.bDescriptorType   = USB_DT_ENDPOINT;
    ep_descriptor_tun_out.bEndpointAddress  = USB_DIR_OUT | 4;
    ep_descriptor_tun_out.bmAttributes      = USB_ENDPOINT_XFER_BULK;
    ep_descriptor_tun_out.wMaxPacketSize    = 512; // HS size

    if_descriptor.bLength = sizeof(if_descriptor);
    if_descriptor.bDescriptorType = USB_DT_INTERFACE;
    if_descriptor.bInterfaceNumber      = 0;
    if_descriptor.bAlternateSetting     = 0;
    if_descriptor.bNumEndpoints         = 4;
    if_descriptor.bInterfaceClass       = USB_CLASS_VENDOR_SPEC;
    if_descriptor.bInterfaceSubClass    = 0;
    if_descriptor.bInterfaceProtocol    = 0;
    if_descriptor.iInterface            = STRINGID_INTERFACE;

    config_hs.bLength = sizeof(config_hs);
    config_hs.bDescriptorType = USB_DT_CONFIG;
    config_hs.wTotalLength = config_hs.bLength +
        if_descriptor.bLength + ep_descriptor_in.bLength + ep_descriptor_out.bLength +
        ep_descriptor_tun_in.bLength + ep_descriptor_tun_out.bLength;
    config_hs.bNumInterfaces = 1;
    config_hs.bConfigurationValue = CONFIG_VALUE;
    config_hs.iConfiguration = STRINGID_CONFIG_HS;
    config_hs.bmAttributes   = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER;
    config_hs.bMaxPower      = 1;

    config.bLength = sizeof(config);
    config.bDescriptorType = USB_DT_CONFIG;
    config.wTotalLength = config.bLength +
        if_descriptor.bLength + ep_descriptor_in.bLength + ep_descriptor_out.bLength +
        ep_descriptor_tun_in.bLength + ep_descriptor_tun_out.bLength;
    config.bNumInterfaces = 1;
    config.bConfigurationValue = CONFIG_VALUE;
    config.iConfiguration = STRINGID_CONFIG_LS;
    config.bmAttributes   = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER;
    config.bMaxPower      = 1;

    FETCH(config);
    FETCH(if_descriptor);
    FETCH(ep_descriptor_in);
    FETCH(ep_descriptor_out);
    FETCH(ep_descriptor_tun_in);
    FETCH(ep_descriptor_tun_out);

    FETCH(config_hs);
    FETCH(if_descriptor);
    FETCH(ep_descriptor_in);
    FETCH(ep_descriptor_out);
    FETCH(ep_descriptor_tun_in);
    FETCH(ep_descriptor_tun_out);

    FETCH(device_descriptor);

    // Configure ep0
    send_size = (uint32_t)cp-(uint32_t)init_config;
    ret = write(g_fd_usb_gadget, init_config, send_size);

    if (ret != send_size)
    {
        usbsg_debug("Write error %d (%m)\n", ret);
        goto fail_end;
    }

    usbsg_debug("ep0 configured\n");

    /**
     * Create EP0 handler thread
     */
    pthread_create(&g_thread_ep0_handler, NULL, &handle_ep0_thread, &g_fd_usb_gadget);

    return;

fail_end:
    if (g_fd_usb_gadget != -1) close(g_fd_usb_gadget);

}

/**
 * Stop USB gadget device
 */
void gadgetfs_usb_stop(void)
{
    int    ires;
    void * vres;

    printf("GadgetFS USB Stop\n");

#ifdef USBG_IO_THREAD
    ires = pthread_cancel(g_usbg_io_thread);
    if (ires != 0)
    {
        printf("g_usbg_io_thread cancel problem\n");
    }


    ires = pthread_join(g_usbg_io_thread, &vres);
    if (ires != 0)
    {
        printf("g_usbg_io_thread pthread_join problem\n");
    }

    if (vres == PTHREAD_CANCELED)
        printf("g_usbg_io_thread thread was canceled\n");
    else
        printf("g_usbg_io_thread thread wasn't canceled (shouldn't happen!)\n");
#endif

    ires = pthread_cancel(g_thread_ep0_handler);
    if (ires != 0)
    {
        printf("g_thread_ep0_handler cancel problem\n");
    }

    ires = pthread_join(g_thread_ep0_handler, &vres);
    if (ires != 0)
    {
        printf("g_thread_ep0_handler pthread_join problem\n");
    }

    if (vres != PTHREAD_CANCELED)
    {
        printf("g_thread_ep0_handler thread wasn't canceled (shouldn't happen!)\n");
    }

    if (g_usbg_io_thread_args.fd_in != -1)
        {
            close(g_usbg_io_thread_args.fd_in);
        }

    if (g_usbg_io_thread_args.fd_out != -1)
        {
            close(g_usbg_io_thread_args.fd_out);
        }

    if (g_usbg_io_thread_args.fd_tun_in != -1)
        {
            close(g_usbg_io_thread_args.fd_tun_in);
        }

    if (g_usbg_io_thread_args.fd_tun_out != -1)
        {
            close(g_usbg_io_thread_args.fd_tun_out);
        }

    if (g_fd_usb_gadget != -1) close(g_fd_usb_gadget);

    gadgetfs_usb_dismount();

}
