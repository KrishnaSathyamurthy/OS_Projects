
# Project preprocessor flags
ifeq ($(SCHED), RR)
    IS_COMPILE = 1
else ifeq ($(SCHED), MLFQ)
    IS_COMPILE = 1
else
    IS_COMPILE = 0
endif
# DEBUG_TL, INFO_TL, NONE
LOG_LEVEL = DEBUG_TL

# Compiler options
CC = gcc
CFLAGS = -g -c
AR = ar -rc
RANLIB = ranlib


all: clean thread-worker.a

thread-worker.a: thread-worker.o
	$(AR) libthread-worker.a thread-worker.o queue.o mutex_types.o
	$(RANLIB) libthread-worker.a

thread-worker.o: logger.h scheduler.h thread-worker.h thread_worker_types.h mutex_types.h

ifeq ($(IS_COMPILE), 1)
	$(CC) $(CFLAGS) -D$(LOG_LEVEL) -D$(SCHED) thread-worker.c
	$(CC) $(CFLAGS) -D$(LOG_LEVEL) -D$(SCHED) queue.c
	$(CC) $(CFLAGS) -D$(LOG_LEVEL) -D$(SCHED) mutex_types.c
else
	echo "no such scheduling algorithm"
endif

clean:
	rm -rf testfile *.o *.a
