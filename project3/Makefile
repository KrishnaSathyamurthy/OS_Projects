ifndef ARCHITECTURE
   	$(error You must define the ARCHITECTURE)
	ARCHITECTURE = 64
endif

CC = gcc
AR = ar -rc
RANLIB = ranlib

ifeq ($(ARCHITECTURE), 32)
	CFLAGS = -m32
endif
CFLAGS += -g -c

all: clean my_vm.a

my_vm.a: my_vm.o
	$(AR) $@ $^
	$(RANLIB) $@

clean:
	rm -rf *.o *.a
