#include "mutex_types.h"

typedef struct q_node_t {
  tcb *thread_block;
  struct q_node_t *next;
} q_node_t;

// circular queue
typedef struct sched_queue_t {
  q_node_t *head;
  q_node_t *tail;
} sched_queue_t;

typedef struct scheduler {
  d_list_t *thread_blocks;

  // should we include another lock for scheduler as well??
  // int scheduler_lock;

  // TODO : Check how you can concat multiple queues to a single queue
  // struct sched_queue_t **multi_queue;
} scheduler;

/*
 * exits process if sufficient memory is not present to allocate for its
 * scheduler or its queues
 */
int init_scheduler();

// this will run the user function in a separate thread to keep track of our
// threads even if worker_exit is not called
void run_thread(void(*func(void *)), void *arg);

/* SCHEDULER QUEUE */
void queue_t_enqueue(struct tcb *t_block, struct sched_queue_t *queue);
tcb *queue_t_dequeue(struct sched_queue_t *queue);

/* SCHEDULER FUNCTIONS */
void timer_sig_handler(int signum);
static void swap_thread(struct sched_queue_t **queue);
static void schedule();
#ifdef RR
static void sched_rr();
#else
static void sched_mlfq();
void mlfq_all_threads_urgent();
#endif
