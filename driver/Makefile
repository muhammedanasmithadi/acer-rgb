# Convenience wrapper for manual (non-DKMS) builds.
# Usage: make [KDIR=/lib/modules/<ver>/build]
# Defaults to the running kernel's headers, falling back to the newest
# installed headers when they are absent (e.g. CI runners).
KDIR ?= /lib/modules/$(shell uname -r)/build
ifeq (,$(wildcard $(KDIR)/Makefile))
KDIR := $(lastword $(sort $(wildcard /usr/src/linux-headers-*)))
endif

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
