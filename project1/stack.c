/*
 * Add NetID and names of all project partners
 * 1. ks2025 - Krishna Sathyamurthy
 * 2. ab2812 - Abhinav Bharadwaj Sarathy
 * CS518
 * Compiler instruction : gcc -m32 -g stack.c -o stack.o
 *
 * Code was tested on ilab1.cs.rutgers.edu
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void signal_handle(int signal_no) {
  printf("OMG, I was slain!\n");

  /*
   * Using the address of signal_no as reference, we add the offset of 15 (60/4)
   * to access the return address location in the stack
   */
  int *return_instruction = (int *)(&signal_no + 15);

  /*
   * To get the desired output, we need to skip 2 instructions, i.e., skip 6
   * bytes from the current return address
   */
  *return_instruction += 6;
}

int main(int argc, char *argv[]) {
  int x = 5, y = 0, z = 4;

  // Catches the signal SIGFPE exception in signal_handle function
  signal(SIGFPE, signal_handle);
  z = x / y;
  printf("LOL, I live again !!!%d\n", z);
  return 0;
}