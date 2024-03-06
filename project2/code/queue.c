#include "scheduler.h"
#include <stdlib.h>

void queue_t_enqueue(tcb *t_block, struct sched_queue_t *queue) {
  struct q_node_t *new_node = malloc(sizeof(q_node_t));
  new_node->thread_block = t_block;

  if (queue->tail) {
    queue->tail->next = new_node;
    queue->tail = new_node;
  } else {
    queue->head = new_node;
    queue->tail = new_node;
  }
  new_node->next = queue->head;
}

tcb *queue_t_dequeue(struct sched_queue_t *queue) {
  q_node_t *curr_node = queue->head;
  tcb *t_block = curr_node->thread_block;
  queue->head = curr_node->next;
  queue->tail->next = queue->head;
  curr_node->next = NULL;

  if (queue->head == curr_node) {
    queue->head = NULL;
    queue->tail = NULL;
  }
  free(curr_node);
  return t_block;
}
