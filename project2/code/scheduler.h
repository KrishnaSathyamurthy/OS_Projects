#include "mutex_types.h"

typedef struct q_node_t {
  tcb *thread_block;
  struct q_node_t *next;
} q_node_t;

typedef struct sched_queue_t {
  q_node_t *head;
  q_node_t *tail;
} sched_queue_t;

typedef struct scheduler {
  d_list_t *thread_blocks;

  // this can be used to ensure the thread_blocks are a thread safe operation as
  // well...
  // int scheduler_lock;

  // TODO : Check how you can concat multiple queues to a single queue
  // struct sched_queue_t **multi_queue;
} scheduler;

/*
 * exits process if sufficient memory is not present to allocate for its
 * scheduler or its queues
 */
int init_scheduler();
static void schedule();
static void swap_thread(struct sched_queue_t *queue);
static void sched_rr();
static void sched_mlfq();

void run_thread(void(*func(void *)), void *arg);
void swap_thread();
void delete_thread();

/* SCHEDULER QUEUE */
void queue_t_enqueue(struct tcb *t_block, struct sched_queue_t *queue);
tcb *queue_t_dequeue(struct sched_queue_t *queue);

/* SCHEDULER FUNCTIONS */
static void sched_rr();
static void sched_mlfq();
static void schedule();
void timer_sig_handler(int signum);
