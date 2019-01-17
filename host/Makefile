#
# Makefile for TPM proxy
#

ifneq ($(KERNELRELEASE),)

include Kbuild

else

KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

debug:
	$(MAKE) EXTRA_CFLAGS='-DTPM_DEBUG'

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install
	depmod

clean:
	rm -rf *.o .depend .*.cmd *.ko *.mod.c \
		modules.order Module.symvers

.PHONY: default clean

endif
