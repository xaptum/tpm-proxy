/**
 * @brief PTUN functionality
 *
 * @file ptun.c
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

#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadgetfs.h>

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "ptun.h"
#include "usbg_service.h"

#define ETH_HLEN        14
#define ETH_FRM_MAX     1560
#define ETH_TUN_BUF_SZ  2048
#define XP_RX_OVERHEAD  7    /* 3 byte header + 4 byte crc tail */

/**
 * Static variables
 */
static int g_tun_client_fd    = -1;
static int g_tun_serv_fd      = -1;
static int g_tun_stop_thr     = 1;
static int g_tun_stopped_thr  = 0;
static pthread_attr_t g_tun_serv_attr;

static pthread_t g_tun_hnd_thread;


static void *handle_tun_thread(void *arg)
{
    int     iret;
    uint8_t buf8[ETH_TUN_BUF_SZ];
    uint8_t buf8_tx[ETH_TUN_BUF_SZ];
    int     itimeoutc;
    int     tx_req_sz;
    struct timeval tv_ac;
    fd_set rfds_ac;

    printf("handle_tun_thread+\n");

    /* Wait for USB */
    itimeoutc = PTUN_WAIT_USBG_CONN;
    while (( gadgetfs_io_is_ready() == 0) &&
            (--itimeoutc))
    {
        usleep(1000);
    }

    /* If timeout => stop thread */
    if (!itimeoutc)
    {
        printf("Tun thread stop\n");
        return NULL;
    }

    FD_ZERO(&rfds_ac);
    FD_SET(g_tun_serv_fd, &rfds_ac);

    memset(&tv_ac, 0, sizeof(tv_ac));
    tv_ac.tv_sec  = PTUN_ACCEPT_TIMEOUT_SEC;
    tv_ac.tv_usec = PTUN_ACCEPT_TIMEOUT_USEC;

    iret = select(g_tun_serv_fd + 1,
            &rfds_ac,
            NULL, NULL,
            &tv_ac);
    if (iret < 0)
    {
        printf("select accept timeout failed\n");
    } else if (iret) {
        printf("select accept OK\n");
    }

    /**
     * Switch to non-blocking mode for accept
     */
    fcntl(g_tun_serv_fd, F_SETFL,
         fcntl(g_tun_serv_fd, F_GETFL) |
         O_NONBLOCK);

    printf("[Ptun] Ready for client connect().\n");


    while(!g_tun_stop_thr)
    {
        g_tun_client_fd = accept(g_tun_serv_fd, NULL, NULL);
        if (g_tun_client_fd < 0)
        {
           /* Wait for accept */
           continue;
        }

        printf("[Ptun] Connection established.\n");

        iret = 1;
        while ((iret != 0) && (!g_tun_stop_thr))
        {
            /**
             * Read first from /run/ptun
             */
            iret = recv(g_tun_client_fd,
                    &buf8[0],
                    ETH_TUN_BUF_SZ,
                    MSG_PEEK | MSG_DONTWAIT);

            if (iret < 0)
            {
                /* recv error */

                if (iret != -1)
                {
                    /* socket closed */
                    break;
                }

            } else if (iret > 0) {

                /* recv ok */

                iret = recv(g_tun_client_fd,
                        &buf8[0],
                        iret,
                        0);

                printf("recv data size %d bytes, transmit to USB\n", iret);

                if ((iret > 0) && (iret < ETH_FRM_MAX))
                {
                    /**
                     * Set RX empty packet
                     *
                     */
                    buf8_tx[0] = 0x00;                 /* status ok */
                    buf8_tx[1] = iret & 0xFF;          /* len low */
                    buf8_tx[2] = (iret & 0xFF00) >> 8; /* len high */

                    memcpy(&buf8_tx[3], buf8, iret);

                    iret = gadgetfs_io_tun_write(&buf8,
                            iret + XP_RX_OVERHEAD,
                            &g_tun_stop_thr);

                    if (iret == -EPERM)
                    {
                        break;
                    }
                } else if (iret < 0)
                {
                    break;
                }

            }

            if (g_tun_stop_thr)
            {
                printf("recv g_tun_stop_thr == 1\n");
                break;
            }

            /**
             * Write to /run/ptun with data from USB
             */
            iret = gadgetfs_io_tun_read_async(&buf8, 1023, &g_tun_stop_thr);
            if (iret > 0)
            {
                /**
                 * b1: packet size low
                 * b2: packet size high
                 */
                tx_req_sz = buf8[0] | (buf8[1] << 8);

                if (tx_req_sz < ETH_FRM_MAX)
                {
                    memcpy(&buf8_tx[0], &buf8[2], tx_req_sz);

                    iret = send(g_tun_client_fd,
                            buf8_tx,
                            tx_req_sz, 0);

                    if (iret < 0)
                    {
                        printf("send error (%d)\n", iret);

                        break;
                    }
                }


            } else if (iret == -EPERM)
            {
                break;
            }

        }

        close(g_tun_client_fd);
        g_tun_client_fd = -1;
    }

    printf("handle_tun_thread-\n");

    g_tun_stopped_thr = 1;

    return NULL;
}

/**
 * PTUN subsystem initialization
 *
 * @return 0 - success, <0 - error
 */
int ptun_init(void)
{
    struct sockaddr_un serveraddr;
    int                iret;

    printf("ptun_init+\n");

    g_tun_serv_fd      = -1;
    g_tun_client_fd    = -1;
    g_tun_stop_thr     = 0;
    g_tun_stopped_thr  = 0;

    unlink(TUN_XAP);

    /**
     *   /run/ptun   - TUN
     */
    g_tun_serv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_tun_serv_fd < 0)
    {
        printf("Failed to create socket\n");
        return g_tun_serv_fd;
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sun_family = AF_UNIX;
    strcpy(serveraddr.sun_path, TUN_XAP);

    iret = bind(g_tun_serv_fd,
            (struct sockaddr *)&serveraddr,
            SUN_LEN(&serveraddr));
    if (iret < 0)
    {
        printf("bind failed\n");
        return iret;
    }

    iret = listen(g_tun_serv_fd, 1);
    if (iret < 0)
    {
       perror("listen failed");
       return iret;
    }

    /**
     * ---------------------------------------------------------------------------------
     */

    g_tun_stop_thr = 0;
    /**
     * Create TUN handler thread
     */
    pthread_attr_init(&g_tun_serv_attr);
    pthread_attr_setstacksize (&g_tun_serv_attr,
            PTUN_TH_STACK_SZ);
    pthread_create(&g_tun_hnd_thread,
            NULL,
            &handle_tun_thread,
            &g_tun_serv_fd);

    printf("ptun_init-\n");

    return 0;
}

/**
 * Deinitialization of PTUN subsystem
 */
void ptun_deinit(void)
{
    int    ires, i;
    void * vres;

    g_tun_stop_thr = 1;

    i = PTUN_WAIT_USBG_THR_STOP;
    while ((--i) && (!g_tun_stopped_thr))
    {
        usleep(10000);
    }

    /* If timeout => cancel thread */
    if (!i)
    {
        printf("pthread_cancel\n");
        ires = pthread_cancel(g_tun_hnd_thread);
        if (ires != 0)
        {
            printf("g_tun_hnd_thread cancel problem\n");
        }
    }

    printf("Wait for stopping g_tun_hnd_thread\n");

    ires = pthread_join(g_tun_hnd_thread, &vres);
    if (ires != 0)
    {
        printf("g_tun_hnd_thread pthread_join problem\n");
    }

    if (g_tun_client_fd != -1)
    {
        close(g_tun_client_fd);
    }

    if (g_tun_serv_fd != -1)
    {
        close(g_tun_serv_fd);
    }

    printf("ptun_deinit-\n");

    unlink(TUN_XAP);
}

