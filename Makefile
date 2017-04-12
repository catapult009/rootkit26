ifneq ($(KERNELRELEASE),)
	obj-m := rt.o
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean: 
	rm -rf *.o *.symvers *.order *.mod.c *.tex *~ *.ko *.ko.unsigned .*.cmd

endif
