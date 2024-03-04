CC = gcc
CFLAGS = -g -c
AR = ar -rc
RANLIB = ranlib

all: clean thread-worker.a

thread-worker.a: thread-worker.o
	$(AR) libthread-worker.a thread-worker.o
	$(RANLIB) libthread-worker.a

thread-worker.o: logger.h scheduler.h thread-worker.h thread_worker_types.h mutex_types.h

ifeq ($(SCHED), RR)
	$(CC) $(CFLAGS) -DDEBUG_TL -DRR thread-worker.c
else ifeq ($(SCHED), MLFQ)
	$(CC) $(CFLAGS) -DDEBUG_TL -DMLFQ thread-worker.c
else
	echo "no such scheduling algorithm"
endif

clean:
	rm -rf testfile *.o *.a
