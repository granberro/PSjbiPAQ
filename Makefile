obj-m := psjbipaq.o

include /home/ubuntu/arm_kernel/kernel/Rules.make

KDIR := /home/ubuntu/arm_kernel/kernel
PWD := $(shell pwd)

build:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) M=$(PWD) modules

clean:
	rm -f *.o *.flags

