/*
 *  Copyright (C) 2024 CS416/CS518 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];
struct superblock *s_block;
bitmap_t inode_bm, block_bm;

int max_inum = MAX_INUM/8;
int max_dnum = MAX_DNUM/8;
int max_dirent = BLOCK_SIZE/sizeof(struct dirent);

void log_msg(const char *format, ...)
{
#ifdef DEBUG_TL
    va_list ap;
    va_start(ap, format);

    vfprintf(stdout, format, ap);
#endif
}

/*
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	bitmap_t curr_inode = inode_bm;
	for (int i = 0; i < max_inum; i++)
	{
	   int available = __builtin_ctz(~(*curr_inode));
		if (available < 8)
		{
		    for (int j = 0; j < 8; j++) {
    			int pos = i*8 + j;
                if (get_bitmap(inode_bm, pos) == 0) {
                    set_bitmap(inode_bm, pos);
                   	bio_write(s_block->i_bitmap_blk, inode_bm);
                    return pos;
                }
		    }
		}
		curr_inode++;
	}
	return -1;
}

/*
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    bitmap_t curr_block = block_bm;
	for (int i = 0; i < max_dnum; i++)
	{
	   int available = __builtin_ctz(~(*curr_block));
		if (available < 8)
		{
		    for (int j = 0; j < 8; j++) {
       			int pos = i*8 + j;
                if (get_bitmap(block_bm, pos) == 0) {
                   	set_bitmap(block_bm, pos);
                    bio_write(s_block->d_bitmap_blk, block_bm);
                    log_msg("New block assigned at bit_pos %d and pos %d\n", pos, s_block->d_start_blk + pos);
    				return s_block->d_start_blk + pos;
                }
		    }
			break;
		}
		curr_block++;
	}
	return -1;
}

/*
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
    int ino_block = s_block->i_start_blk + (ino/INODE_SIZE);
    int ino_offset = ino % INODE_SIZE;
    struct inode *temp_ino = (struct inode *)malloc(BLOCK_SIZE);
    bio_read(ino_block, temp_ino);
    *inode = *(temp_ino + ino_offset);
    free(temp_ino);
    return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	int ino_block = s_block->i_start_blk + (ino/INODE_SIZE);
	int ino_offset = ino % INODE_SIZE;
	struct inode *temp_ino = (struct inode *)malloc(BLOCK_SIZE);
	bio_read(ino_block, temp_ino);
	*(temp_ino + ino_offset) = *inode;
	bio_write(ino_block, temp_ino);
	free(temp_ino);
	return 0;
}


/*
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
  struct inode dir_inode;
  readi(ino, &dir_inode);
  struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
  struct dirent *start_addr_d = dirent_dblock;
  for (int i = 0; i < 16; i++) {
      if (dir_inode.direct_ptr[i] == 0) {
          continue;
      }

      log_msg("Trying to find directory %s\n", fname);
      bio_read(dir_inode.direct_ptr[i], dirent_dblock);
      for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
          if (dirent_dblock->valid && (strncmp(fname, dirent_dblock->name, name_len) == 0) && dirent_dblock->len == name_len) {
              *dirent = *dirent_dblock;
              dirent_dblock = start_addr_d;
              free(dirent_dblock);
              log_msg("Found after %d::%d blocks with ino %d\n", i, j, dirent->ino);
              return 0;
          }
      }
      dirent_dblock = start_addr_d;
  }
  log_msg("Failed due to exceeding limits %d\n", dir_inode.vstat.st_blocks);
  free(dirent_dblock);
  return -ENOENT;
}

void add_new_direc(struct dirent **dirent_dblock, const char *fname, size_t name_len, int dir_index, int f_ino, int bno) {
    (*dirent_dblock)->ino = f_ino;
    (*dirent_dblock)->valid = 1;
    (*dirent_dblock)->len = name_len;
    memcpy((*dirent_dblock)->name, fname, name_len + 1);
    (*dirent_dblock)->name[name_len] = '\0';
    (*dirent_dblock) -= dir_index;
    bio_write(bno, (*dirent_dblock));
    free((*dirent_dblock));
}

void update_dir_inode(struct inode *dir_inode) {
    struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
    *upd_inode = *dir_inode;
    upd_inode->size += sizeof(struct dirent) ;
	upd_inode->vstat.st_size = upd_inode->size;
	time(&upd_inode->vstat.st_mtime) ;
    writei(upd_inode->ino, upd_inode);
    free(upd_inode);
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
	struct dirent *main_dblock = dirent_dblock;
	for (int i = 0; i < 16; i++) {
	    if (dir_inode.direct_ptr[i] == 0) {
			continue; // quite possible for "some block" in the middle to be free??
		}

        bio_read(dir_inode.direct_ptr[i], dirent_dblock);
        log_msg("dirent_dblock::%d\n", dirent_dblock);
        for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
            if (dirent_dblock->valid && strncmp(fname, dirent_dblock->name, name_len) == 0 && dirent_dblock->len == name_len) {
                log_msg("Cannot make same dir...\n");
                dirent_dblock = main_dblock;
                free(dirent_dblock);
                return -EISDIR;
            }
        }
		dirent_dblock = main_dblock;
	}

	log_msg("Attempting to add new dir...\n");
	for (int i = 0; i < 16; i++) {
    	if (dir_inode.direct_ptr[i] != 0) {
            bio_read(dir_inode.direct_ptr[i], dirent_dblock);
            for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
                if (dirent_dblock->valid == 0) {
                    log_msg("Going to add new dir on the same block at pos %d...\n", j);
                    add_new_direc(&dirent_dblock, fname, name_len, j, f_ino, dir_inode.direct_ptr[i]);
                    update_dir_inode(&dir_inode);
                    return 0;
                }
            }
    	} else {
            log_msg("Going to add new dir on a new block...\n");
            memset(dirent_dblock, 0, BLOCK_SIZE);
            dir_inode.direct_ptr[i] = get_avail_blkno();
            dir_inode.vstat.st_blocks++;
            add_new_direc(&dirent_dblock, fname, name_len, 0, f_ino, dir_inode.direct_ptr[i]);
            update_dir_inode(&dir_inode);
            return 0;
        }
        dirent_dblock = main_dblock;
	}
	log_msg("Some issue, cannot find any space to add this dir...\n");
	free(dirent_dblock);
	return -ENOSPC;
}

// Required for 518
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
    struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
    struct dirent *main_dblock = dirent_dblock;
	int is_removed = 0;
    for (int i = 0; i < 16; i++) {
        if (dir_inode.direct_ptr[i] == 0) {
            continue;
        }

        bio_read(dir_inode.direct_ptr[i], dirent_dblock);
        int block_count = 0;
        for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
            if (!is_removed && dirent_dblock->valid && strncmp(fname, dirent_dblock->name, name_len) == 0 && dirent_dblock->len == name_len) {
                log_msg("Removing direc... %s\n", dirent_dblock->name);
                memset(dirent_dblock, 0, sizeof(struct dirent));
                dirent_dblock -= j;
                bio_write(dir_inode.direct_ptr[i], dirent_dblock);
                dirent_dblock += j;
                is_removed = 1;
                block_count++;
            } else if (!dirent_dblock->valid) {
                block_count++;
            }
        }

        dirent_dblock = main_dblock;
        if (is_removed) {
            log_msg("Removed successfully with block_count %d, max_dirent %d, and node at %d\n", block_count, max_dirent, i);
            if (block_count == max_dirent) {
                log_msg("Check removal... %d::%d\n", dir_inode.direct_ptr[i] - s_block->d_start_blk, s_block->d_start_blk - dir_inode.direct_ptr[i]);
                unset_bitmap(block_bm, dir_inode.direct_ptr[i] - s_block->d_start_blk);
                dir_inode.direct_ptr[i] = 0;
                dir_inode.vstat.st_blocks--;
            }

            struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
            *upd_inode = dir_inode;
            upd_inode->size -= sizeof(struct dirent) ;
            upd_inode->vstat.st_size = upd_inode->size;
            time(&upd_inode->vstat.st_mtime);
            writei(upd_inode->ino, upd_inode);
            free(upd_inode);
            free(dirent_dblock);
            return 0;
        }
	}
	free(dirent_dblock);
	return -ENOENT;
}

/*
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
    const char delimitter[1] = "/";
    if (strcmp(path, delimitter) != 0) {
        struct dirent direc;
        const char *token = strtok(path, delimitter);
        while(token != NULL) {
            log_msg("Looping through paths... %s with ino %d\n", token, ino);
            if (dir_find(ino, token, strlen(token), &direc)) {
                return -ENOENT;
            }
            token = strtok(NULL, delimitter);
            ino = direc.ino;
        }
    }
    readi(ino, inode);
    log_msg("get_node_by_path::%d::%d::%s\n", ino, inode->valid, path);
	return 0;
}

void add_dir_entry(int i_no, int parent_ino, int block_no, mode_t mode) {
    log_msg("Adding new dir entry, ino::%d, parent_ino::%d, block_no::%d\n", i_no, parent_ino, block_no);
    struct inode *root_node = (struct inode*)malloc(sizeof(struct inode));
	struct stat* v_stat = (struct stat *)malloc(sizeof(struct stat));
	memset(root_node, 0, sizeof(struct inode));
	memset(v_stat, 0, sizeof(struct stat));
	v_stat->st_ino = root_node->ino = i_no;
	root_node->valid = 1;
	v_stat->st_nlink = root_node->link = 2;
	root_node->direct_ptr[0] = block_no;
	v_stat->st_mode = root_node->type = S_IFDIR | mode;
	v_stat->st_size = root_node->size = sizeof(struct dirent)*2;
	v_stat->st_blocks = 1;
	v_stat->st_blksize = BLOCK_SIZE;
	time(&v_stat->st_mtime);
	root_node->vstat = *v_stat;
	writei(i_no, root_node);

	struct dirent *root_dir = (struct dirent*)malloc(BLOCK_SIZE);
	memset(root_dir, 0, BLOCK_SIZE);
	root_dir->ino = i_no;
	root_dir->valid = 1;
	root_dir->len = 1;
	strncpy(root_dir->name, ".\0", 2);
	struct dirent *parent_dir = root_dir + 1;
	parent_dir->ino = parent_ino;
	parent_dir->valid = 1;
	parent_dir->len = 2;
	strncpy(parent_dir->name, "..\0", 3);
	bio_write(block_no, root_dir);

	free(root_dir);
	free(v_stat);
	free(root_node);
}

/*
 * Make file system
 */
int rufs_mkfs() {
    log_msg("root::%s\n", diskfile_path);
	dev_init(diskfile_path);
	s_block->magic_num = MAGIC_NUM;
	s_block->max_inum = MAX_INUM;
	s_block->max_dnum = MAX_DNUM;
	s_block->i_bitmap_blk = 1;
	s_block->d_bitmap_blk = 2;
	s_block->i_start_blk = 3;
	s_block->d_start_blk = 3 + ((sizeof(struct inode) * MAX_INUM) / BLOCK_SIZE);
	bio_write(0, s_block);

	memset(inode_bm, 0, BLOCK_SIZE);
	set_bitmap(inode_bm, 0);
	bio_write(s_block->i_bitmap_blk, inode_bm);
	memset(block_bm, 0, BLOCK_SIZE);
	set_bitmap(block_bm, 0);
	bio_write(s_block->d_bitmap_blk, block_bm);

	add_dir_entry(0, 0, s_block->d_start_blk, (S_IRWXU|S_IRGRP|S_IXGRP|S_IRWXO|S_IXOTH));
	return 0;
}


/*
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    s_block = (struct superblock*)malloc(BLOCK_SIZE);
    inode_bm = (bitmap_t)malloc(BLOCK_SIZE);
	block_bm = (bitmap_t)malloc(BLOCK_SIZE);
	if (dev_open(diskfile_path) == -1) {
        rufs_mkfs();
        return NULL;
    }

	bio_read(0, s_block);
    bio_read(s_block->i_bitmap_blk, inode_bm);
    bio_read(s_block->d_bitmap_blk, block_bm);
	return NULL;
}

static void rufs_destroy(void *userdata) {
    free(block_bm);
    free(inode_bm);
    free(s_block);
    dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	log_msg("GET_ATTR::%s\n", path);

	struct inode inode;
	if (get_node_by_path(path, 0, &inode)) {
	   return -ENOENT;
	}

	log_msg("path inode::%d with mode %d\n", inode.ino, inode.vstat.st_mode);
    *stbuf = inode.vstat;
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	log_msg("OPEN_PATH::%s\n", path);

	struct inode dir_inode;
	if(get_node_by_path(path, 0, &dir_inode) == 0 && dir_inode.valid) {
	   return 0;
	}
    return -1;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	log_msg("READ_PATH::%s\n", path);

	struct inode dir_inode;
	if (get_node_by_path(path, 0, &dir_inode) == 0 && dir_inode.valid) {
    	struct inode *contents = (struct inode *)malloc(sizeof(struct inode));
        struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
        struct dirent *start_addr_d = dirent_dblock;
        for (int i = 0; i < 16; i++) {
            if (dir_inode.direct_ptr[i] == 0) {
                continue;
            }

            bio_read(dir_inode.direct_ptr[i], dirent_dblock);
            for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
                readi(dirent_dblock->ino, contents);
                if (dirent_dblock->valid) {
                    filler(buffer, dirent_dblock->name, &contents->vstat, 0);
                }
            }
            dirent_dblock = start_addr_d;
        }
        log_msg("EXIT_READ::%s\n", path);
        free(dirent_dblock);
        free(contents);
	}
	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {
    char *parent_m = strdup(path);
    char *base_m = strdup(path);
    char *parent = dirname(parent_m);
    char *base = basename(base_m);
    log_msg("MKDIR\tParent::%s, Base::%s\n", parent, base);

    struct inode p_inode;
    if(get_node_by_path(parent, 0, &p_inode)) {
        free(base_m);
        free(parent_m);
        return -ENOTDIR;
    }

    int f_no = get_avail_ino();
    log_msg("Attempting to add new dir, parent_ino::%d, dir_ino::%d, base::%s, base_len::%d\n", p_inode.ino, f_no, base, strlen(base));
    int result = dir_add(p_inode, f_no, base, strlen(base));
    if (result != 0) {
        free(base_m);
        free(parent_m);
        return result;
    }

    add_dir_entry(f_no, p_inode.ino, get_avail_blkno(), mode);
    free(base_m);
    free(parent_m);
    return 0;
}

// Required for 518
static int rufs_rmdir(const char *path) {
    char *parent_m = strdup(path);
    char *base_m = strdup(path);
    char *parent = dirname(parent_m);
    char *base = basename(base_m);
    log_msg("RMDIR\tParent::%s, Base::%s\n", parent, base);

    struct inode p_inode, c_inode;
    if(get_node_by_path(parent, 0, &p_inode) || get_node_by_path(base, p_inode.ino, &c_inode) || !S_ISDIR(c_inode.vstat.st_mode)) {
        free(base_m);
        free(parent_m);
        return -ENOTDIR;
    }

    // Verifying if the node is indeed a directory, and removing only if it is "empty!"
    if (S_ISDIR(c_inode.vstat.st_mode) && c_inode.vstat.st_size != 2*sizeof(struct dirent)) {
        log_msg("%s is not empty\n", path);
        free(base_m);
        free(parent_m);
        return -ENOTEMPTY;
    }

    struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
    for (int i = 0; i < 16; i++) {
        if (c_inode.direct_ptr[i] == 0) {
            continue;
        }

        bio_read(c_inode.direct_ptr[i], dirent_dblock);
        memset(dirent_dblock, 0, BLOCK_SIZE);
        unset_bitmap(block_bm, c_inode.direct_ptr[i] - s_block->d_start_blk);
        bio_write(c_inode.direct_ptr[i], dirent_dblock);
	}

    struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
    memset(upd_inode, 0, sizeof(struct inode));
    unset_bitmap(inode_bm, c_inode.ino);
    writei(c_inode.ino, upd_inode);
    free(upd_inode);
    free(dirent_dblock);
    log_msg("Child_ino %d with parent %d has been removed\n", c_inode.ino, p_inode.ino);

    int result = dir_remove(p_inode, base, strlen(base));
    free(base_m);
    free(parent_m);
    return result;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
    // For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char *parent_m = strdup(path);
    char *base_m = strdup(path);
    char *parent = dirname(parent_m);
    char *base = basename(base_m);
    log_msg("CREATE\tParent::%s, Base::%s\n", parent, base);

	struct inode p_inode;
	if(get_node_by_path(parent, 0, &p_inode)) {
        free(base_m);
        free(parent_m);
        return -ENOTDIR;
    }

	int f_no = get_avail_ino();
	int result = dir_add(p_inode, f_no, base, strlen(base));
    if (result != 0) {
        free(base_m);
        free(parent_m);
        return result;
    }

	log_msg("Adding new file entry, ino::%d, parent_ino::%d, block_no::%d\n", f_no, p_inode.ino);
    struct inode *root_node = (struct inode*)malloc(sizeof(struct inode));
    struct stat* v_stat = (struct stat *)malloc(sizeof(struct stat));
    memset(root_node, 0, sizeof(struct inode));
    memset(v_stat, 0, sizeof(struct stat));
    v_stat->st_ino = root_node->ino = f_no;
    root_node->valid = 1;
    v_stat->st_nlink = root_node->link = 2;
    v_stat->st_mode = root_node->type = S_IFREG | mode;
    time(&v_stat->st_mtime);
    root_node->vstat = *v_stat;
    writei(f_no, root_node);

    free(v_stat);
    free(root_node);
    free(base_m);
    free(parent_m);
    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
    char *path_m = strdup(path);
    log_msg("OPEN\tPath::%s\n", path_m);

    struct inode p_inode;
	if(get_node_by_path(path_m, 0, &p_inode)) {
        free(path_m);
        return -1;
    }

	free(path_m);
	return 0;
}

void update_inode(struct inode *p_inode) {
    struct inode *update_inode = (struct inode *)malloc(sizeof(struct inode));
	*update_inode = *p_inode;
	time(&update_inode->vstat.st_mtime);
	writei(p_inode->ino, update_inode);
	free(update_inode);
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    char *path_m = strdup(path);
    log_msg("READ\tPath::%s\n", path_m);

	struct inode p_inode;
	if (get_node_by_path(path_m, 0, &p_inode)) {
        free(path_m);
        return 0;
    }

	char *read_buffer = (char*)malloc(BLOCK_SIZE);
	char *r_main = read_buffer;
	int start_block = (offset / BLOCK_SIZE);
	int start_addr = offset % BLOCK_SIZE;
	int read_size = size;
	log_msg("Going to read buffer of size %d from offset %d\n", buffer, size, offset);
	for (int i = start_block; i < 16 && read_size; i++) {
		if (p_inode.direct_ptr[i] == 0) {
		  continue;
		}

	    bio_read(p_inode.direct_ptr[i], read_buffer);
		int j = i == start_block ? start_addr : 0;
		for (; j < BLOCK_SIZE && read_size; j++, buffer++, read_buffer++, read_size--) {
		    *buffer = *read_buffer;
		}

	    read_buffer = r_main;
	}
	update_inode(&p_inode);
	free(read_buffer);
	free(path_m);
	return (size - read_size);
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    char *path_m = strdup(path);
    log_msg("WRITE\tPath::%s\n", path_m);

	struct inode p_inode;
	if (get_node_by_path(path_m, 0, &p_inode)) {
        free(path_m);
        return 0;
    }

	char *write_buffer = (char*)malloc(BLOCK_SIZE);
	char *w_main = write_buffer;
	int start_block = (offset / BLOCK_SIZE);
	int start_addr = offset % BLOCK_SIZE;
	int write_size = size;
	log_msg("Going to write buffer of size %d from offset %d at block %d for inode %d\n", size, offset, start_block, p_inode.ino);
	for (int i = start_block; i < 16 && write_size; i++) {
		if (p_inode.direct_ptr[i] == 0) {
            p_inode.direct_ptr[i] = get_avail_blkno();
            p_inode.vstat.st_blksize++;
		}

	    bio_read(p_inode.direct_ptr[i], write_buffer);
		int j = i == start_block ? start_addr : 0;
		log_msg("Writing message into block %d at index %d, size remaining %d\n", p_inode.direct_ptr[i], i, write_size);
		for (; j < BLOCK_SIZE && write_size; j++, buffer++, write_buffer++, write_size--) {
		    *write_buffer = *buffer;
		}

		write_buffer = w_main;
		bio_write(p_inode.direct_ptr[i], write_buffer);
	}
	p_inode.vstat.st_size += (size - write_size);
	log_msg("Data of size %d has been written at offset %d. Total file size is %d.\n", (size - write_size), offset, p_inode.vstat.st_size);
	update_inode(&p_inode);
	free(write_buffer);
	free(path_m);
	return (size - write_size);
}

// Required for 518
static int rufs_unlink(const char *path) {
    char *parent_m = strdup(path);
    char *base_m = strdup(path);
    char *parent = dirname(parent_m);
    char *base = basename(base_m);
    log_msg("RMDIR\tParent::%s, Base::%s\n", parent, base);

    struct inode p_inode, c_inode;
    if(get_node_by_path(parent, 0, &p_inode) || get_node_by_path(base, p_inode.ino, &c_inode)) {
        free(base_m);
        free(parent_m);
        return -ENOENT;
    }

    if (!S_ISREG(c_inode.vstat.st_mode)) {
        log_msg("%s is not a file type\n", path);
        free(base_m);
        free(parent_m);
        return -ENOENT;
    }

    struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
    for (int i = 0; i < 16; i++) {
        if (c_inode.direct_ptr[i] == 0) {
            continue;
        }

        bio_read(c_inode.direct_ptr[i], dirent_dblock);
        memset(dirent_dblock, 0, BLOCK_SIZE);
        unset_bitmap(block_bm, c_inode.direct_ptr[i] - s_block->d_start_blk);
        bio_write(c_inode.direct_ptr[i], dirent_dblock);
	}

    struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
    memset(upd_inode, 0, sizeof(struct inode));
    unset_bitmap(inode_bm, c_inode.ino);
    writei(c_inode.ino, upd_inode);
    free(upd_inode);
    free(dirent_dblock);
    log_msg("Child_ino %d with parent %d has been removed\n", c_inode.ino, p_inode.ino);

    int result = dir_remove(p_inode, base, strlen(base));
    free(base_m);
    free(parent_m);
    return result;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}
