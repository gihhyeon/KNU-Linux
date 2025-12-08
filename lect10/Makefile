obj-m = hello.o

KDIR := /usr/src/linux-source-6.8.0
PWD := $(shell pwd)

default:
	make -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.ko *.o *.mod.* *.cmd *.o *.ko
