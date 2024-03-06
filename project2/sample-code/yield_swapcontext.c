#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#define STACK_SIZE SIGSTKSZ

ucontext_t scheduler_ctx, main_ctx;
typedef struct tcb {
  ucontext_t t_ctx;
  int status;
} tcb;
struct tcb threads[3];
static int context = 0;
static int t_count = 0;
static int main_init = 0, sched_init = 0;

void loop(int loops) {
  for (int itr = 0; itr < loops; itr++) {
    for (int i = 0; i < 100; i++) {
      for (int j = 0; j < 2000000; j++) {
      }
    }
  }
}

void *f1() {
  puts("Thread 1 prints 1\n");
  loop(1);
  puts("Thread 1 prints 2\n");
  loop(10);
}

void *f2() {
  puts("Thread 2 prints 3\n");
  loop(1);
  puts("Thread 2 prints 4\n");
  loop(1);
  puts("Thread 2 returning context to main\n");
}

void *runner(void(*func())) {
  threads[context].status = 0;
  printf("INSIDE RUNNER %d\n", context);
  func();
  printf("EXITING RUNNER %d\n", context);
  threads[context].status = 0;
  t_count--;
  swapcontext(&threads[context].t_ctx, &scheduler_ctx);
}

void *scheduler() {
  while (1) {
    //  printf("BEFORE   INSIDE SCHEDULER %d::%d\n", context, t_count);
    context = (context + 1) % t_count;
    // printf("INSIDE SCHEDULER %d::%d\n", context, t_count);
    // setcontext(&threads[context].t_ctx);
    swapcontext(&scheduler_ctx, &threads[context].t_ctx);
  }
}

void ring(int signum) {
  if (t_count > 1) {
    // printf("PLEASE WORK DAWWW %d::%d\n", t_count, context);
    swapcontext(&threads[context].t_ctx, &scheduler_ctx);
  }
}

void init_ctx(ucontext_t *init_ctx, void *func, int argc, void *arg) {
  getcontext(init_ctx);

  void *init_stack = malloc(STACK_SIZE);

  if (init_stack == NULL) {
    perror("Failed to allocate stack");
    exit(1);
  }

  init_ctx->uc_link = NULL;
  init_ctx->uc_stack.ss_sp = init_stack;
  init_ctx->uc_stack.ss_size = STACK_SIZE;
  init_ctx->uc_stack.ss_flags = 0;

  if (argc) {
    printf("init_ctx for func %p \n", func);
    makecontext(init_ctx, func, argc, arg);
  } else {
    makecontext(init_ctx, func, 0);
  }
}

void init() {
  if (sched_init == 0) {
    init_ctx(&scheduler_ctx, (void *)&scheduler, 0, NULL);
    sched_init = 1;
    getcontext(&main_ctx);
    printf("SCHEDULING...\n ");
    if (main_init == 0) {
      threads[0].t_ctx = main_ctx;
      main_init = 1;
      t_count++;
      swapcontext(&main_ctx, &scheduler_ctx);
      printf("SCHEDULED ALMOST OVER\n");
    }
    printf("SCHEDULED FINISHED\n");
  }
}

void make_thread(int itr, void *func) {
  init();
  init_ctx(&threads[itr].t_ctx, (void *)&runner, 1, func);
  t_count++;
}

void check_1() {
  printf("sample1\n");
  loop(2);
  printf("sample2\n");
  loop(2);
}

int main(int argc, char **argv) {
  ucontext_t cctx, nctx;

  // Use sigaction to register signal handler
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &ring;
  sigaction(SIGPROF, &sa, NULL);

  // Create timer struct
  struct itimerval timer;

  // Set up what the timer should reset to after the timer goes off
  timer.it_interval.tv_usec = 100;
  timer.it_interval.tv_sec = 0;

  // Set up the current timer to go off in 1 second
  // Note: if both of the following values are zero
  //       the timer will not be active, and the timer
  //       will never go off even if you set the interval value
  timer.it_value.tv_usec = 100;
  timer.it_value.tv_sec = 0;

  // Set the timer up (start the timer)
  setitimer(ITIMER_PROF, &timer, NULL);

  if (argc != 1) {
    printf(": USAGE Program Name and no Arguments expected\n");
    exit(1);
  }

  puts("allocate stack, attach func");

  // Make the context to start running at f1() and f2()
  make_thread(1, (void *)&f1);
  make_thread(2, (void *)&f2);
  puts("Successfully modified context\n");
  check_1();

  /* swap context will activate cctx and store location after swapcontext in
   * nctx */
  puts("**************");
  puts("\nmain thread swapping to thread 1\n");

  /* PC value in nctx will point to here */
  puts("swap context back to main executed correctly\n");
  puts("**************");

  return 0;
}
