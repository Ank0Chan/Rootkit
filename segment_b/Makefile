# Makefile - segment B
#
# Builds a single module rootkit_b.ko from the two object files. Out-of-tree
# build driven by the running kernel's build tree (see KDIR below).

obj-m += rootkit_b.o
rootkit_b-y := src/companion_dev.o src/module_core.o

# Kernel build tree of the currently running kernel. On the LFS/QEMU target
# this must resolve to the kernel sources/headers of the booted kernel.
# Adapt KDIR manually if the LFS kernel headers live elsewhere.
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.PHONY: all clean load unload reload log

all:
	@echo "[*] Building rootkit_b.ko"
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	@echo "[*] Cleaning build artifacts"
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load:
	@echo "[*] Loading rootkit_b.ko"
	insmod rootkit_b.ko

unload:
	@echo "[*] Unloading rootkit_b"
	rmmod rootkit_b

reload: unload load
	@echo "[*] Reloaded rootkit_b"

log:
	@echo "[*] Recent kernel log"
	dmesg | tail -n 20
