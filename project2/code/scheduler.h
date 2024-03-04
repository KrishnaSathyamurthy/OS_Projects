#include "mutex_types.h"

typedef enum scheduler_type { FIFO_ST = 0 } scheduler_type;

typedef struct scheduler {
  tcb tcb_list[MAX_THREAD_COUNT];
  scheduler_type s_type;
} scheduler;

void init_scheduler();

void run_thread(void(*func(void *)), void *arg);
void swap_thread();
void delete_thread();

/* SCHEDULER FUNCTIONS */
void timer_sig_handler(int signum);
void queue_enqueue(worker_t new_thread, queue *queue);
void free_thread_memory(worker_t thread);
static void sched_rr(queue *queue);
static void sched_mlfq();
static void schedule();
