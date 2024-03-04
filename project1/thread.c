/*
 * Add NetID and names of all project partners
 * 1. ab2812 - Abhinav Bharadwaj Sarathy
 * 2. ks2025 - Krishna Sathyamurthy
 * CS518
 * Compiler Options: gcc threads.c -o threads -lpthread
 * Code was tested on ilab1.cs.rutgers.edu
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define NUMBER_OF_THREADS 4
pthread_t threads[NUMBER_OF_THREADS];
pthread_mutex_t mutex;
int x = 0;
int loop = 10000;

/* Counter Incrementer function:
 * This is the function that each thread will run which
 * increments the shared counter x by LOOP times.
 *
 * Your job is to implement the incrementing of x
 * such that update to the counter is sychronized across threads.
 *
 */
void *add_counter(void *arg) {

    int i;

    /* Add thread synchronizaiton logic in this function */	
    pthread_mutex_lock(&mutex); 

    printf("Incrementing for thread %u ....\n", pthread_self());

    for(i = 0; i < loop; i++){
        x = x + 1;
    }

    printf("Done incrementing for thread %u ... \n", pthread_self());

    pthread_mutex_unlock(&mutex);
}

/* Main function:
 * This is the main function that will run.
 *
 * Your job is to create four threads and have them
 * run the add_counter function().
 */
int main(int argc, char *argv[]) {

    if(argc != 2){
        printf("Bad Usage: Must pass in a integer\n");
        exit(1);
    }

    loop = atoi(argv[1]);

    printf("Going to run four threads to increment x up to %d\n", 4 * loop);

    /* Implement Code Here */

    int i, res;

    /* Initializing the mutex */
    if (pthread_mutex_init(&mutex, NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        return 1; 
    } 

    /* The following logic creates four threads within the process 
     * pthread_create call has four arguments
     * thread - pointer to an unsigned integer value that returns the thread id of the thread created.
     * attr - pointer to structure which define thread attributes. Set to NULL for default.
     * start_routine - pointer to a subroutine that is executed by the thread.
     * arg - pointer to void that contains the arguments to the function defined in the earlier argument.
     */
    for(i = 0; i < NUMBER_OF_THREADS; i++) {
        res = pthread_create(&threads[i], NULL, add_counter, NULL);
        if(res) {
            printf("Error:Unable to create Thread-%u, Error code: %d \n", threads[i], res);
            exit(-1);
        }
        printf("Created thread id=%u\n", threads[i]);
    }


    /* Make sure to join the threads */
    for(i = 0; i < NUMBER_OF_THREADS; i++) {
        res = pthread_join(threads[i], NULL);
        if(res) {
            printf("Error:Unable to join Thread-%u, Error code: %d \n", threads[i], res);
            exit(-1);
        }
        printf("Joined thread id =%u\n", threads[i]);
    }

    printf("The final value of x is %d\n", x);

    return 0;
}
