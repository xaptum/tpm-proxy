/**
 * @brief Xaptum mPCIe offload card USB service
 *
 * @file xaprd_arm_main.c
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

#include "usbg_service.h"
#include "psock.h"
#include "ptun.h"

static int g_end_app = 0;

/* Signal Handler for SIGINT */
void sigintHandler(int sig_num)
{
    g_end_app = 1;
}

int main()
{
    int max_cycl;

    printf("XAPRD service\n");

    g_end_app = 0;
    signal(SIGINT, sigintHandler);

    printf("XAPRD service : Started\n");

    /* PSOCK initialization */
    psock_init();

    /* PTUN initialization */
    ptun_init();

    /* Mount GadetFS */
    gadgetfs_usb_mount();

    /* USB setup */
    gadgetfs_usb_init();

    //max_cycl = 3000;
    max_cycl = 500;
    while ((!g_end_app) && (max_cycl))
    {
        usleep(10000);
        //max_cycl--;
    }

    /* PTUN deinitialization */
    ptun_deinit();

    /* PSOCK deinitialization */
    psock_deinit();

    /* Stop USB comm */
    gadgetfs_usb_stop();

    printf("XAPRD service : Stop\n");

    return 0;
}
