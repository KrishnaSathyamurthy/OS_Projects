// File:	thread-worker.h
// List all group member's name:
// username of iLab:
// iLab Server:
#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

#include "scheduler.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

typedef enum worker_ret_status {
  SUCCESS_WCS = -1,
  FAILED_WCS = 0,
  LIMIT_REACHED_WCS = 1,
  MALLOC_FAILURE_WCS = 2,
  NO_THREADS_CREATED_WCS = 3
} worker_status;

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
                      const pthread_mutexattr_t *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

#endif
