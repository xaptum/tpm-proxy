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
#include "tpm_proxy.h"

static int g_end_app = 0;

/* Signal Handler for SIGINT */
void sigintHandler(int sig_num)
{
    g_end_app = 1;
}

int main()
{
    printf("XAPRD TPM proxy service\n");

    g_end_app = 0;
    signal(SIGINT, sigintHandler);

    printf("XAPRD TPM proxy service : Started\n");

    /* Mount GadetFS */
    gadgetfs_usb_mount();

    /* USB setup */
    gadgetfs_usb_init();

    /* TPM proxy function initialization */
    tpm_proxy_init();

    while (!g_end_app)
    {
        usleep(100000);
    }

    /* TPM proxy function deinitialization */
    tpm_proxy_deinit();

    /* Stop USB comm */
    gadgetfs_usb_stop();

    printf("XAPRD TPM proxy service : Stop\n");

    system("umount /root/usbg");

    return 0;
}
