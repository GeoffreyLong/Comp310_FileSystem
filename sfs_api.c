// NOTES
// Simple File system has the following structure
//    Super Block - I Node Table - Data blocks - Free Bitmap
//    Super Block (fields of 4 bytes each)
//        Magic (0xACBD0005)
//        Block Size (typically 1024)
//        File System Size (in blocks)
//        I-node table length (in blocks)
//        Root directory (i-Node number)
//            Root directory is pointed to by an i-Node which is pointed to by this
//            Directory is a mapping table to convert file name to i-Node
//            Contains at least i-Node and file name
//            File name is limited to 16 chars, extension to 3 chars
//            Directory can span multiple blocks 
//                Will not grow larger than max file size though
//                This max is based on # of inode pointers
//        Unused (probably 1024)
//            If I'm making the block size 1024,
//            perhaps the unused space should be 1024-5(4)=1004 bytes
//    I-nodes are implemented contiguously
//        Otherwise, indexing into the table is complex
//        Disk considered as a bunch of blocks
//        All the meta info (size,mode,ownership) associated with the inode
//        I-node structure
//            Mode
//            Link count
//            UID
//            GID
//            Size
//            Pointer 1
//            ...
//            Pointer 12
//            Indirect Pointer (only single)
//    In Memory data structures
//        Directory table
//            Keeps a copy of the directory block in memory
//            This directory could span several blocks
//            Read the entire directory into memory
//        i-Node cache
//        Free block list (perhaps)
//        Need a file descriptor table (only one process so only need one FD table)
//            An entry created in table when file opened
//                Index of newly opened file is the file descriptor 
//                    Returned by the file opening activity
//            Has i-node number and r/w pointer
//                I-node number is the one corresponding to the file
//                r/w pointer set to end of file (to append)
// Six basic file operations
//    Whenever using, first step is to find appropriate directory entry
//      At least for create, delete, read, write 
//    Create: The file is only the EOF, with the EOF, read, and write pointers all to this
//        sets some attrs
//    Delete: Storage is released to the free pool
//    Open: Process first opens a file
//        Fetch the attrs and list of disk addresses into main mem
//    Close: Frees the internal table space
//        OS enforce by limiting the number of open files
//    Read: comes from the current position in a file
//        Bytes to read and where to put specified
//    Seek: Repositions the file pointer for random access
//        After seeeking data can be read or written
//    Write: Data written to file at current position
//        If current position is at end of file, file's size increases

#include "sfs_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk_emu.h"

int seen = 0;

#define MY_DISK "sfs_disk.disk"
#define BLOCK_SZ 1024
#define NUM_BLOCKS 100  //TODO: increase
#define NUM_INODES 10   //TODO: increase

// verify that this is right...
#define NUM_INODE_BLOCKS (sizeof(inode_t) * NUM_INODES / BLOCK_SZ + 1) 

superblock_t sb;
inode_t table[NUM_INODES];

file_descriptor fdt[NUM_INODES];





void init_superblock() {
    sb.magic = 0xACBD0005;
    sb.block_size = BLOCK_SZ;
    sb.fs_size = NUM_BLOCKS * BLOCK_SZ;
    sb.inode_table_len = NUM_INODE_BLOCKS;
    sb.root_dir_inode = 0;
}

void mksfs(int fresh) {
	//Implement mksfs here
  // Formats the virtual disk implemented
  // Creates an instance of the simple file system on top of it
  // Instantiate all the in memory data structures
  // Open file descriptor table, inode cache, disk block cache, root dir cache
  if (fresh) {	
    // File system is created from scratch

    printf("making new file system\n");
    // create super block
    init_superblock();

    // TODO where is this?
    init_fresh_disk(MY_DISK, BLOCK_SZ, NUM_BLOCKS);
    
    /* write super block
     * write to first block, and only take up one block of space
     * pass in sb as a pointer
     */
    write_blocks(0, 1, &sb);

    // write inode table
    write_blocks(1, sb.inode_table_len, table);

    // write free block list
  } 
  else {
    // File system is opened from disk

    printf("reopening file system\n");
    // open super block
    read_blocks(0, 1, &sb);
    printf("Block Size is: %lu\n", sb.block_size);
    // open inode table
    read_blocks(1, sb.inode_table_len, table);

    // open free block list
  }

  return;
}

int sfs_getnextfilename(char *fname) {
  // Copies the name of the next file in the directory into fname
  // Returns a non-zero if there is a new file
  // Once all of the files have been returned, this function returns 0
  // Used to loop over the directory
  // Ensure that the function remembers the current position in the dir at each call
  // Facilitated by the single level directory structure
	return 0;
}


int sfs_getfilesize(const char* path) {
  // Returns the size of a given file

	return 0;
}

int sfs_fopen(char *name) {
  // If file exists
  //    Opens a file and returns the index that corresponds 
  //      to newly opened file in FD table
  //    Opened in append mode (file pointer set to end of file)
  // If not exist, 
  //    creates a new file and sets its size to 0
  //    

  // An entry created in FD table created when file opened
  //    Return the index of that newly opened file (file descriptor) from this method
  // Save the i-node number and r/w pointer here
  //    I-node number is the one corresponding to the file
  //    r/w pointer set to end of file (to append)

// Create a file (part of the open() call)
//    Allocate and init an inode
//        Need to somehow remember state of inode table to find which inode
//        Can't use contiguous, will have holes
//    Write mapping between i-node and file name in root dir
//        Simply update memory and disk copies
//    No disk data block allocated (size set to 0)
//    Can also "open" the file for transactions (r/w)

  //////////
  // Check if file exists
  // if file exists
  //    inodeNum = get i-node number
  // else
  //    inodeNum = create file
  //    set size to 0
  //  
  // create a new file descriptor table entry
  // set the indode to inodeNum
  // set the rwptr to be at the end of file


  uint64_t test_file = 1;

  fdt[test_file].inode = 1;
  fdt[test_file].rwptr = 0;

	return test_file;
}

int sfs_fclose(int fileID){
  // Closes a file
  // Removes the entry from the open file descriptor table

  fdt[0].inode = 0;
  fdt[0].rwptr = 0;
	return 0;
}

int sfs_fread(int fileID, char *buf, int length){

  file_descriptor* f = &fdt[fileID];
  inode_t* n = &table[f->inode];

  int block = n->data_ptrs[0];
  read_blocks(block, 1, (void*) buf);
	return 0;
}

int sfs_fwrite(int fileID, const char *buf, int length){
  // Writes the given number of bytes of buffered data in buf to the open file
  //    Start write at current file pointer
  // Will increase the size of a file by the given number of bytes
  // It may not increase the file size by the number of bytes
  //    If the write pointer is located at a location other than EOF
  //
  // FLOW
  //    Allocate disk blocks (mark them as alloced in free block list)
  //    Modify the file's i-Node to point to these blocks
  //    Write the data the user gives to these blocks
  //    Flush all modifications to disk
  // NOTE: All writes to disk are at block sizes. 
  //    If you are writing a few blocks to a file, might end up writing a block to next
  //    Important to read the last block and set the write pointer to the EOF
  //    Bytes you want to write go to end of previous bytes already part of file
  //    After writing bytes, flush block to disk

  file_descriptor* f = &fdt[fileID];
  inode_t* n = &table[fileID];

    /*
     * We know block 1 is free because this is a canned example
     * You should call a helper function to find a free block
     */

  int block = 20;
  n->data_ptrs[0] = block;
  n->size += length;
  f->rwptr += length;
  write_blocks(block, 1, (void*) buf);

	return 0;
}

int sfs_fseek(int fileID, int loc){
  // Moves the r/w pointer to the given location (nothing to be done on disk)
  //
  // "interesting problem is performing a read or write after moving r/w ptr"
  //    sfs_read and sfs_write both advance the same ptr
  // Could implement with two ptrs?


    /*
     * This is a very minimal implementation of fseek. You should add some type
     * of error checking before you submit this assignment
     */
    fdt[fileID].rwptr = loc;
	return 0;
}

int sfs_remove(char *file) {
  // Removes the file from the directory entry
  // Releases the file allocation entries 
  // Releases the data blocks used by the file 
  //    So they can be used by new files in the future
	return 0;
}
