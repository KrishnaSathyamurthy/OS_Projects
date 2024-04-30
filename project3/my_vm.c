#include "my_vm.h"
#include <string.h>

// TODO: Define static variables and structs, include headers, etc
bool init = false;

vm_manager memory_manager;
tlb_lookup mem_lookup;

// Total number of virtual pages
unsigned long long total_virtual_pages = (MAX_MEMSIZE) / (PAGE_SIZE);

// Total of physical pages
unsigned long total_physical_pages = (MEMSIZE) / (PAGE_SIZE);

int inner_level_bits = 0;
int outer_level_bits = 0;
int offset_bits = 0;
int virtual_page_bits = 0;

/*
  This function initializes the virtual address bits, which will be
  utilized to layout the virtual memory address and map with
  its physical counter
 */
void assign_virtual_page_bits() {

  offset_bits = log2(PAGE_SIZE);

  virtual_page_bits = (ADDRESS_BIT - offset_bits);

  outer_level_bits = log2(PAGE_MEM_SIZE);

  inner_level_bits = (virtual_page_bits - outer_level_bits);
}

/*
  The method takes the pointer to bit_map pointer (address of the bitmap
  pointer), to allocate memory for virtual and physical page wise. The
  total_pages varies for both the space.
 */
void bitmap_init(bitmap **bit_map, size_t total_pages) {

  *bit_map = (bitmap *)malloc(sizeof(bitmap));

  if (bit_map == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  (*bit_map)->num_bytes = total_pages / BYTES_TO_BITS;
  (*bit_map)->bits = calloc((*bit_map)->num_bytes, sizeof(unsigned char));

  if ((*bit_map)->bits == NULL) {
    free((*bit_map));
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  memset((*bit_map)->bits, 0, (*bit_map)->num_bytes);
}

/*
  The following methods are crucial to memory availability by performing
  bit operations over page bitmaps. Everytime memory is allocated/deallocated,
  these functions are executed to ensure the memory availability is intact
 */
static void set_bit_at_index(bitmap *bit_map, int bit_index) {
  bit_map->bits[bit_index / BYTES_TO_BITS] |=
      (1 << (bit_index % BYTES_TO_BITS));
}

static int get_bit_at_index(bitmap *bit_map, int bit_index) {
  return (bit_map->bits[bit_index / 8] & (1 << (bit_index % 8))) != 0;
}

static void reset_bit_at_index(bitmap *bit_map, int bit_index) {
  bit_map->bits[bit_index / 8] &= ~(1 << (bit_index % 8));
}
/* Ends here*/

/*
  Outer page directory that maps outer level index to its
  inner level counterpart is initiated in the last page of the
  physical memory.
 */
void init_page_directories() {

  int bit_index = (total_physical_pages - 1);

  set_bit_at_index(memory_manager.physical_bitmap, bit_index);

  memory_manager.page_directory = &memory_manager.physical_memory[bit_index];
  page_t *page_dr_array = (memory_manager.page_directory)->page_array;
  for (int i = 0; i < (1 << outer_level_bits); i++) {
    // Initiating to -1, stating that any mapping hasn't begun
    page_dr_array[i] = -1;
  }
}

void reset_tlb_data(int pos) {
  mem_lookup.lookup_table[pos].vpn = -1;
  mem_lookup.lookup_table[pos].pfn = -1;
}

void init_tlb() {
  mem_lookup.tlb_lookup = mem_lookup.tlb_hits = mem_lookup.tlb_misses = 0;
  for (int i = 0; i < TLB_ENTRIES; i++) {
    reset_tlb_data(i);
  }
}

/*
  Init method for the VM system
 */
void initialize_vm() {

  if (init == true) {
    return;
  }

  assign_virtual_page_bits();
  set_physical_mem();
  init_tlb();
  init_page_directories();

  init = true;
}

/*
  Allocates memory for physical space, and the bitmaps for virtual and
  the physical pages
 */
void set_physical_mem() {

  memory_manager.physical_memory = (page *)malloc(MEMSIZE);

  if (memory_manager.physical_memory == NULL) {
    fprintf(stderr, "Physical Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  bitmap_init(&memory_manager.physical_bitmap, total_physical_pages);

  bitmap_init(&memory_manager.virtual_bitmap, total_virtual_pages);
}

// Need changes here
/*
  Crucial in computing the data for a given virtual address,
  Deriving inner, outer index, offset and virtual page number
  Which will be resourceful for page_map and translate methods
 */
void get_virtual_data(page_t vp, virtual_page_data *vir_page_data) {

  page_t offset_mask = (1 << offset_bits);
  offset_mask -= 1;
  page_t offset = vp & offset_mask;

  // Calculate the outer index
  int total_bits = ADDRESS_BIT - outer_level_bits;
  page_t outer_index = (vp >> total_bits);

  page_t virtual_page_number = (vp >> offset_bits);
  page_t inner_bits_mask = (1 << inner_level_bits);
  inner_bits_mask -= 1;
  page_t inner_index = (virtual_page_number & inner_bits_mask);

  vir_page_data->inner_index = inner_index;
  vir_page_data->outer_index = outer_index;
  vir_page_data->virtual_page_number = virtual_page_number;
  vir_page_data->offset = offset;
}

/*
  Takes the virtual address, looks up the page directory,
  Finds the corresponding inner table that is mapped,
  dereferences the physical address through inner index from the table
  and returns
 */
void *translate(page_t vp) {
  page_t vpn = vp >> offset_bits;
  page_t pfn = check_TLB(vpn);
  if (pfn != -1) {
    page_t offset_mask = (1 << offset_bits);
    offset_mask -= 1;
    page_t offset = vp & offset_mask;
    page_t physical_addr = pfn << offset_bits;
    physical_addr += offset;
    return (void *)physical_addr;
  }

  virtual_page_data virtual_data;
  get_virtual_data(vp, &virtual_data);

  int bit = get_bit_at_index(memory_manager.virtual_bitmap,
                             virtual_data.virtual_page_number);

  if (bit != 1) {
    return NULL;
  }

  page_t *inner_page_table =
      (memory_manager.page_directory + virtual_data.outer_index);

  page_t *inner_page_table_address =
      &memory_manager.physical_memory[*inner_page_table];

  page_t *page_table_entry =
      (inner_page_table_address + virtual_data.inner_index);

  add_TLB(virtual_data.virtual_page_number, *page_table_entry);

  page_t physical_address =
      ((*page_table_entry << offset_bits) + virtual_data.offset);

  return (void *)physical_address;
}

/*
   Called in t_malloc(), to map the physical and the virtual address,
   by making an entry in the page table(s). From virtual_data method, we
   find out the page directory entry, map a new inner table if not already
   present Reference the physical address into the inner table index.
 */
void page_map(page_t vp, page_t pf) {

  virtual_page_data virtual_data;
  get_virtual_data(vp, &virtual_data);

  page_t physical_frame_number = pf >> offset_bits;

  page_t *page_directory_entry =
      (memory_manager.page_directory + virtual_data.outer_index);

  if (*page_directory_entry == -1) {

    int last_page = total_physical_pages - 2;

    while (last_page >= 0) {

      int bit = get_bit_at_index(memory_manager.physical_bitmap, last_page);

      if (bit == 0) {

        set_bit_at_index(memory_manager.physical_bitmap, last_page);
        *page_directory_entry = last_page;

        break;
      }

      last_page--;
    }

    if (*page_directory_entry == -1) {
      return NULL;
    }
  }

  page_t inner_level_page_table = *page_directory_entry;

  page_t *inner_page_table_address =
      &memory_manager.physical_memory[inner_level_page_table];

  page_t *page_table_entry =
      (inner_page_table_address + virtual_data.inner_index);

  *page_table_entry = physical_frame_number;

  add_TLB(virtual_data.virtual_page_number, physical_frame_number);
}

/*
   Parses through the virtual space, and tries to find contiguous memory
   based on the no_of_pages which is required. Finds empty pages through
   virtual bitmaps and returns the starting page of the page bundle.

   1. Can use __builtin_ctz / __builtin_clz to identify if the 8 pages are free
   or not
   2. how about using integer "map" instead of char "map"???
 */
page_t get_next_avail(int no_of_pages) {

  int start_page, vp_y, vp_x = 0;
  int page_count;

  while (vp_x < total_virtual_pages) {

    int bit = get_bit_at_index(memory_manager.virtual_bitmap, vp_x);

    if (bit == 0) {

      vp_y = vp_x + 1;
      page_count = 1;

      while (page_count < no_of_pages && vp_y < total_virtual_pages) {

        bit = get_bit_at_index(memory_manager.virtual_bitmap, vp_y);
        if (bit == 1) {
          break;
        } else {
          vp_y++;
          page_count++;
        }
      }
      if (page_count == no_of_pages) {
        start_page = vp_x;
        return start_page;
      }
      vp_x = vp_y;
      continue;
    }
    vp_x++;
  }
  return -1;
}

/*
   The memory allocator function:
   Based on the size which is parsed, finds out the number of pages required.
   Parses the physical memory for non-contiguous pages and virtual for
   contiguous pages. Each corresponding page is allocated for the process
   and page_map is called to map the VA and PA in the page tables. Finally it
   returns the virtual_address that is being assigned.
 */
void *t_malloc(size_t n) {

  initialize_vm();

  int no_of_pages = n / (PAGE_SIZE);
  int remainder_bytes = n % (PAGE_SIZE);

  if (remainder_bytes > 0) {
    no_of_pages++;
  }

  int i = 0;
  // We are using this array to consider all the physical pages
  int physical_frames[no_of_pages];
  int frame_count = 0;

  // Find the free pages in the physical memory
  while (frame_count < no_of_pages && i < total_physical_pages) {
    int bit = get_bit_at_index(memory_manager.physical_bitmap, i);
    if (bit == 0) {
      physical_frames[frame_count] = i;
      frame_count++;
    }
    i++;
  }

  // No physical memory is found
  if (frame_count < no_of_pages) {
    return NULL;
  }

  int start_page = get_next_avail(no_of_pages);

  if (start_page == -1) {
    return NULL;
  }

  frame_count = 0;

  for (i = start_page; i < (start_page + no_of_pages); i++) {
    page_t page_virtual_addr = i << offset_bits;
    page_t frame_physical_addr = physical_frames[frame_count] << offset_bits;

    page_map(page_virtual_addr, frame_physical_addr);

    set_bit_at_index(memory_manager.virtual_bitmap, i);

    set_bit_at_index(memory_manager.physical_bitmap,
                     physical_frames[frame_count]);

    frame_count++;
  }

  page_t virtual_address = start_page << offset_bits;

  return (void *)virtual_address;
}

/*
   Free memory allocation:
   Takes the starting virtual address, and its size. Verifies if the virtual
   memory is allocated prior, and for each page, finds out the corresponding
   physical address from the page table using translate(). Using both VA and
   PA, the memory is deallocated from the bitmaps respectively.
 */
int t_free(page_t vp, size_t n) {

  int no_of_pages = n / (PAGE_SIZE);
  int remainder_bytes = n % (PAGE_SIZE);

  if (remainder_bytes > 0) {
    no_of_pages++;
  }

  // Get the starting page from Virtual page/address
  page_t start_page = vp >> offset_bits;

  // NOTE: Ideally here, the offset should be stored especially when
  // Fragmentation comes into picture, but since thats not the case
  // we are not ignoring this.

  bool valid_page = true;

  for (page_t i = start_page; i < (start_page + no_of_pages); i++) {
    int bit = get_bit_at_index(memory_manager.virtual_bitmap, i);
    if (bit == 0) {
      valid_page = false;
      break;
    }
  }

  if (valid_page == false) {
    return -1;
  }

  page_t virt_addr, phy_addr, physical_page;

  for (page_t i = start_page; i < (start_page + no_of_pages); i++) {
    virt_addr = (i << offset_bits);

    phy_addr = translate(virt_addr);

    physical_page = (phy_addr >> offset_bits);

    reset_bit_at_index(memory_manager.virtual_bitmap, i);

    reset_bit_at_index(memory_manager.physical_bitmap, physical_page);

    remove_TLB(i);
  }
}

int access_memory(page_t vp, void *val, size_t n, bool is_put) {
  // Doing this to fetch/update data bytewise
  char *physical_address =
      (char *)memory_manager.physical_memory[(page_t)translate(vp)].page_array;

  char *virtual_address = (char *)vp;
  char *end_virt_address = virtual_address + n;

  page_t vp_first = (page_t)virtual_address >> offset_bits;
  page_t vp_last = (page_t)end_virt_address >> offset_bits;

  char *value = (char *)val;

  for (page_t i = vp_first; i <= vp_last; i++) {

    int bit = get_bit_at_index(memory_manager.virtual_bitmap, i);

    if (bit == 0) {
      return -1;
    }
  }

  for (int i = 0; i < n; i++) {

    if (is_put) {
      *physical_address = *value;
    } else {
      *value = *physical_address;
    }

    value++;
    physical_address++;
    virtual_address++;

    page_t virt_addr = (page_t)virtual_address;

    int outer_bits_mask = (1 << offset_bits);
    outer_bits_mask -= 1;
    int offset = virt_addr & outer_bits_mask;

    if (offset == 0) {
      physical_address =
          (char *)memory_manager.physical_memory[(page_t)translate(virt_addr)]
              .page_array;
    }
  }

  return 0;
}

int put_value(page_t vp, void *val, size_t n) {

  return access_memory(vp, val, n, true);
}

int get_value(page_t vp, void *dst, size_t n) {

  return access_memory(vp, dst, n, false);
}

void mat_mult(page_t a, page_t b, page_t c, size_t l, size_t m, size_t n) {

  int value_a, value_b, value_c;
  unsigned int address_a, address_b, address_c;
  int value_size = sizeof(int);

  for (size_t i = 0; i < l; i++) {

    for (size_t j = 0; j < n; j++) {

      value_c = 0;

      for (size_t k = 0; k < m; k++) {

        address_b = b + (j * value_size) + ((k * n * value_size));
        address_a = a + (k * value_size) + ((i * m * value_size));

        get_value(address_b, &value_b, value_size);
        get_value(address_a, &value_a, value_size);

        value_c += value_a * value_b;
      }

      address_c = c + ((i * n * value_size)) + (j * value_size);
      put_value(address_c, &value_c, value_size);
    }
  }
}

void add_TLB(page_t vpage, page_t ppage) {
  int pos = vpage % TLB_ENTRIES;
  mem_lookup.lookup_table[pos].vpn = vpage;
  mem_lookup.lookup_table[pos].pfn = ppage;
}

int check_TLB(page_t vpage) {
  int pos = vpage % TLB_ENTRIES;
  mem_lookup.tlb_lookup++;

  if (mem_lookup.lookup_table[pos].vpn == vpage) {
    mem_lookup.tlb_hits++;
    return mem_lookup.lookup_table[pos].pfn;
  }
  mem_lookup.tlb_misses++;
  return -1;
}

int remove_TLB(page_t vpage) {
  int pos = vpage % TLB_ENTRIES;
  if (mem_lookup.lookup_table[pos].vpn == vpage) {
    reset_tlb_data(pos);
  }
}

void print_TLB_missrate() {
  float miss_rate = mem_lookup.tlb_misses / mem_lookup.tlb_lookup;
  printf("The TLB miss rate is, %.10f\n", miss_rate);
}
