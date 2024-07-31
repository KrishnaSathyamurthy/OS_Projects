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

#if INTPTR_MAX == INT64_MAX
typedef uint64_t page_t;
#define VM_LEN 48
#define PM_LEN 34
#define PG_LEN 12
#define VPN_LEVELS 4
#else
typedef uint32_t page_t;
#define VM_LEN 32
#define PM_LEN 30
#define PG_LEN 8
#define VPN_LEVELS 2
#endif

#define MAX_MEMSIZE (1ULL << VM_LEN)
#define MEMSIZE (1UL << PM_LEN)
#define PAGE_SIZE (1UL << PG_LEN)
#define FRAME_SIZE PAGE_SIZE
#define PTR_ENTRY_SIZE sizeof(page_t)
#define PAGE_MEM_SIZE (PAGE_SIZE / PTR_ENTRY_SIZE)
#define TLB_ENTRIES 512

#define PGFM_SET (1UL << (PG_LEN - 1))
#define PGFM_VALID (1UL << (PG_LEN - 2))

typedef struct page {
  page_t hunk[PAGE_MEM_SIZE];
} page;

typedef struct {
  unsigned char *bits;
  size_t num_bytes;
} bitmap;

typedef struct {
  // unsigned char *pm_mem; // TODO
  page_t *pg_mem;
  bitmap *pm_bitmap;
  bitmap *vm_bitmap;
  page_t dir_index;
  page_t frames_free;
  page_t page_frame_usage;
} vm_manager;

typedef struct {
  page_t indices[VPN_LEVELS];
  page_t offset;
  page_t vpn;
} vp_data;

typedef struct {
  page_t vpn;
  page_t pfn;
} tlb_data;

typedef struct {
  tlb_data table[TLB_ENTRIES];
  int count;
  int miss;
  int hit;
} tlb_lookup;

void initialize_vm();

void bitmap_init(bitmap **bit_map, size_t total_pages);

static void set_bit_at_index(bitmap **bit_map, int bit_index);

static int get_bit_at_index(bitmap *bit_map, int bit_index);

static void reset_bit_at_index(bitmap **bit_map, int bit_index);

void set_hunk(page_t *hunk);

void init_page_directories();

void assign_virtual_page_bits();

void set_physical_mem();

void read_vpn_data(page_t vm_page, vp_data *vpn_data);

void *translate(page_t vpn);

void invalidate_pm(page_t vpn);

void page_map(page_t vm_page, page_t pm_frame);

int get_next_avail(int page_cnt, page_t *start_page);

void *t_malloc(size_t n);

int t_free(page_t vm_page, size_t n);

void copy_data(unsigned char *dest, const unsigned char *src, size_t n);

int access_memory(page_t vm_page, void *val, size_t n, bool read);

int put_value(page_t vm_page, void *val, size_t n);

int get_value(page_t vm_page, void *val, size_t n);

void mat_mult(page_t l, page_t r, page_t o, size_t col_l, size_t row_r, size_t common);

void add_TLB(page_t vpage, page_t ppage);

int check_TLB(page_t vpage);

int remove_TLB(page_t vpage);

void print_TLB_missrate();
