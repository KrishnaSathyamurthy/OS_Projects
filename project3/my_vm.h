// List all group member's name: Abhinav Bharadwaj Sarathy, Krishna Sathyamurthy
// username of iLab: ab2812, ks2025
// iLab Server: kill.cs.rutgers.edu

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Considering the address space of 32 bits, hence virtual memory size is 4GB
//  Page size considered - 8KB

typedef uint32_t page_t;

// Max size of the virtual memory (4GB)
#define MAX_MEMSIZE (1ULL << 32)

// Physical memory size to simulate (1GB)
#define MEMSIZE (1UL << 30)

// Page size considered - (8KB)
#define PAGE_SIZE (1UL << 13) // 2^13 Bytes

#define ADDRESS_BIT 32

#define BYTES_TO_BITS 8

#define FRAME_SIZE PAGE_SIZE

#define PAGE_MEM_SIZE (PAGE_SIZE / sizeof(page_t)) // 2^11 entries

#define TLB_ENTRIES 256

// Structure to represent each page(aka frame) in memory
typedef struct {
  page_t page_array[PAGE_MEM_SIZE];
} page;

// Structure for page bitmap
// Each char in bits will have 1byte(storing 8 pages bitwise)
typedef struct {
  unsigned char *bits;
  size_t num_bytes;
} bitmap;

// VM manager struct that is responsible for holding
// all the necessary data for the system to function
// Page directory is where outer table entries are stored
typedef struct {
  page *physical_memory;
  bitmap *physical_bitmap;
  bitmap *virtual_bitmap;
  page *page_directory;
} vm_manager;

// For storing data related to virtual space for a process
typedef struct {
  page_t outer_index;
  page_t inner_index;
  page_t offset;
  page_t virtual_page_number;
} virtual_page_data;

typedef struct {
  page_t vpn;
  page_t pfn;
} tlb_data;

typedef struct {
  tlb_data lookup_table[TLB_ENTRIES];
  int tlb_lookup;
  int tlb_misses;
  int tlb_hits;
} tlb_lookup;

void initialize_vm();

void bitmap_init(bitmap **bit_map, size_t total_pages);

static void set_bit_at_index(bitmap *bit_map, int bit_index);

static int get_bit_at_index(bitmap *bit_map, int bit_index);

static void reset_bit_at_index(bitmap *bit_map, int bit_index);

void init_page_directories();

void assign_virtual_page_bits();

void set_physical_mem();

void get_virtual_data(page_t vp, virtual_page_data *vir_page_data);

void *translate(page_t vp);

void page_map(page_t vp, page_t pf);

page_t get_next_avail(int no_of_pages);

void *t_malloc(size_t n);

int t_free(page_t vp, size_t n);

int access_memory(page_t vp, void *val, size_t n, bool is_put);

int put_value(page_t vp, void *val, size_t n);

int get_value(page_t vp, void *dst, size_t n);

void mat_mult(page_t a, page_t b, page_t c, size_t l, size_t m, size_t n);

void add_TLB(page_t vpage, page_t ppage);

int check_TLB(page_t vpage);

int remove_TLB(page_t vpage);

void print_TLB_missrate();
