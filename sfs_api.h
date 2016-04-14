#ifndef _INCLUDE_SFS_API_H_
#define _INCLUDE_SFS_API_H_

#include <stdint.h>

// TODO does this include the 3 bits or not?
#define MAXFILENAME 20

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
  // These are used because I was unsure of how to define EOFs here
  // Realistically a file could have 8 zero bits in a row, 
  // don't want to kill a search prematurely and risk overwrites
  uint64_t EOF_block; // The block the end of file is living in
  unsigned int EOF_offset; // The offset the end of file is living in
  uint64_t data_ptrs[12];
  // Indirect pointer is a 4 byte pointer to a block filled with inode ptrs
  uint64_t indirect_ptr;
} inode_t;

/*
 * inode    which inode this entry describes
 * rwptr    where in the file to start
 */
typedef struct {
  uint64_t inode;
  uint64_t rwptr;
  //Might be cool to have a block offset and a within block offset...
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
