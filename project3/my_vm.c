#include "my_vm.h"

//TODO: Define static variables and structs, include headers, etc.
typedef struct page {
    page_t page_array[PAGE_MEM_SIZE];
} page;

typedef struct vm_manager {
    page *physical_memory;
    bitmap *physical_bitmap;
    bitmap *virtual_bitmap;
    page *page_directory;
} vm_manager;

typedef struct bitmap {
    unsigned char *bits;
    size_t num_bytes;
} bitmap;

typedef struct virtual_page_data {
    page_t outer_index;
    page_t inner_index;
    page_t offset;
    page_t virtual_page_number;
} virtual_page_data;

bool init = false;

vm_manager memory_manager;

// Number of physical pages
unsigned long total_physical_pages = (MEMSIZE)/(PAGE_SIZE);

//Number of virtual pages
unsigned long long total_virtual_pages = (MAX_MEMSIZE)/(PAGE_SIZE);

void assign_virtual_page_bits() {

    offset_bits = log2(PAGE_SIZE);
    
    virtual_page_bits = (ADDRESS_BIT - offset_bits);
    
    outer_level_bits = log2(PAGE_MEM_SIZE);
    
    inner_level_bits = (virtual_page_bits - outer_level_bits);
}

bitmap* bitmap_init(size_t total_pages) {
    
    bitmap* b_map = malloc(sizeof(bitmap));
    
    if (b_map == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    
    b_map->num_bytes = total_pages / BYTES_TO_BITS;
    b_map->bits = calloc(b_map->num_bytes, sizeof(unsigned char));
    
    if (b_map->bits == NULL) {
        free(b_map);
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    
    int i;
    for(i = 0; i < b_map->num_bytes; i++) {
        b_map->bits[i] = '\0';
    }
    
    return b_map;
}


static void set_bit_at_index(char *bit_map, int bit_index) {
    // Calculate the starting location where index is present.
    char *bitmap_index = ((char *) bit_map) + (bit_index / BYTES_TO_BITS);
    
    // Find the exact bit to set
    char set_bit = 1 << (bit_index % BYTES_TO_BITS);

    // Set the bit at that index to 1.
    *bitmap_index |= set_bit;
   
    return;
}

static int get_bit_at_index(char *bit_map, int bit_index) {
    
    // Calculate the starting location where index is present.
    char *bitmap_index = ((char *) bit_map) + (bit_index / BYTES_TO_BITS);

    // Getting the bit at required index
    int get_bit = (int)(*bitmap_index >> (bit_index % BYTES_TO_BITS)) & 1;
    
    return get_bit;
}

void init_page_directories() {
	int bit_index = (total_physical_pages - 1);

	set_bit_at_index(memory_manager.physical_bitmap, bit_index);

	memory_manager.page_directory = &memory_manager.physical_memory[bit_index];
	page_t *page_dr_array = (memory_manager.page_directory)->page_array;

	for(int i = 0; i < (1 << outer_level_bits); i++)
    {
        page_dr_array[i] = -1;
    }
	
}


void initialize_vm() {
    
    if (init == true) {
        return;
    }
    
    assign_virtual_page_bits();
    set_physical_mem();
    init_page_directories();
    
    init = true;
}


void set_physical_mem() {
    
    memory_manager.physical_memory = (page *)malloc(MEMSIZE);

    if (memory_manager.physical_memory == NULL) {
        fprintf(stderr, "Physical Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    memory_manager.physical_bitmap = bitmap_init(total_physical_pages);

    memory_manager.virtual_bitmap = bitmap_init(total_virtual_pages);
}

virtual_page_data* get_virtual_data(page_t vp) {
    
    virtual_page_data *vir_page_data = malloc(sizeof(virtual_page_data));

    if (vir_page_data == NULL) {
        fprintf(stderr, "Failed to allocate Virtual page data\n");
        exit(EXIT_FAILURE);
    }
    
    
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

    (*vir_page_data).inner_index = outer_index;
    (*vir_page_data).outer_index = inner_index;
    (*vir_page_data).virtual_page_number = virtual_page_number;
    (*vir_page_data).offset = offset;
    
    return vir_page_data;

}

void * translate(page_t vp) {

    virtual_page_data* virtual_data = get_virtual_indeces(vp);

    int bit = get_bit_at_index(memory_manager.virtual_bitmap, (int)(*virtual_data).virtual_page_number);
    
    if (bit != 1) {
        return;
    }

    page_t *page_dir_entry = (memory_manager.page_directory + (*virtual_data).outer_index);

    page_t inner_level_page_num = *page_dir_entry;

    // Compute address of inner page table
    page_t *inner_page_table_address = (page_t *)&memory_manager.physical_memory[inner_level_page_num];
    
    page_t *page_table_entry = (inner_page_table_address + (*virtual_data).inner_index);

    // Get the actual page that resides in the physical memory
    unsigned long physical_address = *page_table_entry;

    // Get the physical page address of the above page
    page_t *physical_page_address = (page_t *)&memory_manager.physical_memory[physical_address];

    // Finally, the physical page address + offset gives the corresponding physical address for passed virtual address
    page_t physical_address = (page_t)((char *)physical_page_address + (*virtual_data).offset);
    
    free(virtual_data);

    return (void *)physical_address;
    
}

page_t page_map(page_t vp, page_t pf) {
    
    virtual_page_data* virtual_data = get_virtual_indeces(vp);
    
    page_t physical_frame_number = pf >> offset_bits;
    page_t virtual_page_number = vp >> offset_bits;

    page_t *pd_entry = (memory_manager.page_directory + (*virtual_data).outer_index);

    if (*pd_entry == -1) {
		
        int last_page = total_physical_pages - 1;
		
        while(last_page >= 0) {

			int bit = get_bit(memory_manager.physical_bitmap, last_page);
			
            if(bit == 0) {

				set_bit(memory_manager.physical_bitmap, last_page);
				*pd_entry = last_page;
				
                break;
			}
			
            last_page--;
		}
        
        if(*pd_entry == -1) {
            return ;
        }
	}

    // The pd_entry has the corresponding inner level page number
    page_t inner_level_page_num = *pd_entry;

    // Compute address of inner page table
    page_t *inner_page_table_address = (page *)&memory_manager.physical_memory[inner_level_page_num];

    page_t *page_table_entry = (inner_page_table_address + (*virtual_data).inner_index);

    free(virtual_data);
		
    // Store the ppn into the page table entry.
    *page_table_entry = physical_frame_number;

}

void * t_malloc(size_t n){
    
    initialize_vm();

    int no_of_pages = n / (PAGE_SIZE);
    int remainder_bytes = n % (PAGE_SIZE);
    
    if (remainder_bytes > 0) {
        no_of_pages++;
    }

    // We create an array to store all the physical pages that are free that can be allocated
    int i = 0;
    int physical_frames[no_of_pages];
    int frame_count = 0;

    // Find the free pages in the physical memory
    while(frame_count < no_of_pages && i < total_physical_pages)
    {
        int bit = get_bit_at_index(memory_manager.physical_bitmap, i);
        if(bit == 0)
        {
            physical_frames[frame_count] = i;
            frame_count++;
        }
        i++;
    }

    // We have failed to find free physical pages that are required to alloacte a given memory
    if(frame_count < no_of_pages)
    {
        return NULL;
    }

    int start_page = get_next_avail(no_of_pages);

    if(start_page == -1)
    {
        return NULL;
    }

    int end_page = start_page + no_of_pages - 1;

    frame_count = 0;

    for(i=start_page; i<= end_page; i++)
    {
        page_t page_virtual_addr = i << offset_bits;
        page_t frame_physical_addr = physical_frames[frame_count] << offset_bits;

        set_bit_at_index(memory_manager.virtual_bitmap, i);

        set_bit_at_index(memory_manager.physical_bitmap, physical_frames[frame_count]);

        frame_count++;

        page_map(page_virtual_addr, frame_physical_addr);
    }

    page_t virtual_address = start_page << offset_bits;

    return (void *) virtual_address;



}

int t_free(page_t vp, size_t n){
    //TODO: Finish

    int no_of_pages = n / (PAGE_SIZE);
    int remainder_bytes = n % (PAGE_SIZE);
    
    if (remainder_bytes > 0) {
        no_of_pages++;
    }

    // Get the start page from the virtual address
    page_t start_page = vp >> offset_bits;

    bool valid = true;

    for(page_t i = start_page; i < (start_page + no_of_pages); i++)
    {
        int bit = get_bit_at_index(memory_manager.virtual_bitmap, i);
        if(bit == 0)
        {
            valid = false;
            break;
        }
    }

    if(valid == false)
    {
        return NULL;
    }

    for(page_t i = start_page; i < (start_page + no_of_pages); i++)
    {
        page_t virt_addr = (start_page << offset_bits);

        page_t phy_addr = (page_t)translate(virt_addr);

        page_t physical_page = (phy_addr >> offset_bits);

        reset_bit(memory_manager.virtual_bitmap, i);

        reset_bit(memory_manager.physical_bitmap, physical_page);

        // Remove TLB entries here

    }
}

int put_value(unsigned int vp, void *val, size_t n){
    //TODO: Finish
}

int get_value(unsigned int vp, void *dst, size_t n){
    //TODO: Finish
}

void mat_mult(unsigned int a, unsigned int b, unsigned int c, size_t l, size_t m, size_t n){
    //TODO: Finish
}

void add_TLB(unsigned int vpage, unsigned int ppage){
    //TODO: Finish
}

int check_TLB(unsigned int vpage){
    //TODO: Finish
}

void print_TLB_missrate(){
    //TODO: Finish
}

int main() {
    initialize_vm();

    return 0;
}
