// File:	worker_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef MTX_TYPES_H
#define MTX_TYPES_H

#include "thread_worker_types.h"

typedef struct spinlock_t {

} spinlock_t;

/* mutex struct definition */
typedef struct worker_mutex_t {
  tcb wait_list[MAX_THREAD_COUNT];

} worker_mutex_t;

#endif
