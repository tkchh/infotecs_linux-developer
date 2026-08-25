obj-m += hello_module.o

PWD := $(CURDIR)

all:
	make -C /lib/modules/5.10.265-0510265-generic/build M=$(PWD) modules

clean:
	make -C /lib/modules/5.10.265-0510265-generic/build M=$(PWD) clean