#include "scheduler.h"
#include <stdlib.h>

void queue_t_enqueue(tcb *t_block, struct sched_queue_t *queue) {
  struct q_node_t *new_node = malloc(sizeof(q_node_t));
  new_node->thread_block = t_block;
  new_node->next = queue->head; // circular queue

  if (queue->tail != NULL) {
    queue->tail->next = new_node;
    queue->tail = new_node;
  } else {
    queue->head = new_node;
    queue->tail = new_node;
  }
}

tcb *queue_t_dequeue(struct sched_queue_t *queue) {
  q_node_t *curr_node = queue->head;
  queue->head = queue->head->next;
  queue->tail->next = queue->head;

  if (queue->head == NULL) {
    queue->tail = NULL;
  }
  curr_node->next = NULL;
  tcb *t_block = curr_node->thread_block;
  free(curr_node);
  return t_block;
}
