#include "my_vm.h"
#include <stddef.h>
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
  vpn_pages = malloc(PTR_ENTRY_SIZE*VPN_LEVELS);
  for (int i = 0; i < VPN_LEVELS; i++) {
    if ((i == (VPN_LEVELS - 1)) && add_bit) {
      vpn_len += add_bit; // adding trailing bit(s)
      frame_cnt = (1 << vpn_len);
      page_req = (frame_cnt/PAGE_MEM_SIZE) + ((frame_cnt%PAGE_MEM_SIZE) ? 1 : 0);
    }
    vpn_bits[i] = vpn_len;
    vpn_pages[i] = page_req;
  }
  mem_manager.frames_free = total_physical_pages;
  mem_manager.page_frame_usage = 0;
}

void bitmap_init(bitmap **bit_map, size_t total_pages) {
  *bit_map = (bitmap *)malloc(sizeof(bitmap));

  if (bit_map == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  (*bit_map)->num_bytes = total_pages / 8;
  (*bit_map)->bits = calloc((*bit_map)->num_bytes, sizeof(unsigned char));

  if ((*bit_map)->bits == NULL) {
    free((*bit_map));
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  memset((*bit_map)->bits, 0, (*bit_map)->num_bytes);
}

static void set_bit_at_index(bitmap **bit_map, int bit_index) {
  (*bit_map)->bits[bit_index / 8] |= (1 << (bit_index % 8));
}

static int get_bit_at_index(bitmap *bit_map, int bit_index) {
  return (bit_map->bits[bit_index / 8] & (1 << (bit_index % 8))) > 0;
}

static void reset_bit_at_index(bitmap **bit_map, int bit_index) {
  (*bit_map)->bits[bit_index / 8] &= ~(1 << (bit_index % 8));
}

void set_hunk(page_t *hunk) {
  for (page_t i = 0; i < PAGE_MEM_SIZE; i++) {
    *(hunk + i) = PGFM_SET;
  }
}

void init_page_directories() {
  mem_manager.dir_index = next_page_ref;
  page_t old_ref = next_page_ref;
  for (int level = 0; level < VPN_LEVELS; level++) {
    page_t page_req = vpn_pages[level];
    for (page_t index = next_page_ref; index > (next_page_ref - page_req); index--) {
      set_bit_at_index(&mem_manager.pm_bitmap, index);
      set_hunk((mem_manager.pg_mem + (index*PAGE_MEM_SIZE)));
    }

    if (level > 0) {
      *(mem_manager.pg_mem + old_ref*PAGE_MEM_SIZE) |= ((next_page_ref << PG_LEN) | PGFM_VALID);
    }
    old_ref = next_page_ref;
    next_page_ref -= page_req;
    mem_manager.frames_free -= page_req;
    mem_manager.page_frame_usage += page_req;
  }
  page_map(0, 0);
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
  mem_manager.pg_mem = (page_t *)malloc(MEMSIZE);

  if (mem_manager.pg_mem == NULL) {
    fprintf(stderr, "Physical Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  bitmap_init(&mem_manager.pm_bitmap, total_physical_pages);
  bitmap_init(&mem_manager.vm_bitmap, total_virtual_pages);
}

void read_vpn_data(page_t vm_page, vp_data *vpn_data) {
  page_t offset = (vm_page & (PAGE_SIZE - 1));
  page_t vpn = (vm_page >> PG_LEN);
  vpn_data->vpn = vpn;
  vpn_data->offset = offset;
  for (int i = VPN_LEVELS - 1; i > -1; i--) {
    vpn_data->indices[i] = vpn & ((1 << vpn_bits[i]) - 1);
    vpn >>= vpn_bits[i];
  }
}

void invalidate_pm(page_t vpn) {
  vp_data vpn_data;
  read_vpn_data(vpn << PG_LEN, &vpn_data);
  page_t curr_index = mem_manager.dir_index;
  page_t *page_dir;
  for(int level = 0; level < VPN_LEVELS; level++) {
    page_t index = vpn_data.indices[level];

    if (index > PAGE_MEM_SIZE) {
      curr_index -= index/PAGE_MEM_SIZE; // always contigious
      index %= PAGE_MEM_SIZE;
    }
    page_dir = mem_manager.pg_mem + curr_index*PAGE_MEM_SIZE + index;
    curr_index = ((*page_dir) >> PG_LEN);
    if (level == (VPN_LEVELS - 1)) {
      reset_bit_at_index(&mem_manager.vm_bitmap, vpn);
      reset_bit_at_index(&mem_manager.pm_bitmap, curr_index);
      *page_dir = PGFM_SET; // invalidate the last level pm bit
    }
  }
}

void *translate(page_t vpn) {
  vp_data vpn_data;
  read_vpn_data(vpn, &vpn_data);

  if (get_bit_at_index(mem_manager.vm_bitmap, vpn_data.vpn) == 0) {
    return NULL;
  }
  page_t pfn = check_TLB((vpn >> PG_LEN));

  if ((pfn & PGFM_VALID) > 0) {
    page_t offset = vpn & (PAGE_SIZE - 1);
    page_t physical_addr = ((pfn >> PG_LEN) << PG_LEN) | offset;
    return (void *)physical_addr;
  }
  page_t dir_indices = mem_manager.dir_index;
  page_t *page_dir;
  int level = 0;
  for(; level < VPN_LEVELS; level++) {
    page_t index = vpn_data.indices[level];

    if (index > PAGE_MEM_SIZE) {
      dir_indices -= index/PAGE_MEM_SIZE; // always contigious
      index %= PAGE_MEM_SIZE;
    }
    page_dir = mem_manager.pg_mem + dir_indices*PAGE_MEM_SIZE + index;
    dir_indices = (*page_dir >> PG_LEN);
  }
  add_TLB(vpn_data.vpn, dir_indices);
  page_t physical_address =
      ((dir_indices << PG_LEN) | vpn_data.offset);
  return (void *)physical_address;
}

void page_map(page_t vpn, page_t pm_frame) {
  vp_data vpn_data;
  page_t dir_indices = mem_manager.dir_index;
  read_vpn_data(vpn << PG_LEN, &vpn_data);
  page_t *page_dir;
  for(int level = 0; level < VPN_LEVELS; level++) {
    page_t index = vpn_data.indices[level];

    if (index > PAGE_MEM_SIZE) {
      dir_indices -= index/PAGE_MEM_SIZE;
      index %= PAGE_MEM_SIZE;
    }
    page_dir = mem_manager.pg_mem + dir_indices*PAGE_MEM_SIZE + index;

    if (level == (VPN_LEVELS - 1)) {
      *page_dir |= ((pm_frame << PG_LEN) | PGFM_VALID);
      set_bit_at_index(&mem_manager.vm_bitmap, vpn);
      set_bit_at_index(&mem_manager.pm_bitmap, pm_frame);
    } else if (*page_dir & PGFM_VALID) {
      dir_indices = (*page_dir >> PG_LEN); // reuse if already valid
    } else {
      page_t len = 0;
      for (page_t pos = next_page_ref; pos >= 0 && len < vpn_pages[(level + 1)]; pos--) {
        if (get_bit_at_index(mem_manager.pm_bitmap, pos) == 0) {
          if (len == 0) {
            next_page_ref = pos;
          }
          len++;
        } else {
          len = 0;
        }
      }
      for (page_t pos = next_page_ref; pos < (next_page_ref - len); pos--) {
        set_bit_at_index(&mem_manager.pm_bitmap, pos);
        set_hunk((mem_manager.pg_mem + (pos*PAGE_MEM_SIZE)));
      }
      *page_dir |= ((next_page_ref << PG_LEN) | PGFM_VALID);
      dir_indices = next_page_ref;
      next_page_ref -= len;
      mem_manager.frames_free -= len;
      mem_manager.page_frame_usage += len;
    }
  }
  add_TLB(vpn_data.vpn, pm_frame);
  return;
}

int get_vm_start_page(int page_cnt, page_t *start_page) {
  int frame_cnt = 0;
  page_t vpn_start = 1;
  int new_available = -1;\
  int available = 0;
  unsigned char *vm_bm;
  for (page_t vpn = 0; vpn < mem_manager.vm_bitmap->num_bytes && frame_cnt < page_cnt;) {
    vm_bm = mem_manager.vm_bitmap->bits + vpn;

    if (new_available != -1) {
      available = new_available;
      new_available = -1;
    } else {
      available = __builtin_ctz(~(*vm_bm));
    }

    if (available < 8) {
      page_t vpn_bit = (vpn*8) + available;

      if (frame_cnt == 0) {
        vpn_start = vpn_bit;
      }
      for (; vpn_bit < total_virtual_pages && frame_cnt < page_cnt; vpn_bit++, frame_cnt++) {
        if (get_bit_at_index(mem_manager.vm_bitmap, vpn_bit)) {
          frame_cnt = 0; // if mem not contiguous, repeat
          new_available = (vpn_bit%8) + 1;
          vpn = vpn_bit/8;

          if (new_available == 8) {
            new_available = 0;
            vpn++;
          }
          break;
        }
      }
    } else {
      vpn++;
    }

    if (page_cnt == frame_cnt) {
      *start_page = vpn_start;
      return 1;
    }
  }
  return 0;
}

page_t get_page_count(size_t n) {
  page_t page_cnt = n/PAGE_SIZE;

  if ((n % PAGE_SIZE) > 0) {
    page_cnt++;
  }
  return page_cnt;
}

void *t_malloc(size_t n) {
  initialize_vm();
  page_t page_cnt = get_page_count(n);

  if (mem_manager.frames_free < page_cnt) {
    return NULL; // insufficient memory available
  }
  page_t vpn_start = 0;

  if (get_vm_start_page(page_cnt, &vpn_start) == 0) {
    return NULL;
  }
  page_t frame_cnt = 0;
  page_t vpn = vpn_start;
  page_t pm_index = 0;
  for (; vpn < (vpn_start + page_cnt) && (frame_cnt < page_cnt) && (pm_index < next_page_ref); pm_index++) {
    unsigned char *pm_bm = mem_manager.pm_bitmap->bits + pm_index;
    int available = __builtin_ctz(~(*pm_bm));

    if (available < 8) {
      for (int pm_j_index = available; pm_j_index < 8 && (frame_cnt < page_cnt); pm_j_index++) {
        page_t pm_frame = (pm_index*8) + pm_j_index;
        if (get_bit_at_index(mem_manager.pm_bitmap, pm_frame) == 0) {
          page_map(vpn, pm_frame);
          mem_manager.frames_free--;
          frame_cnt++;
          vpn++;
        }
      }
    }
  }
  page_t virtual_address = vpn_start << PG_LEN;
  return (void *)virtual_address;
}

int t_free(page_t vm_page, size_t n) {
  page_t page_cnt = get_page_count(n);
  page_t start_page = vm_page >> PG_LEN;
  bool is_invalid = false;
  page_t vpn = start_page;
  for (; vpn < (start_page + page_cnt); vpn++) {
    if (get_bit_at_index(mem_manager.vm_bitmap, vpn) == 0) {
      is_invalid = true;
      break;
    }
  }

  if (is_invalid) {
    return -1;
  }
  for (vpn = start_page; vpn < (start_page + page_cnt); vpn++) {
    invalidate_pm(vpn);
    remove_TLB(vpn);
  }
  mem_manager.frames_free += page_cnt;
  return 0;
}

void copy_data(unsigned char *dest, const unsigned char *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    *(dest+i) = *(src+i);
  }
}

int access_memory(page_t vm_page, void *val, size_t n, bool read) {
  page_t page_cnt = get_page_count(n);

  if (page_cnt > total_physical_pages || page_cnt > mem_manager.frames_free) {
    return -1;
  }
  page_t vm_start = vm_page >> PG_LEN;
  page_t vm_end = vm_start + page_cnt;
  page_t rw_opr = n;
  unsigned char *p_val = val;
  for (page_t vm_index = vm_start; vm_index < total_virtual_pages && rw_opr > 0; vm_index++) {
    void *pm_index = (page_t*)((page_t)translate((vm_index << PG_LEN)) >> PG_LEN);
    unsigned char *m_val = (unsigned char *)(mem_manager.pg_mem + (page_t)pm_index*PAGE_MEM_SIZE);
    page_t rw_data = PAGE_SIZE;

    if (pm_index == NULL) {
      return 0;
    }

    if (vm_index == vm_start) {
      vp_data vpn_data;
      read_vpn_data(vm_page, &vpn_data);
      // need to be aware of the type of variable to offset accordingly??
      m_val += vpn_data.offset;
      rw_data -= vpn_data.offset;
    }

    if (rw_opr < PAGE_MEM_SIZE) {
      rw_data = rw_opr;
    }

    if (read) {
      copy_data(p_val, m_val, rw_data);
    } else {
      copy_data(m_val, p_val, rw_data);
    }
    rw_opr -= rw_data;
    p_val += rw_data;
  }
  return 0;
}

int put_value(page_t vm_page, void *val, size_t n) {
  return access_memory(vm_page, val, n, false);
}

int get_value(page_t vm_page, void *val, size_t n) {
  return access_memory(vm_page, val, n, true);
}

void mat_mult(page_t l, page_t r, page_t o, size_t col_l, size_t row_r, size_t common) {
  page_t value_l, value_r, value_o;
  page_t address_l, address_r, address_o;
  for (size_t i = 0; i < col_l; i++) {
    for (size_t j = 0; j < row_r; j++) {
      value_o = 0; //[i][j] = [i][k] * [k][j]
      for (size_t k = 0; k < common; k++) {
        address_l = l + (i*common*PTR_ENTRY_SIZE) + (k*PTR_ENTRY_SIZE);
        address_r = r + (k*row_r*PTR_ENTRY_SIZE) + (j*PTR_ENTRY_SIZE);
        get_value(address_l, &value_l, PTR_ENTRY_SIZE);
        get_value(address_r, &value_r, PTR_ENTRY_SIZE);
        value_o += (value_l*value_r);
      }
      address_o = o + (i*row_r*PTR_ENTRY_SIZE) + (j*PTR_ENTRY_SIZE);
      put_value(address_o, &value_o, PTR_ENTRY_SIZE);
    }
  }
}

void add_TLB(page_t vpage, page_t ppage) {
  int pos = vpage % TLB_ENTRIES;
  int vpn = vpage << PG_LEN;
  int pfn = ppage << PG_LEN;
  vpn |= PGFM_VALID; // first bit from offset
  pfn |= PGFM_VALID; // first bit from offset
  mem_lookup.table[pos].vpn = vpn;
  mem_lookup.table[pos].pfn = pfn;
}

int is_vpage_exists(page_t vpage, int pos) {
  int is_valid = (mem_lookup.table[pos].vpn & PGFM_VALID);
  return (is_valid > 0) && ((mem_lookup.table[pos].vpn >> PG_LEN) == vpage);
}

int check_TLB(page_t vpage) {
  int pos = vpage % TLB_ENTRIES;
  mem_lookup.count++;

  if (is_vpage_exists(vpage, pos)) {
    mem_lookup.hit++;
    return mem_lookup.table[pos].pfn;
  }
  mem_lookup.miss++;
  return 0;
}

int remove_TLB(page_t vpage) {
  int pos = vpage % TLB_ENTRIES;

  if (is_vpage_exists(vpage, pos)) {
    reset_tlb_data(pos);
    return 0;
  }
  return -1;
}

void print_TLB_missrate() {
  float miss_rate = mem_lookup.miss / mem_lookup.count;
  printf("The TLB miss rate is, %.10f\n", miss_rate);
}
