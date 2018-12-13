/**
 * @brief PTUN functionality header
 *
 * @file ptun.h
 */

#ifndef SRC_PTUN_H_
#define SRC_PTUN_H_

#define TUN_XAP                     ("/run/ptun")
#define PTUN_TH_STACK_SZ            (65535)

#define PTUN_ACCEPT_TIMEOUT_SEC     (0)
#define PTUN_ACCEPT_TIMEOUT_USEC    (100000)

#define PTUN_READ_TIMEOUT_SEC       (0)
#define PTUN_READ_TIMEOUT_USEC      (100000)
#define PTUN_WAIT_USBG_CONN         (10000)

#define PTUN_WAIT_USBG_THR_STOP     (100)

int  ptun_init(void);

void ptun_deinit(void);


#endif /* SRC_PTUN_H_ */
