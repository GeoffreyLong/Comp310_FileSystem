// NOTES
// Simple File system has the following structure
//    Super Block - I Node Table - Data blocks - Free Bitmap
//    Super Block (fields of 4 bytes each)
//        Magic (0xACBD0005)
//        Block Size (typically 1024)
//        File System Size (in blocks)
//        I-node table length (in blocks)
//        Root directory (i-Node number)
//            Root directory is pointed to by an i-Node which is pointed to by super block
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
//            Mode: file type, how the owner and group and others can access
//            Link count: How many hard links point to the inode (one in this case)
//            UID: User id
//            GID: Group id
//            Size: size in bytes
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
#include <math.h>
#include "disk_emu.h"
#include <inttypes.h>
int seen = 0;

#define MY_DISK "sfs_disk.disk"
#define BLOCK_SZ 1024
#define NUM_BLOCKS 100  //TODO: increase
#define NUM_INODES 10   //TODO: increase

// TODO get ceil to work
#define FREE_BITMAP_SIZE ((NUM_BLOCKS+8-1) / 8)
#define FREE_BITMAP_BLOCKS ((FREE_BITMAP_SIZE + BLOCK_SZ - 1) / BLOCK_SZ)

// TODO verify that this is right...
#define NUM_INODE_BLOCKS (sizeof(inode_t) * NUM_INODES / BLOCK_SZ + 1) 
// TODO define num root directory blocks
#define NUM_ROOTDIR_BLOCKS 1
#define DEBUG 1

superblock_t sb;
inode_t inode_table[NUM_INODES];

file_descriptor fd_table[NUM_INODES];

file_map root_directory[NUM_INODES]; 

// Use a type that only takes up a single byte for ease of use
// Originally had unsigned chars but uint_8 better demonstrates intent
uint8_t free_space_bitmap[FREE_BITMAP_SIZE];

char* previousFileName;

// TODO might want to mark in this method... 
// I can't think of when you would get a free block if it isn't being used
int getNextFreeBlock(){
  int freeIndex = 0;
  for (int i = 0; i < FREE_BITMAP_SIZE; i++){
    uint8_t val = free_space_bitmap[i];
    if (val != 0){
      // 7 because I use uint_8
      for (int j = 7; j >= 0; j --){
        // If the jth bit is a 1 and the freeIndex is valid then return the value
        if ((val & 1<<j) >> j && freeIndex <= NUM_BLOCKS) return freeIndex;
        freeIndex += 1;
      }
    }
    else{
      // Since 8 bit
      freeIndex += 8;
    }

  }

  // If you cannot locate a free block return -1
  return -1;
}



// TODO error handling
int mark_block_at(int blockIndex, int free){
  // The map index is the block index divided by the 8 bit size
  int mapIndex = blockIndex / 8;
  // Get the specific bit
  int mapSub = blockIndex % 8;


  // Modify the location according to free flag
  uint8_t mask = 1 << mapSub;
  if (free == 1){
    // OR with the mask to default the index to 1
    free_space_bitmap[mapIndex] |= mask;
  }
  else {
    // AND with the masks complement (and with 0 at index) to default index to 0
    free_space_bitmap[mapIndex] &= ~mask;
  }
  
  // Write the free map table back to disk
  // Might not be the most efficient, but it seems to work
  write_blocks(NUM_BLOCKS-FREE_BITMAP_BLOCKS, FREE_BITMAP_BLOCKS, &free_space_bitmap);
  
  return 0;
}

int write_blocks_plus_mark(int start_address, int nblocks, void *buffer){
  // Write the buffer
  write_blocks(start_address, nblocks, buffer);
  
  // Mark each block 
  for (int i = 0; i < nblocks; i++){
    mark_block_at(start_address + i, 0);
  }
  
  return 0;
}

// Get the inode number from the root directory using the name
uint64_t get_inode_from_name(char* name){
  // Iterate over the entire directory in memory.
  // If a file exists by that name, return its inode number
  for (int i = 0; i < NUM_INODES; i ++){
    file_map curFile = root_directory[i];

    // If curFile has a null name or inode then clearly invalid
    if (curFile.filename == NULL || curFile.inode <= 0) continue;


    if (strncmp(name, curFile.filename,MAXFILENAME) == 0) return curFile.inode;
    if (DEBUG) printf("Comparing %s,%s\n", name, curFile.filename);
  }

  // If no file found then return -1
  return -1;
}


uint64_t create_inode(){
  for (int i = 0; i < NUM_INODES; i ++){
    // Overloading one of the fields... typically considered bad practice
    // If mode is -1 then it is empty
    if (inode_table[i].mode == -1){
      // Set some parameters, not sure what to set UID or GID to
      inode_table[i].mode = 0;
      inode_table[i].link_cnt = 0;
      inode_table[i].size = 0;
      // Get the next free block and mark it as taken
      // Again, might not be the most efficient implementation
      // TODO actually, should probably put this as -1...
      // Set when I have something to write
      int nextFree = getNextFreeBlock();
      free_space_bitmap[nextFree] = 0;
      inode_table[i].EOF_block = nextFree; 
      inode_table[i].EOF_offset = 0;
      // Return the index of the inode
      return i;
    }
  }

  return -1;
}


uint64_t getRWPosition(int iNodeNum){
  return 0;  

}


/* I don't think this is necessary... just indexing off inode number
uint64_t get_next_fd(){
  for (int i = 0; i < NUM_INODES; i ++){
    // Overloading one of the fields... typically considered bad practice
    // If inode is -1 then it is empty
    if (fd_table[i].inode == -1){
      // Return the index of the inode
      return i;
    }
  }

  return -1;
}
*/

void init_superblock() {
  sb.magic = 0xACBD0005;
  sb.block_size = BLOCK_SZ;
  sb.fs_size = NUM_BLOCKS * BLOCK_SZ;
  sb.inode_table_len = NUM_INODE_BLOCKS;
  sb.root_dir_inode = 0;
}


//////////////////////////////////////////////////////////////////////////////
///////////////////////// API METHODS //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
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

    // In disk emu
    init_fresh_disk(MY_DISK, BLOCK_SZ, NUM_BLOCKS);

    // Free block list
    // Clear the bitmap
    // could probs declare this statically above
    // all of the params known
    for (int i = 0; i < FREE_BITMAP_SIZE; i++){
      free_space_bitmap[i] = 255;
    }
    // Set all of these for my naive overloading of mode field
    for (int i = 0; i < NUM_INODES; i++){
      inode_table[i].mode = -1;
      // Set this to be the first spot after the i-nodes
      inode_table[i].EOF_block = 1+sb.inode_table_len+NUM_ROOTDIR_BLOCKS;
      inode_table[i].EOF_offset = 0;
    }
    // Set all of these for my naive overloading of inode field
    for (int i = 0; i < NUM_INODES; i++){
      fd_table[i].inode = -1;
    }


    // write super block
    write_blocks_plus_mark(0, 1, &sb);
    
    // Create root directory
    inode_table[sb.root_dir_inode].mode = 0;
    // Is size the size of the table?
    //inode_table[sb.root_dir_inode].size = 
    // Should write this out to blocks
    write_blocks_plus_mark(1+sb.inode_table_len, NUM_ROOTDIR_BLOCKS, root_directory);
    
    // write inode table
    // TODO figure out this inode stuff
    write_blocks_plus_mark(1, sb.inode_table_len, inode_table);


  } 
  else {
    // File system is opened from disk
    // TODO getting out of bound errors
    // Might have to do some buffer stuff in here
    printf("reopening file system\n");
    // open super block
    read_blocks(0, 1, &sb);
    printf("Block Size is: %lu\n", sb.block_size);
    // open inode table
    read_blocks(1, sb.inode_table_len, inode_table);

    // open free block list
    //read_blocks(NUM_BLOCKS-FREE_BITMAP_BLOCKS, FREE_BITMAP_BLOCKS, &free_space_bitmap);
    //
    // read the root directory in
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
// Create a file (part of the open() call)
//    Allocate and init an inode
//        Need to somehow remember state of inode table to find which inode
//        Can't use contiguous, will have holes
//    Write mapping between i-node and file name in root dir
//        Simply update memory and disk copies
//    No disk data block allocated (size set to 0)
//    Can also "open" the file for transactions (r/w)



  // See if the name exceeds the max
  // The name passed in will include the extension I believe
  //      i.e. some_name.txt
  if (DEBUG) printf("\nOpening %s \n", name);  
  if (strlen(name) >= MAXFILENAME+1) return -1;

  // Find the file in the file map
  //    This will return an inode if there is a file, -1 otherwise
  uint64_t iNodeNum = get_inode_from_name(name);

  // Create the file if it doesn't already exist
  if (iNodeNum == -1){
    if (DEBUG) printf("No file found, creating one \n");

    // Need to create an inode, root_directory entry, and 
    // Method will create an inode at the next available slot
    iNodeNum = create_inode();

    // Perhaps could do this this way
    // Rather naive but good for now
    root_directory[iNodeNum].filename = name;
    root_directory[iNodeNum].inode = iNodeNum;

    if (DEBUG) printf("File created at inode %d \n", iNodeNum);
  }
  else{
    // TODO check to see if the file is already open???
    // Done implicitly
  }


  // Get the next available fd index and set the fields
  // fd indexes are just going to be the inode numbers for now
  uint64_t fd_idx = iNodeNum;

  // If the file was not already open
  if (fd_table[fd_idx].inode == -1){
    // Set the inode number to be the proper inode
    //    I-node number is the one corresponding to the file
    fd_table[fd_idx].inode = iNodeNum;
  }

  // Set the RW pointers
  // If the inode is new, the RW pointers have an offset of 0
  // Else set it according to the inode EOF block and offset 
  if (inode_table[iNodeNum].EOF_block == -1){
    fd_table[fd_idx].rwptr = 0;
  }
  else{
    fd_table[fd_idx].rwptr = inode_table[iNodeNum].EOF_block * BLOCK_SZ 
                            + inode_table[iNodeNum].EOF_offset;
  }


  if (DEBUG) printf("Returning FD %d\n", fd_idx);

	return fd_idx;
}

int sfs_fclose(int fileID){
  // Closes a file
  // Removes the entry from the open file descriptor table

  fd_table[0].inode = 0;
  fd_table[0].rwptr = 0;
	return 0;
}

int sfs_fread(int fileID, char *buf, int length){

  file_descriptor* f = &fd_table[fileID];
  inode_t* n = &inode_table[f->inode];

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


  // IMPORTANT: Don't forget to maintain the EOFs in the inode


  // Allocate disk blocks (mark them as allocated in free block list)

  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fileID];
  
  // Get the current file location to write to based on the RWptr
  int rwOffset = fd->rwptr;
  
  // Get the current block pointed to by the RW pointer
  int blockOffset = rwOffset / BLOCK_SZ;

  // Want to get the current data page pointed to by the rwptr 
  uint64_t curDataPageIdx;
  if (blockOffset < 12){
    if (DEBUG) printf("Acquiring data page from direct pointers \n");
    // If it is a direct pointer then the corresponding block can be read directly
    curDataPageIdx = inode->data_ptrs[blockOffset]; 
  }
  else{
    if (DEBUG) printf("Acquiring data page from indirect pointers \n");
    // Indirect pointers
    // The indirect pointer will be a pointer to a block in memory
    // This block will be filled with contiguous pointers to data pages
    uint64_t indirPtr = inode->indirect_ptr;
    char *pointerPage = calloc(1,BLOCK_SZ);
    
    // If the indirect ptr hasn't been set up yet, then this will trigger
    // The pointer page itself will just be empty
    if (indirPtr <=0){
      if (DEBUG) printf("No indirect found, creating new indirect for inode %d \n", fileID);

      // Get the next free block and set the indirect pointer to be this location
      indirPtr = getNextFreeBlock();
      inode->indirect_ptr = indirPtr;

      // This only gets the pointer page though
      // Need to set up a data page as well
      curDataPageIdx = getNextFreeBlock();
      // Set the first index in the pointer page to be the current data page index
      pointerPage[0] = curDataPageIdx;
      //TODO write this back out as block
    }
    else{
      if (DEBUG) printf("Indirect pointer found");
      
      // Read index page from memory
      uint64_t *pointerPage = calloc(1,BLOCK_SZ);
      read_blocks(indirPtr, 1, (void*) pointerPage);
      
      
      // from 0 to the block size, iterating in 8 byte increments (64 bits)
      // NOTE this might be too many bytes, seems unnecessary 
      // Iterate through pointer page to find first empty block
      int i;
      for (i = 0; i < BLOCK_SZ/8; i += 1){ // TODO might want to error handle this size
        if (pointerPage[i] == 0){
          curDataPageIdx = getNextFreeBlock();
          if (DEBUG) printf("Found new pointer slot for page %d \n", curDataPageIdx);
          pointerPage[i] = curDataPageIdx;
          break;
        }
      }
      // If i iterates all the way to block size, then the pointer page is full
      if (i == BLOCK_SZ){
        if (DEBUG) printf("Unable to find free data pointer on inode \n");
      }
    }
  }


  // fileOffset is the byte location within the current file
  int fileOffset = rwOffset % BLOCK_SZ;
  // This is the location within the buffer (how far through the data we are)
  int bufferIdx = 0;
  // There is a page that is partially filled, fill it the rest of the way
  if (!rwOffset == 0){
    if (DEBUG) printf("Filling up remainder of partially filled page \n");
    // read block from current page
    char *curDataPage = calloc(1,BLOCK_SZ);
    read_blocks(curDataPageIdx, 1, (char*) curDataPage);

    for (fileOffset; fileOffset < BLOCK_SZ; fileOffset ++){
      curDataPage[fileOffset] = buf[bufferIdx];
      fileOffset += 1;
    }
    write_blocks_plus_mark(curDataPageIdx, 1, &curDataPage);
    curDataPageIdx = getNextFreeBlock();
  }
  
  
  while(fileOffset != length){
    printf("Writing to file");
    char *curDataPage = calloc(1,BLOCK_SZ);
    // memcpy didn't want to work
    for (int i = 0; i < BLOCK_SZ; i ++){
      curDataPage[i] = buf[fileOffset];
      fileOffset += 1;
    }
    write_blocks_plus_mark(curDataPageIdx, 1, &curDataPage);
    curDataPageIdx = getNextFreeBlock();
    break;
  }


  //int tempLength = length;
  //while (tempLength > 0){
  //}
  

  int block = 20;
  inode->data_ptrs[0] = block;
  inode->size += length;
  fd->rwptr += length;
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
    fd_table[fileID].rwptr = loc;
	return 0;
}

int sfs_remove(char *file) {
  // Removes the file from the directory entry
  // Releases the file allocation entries 
  // Releases the data blocks used by the file 
  //    So they can be used by new files in the future
	return 0;
}
