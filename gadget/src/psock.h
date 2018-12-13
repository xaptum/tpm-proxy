/**
 * @brief PSOCK functionality header
 *
 * @file psock.h
 */

#ifndef SRC_PSOCK_H_
#define SRC_PSOCK_H_

#define SOCK0_XAP                   ("/run/sock0")
#define TUN_XAP                     ("/run/ptun")
#define PSOCK_TH_STACK_SZ           (65535)

#define PSOCK_ACCEPT_TIMEOUT_SEC    (0)
#define PSOCK_ACCEPT_TIMEOUT_USEC   (100000)

#define PSOCK_READ_TIMEOUT_SEC      (0)
#define PSOCK_READ_TIMEOUT_USEC     (100000)
#define PSOCK_WAIT_USBG_CONN        (10000)

#define PSOCK_WAIT_USBG_THR_STOP    (100)

int  psock_init(void);

void psock_deinit(void);


#endif /* SRC_PSOCK_H_ */
