make -f Makefile SCHED=RR
# make -f Makefile SCHED=MLFQ
cd benchmarks
#gcc -g -w -o one_thread one_thread.c -L../ -lthread-worker
#gcc -g -w -o multiple_threads multiple_threads.c -L../ -lthread-worker
gcc -g -w -o multiple_threads_mutex multiple_threads_mutex.c -L../ -lthread-worker
#./one_thread
#./multiple_threads 10
./multiple_threads_mutex
#rm -rf one_thread multiple_threads
cd ..
