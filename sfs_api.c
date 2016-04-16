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

//#define MY_DISK "sfs_disk.disk"
#define MY_DISK "sfs_disk.sfs"
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
#define PTR_SIZE (sizeof(uint64_t)) // This is the default size for any pointer to index

superblock_t sb;

//superblock_t sb = {.magic = 0xACBD0005, .block_size = BLOCK_SZ,  .fs_size = NUM_BLOCKS * BLOCK_SZ,
//                      .inode_table_len = NUM_INODE_BLOCKS, .root_dir_inode = 0};

inode_t inode_table[NUM_INODES];

file_descriptor fd_table[NUM_INODES];

file_map root_directory[NUM_INODES]; 

// Use a type that only takes up a single byte for ease of use
// Originally had unsigned chars but uint_8 better demonstrates intent
uint8_t free_space_bitmap[FREE_BITMAP_SIZE] = { [0 ... FREE_BITMAP_SIZE-1] = UINT8_MAX };
char* previousFileName;

/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)


//////////////// MARK BLOCK AS OCCUPIED /////////////////
// From tutorials
uint64_t getNextFreeBlock() {
    uint64_t i = 0;

    // find the first section with a free bit
    // let's ignore overflow for now...
    while (free_space_bitmap[i] == 0) { i++; }

    // now, find the first free bit
    // ffs has the lsb as 1, not 0. So we need to subtract
    uint8_t bit = ffs(free_space_bitmap[i]) - 1;

    // set the bit to used
    USE_BIT(free_space_bitmap[i], bit);
    
    // Questionable position
    write_blocks(NUM_BLOCKS-FREE_BITMAP_BLOCKS, FREE_BITMAP_BLOCKS, &free_space_bitmap);

    //return which bit we used
    return i*8 + bit;
}
// From tutorials
void rm_index(uint32_t index) {

    // get index in array of which bit to free
    uint32_t i = index / 8;

    // get which bit to free
    uint8_t bit = index % 8;

    // free bit
    FREE_BIT(free_space_bitmap[i], bit);
}







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




//////////////////// GET INODE FROM NAME /////////////////////
// Get the inode number from the root directory using the name
uint64_t get_inode_from_name(char* name){
  // Iterate over the entire directory in memory.
  // If a file exists by that name, return its inode number
  for (int i = 0; i < NUM_INODES; i ++){
    file_map curFile = root_directory[i];

    // If curFile has a null name or inode then clearly invalid
    if (curFile.filename == NULL || curFile.inode <= 0) continue;


    // Compare the two strings, return the inode if there is a match
    if (strncmp(name, curFile.filename, MAXFILENAME) == 0) return curFile.inode;
    if (DEBUG) printf("Comparing %s,%s\n", name, curFile.filename);
  }

  // If no file found then return -1
  return -1;
}


//////////////////// CREATE AN INODE ////////////////////
// These already exist in memory, so don't need to get next free blocks or anything
uint64_t create_inode(){
  for (int i = 0; i < NUM_INODES; i ++){
    // Overloading one of the fields... typically considered bad practice
    // If mode is -1 then it is empty
    if (inode_table[i].mode == -1){
      // Set some parameters, not sure what to set UID or GID to
      inode_table[i].mode = 0;
      inode_table[i].link_cnt = 0;
      inode_table[i].size = 0;
      inode_table[i].indirect_ptr = -1;

      // Might not need these
      inode_table[i].EOF_block = -1; 
      inode_table[i].EOF_offset = 0;
      // Return the index of the inode
      return i;
    }
  }

  return -1;
}


uint64_t get_RW_block(int fileID, int write){
  // This function gets the block index from the rwpointer information (fileID)
  // If the write flag is on then we are in write mode, (write == 1)
  //    This will also allocate the blocks

  // fd and inode use same index, need both of them
  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fileID];

  // Get the current file location to write to based on the rwptr
  int rwOffset = fd->rwptr;
  
  // Get the current block pointed to by the RW pointer
  int blockOffset = rwOffset / BLOCK_SZ;

  // Get the location of the current block to be written
  uint64_t curDataPageIdx;


  if (blockOffset < 12){
    if (DEBUG) printf("Acquiring data page from direct pointers \n");

    // If it is a direct pointer then the corresponding block can be read directly
    curDataPageIdx = inode->data_ptrs[blockOffset]; 

    // If the index is 0 then it is empty
    // Create a pointer to a page if write
    if (curDataPageIdx == 0){
      if (write == 1){
        curDataPageIdx = getNextFreeBlock();
        inode->data_ptrs[blockOffset] = curDataPageIdx;
        return curDataPageIdx;
      }

      // If trying to read from empty then we have a problem
      if (DEBUG) printf("Attempting to read from invalid location" PRId64 "  \n", fileID);
      return -1;
    }

    return curDataPageIdx;
  }
  else{
    if (DEBUG) printf("Acquiring data page from indirect pointers \n");
    // Indirect pointers
    // The indirect pointer will be the block index of the pointerPage
    // This block will be filled with contiguous pointers to data pages
    uint64_t indirPtr = inode->indirect_ptr;
    char *pointerPage = calloc(1,BLOCK_SZ);
    
    // If the indirect ptr hasn't been set up yet
    // Need to create a pointer page
    // The farthest an RW pointer will be is pointing to this first page
    // Probably has a block offset of 12
    if (indirPtr <=0){
      if (write == 1){
        if (DEBUG) printf("No indirect found, creating new indirect for inode %" PRId64 "  \n", fileID);

        // Get the next free block and set the indirect pointer to be this location
        indirPtr = getNextFreeBlock();
        inode->indirect_ptr = indirPtr;

        // Set up a data page as well
        curDataPageIdx = getNextFreeBlock();
        // Set the first index in the pointer page to be the current data page index
        pointerPage[0] = curDataPageIdx;

        return curDataPageIdx;
      }
      // If trying to read from empty then we have a problem
      if (DEBUG) printf("Attempting to read from invalid location" PRId64 "  \n", fileID);
      return -1;
    }
    else{
      if (DEBUG) printf("Indirect pointer found \n");
      
      // Read index page from memory (using the indirect pointer to get pointer page)
      uint64_t *pointerPage = calloc(1,BLOCK_SZ);
      read_blocks(indirPtr, 1, (void*) pointerPage);
      
      // We know that the block offset is at least 12
      // Now have to find the offset on the pointer page
      // TODO I think this is causing issues from writing currupted data from disk
      blockOffset -= 12;
      curDataPageIdx = pointerPage[blockOffset];

      // If i iterates all the way to block size, then the pointer page is full
      // Cannot allocate any memory so quit
      if (blockOffset >= BLOCK_SZ/PTR_SIZE){
        if (DEBUG) printf("Unable to find free data pointer on inode \n");
        return -1;
      }

      // If the data page does not exist and there is a write, then create it
      if (curDataPageIdx == 0){
        if (write == 1){
          // Get the next free block
          // Set the proper pointer on the idirect page
          // Write the indirect page back to disk
          curDataPageIdx = getNextFreeBlock();
          if (DEBUG) printf("Create new pointer slot for page %" PRId64 "  \n", curDataPageIdx);
          pointerPage[blockOffset] = curDataPageIdx;
          write_blocks(indirPtr, 1, pointerPage);
          return curDataPageIdx;
        }
        // If trying to read from empty then we have a problem
        if (DEBUG) printf("Attempting to read from invalid location \n");
        return -1;
      }

      return curDataPageIdx;

    }
  }

  return -1;  

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


    uint64_t firstBlock = getNextFreeBlock();
    // write super block
    write_blocks(firstBlock, 1, &sb);
    
    // Create root directory
    // Is this right?
    inode_table[sb.root_dir_inode].mode = 0;
    write_blocks(1+sb.inode_table_len, NUM_ROOTDIR_BLOCKS, root_directory);
    for (int i = 0; i < NUM_ROOTDIR_BLOCKS; i++){
      getNextFreeBlock();
    }
    
    // write inode table
    // TODO figure out this inode stuff
    write_blocks(1, sb.inode_table_len, inode_table);


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



  // In this implementation the index for the iNode will be the same as the FD table
  // So they can share this one index
  // Find the file in the file map
  uint64_t index = get_inode_from_name(name);

  // Create the file if it doesn't already exist
  if (index == -1){
    if (DEBUG) printf("No file found, creating one \n");

    // Need to create an inode
    index = create_inode();

    // Add the file to the root directory
    root_directory[index].filename = name;
    root_directory[index].inode = index;

    if (DEBUG) printf("File created at inode %" PRId64 "  \n", index);
  }


  // fd_table.inode is initialized to -1
  // So this is if the file was not already open
  // Don't want to open it twice
  if (fd_table[index].inode == -1){
    // Set the inode number to be the proper inode
    fd_table[index].inode = index;
  }


  // Set the rwptr to be the size
  // Assume there is no free space in a file, when it's written it remains tight
  fd_table[index].rwptr = inode_table[index].size;

  // The inode table and root directory were modified, so write these to disk
  write_blocks(1, sb.inode_table_len, inode_table);
  write_blocks(1+sb.inode_table_len, NUM_ROOTDIR_BLOCKS, root_directory);


  if (DEBUG) printf("Returning FD %" PRId64 " \n", index);

	return index;
}

int sfs_fclose(int fileID){
  // Closes a file
  // Removes the entry from the open file descriptor table

  // If there is no fd_table entry for the given ID then either closed
  // Or the entry otherwise doesn't exist
  if (fd_table[fileID].inode == 0){
    printf("No such file descriptor entry at index %d \n", fileID);
    return -1;
  }

  // If the entry does exist, reset both of the fields in the fd_table
  // Return 0 for success
  fd_table[fileID].inode = 0;
  fd_table[fileID].rwptr = 0;
	return 0;
}

int sfs_fread(int fileID, char *buf, int length){
  // Want to read from the given fileID at the current offset
  
  // First, get the FD and inodes corresponding to the fileID
  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fd->inode];

  // If the fd's inode is 0 then the fd entry is empty
  if (fd->inode == 0){
    printf("FD table is empty \n");
    return 0;
  }

  // Get the current file location to write to based on the rwptr
  int rwOffset = fd->rwptr;
  if (DEBUG) printf("RW offset %d \n", rwOffset);
  

  // fileOffset is the byte location within the current block
  // Also get the block to be read to from the current RW pointer location
  int fileOffset = rwOffset % BLOCK_SZ;
  int curDataBlockIdx = get_RW_block(fileID, 0);
  

  int bufferIdx = 0;
  while(bufferIdx < length){
    // Read the whole block from memory... cannot read a partial block
    char *tempBlock = calloc(1,BLOCK_SZ);
    read_blocks(curDataBlockIdx, 1, (void*) tempBlock);

    // Iterate over the block starting at the file offset
    // Copy one byte at a time into the buffer
    for (fileOffset = fileOffset; fileOffset < BLOCK_SZ; fileOffset ++){
      if (bufferIdx == length) break;
      buf[bufferIdx] = tempBlock[fileOffset];
      bufferIdx ++;
    }
    fileOffset = 0;
  }


	return bufferIdx;
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


  // fd and inode use same index, need both of them
  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fileID];

  // If the fd's inode is 0 then the fd entry is empty
  if (fd->inode == 0){
    printf("FD table is empty \n");
    return 0;
  }

  uint64_t curDataPageIdx = get_RW_block(fileID, 1);
  if (DEBUG) printf("Block to write file to is %" PRId64 "\n", curDataPageIdx);
  
  // Get the current file location to write to based on the rwptr
  int rwOffset = fd->rwptr;
  if (DEBUG) printf("RW offset %d \n", rwOffset);

  ///////// WRITE TO PARTIALLY FILLED BLOCK //////////
  // fileOffset is the byte location within the current block
  int fileOffset = rwOffset % BLOCK_SZ;
  // This is the location within the buffer (how far through the data we are)
  int bufferIdx = 0;
  // There is a page that is partially filled, fill it the rest of the way
  if (fileOffset != 0){
    if (DEBUG) printf("Filling up remainder of partially filled page \n");
    // read block from current page
    char *curDataPage = calloc(1,BLOCK_SZ);
    read_blocks(curDataPageIdx, 1, (char*) curDataPage);

    // Copy over buffer to fill remainder of space
    for (fileOffset = fileOffset; fileOffset < BLOCK_SZ; fileOffset ++){
      // Break out if have written all the bytes
      if (bufferIdx == length) break;
      curDataPage[fileOffset] = buf[bufferIdx];
      fileOffset += 1;
    }

    // write the blocks to memory
    write_blocks(curDataPageIdx, 1, &curDataPage);
    curDataPageIdx = getNextFreeBlock();
  }
  

  ////////// WRITE TO FULL BLOCKS //////////
  // The remainder of the writes will only be full blocks
  while(bufferIdx != length){
    printf("Writing to file \n");

    // Copy the buffer into a temporary block size element
    char *curDataPage = calloc(1,BLOCK_SZ);
    for (int i = 0; i < BLOCK_SZ; i ++){
      if (bufferIdx >= length) break;
      curDataPage[i] = buf[bufferIdx];
      // if (DEBUG) printf("%c", buf[i]);
      bufferIdx += 1;
      fd->rwptr += 1;
    }

    // Update size of inode
    // If the rwptr has a larger offset than the inode size then size increases
    // Assume optimal file writing
    if (fd->rwptr > inode->size) inode->size = fd->rwptr;
    // Write the block and get the next block to write
    write_blocks(curDataPageIdx, 1, &curDataPage);

    // Double checks
    if (bufferIdx != length) curDataPageIdx = get_RW_block(fileID, 1);
  }


  // Write back to inode
  write_blocks(1, sb.inode_table_len, inode_table);


	return bufferIdx;
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
