CONFIG_MODULE_SIG=n
#NAME = gamepad_driver
NAME = gamepad_driver_mapped

ifneq ($(KERNELRELEASE),)
	obj-m := $(NAME).o
else
	KERN_DIR ?= /usr/src/linux-headers-$(shell uname -r)/
    PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) modules
endif
clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions