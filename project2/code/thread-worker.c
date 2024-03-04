// File:	thread-worker.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "thread-worker.h"

#define STACK_SIZE 16 * 1024
#define QUANTUM 10 * 1000 // 10 ms

int init_scheduler_done = 0;
int is_child_thread = 0;
scheduler t_scheduler;
int thread_count = 0;
int current_thread = 0;
int thread_yielded = 0;

ucontext_t main_context, *main_context_p = &main_context;


/* SCHEDULER VARIABLES */


struct sigaction sa;
struct itimerval timer;

typedef struct node{
	worker_t thread;
	struct node *next;
} node;


typedef struct queue{
  node *head;
  node *tail;
} queue;

struct queue *round_robin;

struct queue *mlfq_level_3;
struct queue *mlfq_level_2;
struct queue *mlfq_level_1;

ucontext_t scheduler_context;



int initialize() {
  if (init_scheduler_done) {
    return -1;
  }

  worker_t tid;


  // init all threads as waiting thread with medium priority
  for (int tid = 0; tid < MAX_THREAD_COUNT; ++tid) {
    t_scheduler.tcb_list[tid].status = WAITING_T;
    t_scheduler.tcb_list[tid].priority = URGENT_PRIORITY_T;
    t_scheduler.tcb_list[tid].ret_val = NULL; // Changed from current_thread to i
  }

  // Cleanup required

  if ((round_robin = (queue *)malloc(sizeof(queue))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    return MALLOC_FAILURE_WCS;
  }

  if ((mlfq_level_3 = (queue *)malloc(sizeof(queue))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    return MALLOC_FAILURE_WCS;
  }
  if ((mlfq_level_2 = (queue *)malloc(sizeof(queue))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    return MALLOC_FAILURE_WCS;
  }
  if ((mlfq_level_1 = (queue *)malloc(sizeof(queue))) == NULL) {
    DEBUG_OUT("Error while allocating memory ");
    return MALLOC_FAILURE_WCS;
  }

  // Configuring signal handler for the scheduler
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &timer_sig_handler;
	sigaction(SIGPROF, &sa, NULL);

  // Setting up the scheduler context
	if (getcontext(&scheduler_context) < 0) {
		perror("getcontext fails for scheduler");
		exit(1);
	}

	void *stack = malloc(STACK_SIZE);
	scheduler_context.uc_link = NULL;
	scheduler_context.uc_stack.ss_sp = stack;
	scheduler_context.uc_stack.ss_size = STACK_SIZE;
	scheduler_context.uc_stack.ss_flags = 0;

	makecontext(&scheduler_context, schedule, 0);

  return -1;
}

void run_thread(void(*func(void *)), void *arg) {
  tcb *thread_block = &t_scheduler.tcb_list[current_thread];
  thread_block->status = RUNNING_T;
  DEBUG_OUT_ARG("Executing thread", current_thread);
  func(arg);
  thread_block->status = TERMINATING_T;
  worker_yield();
}

void swap_thread() {
  tcb *thread_block = &t_scheduler.tcb_list[current_thread];

  if (thread_block->status & WAITING_T) {
    return;
  }
  DEBUG_OUT_ARG("Switching from main thread to user thread", current_thread);
  is_child_thread = 1;
  swapcontext(main_context_p, &thread_block->context);
  is_child_thread = 0;
}

void delete_thread() {
  if (t_scheduler.tcb_list[current_thread].status & ~TERMINATING_T) {
    return;
  }
  tcb *thread_block = &t_scheduler.tcb_list[current_thread];
  DEBUG_OUT_ARG("Terminating user thread", thread_block->t_id);
  free(thread_block->stack);
  thread_block->status = WAITING_T;
  --thread_count;
}

int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg) {
  
  int ret;

  if ((ret = initialize()) > 0) {
    return ret;
  }

  if (thread_count >= MAX_THREAD_COUNT) {
    return LIMIT_REACHED_WCS;
  }
  tcb *thread_block = &t_scheduler.tcb_list[thread_count];

  if (getcontext(&thread_block->context)) {
    DEBUG_OUT("Error occurred while getting context");
    return FAILED_WCS;
  }

  if ((thread_block->stack = malloc(STACK_SIZE)) == NULL) {
    DEBUG_OUT("Error occurred while getting context");
    return MALLOC_FAILURE_WCS;
  }
  thread_block->t_id = thread_count;
  thread_block->context.uc_link = NULL;
  thread_block->context.uc_stack.ss_sp = thread_block->stack;
  thread_block->context.uc_stack.ss_size = STACK_SIZE;
  thread_block->context.uc_stack.ss_flags = 0;
  thread_block->status = READY_T;

  // create our thread and start running it in a different context
  makecontext(&thread_block->context, (void *)&run_thread, 2, function, arg);
  *thread = thread_count;
  ++thread_count;
  return SUCCESS_WCS;
}

int worker_yield() {
  if (thread_count == 0) {
    DEBUG_OUT_ARG("Invoking worker thread when no worker thread were created",
                  current_thread);
    return NO_THREADS_CREATED_WCS;
  }

  thread_yielded = 1;

  if (is_child_thread) {
    DEBUG_OUT_ARG("Switching to main thread from user thread", current_thread);
    t_scheduler.tcb_list[current_thread].status = READY_T;
    swapcontext(&t_scheduler.tcb_list[current_thread].context, main_context_p);
  } else {
    current_thread = (current_thread + 1) % thread_count;
    swap_thread();
    delete_thread();
  }
  return 0;
};

void worker_exit(void *value_ptr) {
  t_scheduler.tcb_list[current_thread].ret_val = value_ptr;
  t_scheduler.tcb_list[current_thread].status = TERMINATING_T;
  worker_yield();
}

int worker_join(worker_t thread, void **value_ptr) {
  DEBUG_OUT_ARG("Join worker thread", thread);
  current_thread = thread;
  swap_thread();

  if (value_ptr) {
    *value_ptr = t_scheduler.tcb_list[current_thread].ret_val;
  }
  delete_thread();
  return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
                      const pthread_mutexattr_t *mutexattr) {
  //- initialize data structures for this mutex
  return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

  // - use the built-in test-and-set atomic function to test the mutex
  // - if the mutex is acquired successfully, enter the critical section
  // - if acquiring mutex fails, push current thread into block list and
  // context switch to the scheduler thread
  return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
  // - release mutex and make it available again.
  // - put one or more threads in block list to run queue
  // so that they could compete for mutex later.

  return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
  // - make sure mutex is not being used
  // - de-allocate dynamic memory created in worker_mutex_init

  return 0;
};

/* scheduler */
static void schedule() {
// - every time a timer interrupt occurs, your worker thread library
// should be contexted switched from a thread context to this
// schedule() function

// 

// - invoke scheduling algorithms according to the policy (RR or MLFQ)

// - schedule policy
#ifndef MLFQ
  // Choose RR
  sched_rr(round_robin);
#else
  // Choose MLFQ
  sched_mlfq();
#endif
}

static void sched_rr(queue * queue) {
	/// - your own implementation of RR
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	#ifndef MLFQ
  /*
    if the current scheduler is RR, The current_thread has to be fetched 
    from the round_robin queue and begin execution.
  */

		if (current_thread != NULL)
		{
      tcb *thread_block = &t_scheduler.tcb_list[current_thread];
			thread_block-> status = READY_T;
			queue_enqueue(current_thread, queue);
		}

	#endif

	// Do scheduling if there is a job present in the queue.
  if (queue->tail !=NULL) {

		// dequeue head from the queue which is to be scheduled
    current_thread = queue_dequeue(queue);

		// mark the current_thread tcb as scheduled
    tcb *thread_block = &t_scheduler.tcb_list[current_thread];
		thread_block->status = SCHEDULED_T;

		//Configure the timer to expire after the quantum time slice
		timer.it_value.tv_usec = QUANTUM;
		timer.it_value.tv_sec = 0;	
		setitimer(ITIMER_PROF, &timer, NULL);

		setcontext(&thread_block->context);
	}
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
  // - your own implementation of MLFQ
  // (feel free to modify arguments and return types)

  if(current_thread != NULL ){

    tcb *thread_block = &t_scheduler.tcb_list[current_thread];
		int priority = thread_block->priority;

		thread_block->status = READY_T;

		if (thread_yielded == 1)
		{
			//If the current thread yielded retain in the same priority queue
      switch(priority) {
        case URGENT_PRIORITY_T:
          queue_enqueue(current_thread, round_robin);
          break;
        case HIGH_PRIORITY_T:
          queue_enqueue(current_thread, mlfq_level_3);
          break;
        case MEDIUM_PRIORITY_T:
          queue_enqueue(current_thread, mlfq_level_2);
          break;
        case LOW_PRIORITY_T:
          queue_enqueue(current_thread, mlfq_level_1);
          break;
        default:
          queue_enqueue(current_thread, round_robin);
      }

			thread_yielded = 0;

		}
		else
		{		
			
      // Else, move the current_thread to the next low priority queue
      switch(priority)
      {
        case URGENT_PRIORITY_T:
          thread_block->priority = HIGH_PRIORITY_T;
          queue_enqueue(current_thread, mlfq_level_3);
          break;
        case HIGH_PRIORITY_T:
          thread_block->priority = MEDIUM_PRIORITY_T;
          queue_enqueue(current_thread, mlfq_level_2);
          break;
        case MEDIUM_PRIORITY_T:
          thread_block->priority = LOW_PRIORITY_T;
          queue_enqueue(current_thread, mlfq_level_1);
          break;
        case LOW_PRIORITY_T:
          queue_enqueue(current_thread, mlfq_level_1);
          break;
        default:
          thread_block->priority = HIGH_PRIORITY_T;
          queue_enqueue(current_thread, mlfq_level_3);
      }
			
		}
	}
	
	/*
    1. Among same priority threads, perform RR between each other
    2. Executes a queue only if the previous higher queue is empty.
  */
	if (round_robin->head != NULL){
		sched_rr(round_robin);
	}else if (mlfq_level_3->head != NULL){
		sched_rr(mlfq_level_3);
	}else if (mlfq_level_2->head != NULL){
		sched_rr(mlfq_level_2);
	}else if (mlfq_level_1->head != NULL){
		sched_rr(mlfq_level_1);
	}
}

// Feel free to add any other functions you need.
// You can also create separate files for helper functions, structures, etc.
// But make sure that the Makefile is updated to account for the same.


void timer_sig_handler(int signum)
{
  tcb *thread_block = &t_scheduler.tcb_list[current_thread];
	swapcontext(&thread_block->context, &scheduler_context); // ### to add scheduler_context
}

//code to enqueue a thread to the end of a particular queue
void queue_enqueue(worker_t new_thread, struct queue *queue)
{
	node *new_node = (node *) malloc(sizeof(node));
	new_node->thread = new_thread;
	new_node->next = NULL;

	if(queue->tail != NULL)
	{	
		(queue->tail)->next = new_node;
		queue->tail = new_node;
	}
	else
	{
		queue->head = new_node;
		queue->tail = new_node;
	}
}

int queue_dequeue(struct queue *queue)
{
  node *curr_node = queue->head;

  queue->head = (queue->head)-> next;
  if (queue->head == NULL) {
  	queue->tail = NULL;
  }
  curr_node->next = NULL;

  worker_t thread = curr_node->thread;

  free(curr_node);

  return (int)(thread);
}

void free_thread_memory(worker_t thread)
{
  tcb *thr_block = &t_scheduler.tcb_list[thread];
	free(thr_block->context.uc_stack.ss_sp);
	free(thr_block);
}


