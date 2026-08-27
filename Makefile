all: module helloctl

helloctl:
	$(MAKE) -C helloctl

module:
	$(MAKE) -C module

load: module
	sudo insmod module/hello_module.ko

unload:
	sudo rmmod hello_module

clean:
	$(MAKE) -C module clean
	$(MAKE) -C helloctl clean

.PHONY: all module helloctl load unload clean
