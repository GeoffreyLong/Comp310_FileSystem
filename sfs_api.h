#ifndef _INCLUDE_SFS_API_H_
#define _INCLUDE_SFS_API_H_

#include <stdint.h>

// TODO does this include the 3 bits or not?
#define MAXFILENAME 16

typedef struct {
    uint64_t magic;
    uint64_t block_size;
    uint64_t fs_size;
    uint64_t inode_table_len;
    uint64_t root_dir_inode;
} superblock_t;

typedef struct {
    unsigned int mode;
    unsigned int link_cnt;
    unsigned int uid;
    unsigned int gid;
    unsigned int size;
    unsigned int data_ptrs[12];
    // TODO indirect pointer
} inode_t;

/*
 * inode    which inode this entry describes
 * rwptr    where in the file to start
 */
typedef struct {
    uint64_t inode;
    uint64_t rwptr;
} file_descriptor;


// Very simple mapping from filename to inode
// Don't care about performance so can just iterate over all files in dir
typedef struct {
  char* filename;
  uint64_t inode;
} file_map;

void mksfs(int fresh);
int sfs_getnextfilename(char *fname);
int sfs_getfilesize(const char* path);
int sfs_fopen(char *name);
int sfs_fclose(int fileID);
int sfs_fread(int fileID, char *buf, int length);
int sfs_fwrite(int fileID, const char *buf, int length);
int sfs_fseek(int fileID, int loc);
int sfs_remove(char *file);

#endif //_INCLUDE_SFS_API_H_
