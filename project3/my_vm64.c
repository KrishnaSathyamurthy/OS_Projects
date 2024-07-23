#include "my_vm64.h"
#include <string.h>

bool init_vm = false;

vm_manager mem_manager;
tlb_lookup mem_lookup;

page_t total_virtual_pages = MAX_MEMSIZE/PAGE_SIZE;
page_t total_physical_pages = MEMSIZE/PAGE_SIZE;
page_t next_page_ref = (MEMSIZE/PAGE_SIZE) - 1;

int *vpn_bits = 0;
page_t *vpn_pages = 0;

void init_vbits() {
  int full_vpn_len = VM_LEN - PG_LEN;
  int add_bit = (full_vpn_len % VPN_LEVELS);
  int vpn_len = full_vpn_len / VPN_LEVELS;
  page_t frame_cnt = (1 << vpn_len);
  page_t page_req = (frame_cnt/PAGE_MEM_SIZE) + ((frame_cnt%PAGE_MEM_SIZE) ? 1 : 0);
  vpn_bits = malloc(sizeof(int)*VPN_LEVELS);
  vpn_pages = malloc(sizeof(page_t)*VPN_LEVELS);
  for (int i = 0; i < VPN_LEVELS; i++) {
    if ((i == (VPN_LEVELS - 1)) && add_bit) {
      vpn_len += add_bit; // adding trailing bit(s)
      frame_cnt = (1 << vpn_len);
      page_req = (frame_cnt/PAGE_MEM_SIZE) + ((frame_cnt%PAGE_MEM_SIZE) ? 1 : 0);
    }
    vpn_bits[i] = vpn_len;
    vpn_pages[i] = page_req;
  }
}

void bitmap_init(bitmap **bit_map, size_t total_pages) {
  *bit_map = (bitmap *)malloc(sizeof(bitmap));

  if (bit_map == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  (*bit_map)->num_bytes = total_pages / BYTES_SIZE;
  (*bit_map)->bits = calloc((*bit_map)->num_bytes, sizeof(unsigned char));

  if ((*bit_map)->bits == NULL) {
    free((*bit_map));
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  memset((*bit_map)->bits, 0, (*bit_map)->num_bytes);
}

static void set_bit_at_index(bitmap *bit_map, int bit_index) {
    bit_map->bits[bit_index / BYTES_SIZE] |=
        (1 << (bit_index % BYTES_SIZE));
}

static int get_bit_at_index(bitmap *bit_map, int bit_index) {
  return (bit_map->bits[bit_index / 8] & (1 << (bit_index % 8))) > 0;
}

static void reset_bit_at_index(bitmap *bit_map, int bit_index) {
  bit_map->bits[bit_index / 8] &= ~(1 << (bit_index % 8));
}

void init_page_directories() {
  int i = 0;
  mem_manager.dir_index = next_page_ref;
  for (int level = 0; level < VPN_LEVELS; level++) {
    page_t page_req = vpn_pages[level];
    for (page_t index = next_page_ref; index > (next_page_ref - page_req); index--, i++) {
      set_bit_at_index(mem_manager.pm_bitmap, index);
      memset(mem_manager.pm_mem[index].hunk, PGFM_SET, PAGE_MEM_SIZE);

      if (level > 0) {
        *mem_manager.pm_mem[next_page_ref].hunk |= ((index << PG_LEN) | PGFM_VALID);
      }
    }
    next_page_ref -= page_req;
  }
}

void reset_tlb_data(int pos) {
  mem_lookup.table[pos].vpn = 0;
  mem_lookup.table[pos].pfn = 0;
}

void init_tlb() {
  mem_lookup.count = mem_lookup.hit = mem_lookup.miss = 0;
  for (int i = 0; i < TLB_ENTRIES; i++) {
    reset_tlb_data(i);
  }
}

void initialize_vm() {
  if (init_vm) {
    return;
  }
  init_vbits();
  set_physical_mem();
  init_tlb();
  init_page_directories();
  init_vm = true;
}

void set_physical_mem() {
  mem_manager.pm_mem = (page *)malloc(MEMSIZE);

  if (mem_manager.pm_mem == NULL) {
    fprintf(stderr, "Physical Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  bitmap_init(&mem_manager.pm_bitmap, total_physical_pages);
  bitmap_init(&mem_manager.vm_bitmap, total_virtual_pages);
}

void read_vpn_data(page_t vm_page, vp_data *vpn_data) {
  page_t offset = (vm_page & (PAGE_SIZE - 1));
  page_t vpn = (vm_page >> PG_LEN);
  int inner_data = vpn;
  for (int i = VPN_LEVELS - 1; i > -1; i--) {
    vpn_data->indices[i] = inner_data & vpn_bits[i];
    inner_data = inner_data >> vpn_bits[i];
  }
  vpn_data->vpn = vpn;
  vpn_data->offset = offset;
}

void *translate(page_t vp) {
  page_t vpn = vp >> PG_LEN;
  page_t pfn = check_TLB(vpn);

  if (pfn != -1) {
    page_t offset = vp & (PAGE_SIZE - 1);
    page_t physical_addr = pfn << PG_LEN;
    physical_addr += offset;
    return (void *)physical_addr;
  }
  vp_data virtual_data;
  read_vpn_data(vp, &virtual_data);
  int bit = get_bit_at_index(mem_manager.vm_bitmap,
                             virtual_data.vpn);

  if (bit != 1) {
    return NULL;
  }
  page_t *second_level =
      mem_manager.page_dir + virtual_data.indices[0];
  page_t *second_level_addr = &mem_manager.pm_mem[*second_level];
  page_t *third_level = second_level_addr + virtual_data.indices[1];
  page_t *third_level_addr = &mem_manager.pm_mem[*third_level];
  page_t *fourth_level = third_level_addr + virtual_data.indices[2];
  page_t *fourth_level_addr = &mem_manager.pm_mem[*fourth_level];
  page_t *page_table = fourth_level_addr + virtual_data.indices[3];
  add_TLB(virtual_data.vpn, *page_table);
  page_t physical_address =
      ((*page_table << PG_LEN) + virtual_data.offset);
  return (void *)physical_address;
}

void page_map(page_t vm_page, page_t pm_frame) {
  vp_data vpn_data;
  page_t dir_indices = mem_manager.dir_index;
  read_vpn_data(vm_page << PG_LEN, &vpn_data);
  page_t *page_dir;
  int level = 0;
  for(; level < VPN_LEVELS; level++) {
    page_t index = vpn_data.indices[level];

    if (index > PAGE_MEM_SIZE) {
      dir_indices -= index/PAGE_MEM_SIZE;
      index %= PAGE_MEM_SIZE;
    }
    page_dir = mem_manager.pm_mem[dir_indices].hunk; // always contigious
    page_dir += index;
    page_t offset = (*page_dir & (PAGE_SIZE - 1));

    // reuse memory if already valid
    if (offset & PGFM_VALID) {
      dir_indices = (*page_dir >> PG_LEN);
    } else if (level == (VPN_LEVELS - 1)) {
      *page_dir = (pm_frame << PG_LEN);
      set_bit_at_index(mem_manager.vm_bitmap, vm_page);
      set_bit_at_index(mem_manager.pm_bitmap, pm_frame);
    } else {
      page_t len = 0;
      for (page_t pos = next_page_ref; pos >= 0 && len < vpn_pages[(VPN_LEVELS + 1)]; pos--, len++) {
        if (get_bit_at_index(mem_manager.pm_bitmap, pos) == 0) {
          if (len == 0) {
            next_page_ref = pos;
          }
        } else {
          len = 0;
        }
      }
      for (page_t pos = next_page_ref; pos < (next_page_ref - vpn_pages[VPN_LEVELS + 1]); pos--) {
        set_bit_at_index(mem_manager.pm_bitmap, pos);
        memset(mem_manager.pm_mem[pos].hunk, PGFM_SET, PAGE_MEM_SIZE);
      }
      *page_dir = ((next_page_ref << PG_LEN) | PGFM_VALID);
      dir_indices = next_page_ref;
    }
  }
  page_t index = vpn_data.indices[level];

  if (index > PAGE_MEM_SIZE) {
    dir_indices -= index/PAGE_MEM_SIZE;
    index %= PAGE_MEM_SIZE;
  }
  page_dir = mem_manager.pm_mem[dir_indices].hunk; // always contigious
  page_dir += index;
  page_t last_level = *page_dir;
  page_t *last_level_addr = mem_manager.pm_mem[last_level].hunk;
  page_t *page_table_entry = (last_level_addr + vpn_data.indices[3]);
  *page_table_entry = pm_frame;
  add_TLB(vpn_data.vpn, pm_frame);
  return;
}

int get_vm_start_page(int page_cnt, page_t *start_page) {
  int frame_cnt = 1;
  unsigned char *vm_bm = mem_manager.vm_bitmap->bits;
  for (int index = 0; index < mem_manager.vm_bitmap->num_bytes && frame_cnt < page_cnt; index++, vm_bm++) {
    int available = __builtin_ctz(~(*vm_bm));

    if (available < 8) {
      int tmp_index = (index*8) + available;
      for (; tmp_index < mem_manager.vm_bitmap->num_bytes && frame_cnt < page_cnt; tmp_index++, frame_cnt++) {
        if (get_bit_at_index(mem_manager.vm_bitmap, tmp_index)) {
          frame_cnt = 1; // if mem not contiguous, repeat
          break;
        }
      }
    }

    if (page_cnt == frame_cnt) {
      *start_page = index;
      return 1;
    }
  }
  return 0;
}

void *t_malloc(size_t n) {
  initialize_vm();
  int page_cnt = n/PAGE_SIZE;

  if ((n % PAGE_SIZE) > 0) {
    page_cnt++;
  }
  int pm_frames[page_cnt];
  int frame_cnt = 0;
  unsigned char *pm_bm = mem_manager.pm_bitmap->bits;
  for (int index = 0; index < mem_manager.pm_bitmap->num_bytes && frame_cnt < page_cnt; index++, pm_bm++) {
    int available = __builtin_ctz(~(*pm_bm));

    if (available < 8) {
      for (int j_index = available; j_index < 8; j_index++) {
        int frame = (index*8) + j_index;
        if (get_bit_at_index(mem_manager.pm_bitmap, frame) == 0) {
          pm_frames[frame_cnt] = frame;
          frame_cnt++;
        }
      }
    }
  }

  if (frame_cnt < page_cnt) {
    return NULL; // insufficient memory available
  }
  page_t start_page = 0;

  if (get_vm_start_page(page_cnt, &start_page) == 0) {
    return NULL;
  }
  frame_cnt = 0;
  for (int index = start_page; index < (start_page + page_cnt); index++, frame_cnt++) {
    page_map(index, pm_frames[frame_cnt]);
  }
  page_t virtual_address = start_page << PG_LEN;
  return (void *)virtual_address;
}

int t_free(page_t vp, size_t n) {
  int no_of_pages = n / (PAGE_SIZE);
  int remainder_bytes = n % (PAGE_SIZE);

  if (remainder_bytes > 0) {
    no_of_pages++;
  }
  page_t start_page = vp >> PG_LEN;
  bool valid_page = true;
  for (page_t i = start_page; i < (start_page + no_of_pages); i++) {
    int bit = get_bit_at_index(mem_manager.vm_bitmap, i);
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
    virt_addr = (i << PG_LEN);
    phy_addr = translate(virt_addr);
    physical_page = (phy_addr >> PG_LEN);
    reset_bit_at_index(mem_manager.vm_bitmap, i);
    reset_bit_at_index(mem_manager.pm_bitmap, physical_page);
    remove_TLB(i);
  }
}

int access_memory(page_t vp, void *val, size_t n, bool is_put) {
  char *physical_address =
      (char *)mem_manager.pm_mem[(page_t)translate(vp)].hunk;
  char *virtual_address = (char *)vp;
  char *end_virt_address = virtual_address + n;
  page_t vp_first = (page_t)virtual_address >> PG_LEN;
  page_t vp_last = (page_t)end_virt_address >> PG_LEN;
  char *value = (char *)val;
  for (page_t i = vp_first; i <= vp_last; i++) {
    int bit = get_bit_at_index(mem_manager.vm_bitmap, i);

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
    int outer_bits_mask = (1 << PG_LEN);
    outer_bits_mask -= 1;
    int offset = virt_addr & outer_bits_mask;

    if (offset == 0) {
      physical_address =
          (char *)mem_manager.pm_mem[(page_t)translate(virt_addr)]
              .hunk;
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
  int vpn = vpage << PG_LEN;
  int pfn = ppage << PG_LEN;
  vpn |= (1 << (PG_LEN - 1)); // first bit from offset
  pfn |= (1 << (PG_LEN - 1)); // first bit from offset
  mem_lookup.table[pos].vpn = vpn;
  mem_lookup.table[pos].pfn = pfn;
}

int is_vpage_exists(page_t vpage, int pos) {
    int is_valid = ((mem_lookup.table[pos].vpn >> (PG_LEN - 1)) & 1);
    return is_valid && ((mem_lookup.table[pos].vpn >> PG_LEN) == vpage);
}

int check_TLB(page_t vpage) {
  int pos = vpage % TLB_ENTRIES;
  mem_lookup.count++;

  if (is_vpage_exists(vpage, pos)) {
    mem_lookup.hit++;
  }
  mem_lookup.miss++;
  return mem_lookup.table[pos].pfn;
}

int remove_TLB(page_t vpage) {
  int pos = vpage % TLB_ENTRIES;
  int is_valid = ((mem_lookup.table[pos].vpn >> (PG_LEN - 1)) & 1);

  if (is_vpage_exists(vpage, pos)) {
    reset_tlb_data(pos);
  }
}

void print_TLB_missrate() {
  float miss_rate = mem_lookup.miss / mem_lookup.count;
  printf("The TLB miss rate is, %.10f\n", miss_rate);
}
