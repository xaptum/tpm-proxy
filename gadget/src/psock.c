/**
 * @brief PSOCK functionality
 *
 * @file psock.c
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

#include "psock.h"
#include "psock_proto.h"
#include "usbg_service.h"

/**
 * Static variables
 */
static int g_psock_serv_fd    = -1;
static int g_psock_client_fd  = -1;
static int g_sock_stop_thr    = 1;
static int g_sock_stopped_thr = 0;
static pthread_attr_t g_psock_serv_attr;

static pthread_t g_psock_hnd_thread;


static void *handle_psock_thread(void *arg)
{
    int     iret;
    uint8_t buf8[2048];
#ifdef PSOCK_RCV_TIMOUT
    struct timeval tv_rx;
#endif
    struct timeval tv_ac;
    fd_set rfds_ac;
    int     itimeoutc;
    t_psock_min_hdr psock_hdr;
    t_psock_min_ack psock_ack;

    printf("handle_psock_thread+\n");

    g_psock_client_fd = -1;

    /* Wait for USB */
    itimeoutc = PSOCK_WAIT_USBG_CONN;
    while (( gadgetfs_io_is_ready() == 0) &&
            (--itimeoutc))
    {
        usleep(1000);
    }

    /* If timeout => stop thread */
    if (!itimeoutc)
    {
        printf("Psock thread stop\n");
        return NULL;
    }

    FD_ZERO(&rfds_ac);
    FD_SET(g_psock_serv_fd, &rfds_ac);

    memset(&tv_ac, 0, sizeof(tv_ac));
    tv_ac.tv_sec  = PSOCK_ACCEPT_TIMEOUT_SEC;
    tv_ac.tv_usec = PSOCK_ACCEPT_TIMEOUT_USEC;

    iret = select(g_psock_serv_fd + 1,
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
    fcntl(g_psock_serv_fd, F_SETFL,
         fcntl(g_psock_serv_fd, F_GETFL) |
         O_NONBLOCK);

    printf("[Psock] Ready for client connect().\n");

    while(!g_sock_stop_thr)
    {

        g_psock_client_fd = accept(g_psock_serv_fd, NULL, NULL);
        if (g_psock_client_fd < 0)
        {
           /* Wait for accept */
           continue;
        }

#ifdef PSOCK_RCV_TIMOUT

        /**
         * Switch to blocking mode for read/write
         */
        fcntl(g_psock_client_fd, F_SETFL,
             fcntl(g_psock_client_fd, F_GETFL) &
             ~O_NONBLOCK);

        tv_rx.tv_sec  = PSOCK_READ_TIMEOUT_SEC;
        tv_rx.tv_usec = PSOCK_READ_TIMEOUT_USEC;
        iret = setsockopt(g_psock_client_fd,
                SOL_SOCKET,
                SO_RCVTIMEO,
                (const char*)&tv_rx,
                sizeof(tv_rx));

        if (iret < 0)
        {
            printf("[Psock] setsockopt rx failed\n");
        }
#endif

        while (1)
        {
            memset(&psock_hdr, 0, sizeof(t_psock_min_hdr));
            iret = recv(g_psock_client_fd,
                    &psock_hdr,
                    sizeof(t_psock_min_hdr),
                    0);//MSG_PEEK | MSG_DONTWAIT);

            if (iret > 0)
            {
                /* Data received */
            }

            if (iret == 0)
            {
                /* Socket closed */
                break;
            }

            /* Header correct? */
            if ((iret == sizeof(t_psock_min_hdr))
                    && (psock_hdr.key == PSOCK_KEY))
            {
                if (psock_hdr.data_size)
                {
                    iret = recv(g_psock_client_fd,
                            &buf8,
                            psock_hdr.data_size,
                            0);//MSG_PEEK | MSG_DONTWAIT);

                    if (iret > 0)
                    {
                        /* printf("[Psock] %d bytes of data were received (%02X) from PSOCK\n", iret, buf8[0] & 0xFF); */
                    }

                    if (iret == 0)
                    {
                        /* Socket closed */
                        break;
                    }
                }

                /* Send data by USB */
                gadgetfs_io_write(&psock_hdr,
                    sizeof(t_psock_min_hdr), &g_sock_stop_thr);

                if (psock_hdr.data_size)
                {
                    iret = gadgetfs_io_write(&buf8,
                            psock_hdr.data_size, &g_sock_stop_thr);

                    if (iret == -EPERM)
                    {
                        /* Stop request */
                        break;
                    }
                }

                memset(&psock_ack, 0, sizeof(psock_ack));

                /* Receive data by USB */
                iret = gadgetfs_io_read(&psock_ack,
                    sizeof(psock_ack), &g_sock_stop_thr);

                if (iret == -EPERM)
                {
                    /* Stop request */
                    break;
                }

                /* Header correct? */
                if ((iret == sizeof(psock_ack))
                        && (psock_ack.key == PSOCK_KEY))
                {
                    if (psock_ack.data_size)
                    {
                        iret = gadgetfs_io_read(&buf8,
                                psock_ack.data_size, &g_sock_stop_thr);

                        if (iret == -EPERM)
                        {
                            /* Stop request */
                            break;
                        }
                    }

                    iret = send(g_psock_client_fd, &psock_ack, sizeof(psock_ack), 0);

                    if (psock_ack.data_size)
                    {
                        iret = send(g_psock_client_fd, &buf8[0], psock_ack.data_size, 0);
                    }

                } else {
                    printf("[Psock] wrong ack key %x\n", psock_ack.key);
                }

            } else {
                printf("[Psock] wrong header key %x\n", psock_hdr.key);

            }

#if 0
            if (iret < 0)
            {
                printf("recv() failed, code = %d\n", iret);
            }
#endif


        }

        close(g_psock_client_fd);
        g_psock_client_fd = -1;
    }

    printf("handle_psock_thread-\n");

    g_sock_stopped_thr = 1;

    return NULL;
}

/**
 * PSOCK subsystem initialization
 *
 * @return 0 - success, <0 - error
 */
int psock_init(void)
{
    struct sockaddr_un serveraddr;
    int                iret;

    printf("psock_init+\n");

    g_psock_serv_fd    = -1;
    g_psock_client_fd  = -1;
    g_sock_stop_thr    = 1;
    g_sock_stopped_thr = 0;

    unlink(SOCK0_XAP);

    /**
     *   /run/sock0 - socket
     */
    g_psock_serv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_psock_serv_fd < 0)
    {
        printf("Failed to create socket\n");
        return g_psock_serv_fd;
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sun_family = AF_UNIX;
    strcpy(serveraddr.sun_path, SOCK0_XAP);

    iret = bind(g_psock_serv_fd,
            (struct sockaddr *)&serveraddr,
            SUN_LEN(&serveraddr));
    if (iret < 0)
    {
        printf("bind failed\n");
        return iret;
    }

    iret = listen(g_psock_serv_fd, 1);
    if (iret < 0)
    {
       perror("listen failed");
       return iret;
    }

    /**
     * ---------------------------------------------------------------------------------
     */

    g_sock_stop_thr = 0;
    /**
     * Create PSOCK handler thread
     */
    pthread_attr_init(&g_psock_serv_attr);
    pthread_attr_setstacksize (&g_psock_serv_attr, PSOCK_TH_STACK_SZ);
    pthread_create(&g_psock_hnd_thread, NULL, &handle_psock_thread, &g_psock_serv_fd);

    printf("psock_init-\n");

    return 0;
}

/**
 * Deinitialization of PSOCK subsystem
 */
void psock_deinit(void)
{
    int    ires, i;
    void * vres;

    printf("Wait for stopping g_psock_hnd_thread\n");

    g_sock_stop_thr = 1;

    i = PSOCK_WAIT_USBG_THR_STOP;
    while ((--i) && (!g_sock_stopped_thr))
    {
        usleep(10000);
    }

    /* If timeout => cancel thread */
    if (!i)
    {
        printf("pthread_cancel\n");
        ires = pthread_cancel(g_psock_hnd_thread);
        if (ires != 0)
        {
            printf("g_psock_hnd_thread cancel problem\n");
        }
    }

    ires = pthread_join(g_psock_hnd_thread, &vres);
    if (ires != 0)
    {
        printf("g_psock_hnd_thread pthread_join problem\n");
    }

    if (g_psock_client_fd != -1)
    {
        close(g_psock_client_fd);
    }

    if (g_psock_serv_fd != -1)
    {
        close(g_psock_serv_fd);
    }

    printf("psock_deinit-\n");

    unlink(SOCK0_XAP);
}

