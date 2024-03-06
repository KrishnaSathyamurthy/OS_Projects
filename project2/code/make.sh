make -f Makefile SCHED=RR
# make -f Makefile SCHED=MLFQ
cd benchmarks
make -f Makefile
./one_thread > one_thread.out
./multiple_threads > multiple_threads.out
./multiple_threads_yield > multiple_threads_yield.out
./multiple_threads_mutex > multiple_threads_mutex.out
./multiple_threads_different_workload > multiple_threads_different_workload.out
./multiple_threads_with_return > multiple_threads_with_return.out
rm -rf one_thread multiple_threads multiple_threads_yield multiple_threads_mutex multiple_threads_different_workload multiple_threads_with_return
cd ..
