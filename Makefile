obj-m = producer_consumer.o
ccflags-y := -Wno-error=missing-prototypes -Wno-error=restrict

all:
	cd process_gen && $(MAKE)
	make -C /usr/src/linux M=$(PWD) modules

clean:
	make -C /usr/src/linux M=$(PWD) clean
	cd process_gen && $(MAKE) clean
