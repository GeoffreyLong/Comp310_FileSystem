#ifndef _INCLUDE_SFS_API_H_
#define _INCLUDE_SFS_API_H_

#include <stdint.h>

#define MAXFILENAME 20

typedef struct {
    int magic;
    int block_size;
    int fs_size;
    int inode_table_len;
    int root_dir_inode;
} superblock_t;

typedef struct {
    int mode;
    int link_cnt;
    int uid;
    int gid;
    int size;
    int data_ptrs[12];
    int indirect_ptr;
} inode_t;

/*
 * inode    which inode this entry describes
 * rwptr    where in the file to start
 */
typedef struct {
    int inode;
    int rwptr;
} file_descriptor;

// Very simple mapping from filename to inode
// Don't care about performance so can just iterate over all files in dir
typedef struct {
  char* filename;
  int inode;
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
