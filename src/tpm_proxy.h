/**
 * @brief TPM proxy functionality header
 *
 * @file tpm_proxy.h
 */

#ifndef TPM_PROXY_H_
#define TPM_PROXY_H_

#define TPM_TH_STACK_SZ             (65535)
#define TPM_WAIT_USBG_THR_STOP      (100)
#define TPM_WAIT_USBG_CONN          (10000)
#define USBG_READ_MAX               (512)


int  tpm_proxy_init(void);

void tpm_proxy_deinit(void);


#endif /* TPM_PROXY_H_ */
