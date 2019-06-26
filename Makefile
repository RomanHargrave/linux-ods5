ifneq ($(KERNELRELEASE),)

obj-m  := ods5.o
ods5-y := dir.o file.o home.o indexf.o inode.o ioctl.o sizchk.o super.o

else

KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

endif
