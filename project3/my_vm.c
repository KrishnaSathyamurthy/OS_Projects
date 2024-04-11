#include "my_vm.h"

//TODO: Define static variables and structs, include headers, etc
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
    
    int i;
    for(i = 0; i < (*bit_map)->num_bytes; i++) {
        (*bit_map)->bits[i] = '\0';
    }
}

static void set_bit_at_index(bitmap *bit_map, int bit_index) {
    bit_map->bits[bit_index / BYTES_TO_BITS] |= (1 << (bit_index % BYTES_TO_BITS));
}

static int get_bit_at_index(bitmap *bit_map, int bit_index) {
    return (bit_map->bits[bit_index / 8] & (1 << (bit_index % 8))) != 0;
}

void reset_bit_at_index(bitmap *bit_map, size_t index) {
    bit_map->bits[index / 8] &= ~(1 << (index % 8));
}

void init_page_directories() {
	
    int bit_index = (total_physical_pages - 1);

	set_bit_at_index(memory_manager.physical_bitmap, bit_index);

	memory_manager.page_directory = &memory_manager.physical_memory[bit_index];
	page_t *page_dr_array = (memory_manager.page_directory)->page_array;

	for(int i = 0; i < (1 << outer_level_bits); i++) {
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

    bitmap_init(&memory_manager.physical_bitmap, total_physical_pages);

    bitmap_init(&memory_manager.virtual_bitmap, total_virtual_pages);
}

// Need changes here
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

void * translate(page_t vp) {

    virtual_page_data virtual_data;
    get_virtual_data(vp, &virtual_data);

    // Verifies if the bit is already allocated
    int bit = get_bit_at_index(memory_manager.virtual_bitmap, virtual_data.virtual_page_number);
    
    if (bit != 1) {
        return NULL;
    }

    page_t *inner_page_table = (memory_manager.page_directory + virtual_data.outer_index);

    // Calculate inner level address
    page_t *inner_page_table_address = &memory_manager.physical_memory[*inner_page_table];
    
    // Pick the entry
    page_t *page_table_entry = (inner_page_table_address + virtual_data.inner_index);

    // Finally, the physical page address + offset gives the corresponding physical address for passed virtual address
    page_t physical_address = (*page_table_entry + virtual_data.offset);

    return (void *)physical_address;
    
}

void page_map(page_t vp, page_t pf) {
    
    virtual_page_data virtual_data;
    get_virtual_data(vp, &virtual_data);

    page_t physical_frame_number = pf >> offset_bits;
    page_t virtual_page_number = vp >> offset_bits;

    page_t *page_directory_entry = (memory_manager.page_directory + virtual_data.outer_index);

    if (*page_directory_entry == -1) {
		
        int last_page = total_physical_pages - 2;
		
        while(last_page >= 0) {

			int bit = get_bit_at_index(memory_manager.physical_bitmap, last_page);
			
            if(bit == 0) {

				set_bit_at_index(memory_manager.physical_bitmap, last_page);
				*page_directory_entry = last_page;
				
                break;
			}
			
            last_page--;
		}
        
        if(*page_directory_entry == -1) {
            return NULL;
        }
	}

    page_t inner_level_page_table = *page_directory_entry;

    // Compute address of inner page table
    page_t *inner_page_table_address = &memory_manager.physical_memory[inner_level_page_table];

    page_t *page_table_entry = (inner_page_table_address + virtual_data.inner_index);
		
    // Store the ppn into the page table entry.
    *page_table_entry = physical_frame_number;
}


// Need to change this
page_t get_next_avail(int no_of_pages) {

    int start_page, vp_y, vp_x = 0;
    int page_count;

    while(vp_x < total_virtual_pages) {
        
        int bit = get_bit_at_index(memory_manager.virtual_bitmap, vp_x);
        
        if(bit == 0) {
            
            vp_y = vp_x + 1;
            page_count = 1;

            while(page_count < no_of_pages && vp_y < total_virtual_pages) {
                
                bit = get_bit_at_index(memory_manager.virtual_bitmap, vp_y);
                if(bit == 1) {
                    break;
                }
                else {
                    vp_y++;
                    page_count++;
                }
            }
            if(page_count == no_of_pages)
            {
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

    frame_count = 0;

    for(i=start_page; i< (start_page + no_of_pages); i++)
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

    // Get the starting page from Virtual page/address
    page_t start_page = vp >> offset_bits;
    
    // NOTE: Ideally here, the offset should be stored especially when
    // Fragmentation comes into picture, but since thats not the case
    // we are not ignoring this.

    bool valid_page = true;

    for(page_t i = start_page; i < (start_page + no_of_pages); i++)
    {
        int bit = get_bit_at_index(memory_manager.virtual_bitmap, i);
        if(bit == 0)
        {
            valid_page = false;
            break;
        }
    }

    if(valid_page == false)
    {
        return -1;
    }

    page_t virt_addr, phy_addr, physical_page;

    for(page_t i = start_page; i < (start_page + no_of_pages); i++)
    {
        virt_addr = (i << offset_bits);

        phy_addr = translate(virt_addr);

        physical_page = (phy_addr >> offset_bits);

        reset_bit_at_index(memory_manager.virtual_bitmap, i);

        reset_bit_at_index(memory_manager.physical_bitmap, physical_page);

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
    int size = (2 * (1<<13)) + 2;
    int *p = t_malloc(size);

    t_free(p, size);

    return 0;
}
