/**
 * @brief TPM proxy functionality
 *
 * @file tpm_proxy.c
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

#include "tpm_proxy.h"
#include "usbg_service.h"

/**
 * Static variables
 */
static int g_tpm_serv_fd          = -1;
static int g_tpm_stop_thr         = 1;
static int g_tpm_stopped_thr_srv  = 0;
static pthread_attr_t g_tpm_serv_attr;

static pthread_t g_tpm_hnd_thread_srv;


/*** Function prototypes ***/


static void *handle_tpm_thread_srv(void *arg)
{
    int     iret, fd_usb, iact, sock_max;
    fd_set  read_fds;
    fd_set  except_fds;
    int     itimeoutc;
    uint8_t buf8[USBG_READ_MAX];
    int     fd_tpm0, fd_wr_usb;

    printf("handle_psock_thread_usb+\n");

    fd_tpm0 = open("/dev/tpm0", O_RDWR | O_SYNC);

    if (fd_tpm0 < 0)
    {
        perror("/dev/tpm0 open fails");
        goto thr_error2;
    }

    printf("open OK tpm0 (%d)\r\n", fd_tpm0);

    /* Wait for USB */
    itimeoutc = TPM_WAIT_USBG_CONN;
    while (( gadgetfs_io_is_ready() == 0) &&
            (--itimeoutc))
    {
        usleep(1000);
    }

    /* If timeout => stop thread */
    if (!itimeoutc)
    {
        printf("USB thread stop\n");
        return NULL;
    }

    printf("[GadgetFS] Ready for client connect().\n");

    fd_usb = gadgetfs_io_get_read_fd();
    fd_wr_usb = gadgetfs_io_get_write_fd();
    FD_ZERO(&read_fds);
    FD_ZERO(&except_fds);
    FD_SET(fd_usb, &read_fds);
    FD_SET(fd_usb, &except_fds);
    FD_SET(fd_tpm0, &read_fds);
    FD_SET(fd_tpm0, &except_fds);

    if (fd_usb >= fd_tpm0)
    {
        sock_max = fd_usb;
    } else {
        sock_max = fd_tpm0;
    }

    while(!g_tpm_stop_thr)
    {
        iact = select(sock_max + 1, &read_fds, 0, &except_fds, NULL);

        printf("select DONE\r\n");

        switch (iact) {
          case -1:
            perror("select()");
            goto thr_error1;

          case 0:
            // you should never get here
            printf("select() returns 0.\r\n");
            goto thr_error1;

          default:
            /* All set fds should be checked. */

            if (FD_ISSET(fd_usb, &read_fds)) {
                /* Data received */

                printf("USB RX\r\n");

                iret = read(fd_usb, &buf8[0], USBG_READ_MAX);

                if (iret <= 0)
                {
                    printf("Read USB fd <= 0.\r\n");
                    goto thr_error1;
                }

                if (iret > 0)
                {
                    printf("read usb %d\r\n", iret);

                    write(fd_tpm0, &buf8[0], iret);
                }
            }

            if (FD_ISSET(fd_tpm0, &read_fds)) {
                /* Data received */

                printf("TPM0 RX\r\n");

                iret = read(fd_tpm0, &buf8[0], USBG_READ_MAX);

                if (iret <= 0)
                {
                    printf("Read TPM fd <= 0.\r\n");
                    //goto thr_error1;
                }

                if (iret > 0)
                {
                    printf("write to usb %d\r\n", iret);

                    write(fd_wr_usb, &buf8[0], iret);
                }
            }

            if (FD_ISSET(fd_usb, &except_fds)) {
              printf("Exception USB fd.\r\n");
              goto thr_error1;
            }

            if (FD_ISSET(fd_tpm0, &except_fds)) {
              printf("Exception TPM0 fd.\r\n");
              goto thr_error1;
            }

        } /* switch (iact) { */
    }

thr_error1:

    printf("handle_psock_thread_usb-\n");

    close(fd_tpm0);

thr_error2:

    g_tpm_stopped_thr_srv = 1;

    return NULL;
}

/**
 * TPM proxy subsystem initialization
 *
 * @return 0 - success, <0 - error
 */
int tpm_proxy_init(void)
{
    printf("tpm_proxy_init+\n");

    /**
     * ---------------------------------------------------------------------------------
     */

    g_tpm_stop_thr = 0;
    /**
     * Create TPM proxy handler thread
     */
    pthread_attr_init(&g_tpm_serv_attr);
    pthread_attr_setstacksize (&g_tpm_serv_attr, TPM_TH_STACK_SZ);
    pthread_create(&g_tpm_hnd_thread_srv, NULL, &handle_tpm_thread_srv,
            &g_tpm_serv_fd);

    printf("tpm_proxy_init-\n");

    return 0;
}

/**
 * Deinitialization of TPM proxy subsystem
 */
void tpm_proxy_deinit(void)
{
    int    ires, i;
    void * vres;

    printf("Wait for stopping g_tpm_hnd_thread\n");

    g_tpm_stop_thr = 1;

    i = TPM_WAIT_USBG_THR_STOP;
    while ((--i) && (!g_tpm_stopped_thr_srv))
    {
        usleep(10000);
    }

    /* If timeout => cancel thread SRV */
    if (!i)
    {
        printf("pthread_cancel srv\n");
        ires = pthread_cancel(g_tpm_hnd_thread_srv);
        if (ires != 0)
        {
            printf("g_tpm_hnd_thread_srv cancel problem\n");
        }
    }

    ires = pthread_join(g_tpm_hnd_thread_srv, &vres);
    if (ires != 0)
    {
        printf("g_tpm_hnd_thread_srv pthread_join problem\n");
    }
    if (g_tpm_serv_fd != -1)
    {
        close(g_tpm_serv_fd);
    }

    printf("tpm_proxy_deinit-\n");
}
