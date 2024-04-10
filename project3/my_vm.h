#include <stddef.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

//Considering the address space of 32 bits, hence virtual memory size is 4GB
// Page size considered - 8KB

typedef uint32_t page_t;

// Max size of the virtual memory (4GB)
#define MAX_MEMSIZE (1ULL<<32)

// Physical memory size to simulate (1GB)
#define MEMSIZE (1UL<<30)

// Page size considered - (8KB)
#define PAGE_SIZE (1UL<<13) // 2^13 Bytes

#define ADDRESS_BIT 32

#define BYTES_TO_BITS 8

#define FRAME_SIZE PAGE_SIZE

#define PAGE_MEM_SIZE (PAGE_SIZE/sizeof(page_t)) // 2^11 entries

int inner_level_bits = 0;
int outer_level_bits = 0;
int offset_bits = 0;
int virtual_page_bits = 0;

void initialize_vm();

bitmap* bitmap_init(size_t total_pages);

static void set_bit_at_index(char *bit_map, int bit_index);

static int get_bit_at_index(char *bit_map, int bit_index);

void init_page_directories();

void assign_virtual_page_bits();

void set_physical_mem();

virtual_page_data* get_virtual_data(page_t vp);

void * translate(page_t vp);

page_t page_map(page_t vp, page_t pf);

void * t_malloc(size_t n);

int t_free(page_t vp, size_t n);

int put_value(page_t vp, void *val, size_t n);

int get_value(page_t vp, void *dst, size_t n);

void mat_mult(page_t a, page_t b, page_t c, size_t l, size_t m, size_t n);

void add_TLB(page_t vpage, page_t ppage);

int check_TLB(page_t vpage);

void print_TLB_missrate();
