// File:	worker_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef MTX_TYPES_H
#define MTX_TYPES_H

#include "thread_worker_types.h"

#define LOCKED_T 1
#define UNLOCKED_T 0

#define atomic_t int

typedef struct list_node_t {
  struct tcb *t_block;
  struct list_node_t *next;
  struct list_node_t *prev;
} list_node_t;

typedef struct d_list_t {
  struct list_node_t *head;
  // struct list_node_t *current; // is this necessary?
  struct list_node_t *tail;
  int length;
} d_list_t;

typedef struct worker_mutex_t {
  atomic_t mutex_lock;
  d_list_t *block_list;
  atomic_t list_lock;
} worker_mutex_t;

struct d_list_t *init_list(struct tcb *data);
struct list_node_t *list_add_tail(struct tcb *data, struct d_list_t **d_list);
void list_del_node(struct list_node_t *data, struct d_list_t **d_list);

#endif
