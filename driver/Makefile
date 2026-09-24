# Convenience wrapper for manual (non-DKMS) builds.
# Usage: make [KDIR=/lib/modules/<ver>/build]
# Defaults to the running kernel's headers, falling back to the newest
# installed *flavored* headers tree. The bare linux-headers-<version>/
# tree is only the shared common subset and cannot build modules alone,
# so it is never selected.
KDIR ?= /lib/modules/$(shell uname -r)/build
ifeq (,$(wildcard $(KDIR)/Makefile))
KDIR := $(lastword $(sort $(dir $(wildcard /usr/src/linux-headers-*/arch))))
endif
ifeq (,$(wildcard $(KDIR)/Makefile))
$(error No kernel headers found (tried $(KDIR)); install kernel-devel or linux-headers)
endif

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
