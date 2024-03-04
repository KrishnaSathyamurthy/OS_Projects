#include "mutex_types.h"

#define MAX_THREAD_COUNT 10

typedef enum scheduler_type { FIFO_ST = 0 } scheduler_type;

typedef struct scheduler {
  tcb tcb_list[MAX_THREAD_COUNT];
  scheduler_type s_type;
} scheduler;

void init_scheduler();

void run_thread(void(*func(void *)), void *arg);
void delete_thread(tcb *thread_block);
