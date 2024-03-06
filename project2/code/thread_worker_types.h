#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#ifndef TW_TYPES_H
#define TW_TYPES_H

#include "logger.h"
#include <ucontext.h>

#ifdef __x86_64__
typedef unsigned long worker_t;
#else
typedef unsigned int worker_t;
#endif

#define MAX_THREAD_COUNT 200
#define YIELD_LIMIT 100

// for now lets treat there are only two status? waiting being nothing is there
// types?
typedef enum status_t {
  WAITING_T = 1,
  READY_T = 2,
  RUNNING_T = 4,
  TERMINATING_T = 8,
  SCHEDULED_T = 16
} status_t;

typedef enum priority_t {
  LOW_PRIORITY_T = 1,
  MEDIUM_PRIORITY_T = 2,
  HIGH_PRIORITY_T = 4,
  URGENT_PRIORITY_T = 8
} priority_t;

typedef struct tcb {
  worker_t t_id; // node pointer
  status_t status;
  priority_t priority;
  ucontext_t context;
  int is_yield;
  int yield_cnt;
  void *stack;
  void *ret_val;
} tcb;

#endif
