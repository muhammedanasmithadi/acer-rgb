# Convenience wrapper for manual (non-DKMS) builds.
# Usage: make [KDIR=/lib/modules/<ver>/build]
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
