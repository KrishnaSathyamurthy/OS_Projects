// File:	thread-worker.c

// List all group member's name:
// username of iLab:
// iLab Server:
#include "thread-worker.h"
#include "mutex_types.h"
#include <string.h>

#define STACK_SIZE 16 * 1024
#define QUANTUM 10 * 1000 // 10 ms
#ifdef MLFQ
#define LONG_QUANTUM 100 // 100 s
#endif

static int is_init_scheduler = 0;

tcb *current_worker = NULL;

ucontext_t scheduler_context, *scheduler_context_p = &scheduler_context;
scheduler *t_scheduler;

struct sigaction short_signal;
struct itimerval short_timer;
struct sched_queue_t *urgent_p_queue;

#ifdef MLFQ
struct sigaction long_signal;
struct itimerval long_timer;
struct sched_queue_t *high_p_queue, *med_p_queue, *low_p_queue;
#endif

// should i maintain this?? user responsibility to remove the resource??
struct sched_queue_t *termination_queue;

int init_scheduler_queue() {
  // Cleanup required
  if ((urgent_p_queue = (sched_queue_t *)malloc(sizeof(d_list_t))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    exit(0);
  }

#ifdef MLFQ
  if ((high_p_queue = (sched_queue_t *)malloc(sizeof(d_list_t))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    exit(0);
  }

  if ((med_p_queue = (sched_queue_t *)malloc(sizeof(d_list_t))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    exit(0);
  }

  if ((low_p_queue = (sched_queue_t *)malloc(sizeof(d_list_t))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    exit(0);
  }
#endif
  return SUCCESS_WCS;
}

// can use variadic args(?) to avoid callee, arg1, arg2, ...
int create_context(ucontext_t *context, void *thread_func, int argv,
                   void *callee, void *arg) {
  if (getcontext(context) < 0) {
    DEBUG_OUT("Getcontext failed for scheduler");
    exit(FAILED_WCS);
  }

  void *stack;
  if ((stack = malloc(STACK_SIZE)) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    exit(MALLOC_FAILURE_WCS);
  }
  context->uc_link = NULL;
  context->uc_stack.ss_sp = stack;
  context->uc_stack.ss_size = STACK_SIZE;
  context->uc_stack.ss_flags = 0;
  makecontext(context, thread_func, argv, callee, arg);
  return SUCCESS_WCS;
}

int init_main_context() {
  ucontext_t main_context;
  if (getcontext(&main_context) < 0) {
    DEBUG_OUT("Getcontext failed for scheduler");
    exit(FAILED_WCS);
  }
  tcb *thread_block = malloc(sizeof(tcb));
  thread_block->context = main_context;
  thread_block->priority = URGENT_PRIORITY_T;
  thread_block->status = READY_T;

  // to add the thread to main queue during swap context
  thread_block->is_yield = 1;
  thread_block->yield_cnt = 0;
  is_init_scheduler = 1;
  t_scheduler->thread_blocks = init_list(thread_block);
  thread_block->t_id = (void *)t_scheduler->thread_blocks->tail;
  DEBUG_OUT_ARG("Created main thread", thread_block->t_id);
  current_worker = thread_block;
  swapcontext(&thread_block->context, scheduler_context_p);
  return SUCCESS_WCS;
}

int init_scheduler() {
  if (is_init_scheduler) {
    return SUCCESS_WCS;
  }
  DEBUG_OUT("STARTING SCHEDULER...");
  init_scheduler_queue();

  // configuring signal handler for the scheduler
  // additional timer for changing all threads to common queue still pending...
  memset(&short_signal, 0, sizeof(short_signal));
  short_signal.sa_handler = &timer_sig_handler;
  sigaction(SIGPROF, &short_signal, NULL);

#ifdef MLFQ
  memset(&long_signal, 0, sizeof(long_signal));
  long_signal.sa_handler = &timer_sig_handler;
  sigaction(SIGVTALRM, &long_signal, NULL);
  long_timer.it_interval.tv_usec = 0;
  long_timer.it_interval.tv_sec = LONG_QUANTUM;
  long_timer.it_value.tv_usec = 0;
  long_timer.it_value.tv_sec = LONG_QUANTUM;
  setitimer(ITIMER_VIRTUAL, &long_timer, NULL);
#endif

  if ((t_scheduler = malloc(sizeof(scheduler))) == 0) {
    DEBUG_OUT("Error while allocating scheduler memory ");
    exit(0);
  }

  // creating scheduler context
  create_context(scheduler_context_p, (void *)&schedule, 0, NULL, NULL);

  // creating and starting main thread context
  init_main_context();
  DEBUG_OUT("SCHEDULER STARTED SUCCESSFULLY");
  return SUCCESS_WCS;
}

void run_thread(void(*func(void *)), void *arg) {
  DEBUG_OUT_ARG("Executing thread...", current_worker->t_id);
  current_worker->status = RUNNING_T;
  func(arg);
  current_worker->status = TERMINATING_T;
  DEBUG_OUT_ARG("Terminating thread...", current_worker->t_id);
  setcontext(scheduler_context_p);
}

int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg) {
  init_scheduler();

  if (t_scheduler->thread_blocks->length >= MAX_THREAD_COUNT) {
    return LIMIT_REACHED_WCS;
  }
  tcb *thread_block = malloc(sizeof(tcb));
  int argv = arg == NULL ? 1 : 2;
  create_context(&thread_block->context, &run_thread, argv, function, arg);
  thread_block->priority = URGENT_PRIORITY_T;
  thread_block->stack = thread_block->context.uc_stack.ss_sp;
  thread_block->status = READY_T;
  struct list_node_t *node =
      list_add_tail(thread_block, &t_scheduler->thread_blocks);
  thread_block->is_yield = thread_block->yield_cnt = 0;
  thread_block->t_id = (void *)node;
  (*thread) = thread_block->t_id;
  queue_t_enqueue(thread_block, urgent_p_queue);
  DEBUG_OUT_ARG("Created user thread", node->t_block->t_id);
  return SUCCESS_WCS;
}

int worker_yield() {
  if (!is_init_scheduler) {
    DEBUG_OUT("Invoking yield when scheduler was not inited");
    return NO_THREADS_CREATED_WCS;
  }
  current_worker->is_yield = 1;
  current_worker->yield_cnt++;
  swapcontext(&current_worker->context, scheduler_context_p);
  return SUCCESS_WCS;
};

void worker_exit(void *value_ptr) {
  current_worker->ret_val = value_ptr;
  current_worker->status = TERMINATING_T;
  setcontext(scheduler_context_p);
}

int worker_join(worker_t thread, void **value_ptr) {
  DEBUG_OUT_ARG("Join worker thread", thread);
  struct list_node_t *node = (void *)thread;

  while (node->t_block->status & ~TERMINATING_T)
    ;

  if (value_ptr) {
    (*value_ptr) = node->t_block->ret_val;
  }
  DEBUG_OUT_ARG("Terminating user thread", node->t_block->t_id);
  free(node->t_block->stack);
  free(node->t_block);
  list_del_node(node, &t_scheduler->thread_blocks);
  return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
                      const pthread_mutexattr_t *mutexattr) {
  __sync_lock_release(&mutex->mutex_lock);
  __sync_lock_release(&mutex->list_lock);

  if ((mutex->block_list = malloc(sizeof(d_list_t))) == NULL) {
    DEBUG_OUT("Memory allocation for block list failed");
    exit(0);
  }
  mutex->block_list->head = mutex->block_list->tail = NULL;
  mutex->block_list->length = 0;
  return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {
  while (1) {
    if (__atomic_test_and_set(&mutex->mutex_lock, LOCKED_T) == 0) {
      DEBUG_OUT("Mutex lock has been acquired");
      return 0;
    }

    while (__atomic_test_and_set(&mutex->list_lock, LOCKED_T) == 1) {
    };
    current_worker->status = WAITING_T; // Adding worker to block list
    list_add_tail(current_worker, &mutex->block_list);
    __sync_lock_release(&mutex->list_lock);
    swapcontext(&current_worker->context, scheduler_context_p);
  }
};

void enqueue_blocked_nodes(list_node_t *node) {
  node->t_block->status = READY_T;
#ifdef MLFQ
  switch (node->t_block->priority) {
  case URGENT_PRIORITY_T:
    queue_t_enqueue(node->t_block, urgent_p_queue);
    break;
  case HIGH_PRIORITY_T:
    queue_t_enqueue(node->t_block, high_p_queue);
    break;
  case MEDIUM_PRIORITY_T:
    queue_t_enqueue(node->t_block, med_p_queue);
    break;
  case LOW_PRIORITY_T:
    queue_t_enqueue(node->t_block, low_p_queue);
    break;
  default:
    queue_t_enqueue(node->t_block, urgent_p_queue);
  }
#else
  // Added only to urgent queue by default for RR
  queue_t_enqueue(node->t_block, urgent_p_queue);
#endif
}

int worker_mutex_unlock(worker_mutex_t *mutex) {
  while (__atomic_test_and_set(&mutex->list_lock, LOCKED_T) == 1) {
  };
  DEBUG_OUT("Mutex unlock invoked");
  while (mutex->block_list->length) {
    list_node_t *node = mutex->block_list->head;
    enqueue_blocked_nodes(node);
    list_del_node(node, &mutex->block_list);
  }
  __sync_lock_release(&mutex->list_lock);
  __sync_lock_release(&mutex->mutex_lock);
  return 0;
};

int worker_mutex_destroy(worker_mutex_t *mutex) {
  DEBUG_OUT("Memory deallocated for block list failed");

  // assumes unlock has been called??
  free(mutex->block_list);
  __sync_lock_release(&mutex->list_lock);
  __sync_lock_release(&mutex->mutex_lock);
  return 0;
};

#ifdef MLFQ
void move_threads_urgent(sched_queue_t *queue) {
  if (queue->head) {
    while (queue->head) {
      tcb *thread_block = queue_t_dequeue(queue);
      thread_block->priority = URGENT_PRIORITY_T;
      queue_t_enqueue(thread_block, urgent_p_queue);
    }
  }
}

void mlfq_all_threads_urgent() {
  // timer stopped temporarily till the threads are moved to urgent queue
  short_timer.it_value.tv_usec = 0;
  short_timer.it_value.tv_sec = 0;
  setitimer(ITIMER_PROF, &short_timer, NULL);
  move_threads_urgent(high_p_queue);
  move_threads_urgent(med_p_queue);
  move_threads_urgent(low_p_queue);

  // Timer configured to continue switching...
  swapcontext(&current_worker->context, scheduler_context_p);
}
#endif

void timer_sig_handler(int signum) {
#ifdef RR
  swapcontext(&current_worker->context, scheduler_context_p);
#else
  if (signum == SIGPROF) {
    swapcontext(&current_worker->context, scheduler_context_p);
  } else if (signum == SIGVTALRM) {
    DEBUG_OUT("MLFQ long alarm called, making all threads urgent");
    mlfq_all_threads_urgent();
  }
#endif
}

/* scheduler */
static void schedule() {
#ifndef MLFQ
  sched_rr();
#else
  sched_mlfq();
#endif
}

static void swap_threads(struct sched_queue_t **queue) {
  if ((*queue)->head) {
    // dequeue the head and schedule the thread
    current_worker = queue_t_dequeue(*queue);
    current_worker->status = RUNNING_T;

    // Configure the timer to expire after the quantum time slice
    short_timer.it_value.tv_usec = QUANTUM;
    short_timer.it_value.tv_sec = 0;
    setitimer(ITIMER_PROF, &short_timer, NULL);

    // swap to the thread
    DEBUG_OUT_ARG("Swapping threads...", current_worker->t_id);
    setcontext(&current_worker->context);
  }
}

// Preemptive RR scheduling algorithm
static void sched_rr() {
  if (current_worker->status & (READY_T | RUNNING_T)) {
    queue_t_enqueue(current_worker, urgent_p_queue);
  }
  swap_threads(&urgent_p_queue);
}

#ifdef MLFQ
void mlfq_schedule() {
  /*
   * 1. Among same priority threads, perform RR between each other
   * 2. Executes a queue only if the previous higher queue is empty.
   */
  if (urgent_p_queue->head) {
    swap_threads(&urgent_p_queue);
  } else if (high_p_queue->head) {
    swap_threads(&high_p_queue);
  } else if (med_p_queue->head) {
    swap_threads(&med_p_queue);
  } else if (low_p_queue->head) {
    swap_threads(&low_p_queue);
  }
}

// Preemptive MLFQ scheduling algorithm
static void sched_mlfq() {
  if (current_worker->status & (WAITING_T | TERMINATING_T)) {
    mlfq_schedule();
    return;
  }

  priority_t priority = current_worker->priority;
  int is_yield_thread = 0;
  if (current_worker->is_yield == 1) {
    is_yield_thread = 1;
    if (current_worker->yield_cnt > YIELD_LIMIT) {
      current_worker->yield_cnt = 0;
      is_yield_thread = 0;
    }
  }
  current_worker->is_yield = 0;

  if (is_yield_thread) {

    // If the current thread yielded retain in the same priority queue
    if (priority & HIGH_PRIORITY_T) {
      queue_t_enqueue(current_worker, high_p_queue);
    } else if (priority & MEDIUM_PRIORITY_T) {
      queue_t_enqueue(current_worker, med_p_queue);
    } else if (priority & LOW_PRIORITY_T) {
      queue_t_enqueue(current_worker, low_p_queue);
    } else {
      queue_t_enqueue(current_worker, urgent_p_queue);
    }
  } else {
    if (priority & HIGH_PRIORITY_T) {
      current_worker->priority = MEDIUM_PRIORITY_T;
      queue_t_enqueue(current_worker, med_p_queue);
    } else if (priority & (MEDIUM_PRIORITY_T | LOW_PRIORITY_T)) {
      current_worker->priority = LOW_PRIORITY_T;
      queue_t_enqueue(current_worker, low_p_queue);
    } else {
      current_worker->priority = HIGH_PRIORITY_T;
      queue_t_enqueue(current_worker, high_p_queue);
    }
  }
  mlfq_schedule();
}
#endif
