#include "mutex_types.h"
#include <stdlib.h>

void _list_add(struct list_node_t *data, struct list_node_t *prev,
               struct list_node_t *next) {
  next->prev = data;
  data->next = next;
  data->prev = prev;
  prev->next = data;
}

void _init_list(struct list_node_t *node, struct d_list_t **d_list) {
  _list_add(node, node, node);
  (*d_list)->head = (*d_list)->tail = node;
}

struct d_list_t *init_list(struct tcb *data) {
  struct list_node_t *node;
  struct d_list_t *d_list;

  if ((node = malloc(sizeof(list_node_t))) == NULL) {
    DEBUG_OUT("Memory allocation for node failed");
    exit(0);
  }

  if ((d_list = malloc(sizeof(d_list_t))) == NULL) {
    DEBUG_OUT("Memory allocation for d_list failed");
    exit(0);
  }
  node->t_block = data;
  _init_list(node, &d_list);
  d_list->length = 1;
  return d_list;
}

struct list_node_t *list_add_tail(struct tcb *data, struct d_list_t **d_list) {
  struct list_node_t *node;

  if ((node = malloc(sizeof(list_node_t))) == NULL) {
    DEBUG_OUT("Memory allocation for node failed");
    exit(0);
  }
  node->t_block = data;

  if ((*d_list)->head == NULL) {
    _init_list(node, d_list);
  } else {
    _list_add(node, (*d_list)->tail, (*d_list)->head);
    (*d_list)->tail = node;
  }
  (*d_list)->length++;
  return node;
}

void list_del_node(struct list_node_t *node, struct d_list_t **d_list) {
  if ((*d_list)->length == 0) {
    DEBUG_OUT("Don't try to free a node that is not in the list?!?");
    return;
  }

  if ((*d_list)->length > 1) {
    // Remapping list and nodes
    if ((*d_list)->head == node) {
      (*d_list)->head = node->next;
      (*d_list)->head->prev = (*d_list)->tail;
    }

    if ((*d_list)->tail == node) {
      (*d_list)->tail = node->prev;
      (*d_list)->tail->next = (*d_list)->head;
    }
    node->prev->next = node->next;
    node->next->prev = node->prev;
  } else {
    (*d_list)->head = NULL;
    (*d_list)->tail = NULL;
  }

  (*d_list)->length--;
  DEBUG_OUT("Deallocating memory for thread node");
  free(node->t_block->stack);
  free(node->t_block);
  free(node);
}

void free_list(struct d_list_t **d_list) {
  while ((*d_list)->length) {
    list_del_node((*d_list)->tail, d_list);
  }
  DEBUG_OUT("Deallocating memory for d_list");
  free((*d_list));
}
