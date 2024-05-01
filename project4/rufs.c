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
#include <time.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];
char logfile_path[PATH_MAX];
struct superblock *s_block;
bitmap_t inode_bm, block_bm;

int max_inum = MAX_INUM/8;
int max_dnum = MAX_DNUM/8;
int max_dirent = BLOCK_SIZE/sizeof(struct dirent);
int max_indirect = BLOCK_SIZE/sizeof(int);
FILE *logfile;
int total_block_count = 0;
int total_inode_count = 0;
int max_directs = 16;

void log_msg(const char *format, ...) {
#ifdef DEBUG_TL
// Use debugging terminal
// gdb --args ./rufs -s -d /tmp/<NET_ID>/mountdir/
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
#endif
}

void log_stats(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(logfile, format, ap);
}

/*
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	bitmap_t curr_inode = inode_bm;
	for (int i = 0; i < max_inum; i++, curr_inode++)
	{
	   int available = __builtin_ctz(~(*curr_inode));
		if (available < 8)
		{
		    int pos = i*8 + available;
            if (get_bitmap(inode_bm, pos) == 0) {
                set_bitmap(inode_bm, pos);
               	bio_write(s_block->i_bitmap_blk, inode_bm);
                return pos;
            }
		}
	}
	return -1;
}

/*
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    bitmap_t curr_block = block_bm;
	for (int i = 0; i < max_dnum; i++, curr_block++)
	{
	   int available = __builtin_ctz(~(*curr_block));
		if (available < 8)
		{
		    int pos = i*8 + available;
            if (get_bitmap(block_bm, pos) == 0) {
               	set_bitmap(block_bm, pos);
                bio_write(s_block->d_bitmap_blk, block_bm);
                log_msg("New block assigned at bit_pos %d and pos %d\n", pos, s_block->d_start_blk + pos);
                return s_block->d_start_blk + pos;
            }
		}
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
 * Unset block number from bitmap
 */
int unset_block(int pos, void *block) {
    unset_bitmap(block_bm, pos - s_block->d_start_blk);
    return bio_write(s_block->d_bitmap_blk, block_bm) && bio_write(pos, block);
}

/*
 * Unset inode number from bitmap
 */
int unset_inode(int pos, struct inode *upd_inode) {
    unset_bitmap(inode_bm, pos);
    return bio_write(s_block->i_bitmap_blk, inode_bm) && writei(pos, upd_inode);
}

int find_dirent(int b_no, const char *fname, size_t name_len, struct dirent *dirent, struct dirent *dirent_dblock) {
    bio_read(b_no, dirent_dblock);
    for (int k = 0; k < max_dirent; k++, dirent_dblock++) {
        if (dirent_dblock->valid && (strncmp(fname, dirent_dblock->name, name_len) == 0) && dirent_dblock->len == name_len) {
            *dirent = *dirent_dblock;
            dirent_dblock -= k;
            free(dirent_dblock);
            log_msg("Found in %d at %d blocks with ino %d\n", b_no, k, dirent->ino);
            return 0;
        }
    }
    return -ENOENT;
}

/*
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
  struct inode dir_inode;
  memset(&dir_inode, 0, sizeof(struct inode));
  readi(ino, &dir_inode);
  struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
  struct dirent *start_addr_d = dirent_dblock;
  log_msg("Trying to find entry %s on %d\n", fname, ino);
  for (int i = 0; i < max_directs; i++) {
      if (dir_inode.direct_ptr[i] == 0) {
          continue;
      }

      if(find_dirent(dir_inode.direct_ptr[i], fname, name_len, dirent, dirent_dblock) == 0) {
          return 0;
      }
      dirent_dblock = start_addr_d;
  }

  int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
  int *start_addr_id = indirect_blocks;
  for (int i = 0; i < 8; i++) {
      if (dir_inode.indirect_ptr[i] == 0) {
          continue;
      }

      bio_read(dir_inode.indirect_ptr[i], indirect_blocks);
      for (int j = 0; j < max_indirect; j++, indirect_blocks++) {
          if (*indirect_blocks == 0) {
              continue;
          }

          if(find_dirent(*indirect_blocks, fname, name_len, dirent, dirent_dblock) == 0) {
              indirect_blocks -= j;
              free(indirect_blocks);
              return 0;
          }
          dirent_dblock = start_addr_d;
      }
      indirect_blocks = start_addr_id;
  }
  log_msg("Failed due to exceeding limits\n");
  free(indirect_blocks);
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
	struct dirent check_dirent;
	memset(&check_dirent, 0, sizeof(struct dirent));
    if (dir_find(dir_inode.ino, fname, name_len, &check_dirent) == 0) {
        return -EISDIR;
    }

	log_msg("Attempting to add new dir...\n");
	struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
	struct dirent *start_addr_d = dirent_dblock;
	for (int i = 0; i < max_directs; i++) {
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
        dirent_dblock = start_addr_d;
	}

	log_msg("Attempting to add new dir in indirect block...\n");
	int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
	int *start_addr_id = indirect_blocks;
	for (int i = 0; i < 8; i++) {
	    if (dir_inode.indirect_ptr[i] != 0) {
			bio_read(dir_inode.indirect_ptr[i], indirect_blocks);
			for (int j = 0; j < max_indirect; j++, indirect_blocks++) {
			    if (*indirect_blocks != 0) {
    			    bio_read(*indirect_blocks, dirent_dblock);
                    for (int k = 0; k < max_dirent; k++, dirent_dblock++) {
                        if (dirent_dblock->valid == 0) {
                            log_msg("Going to add new dir in the same indirect block %d::%d at pos %d...\n", dir_inode.indirect_ptr[i], *indirect_blocks, k);
                            add_new_direc(&dirent_dblock, fname, name_len, k, f_ino, *indirect_blocks);
                            update_dir_inode(&dir_inode);
                            indirect_blocks -= j;
                            free(indirect_blocks);
                            return 0;
                        }
                    }
				} else {
					memset(dirent_dblock, 0, BLOCK_SIZE);
					*indirect_blocks = get_avail_blkno();
				    log_msg("Going to add new dir in a new indirect block %d::%d at pos %d\n", dir_inode.indirect_ptr[i], *indirect_blocks, j);
					dir_inode.vstat.st_blocks++;
					add_new_direc(&dirent_dblock, fname, name_len, 0, f_ino, *indirect_blocks);
					update_dir_inode(&dir_inode);
					indirect_blocks -= j;
					bio_write(dir_inode.indirect_ptr[i], indirect_blocks);
					free(indirect_blocks);
					return 0;
				}
				dirent_dblock = start_addr_d;
			}
		} else {
			memset(dirent_dblock, 0, BLOCK_SIZE);
			memset(indirect_blocks, 0, BLOCK_SIZE);
			dir_inode.indirect_ptr[i] = get_avail_blkno();
			*indirect_blocks = get_avail_blkno();
		    log_msg("Going to add new dir onto a new indirect block entry %d::%d::%d\n", i, dir_inode.indirect_ptr[i], *indirect_blocks);
			dir_inode.vstat.st_blocks += 2;
			add_new_direc(&dirent_dblock, fname, name_len, 0, f_ino, *indirect_blocks);
			update_dir_inode(&dir_inode);
			bio_write(dir_inode.indirect_ptr[i], indirect_blocks);
			free(indirect_blocks);
			return 0;
		}
		indirect_blocks = start_addr_id;
	}
	log_msg("Some issue, cannot find any space to add this dir...\n");
	free(indirect_blocks);
	free(dirent_dblock);
	return -ENOSPC;
}

// Required for 518
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
    struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
    struct dirent *start_addr_d = dirent_dblock;
	int is_removed = 0;
    for (int i = 0; i < max_directs; i++) {
        if (dir_inode.direct_ptr[i] == 0) {
            continue;
        }

        bio_read(dir_inode.direct_ptr[i], dirent_dblock);
        int block_count = 0;
        for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
            if (dirent_dblock->valid == 0) {
                block_count++;
                continue;
            }

            if (!is_removed && dirent_dblock->valid && strncmp(fname, dirent_dblock->name, name_len) == 0 && dirent_dblock->len == name_len) {
                log_msg("Removed %s successfully with block_count %d, j %d, and node at %d\n", dirent_dblock->name, block_count, j, i);
                memset(dirent_dblock, 0, sizeof(struct dirent));
                dirent_dblock -= j;
                bio_write(dir_inode.direct_ptr[i], dirent_dblock);
                dirent_dblock += j;
                is_removed = 1;
                block_count++;
            }
        }

        dirent_dblock = start_addr_d;
        memset(dirent_dblock, 0, BLOCK_SIZE);
        if (is_removed) {
            if (block_count == max_dirent) {
                unset_block(dir_inode.direct_ptr[i], dirent_dblock);
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

    int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
    int *start_addr_id = indirect_blocks;
    for (int i = 0; i < 8; i++) {
        if (dir_inode.indirect_ptr[i] == 0) {
            continue;
        }

        bio_read(dir_inode.indirect_ptr[i], indirect_blocks);
        int outer_blocks = 0;
        for (int j = 0; j < max_indirect; j++, indirect_blocks++) {
            if (*indirect_blocks == 0) {
                outer_blocks++;
                continue;
            }

            if (is_removed) {
                continue;
            }

            bio_read(*indirect_blocks, dirent_dblock);
            int inner_blocks = 0;
            for (int k = 0; k < max_dirent; k++, dirent_dblock++) {
                if (dirent_dblock->valid == 0) {
                    inner_blocks++;
                    continue;
                }

                if (!is_removed && dirent_dblock->valid && strncmp(fname, dirent_dblock->name, name_len) == 0 && dirent_dblock->len == name_len) {
                    log_msg("Removed %s successfully with block_count %d, j %d, and node at %d\n", dirent_dblock->name, inner_blocks, j, i);
                    memset(dirent_dblock, 0, sizeof(struct dirent));
                    dirent_dblock -= k;
                    bio_write(*indirect_blocks, dirent_dblock);
                    dirent_dblock += k;
                    is_removed = 1;
                    inner_blocks++;
                }
            }

            dirent_dblock = start_addr_d;
            memset(dirent_dblock, 0, BLOCK_SIZE);
            if (is_removed && inner_blocks == max_dirent) {
                unset_block(*indirect_blocks, dirent_dblock);
                *indirect_blocks = 0;
                dir_inode.vstat.st_blocks--;
                outer_blocks++;
            }
        }

        indirect_blocks = start_addr_id;
        memset(indirect_blocks, 0, BLOCK_SIZE);
        if (is_removed) {
            if (outer_blocks == max_indirect) {
                unset_block(dir_inode.indirect_ptr[i], indirect_blocks);
                dir_inode.indirect_ptr[i] = 0;
                dir_inode.vstat.st_blocks--;
            }

            struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
            *upd_inode = dir_inode;
            upd_inode->size -= sizeof(struct dirent) ;
            upd_inode->vstat.st_size = upd_inode->size;
            time(&upd_inode->vstat.st_mtime);
            writei(upd_inode->ino, upd_inode);
            free(upd_inode);
            free(indirect_blocks);
            free(dirent_dblock);
            return 0;
        }
    }
    free(indirect_blocks);
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
    log_msg("get_node_by_path::%d::%d::%s::%d\n", ino, inode->valid, path, inode->vstat.st_blocks);
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
	v_stat->st_nlink = root_node->link = 1;
	root_node->direct_ptr[0] = block_no;
	v_stat->st_mode = root_node->type = S_IFDIR | mode;
	v_stat->st_size = root_node->size = sizeof(struct dirent)*2;
	v_stat->st_blocks = 1;
	v_stat->st_blksize = BLOCK_SIZE;
	v_stat->st_uid = getuid();
	v_stat->st_gid = getgid();
	time(&v_stat->st_mtime);
	root_node->vstat = *v_stat;
	log_msg("add_dir_enrty::Verify vstate %d::%d\n", root_node->vstat.st_ino, root_node->vstat.st_blocks);
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

void set_block_count(char *local_time) {
    bitmap_t curr_block = block_bm;
    total_inode_count = 0;
    total_block_count = 1;
    for (int i = 0; i < max_dnum; i++, curr_block++) {
        total_block_count += __builtin_popcount(*curr_block);
    }
    curr_block = inode_bm;
    for (int i = 0; i < max_inum; i++, curr_block++) {
        total_inode_count += __builtin_popcount(*curr_block);
    }
    total_block_count += (int)(total_inode_count/max_inum);
    if (local_time) {
        log_stats("%s::TOTAL BLOCKS INUSE AT OPEN %d\n", local_time, total_block_count);
        log_stats("%s::TOTAL INODES INUSE AT OPEN %d\n", local_time, total_inode_count);
    }
}

void set_end_count(char *local_time) {
    int old_block_count = total_block_count;
    int old_inode_count = total_inode_count;
    set_block_count(NULL);
    log_stats("%s::TOTAL BLOCKS ISUSE AT CLOSE %d\n", local_time, old_block_count);
	if (total_block_count < old_block_count) {
	   log_stats("%s::BLOCKS USAGE HAS REDUCED BY %d\n", local_time, old_block_count - total_block_count);
	} else if (total_block_count > old_block_count) {
	   log_stats("%s::BLOCKS USAGE HAS INCREASED BY %d\n", local_time, total_block_count - old_block_count);
	}
	log_stats("%s::TOTAL INODES ISUSE AT CLOSE %d\n", local_time, old_inode_count);
    if (total_inode_count < old_inode_count) {
    log_stats("%s::INODES USAGE HAS REDUCED BY %d\n", local_time, old_inode_count - total_inode_count);
    } else if (total_inode_count > old_inode_count) {
    log_stats("%s::INODES USAGE HAS INCREASED BY %d\n", local_time, total_inode_count - old_inode_count);
    }
}

/*
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    s_block = (struct superblock*)malloc(BLOCK_SIZE);
    inode_bm = (bitmap_t)malloc(BLOCK_SIZE);
	block_bm = (bitmap_t)malloc(BLOCK_SIZE);
	logfile = fopen(logfile_path, "w");
	if (logfile == NULL) {
	   perror("LOG FILE CREATION ERROR!!");
	   exit(-1);
	}

    setvbuf(logfile, NULL, _IOLBF, 0);
	time_t rawtime;
    time(&rawtime);
    char local_time[1000];
    strftime(local_time, sizeof local_time, "%A, %B %d %Y", localtime(&rawtime));
	log_stats("%s::OPENING DISKFILE %s\n", local_time, diskfile_path);
	if (dev_open(diskfile_path) == -1) {
        rufs_mkfs();
	    time(&rawtime);
		strftime(local_time, sizeof local_time, "%A, %B %d %Y", localtime(&rawtime));
	    log_stats("%s::CREATING NEW DISKFILE\n", local_time);
		set_block_count(local_time);
        return NULL;
    }

	bio_read(0, s_block);
    bio_read(s_block->i_bitmap_blk, inode_bm);
    bio_read(s_block->d_bitmap_blk, block_bm);
    set_block_count(local_time);
	return NULL;
}

static void rufs_destroy(void *userdata) {
    time_t rawtime;
    time(&rawtime);
    char local_time[1000];
    strftime(local_time, sizeof local_time, "%A, %B %d %Y", localtime(&rawtime));
	set_end_count(local_time);
	log_stats("%s::CLOSING DISKFILE %s\n", local_time, diskfile_path);
	fclose(logfile);
	bio_write(0, s_block);
    bio_write(s_block->i_bitmap_blk, inode_bm);
    bio_write(s_block->d_bitmap_blk, block_bm);
    free(block_bm);
    free(inode_bm);
    free(s_block);
    dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	log_msg("GET_ATTR::%s\n", path);

	struct inode data_ino;
	memset(&data_ino, 0, sizeof(struct inode));
	if (get_node_by_path(path, 0, &data_ino)) {
	   return -ENOENT; // Automatically called prior to creating a new file or direc...
	}
	log_msg("path inode::%d with mode %d\n", data_ino.ino, data_ino.vstat.st_mode);
    *stbuf = data_ino.vstat;
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	log_msg("OPEN_PATH::%s\n", path);

	struct inode dir_inode;
	memset(&dir_inode, 0, sizeof(struct inode));
	if(get_node_by_path(path, 0, &dir_inode) == 0 && dir_inode.valid) {
	   return 0;
	}
    return -1;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	log_msg("READ_PATH::%s\n", path);

	struct inode dir_inode;
	memset(&dir_inode, 0, sizeof(struct inode));
	if (get_node_by_path(path, 0, &dir_inode) || !dir_inode.valid) {
	   return 0;
	}

    struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
    struct dirent *start_addr_d = dirent_dblock;
    for (int i = 0; i < max_directs; i++) {
        if (dir_inode.direct_ptr[i] == 0) {
            continue;
        }

        log_msg("READ_DIR from direct block::%d\n", dir_inode.direct_ptr[i]);
        bio_read(dir_inode.direct_ptr[i], dirent_dblock);
        for (int j = 0; j < max_dirent; j++, dirent_dblock++) {
            struct inode contents;
            memset(&contents, 0, sizeof(struct inode));
            readi(dirent_dblock->ino, &contents);
            if (dirent_dblock->valid) {
                filler(buffer, dirent_dblock->name, &contents.vstat, 0);
            }
        }
        dirent_dblock = start_addr_d;
    }

    int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
    int *start_addr_id = indirect_blocks;
    for (int i = 0; i < 8; i++) {
        if (dir_inode.indirect_ptr[i] == 0) {
            continue;
        }

        log_msg("READ_DIR from indirect block::%d\n", dir_inode.indirect_ptr[i]);
        bio_read(dir_inode.indirect_ptr[i], indirect_blocks);
        for (int j = 0; j < max_indirect; j++, indirect_blocks++) {
            if (*indirect_blocks == 0) {
                continue;
            }

            log_msg("READ_DIR from indirect block %d at %d\n", dir_inode.indirect_ptr[i], *indirect_blocks);
            bio_read(*indirect_blocks, dirent_dblock);
            for (int k = 0; k < max_dirent; k++, dirent_dblock++) {
                struct inode contents;
                memset(&contents, 0, sizeof(struct inode));
                readi(dirent_dblock->ino, &contents);
                if (dirent_dblock->valid) {
                    filler(buffer, dirent_dblock->name, &contents.vstat, 0);
                }
            }
            dirent_dblock = start_addr_d;
        }
        indirect_blocks = start_addr_id;
    }
    log_msg("EXIT_READ::%s\n", path);
    free(indirect_blocks);
    free(dirent_dblock);
	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {
    char *parent_m = strdup(path);
    char *base_m = strdup(path);
    char *parent = dirname(parent_m);
    char *base = basename(base_m);
    log_msg("MKDIR\tParent::%s, Base::%s\n", parent, base);

    struct inode p_inode;
    memset(&p_inode, 0, sizeof(struct inode));
    if(get_node_by_path(parent, 0, &p_inode)) {
        free(base_m);
        free(parent_m);
        return -ENOTDIR; // safety net
    }

    int f_no = get_avail_ino();
    log_msg("Attempting to add new dir, parent_ino::%d, dir_ino::%d, base::%s, base_len::%d\n", p_inode.ino, f_no, base, strlen(base));
    p_inode.link++; // ".." entry link added
    p_inode.vstat.st_nlink++;
    int result = dir_add(p_inode, f_no, base, strlen(base));
    if (result != 0) {
        free(base_m);
        free(parent_m);
        return result == -ENOSPC ? -ENOSPC : -EISDIR;
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
    memset(&p_inode, 0, sizeof(struct inode));
    memset(&c_inode, 0, sizeof(struct inode));
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

    p_inode.link--; // ".." entry link removed
    p_inode.vstat.st_nlink--;
    // Only the first 2 enties "." and ".." has to be deleted, so we can ignore indirect
    struct dirent *dirent_dblock = (struct dirent *)malloc(BLOCK_SIZE);
    memset(dirent_dblock, 0, BLOCK_SIZE);
    for (int i = 0; i < max_directs && c_inode.vstat.st_blocks; i++) {
        if (c_inode.direct_ptr[i] == 0) {
            continue;
        }

        unset_block(c_inode.direct_ptr[i], dirent_dblock);
        c_inode.vstat.st_blocks--;
	}

    if (c_inode.vstat.st_blocks) {
        int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
        int *startaddr_id = indirect_blocks;
        for (int i = 0; i < 8 && c_inode.vstat.st_blocks; i++) {
            if (c_inode.indirect_ptr[i] == 0) {
                continue;
            }

            bio_read(c_inode.indirect_ptr[i], indirect_blocks);
            for (int j = 0; j < max_indirect &&  c_inode.vstat.st_blocks; j++) {
                unset_block(*indirect_blocks, dirent_dblock);
                c_inode.vstat.st_blocks--;
            }
            indirect_blocks = startaddr_id;
            memset(indirect_blocks, 0, BLOCK_SIZE);
            unset_block(c_inode.indirect_ptr[i], indirect_blocks);
            c_inode.vstat.st_blocks--;
    	}
        free(indirect_blocks);
    }

    struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
    memset(upd_inode, 0, sizeof(struct inode));
    unset_inode(c_inode.ino, upd_inode);
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
	memset(&p_inode, 0, sizeof(struct inode));
	if(get_node_by_path(parent, 0, &p_inode)) {
        free(base_m);
        free(parent_m);
        return -ENOTDIR; // safety net
    }

	int f_no = get_avail_ino();
	int result = dir_add(p_inode, f_no, base, strlen(base));
    if (result != 0) {
        free(base_m);
        free(parent_m);
        return result == -ENOSPC ? -ENOSPC : -EEXIST;
    }

	log_msg("Adding new file entry, ino::%d, parent_ino::%d, block_no::%d\n", f_no, p_inode.ino);
    struct inode *root_node = (struct inode*)malloc(sizeof(struct inode));
    struct stat* v_stat = (struct stat *)malloc(sizeof(struct stat));
    memset(root_node, 0, sizeof(struct inode));
    memset(v_stat, 0, sizeof(struct stat));
    v_stat->st_ino = root_node->ino = f_no;
    v_stat->st_blksize = BLOCK_SIZE;
    root_node->valid = 1;
    v_stat->st_uid = getuid();
	v_stat->st_gid = getgid();
    v_stat->st_nlink = root_node->link = 1;
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
    memset(&p_inode, 0, sizeof(struct inode));
	if(get_node_by_path(path_m, 0, &p_inode)) {
        free(path_m);
        return -1;
    }

	free(path_m);
	return 0;
}

void update_inode(struct inode *p_inode) {
    struct inode *upd_inode = (struct inode *)malloc(sizeof(struct inode));
	*upd_inode = *p_inode;
	time(&upd_inode->vstat.st_mtime);
	log_msg("Updating inode %d of size %d\n", p_inode->ino, p_inode->size);
	writei(p_inode->ino, upd_inode);
	free(upd_inode);
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    char *path_m = strdup(path);
    log_msg("READ\tPath::%s\n", path_m);

	struct inode p_inode;
	memset(&p_inode, 0, sizeof(struct inode));
	if (get_node_by_path(path_m, 0, &p_inode)) {
        free(path_m);
        return 0;
    }

	char *read_buffer = (char*)malloc(BLOCK_SIZE);
	char *r_main = read_buffer;
	int start_block = (offset / BLOCK_SIZE);
	int start_addr = offset % BLOCK_SIZE;
	int read_size = size;
	log_msg("Going to read buffer of size %d from offset %d\n", size, offset);
	for (int i = start_block; i < max_directs && read_size; i++) {
		if (p_inode.direct_ptr[i] == 0) {
		  continue;
		}

		log_msg("Read from direct block::%d\n", p_inode.direct_ptr[i]);
	    bio_read(p_inode.direct_ptr[i], read_buffer);
		int j = i == start_block ? start_addr : 0;
		for (; j < BLOCK_SIZE && read_size; j++, buffer++, read_buffer++, read_size--) {
		    *buffer = *read_buffer;
		}
	    read_buffer = r_main;
		memset(read_buffer, 0, BLOCK_SIZE);
	}

	if (read_size == 0) { // Why bother reading from indirect if direct was enough?
    	log_msg("Finished reading %d into buffer %d\n", size, size - read_size);
	    update_inode(&p_inode);
    	free(read_buffer);
    	free(path_m);
    	return (size - read_size);
	}

	int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
    int *start_addr_id = indirect_blocks;
    for (int i = 0; i < 8 && read_size; i++) {
        if (start_block > (max_directs + ((i+1)*max_indirect))) {
            continue; // if start_block is greater than next indirect, don't read from this iter
        }

		if (p_inode.indirect_ptr[i] == 0) {
		    continue;
		}

		log_msg("Read from indirect block::%d\n", p_inode.indirect_ptr[i]);
		bio_read(p_inode.indirect_ptr[i], indirect_blocks);
		int j = start_block < (max_directs + ((i+1)*max_indirect)) ? (start_block - max_directs - (i*max_indirect)) : 0;
		indirect_blocks += j;
		for (; j < max_indirect && read_size; j++, indirect_blocks++) {
		    if (*indirect_blocks == 0) {
				continue;
			}

			log_msg("Read from indirect block %d at %d\n", p_inode.indirect_ptr[i], *indirect_blocks);
		    int k = start_block == (max_directs + (i*max_indirect) + j) ? start_addr : 0;
			log_msg("READ::Is the start block correct?? start_block::%d, k::%d, rhs::%d\n", start_block, k, max_directs + (i*max_indirect) + j);
			bio_read(*indirect_blocks, read_buffer);
    		for (; k < BLOCK_SIZE && read_size; k++, buffer++, read_buffer++, read_size--) {
    		    *buffer = *read_buffer;
    		}
            read_buffer = r_main;
            memset(read_buffer, 0, BLOCK_SIZE);
		}
		indirect_blocks = start_addr_id;
		memset(indirect_blocks, 0, BLOCK_SIZE);
	}
    log_msg("Finished reading %d into buffer %d\n", size, size - read_size);
	update_inode(&p_inode);
	free(indirect_blocks);
	free(read_buffer);
	free(path_m);
	return (size - read_size);
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    char *path_m = strdup(path);
    log_msg("WRITE\tPath::%s\n", path_m);

	struct inode p_inode;
	memset(&p_inode, 0, sizeof(struct inode));
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
	int is_new = 0;
	for (int i = start_block; i < max_directs && write_size; i++) {
	    is_new = 0;
		if (p_inode.direct_ptr[i] == 0) {
            p_inode.direct_ptr[i] = get_avail_blkno();
            p_inode.vstat.st_blocks++;
            is_new = 1;
            memset(write_buffer, 0, BLOCK_SIZE);
		}

		log_msg("Write onto direct block::%d\n", p_inode.direct_ptr[i]);
		if (is_new) {
	       bio_read(p_inode.direct_ptr[i], write_buffer);
		}
		int j = i == start_block ? start_addr : 0;
		int old_size = write_size;
		log_msg("Writing message into block %d at index %d, size remaining %d\n", p_inode.direct_ptr[i], i, write_size);
		for (; j < BLOCK_SIZE && write_size; j++, buffer++, write_buffer++, write_size--) {
		    *write_buffer = *buffer;
		}

		write_buffer = w_main;
		if (write_size != old_size) {
		    log_msg("Wrote message of size %d at block %d\n", old_size-write_size, p_inode.direct_ptr[i]);
			bio_write(p_inode.direct_ptr[i], write_buffer);
		}
	}

	if (write_size == 0) { // Why bother writing into indirect if direct was enough?
	    if ((offset + size - write_size) > p_inode.size) { // No need to increase size if writing buffer over the same buffer
            p_inode.size = p_inode.vstat.st_size = (offset + size - write_size);
        }
    	log_msg("Data of size %d has been written at offset %d. Total file size is %d.\n", (size - write_size), offset, p_inode.vstat.st_size);
    	update_inode(&p_inode);
    	free(write_buffer);
    	free(path_m);
    	return (size - write_size);
	}

	log_msg("Going to start writing in indirect_ptr from addr %d\n", start_block);
	int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
    int *start_addr_id = indirect_blocks;
    for (int i = 0; i < 8 && write_size; i++) {
        int is_new_ptr = 0;
        if (start_block > (max_directs + ((i+1)*max_indirect))) {
            continue; // if start_block is greater than next indirect, don't write on this iter
        }

		if (p_inode.indirect_ptr[i] == 0) {
		    p_inode.indirect_ptr[i] = get_avail_blkno();
            p_inode.vstat.st_blocks++;
		    memset(indirect_blocks, 0, BLOCK_SIZE);
			is_new_ptr = 1;
		}

		log_msg("Write onto indirect block::%d\n", p_inode.indirect_ptr[i]);
		if (!is_new_ptr) {
		    bio_read(p_inode.indirect_ptr[i], indirect_blocks);
		}
		int is_new = 0;
		int j = start_block < (max_directs + ((i+1)*max_indirect)) ? (start_block - max_directs - (i*max_indirect)) : 0;
		indirect_blocks += j;
		for (; j < max_indirect && write_size; j++, indirect_blocks++) {
		    int l_is_new = 0;
		    if (*indirect_blocks == 0) {
				*indirect_blocks = get_avail_blkno();
				p_inode.vstat.st_blocks++;
				memset(write_buffer, 0, BLOCK_SIZE);
				l_is_new = is_new = 1;
			}

			log_msg("Write onto indirect block %d at %d\n", p_inode.indirect_ptr[i], *indirect_blocks);
		    int k = start_block == (max_directs + (i*max_indirect) + j) ? start_addr : 0;
			log_msg("WRITE::Is the start block correct?? start_block::%d, k::%d, rhs::%d\n", start_block, k, max_directs + (i*max_indirect) + j);
			int old_size = write_size;
			if (!l_is_new) {
			    bio_read(*indirect_blocks, write_buffer);
			}
    		for (; k < BLOCK_SIZE && write_size; k++, buffer++, write_buffer++, write_size--) {
    		    *write_buffer = *buffer;
    		}
            write_buffer = w_main;
            if (old_size != write_size) {
                log_msg("Wrote message of size %d at block %d\n", old_size-write_size, *indirect_blocks);
                bio_write(*indirect_blocks, write_buffer);
            }
		}

		indirect_blocks = start_addr_id;
		if (is_new || is_new_ptr) {
		    bio_write(p_inode.indirect_ptr[i], indirect_blocks);
		}
	}

    if ((offset + size - write_size) > p_inode.size) { // No need to increase size if writing buffer over the same buffer
        p_inode.size = p_inode.vstat.st_size = (offset + size - write_size);
    }
	log_msg("Data of size %d has been written at offset %d. Total file size is %d.\n", (size - write_size), offset, p_inode.vstat.st_size);
	update_inode(&p_inode);
	free(indirect_blocks);
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
    log_msg("UNLINK\tParent::%s, Base::%s\n", parent, base);

    struct inode p_inode, c_inode;
    memset(&p_inode, 0, sizeof(struct inode));
    memset(&c_inode, 0, sizeof(struct inode));
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
    for (int i = 0; i < max_directs && c_inode.vstat.st_blocks; i++) {
        if (c_inode.direct_ptr[i] == 0) {
            continue;
        }

        memset(dirent_dblock, 0, BLOCK_SIZE);
        unset_block(c_inode.direct_ptr[i], dirent_dblock);
        c_inode.vstat.st_blocks--;
	}

    if (c_inode.vstat.st_blocks) {
        int *indirect_blocks = (int*)malloc(BLOCK_SIZE);
        int *start_addr_id = indirect_blocks;
        for (int i = 0; i < 8 && c_inode.vstat.st_blocks; i++) {
            if (c_inode.indirect_ptr[i] == 0) {
                continue;
            }

            bio_read(c_inode.indirect_ptr[i], indirect_blocks);
            for (int j = 0; j < max_indirect && c_inode.vstat.st_blocks; j++, indirect_blocks++) {
                if (*indirect_blocks == 0) {
                    continue;
                }

                memset(dirent_dblock, 0, BLOCK_SIZE);
                unset_block(*indirect_blocks, dirent_dblock);
                c_inode.vstat.st_blocks--;
            }
            indirect_blocks = start_addr_id;
            memset(indirect_blocks, 0, BLOCK_SIZE);
            unset_block(c_inode.indirect_ptr[i], indirect_blocks);
            c_inode.vstat.st_blocks--;
        }
        free(indirect_blocks);
    }

    struct inode *upd_inode = (struct inode*)malloc(sizeof(struct inode));
    memset(upd_inode, 0, sizeof(struct inode));
    unset_inode(c_inode.ino, upd_inode);
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
	getcwd(logfile_path, PATH_MAX);
	strcat(logfile_path, "/rufs_stats.log");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}
