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

#include "disk_emu.h"

int seen = 0;

#define JITS_DISK "sfs_disk.disk"
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 100  //TODO: increase
#define NUM_INODES 10   //TODO: increase
#define FREE_MAP_SIZE ((NUM_BLOCKS+8-1) / 8)
#define FREE_MAP_BLOCKS ((FREE_MAP_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE)
#define NUM_INODE_BLOCKS (sizeof(inode_t) * NUM_INODES / BLOCK_SIZE + 1) 
// TODO figure this out
#define NUM_ROOTDIR_BLOCKS 1
#define PTR_SIZE (sizeof(int))
/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)


superblock_t sb;
uint8_t free_bit_map[FREE_MAP_SIZE] = { [0 ... FREE_MAP_SIZE-1] = UINT8_MAX };
inode_t inode_table[NUM_INODES];
file_descriptor fd_table[NUM_INODES];
file_map root_directory[NUM_INODES]; 


// Flag for debugging printing
int DEBUG = -1;
// Index for iterating over files in sfs_getnextfilename()
int nextFilenameIdx = 0;


//////////////////// MARK NEXT FREE BLOCK ////////////////////
// From tutorial code
int get_next_free_block() {
    int i = 0;

    // find the first section with a free bit
    // let's ignore overflow for now...
    while (free_bit_map[i] == 0) { i++; }
    // now, find the first free bit
    // ffs has the lsb as 1, not 0. So we need to subtract
    uint8_t bit = ffs(free_bit_map[i]) - 1;

    // The map is full
    if (i*8 + bit >= NUM_BLOCKS){
      if (DEBUG==1) printf("Unable to allocate a block \n");
      return -1;
    }

    if (DEBUG==1) printf("Grabbing block at char %d bit %d \n", i, bit);

    // set the bit to used
    USE_BIT(free_bit_map[i], bit);

    // Write the new table back to memory
    char* tempBlock = calloc(BLOCK_SIZE,1);
    memcpy(tempBlock, free_bit_map, sizeof(free_bit_map));
    write_blocks(NUM_BLOCKS-FREE_MAP_BLOCKS, FREE_MAP_BLOCKS, tempBlock);
    free(tempBlock);
    //return which bit we used
    return i*8 + bit;
}


//////////////////// UNMARK NEXT FREE BLOCK ////////////////////
// From tutorial code
void rm_index(int index) {

    // get index in array of which bit to free
    int i = index / 8;

    // get which bit to free
    uint8_t bit = index % 8;

    // free bit
    FREE_BIT(free_bit_map[i], bit);

    // Write the new table back to memory
    char* tempBlock = calloc(BLOCK_SIZE,1);
    memcpy(tempBlock, free_bit_map, sizeof(free_bit_map));
    write_blocks(NUM_BLOCKS-FREE_MAP_BLOCKS, FREE_MAP_BLOCKS, tempBlock);
    free(tempBlock);
}

//////////////////// CREATE AN INODE ////////////////////
// These already exist in memory, so don't need to get next free blocks or anything
int create_inode(){
  for (int i = 0; i < NUM_INODES; i ++){
    // Overloading one of the fields... typically considered bad practice
    // If mode is <= 0
    if (inode_table[i].mode <= 0 || inode_table[i].mode > 1){
      // Set some parameters, not sure what to set UID or GID to
      inode_table[i].mode = 1;
      inode_table[i].indirect_ptr = 0;

      // Return the index of the inode
      return i;
    }
  }

  return -1;
}


void init_superblock() {
    sb.magic = 0xACBD0005;
    sb.block_size = BLOCK_SIZE;
    sb.fs_size = NUM_BLOCKS * BLOCK_SIZE;
    sb.inode_table_len = NUM_INODE_BLOCKS;
    sb.root_dir_inode = 0;
}


///////////////////////////////////////////////////////////////////////////////
//////////////////////// API CALLS ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


void mksfs(int fresh) {
	//Implement mksfs here
  // Formats the virtual disk implemented
  // Creates an instance of the simple file system on top of it
  // Instantiate all the in memory data structures
  // Open file descriptor table, inode cache, disk block cache, root dir cache
  if (fresh) {	
    // File system is created from scratch
    if (DEBUG==1) printf("making new file system\n");

    // create super block
    init_superblock();
    init_fresh_disk(JITS_DISK, BLOCK_SIZE, NUM_BLOCKS);
    write_blocks(get_next_free_block(), 1, &sb);


    // Instantiate some important values
    // instantiate the inode table
    for (int i = 0; i < NUM_INODES; i++){
      inode_table[i].mode = 0;
      // Set this to be the first spot after the i-nodes
      // inode_table[i].EOF_block = 1+sb.inode_table_len+NUM_ROOTDIR_BLOCKS;
      // inode_table[i].EOF_offset = 0;
    }
    // Set all of these for my naive overloading of inode field
    for (int i = 0; i < NUM_INODES; i++){
      fd_table[i].inode = 0;
    }
    // Set the location of the root node
    // The root directory will be at the sb.root_dir_inode (0)
    inode_table[sb.root_dir_inode].mode = 1;


    // write inode table
    write_blocks(get_next_free_block(), sb.inode_table_len, inode_table);
  } 
  else {
    if (DEBUG==1) printf("reopening file system\n");
    // open super block
    read_blocks(0, 1, &sb);
    if (DEBUG==1) printf("Block Size is: %lu\n", sb.block_size);
    // open inode table
    read_blocks(1, NUM_INODE_BLOCKS, inode_table);

    // open free block list
  }
  return;
}

int sfs_get_next_filename(char *fname) {
  // Copies the name of the next file in the directory into fname
  // Returns a non-zero if there is a new file
  // Once all of the files have been returned, this function returns 0
  // Used to loop over the directory
  // Ensure that the function remembers the current position in the dir at each call
  // Facilitated by the single level directory structure

  // Get the next file name according to the indexing variable 
  file_map curFile = root_directory[nextFilenameIdx];
  if (DEBUG==1) printf("%d", curFile.inode);
  // If curFile has a null name or inode then clearly invalid
  if (curFile.filename == NULL || curFile.inode <= 0) {
    nextFilenameIdx = 0;
    return 0;
  }

  // length is wrong in this case, not null terminated?
  int copySize = strlen(curFile.filename);
  if (strlen(curFile.filename) > MAXFILENAME) copySize = MAXFILENAME;

  // Copy the filename into fname according to the size of the filename
  memcpy(fname, curFile.filename, copySize);
  
  // increment the filename looper index
  nextFilenameIdx ++;

  // return the inode of the file on success
	return curFile.inode;
}

/*
//////////////////// GET INODE FROM NAME /////////////////////
//TODO in this iter set the root dir location to be the returned inode -1 (possibly)
//    Or skip the first index
// Get the inode number from the root directory using the name
int get_inode_from_name(const char* name){
  char* copyName;
  int inode = -1;

  // Set the inode to be output of next filename fn
  // If next filename fn returns 0 then no more files left, else it's the inode
  while ((inode = sfs_get_next_filename(copyName)) != 0){
    printf("made it");
    // Don't want to exceed the index
    if (nextFilenameIdx >= NUM_INODES) return -1;
    
    // Compare the two strings, return the inode if there is a match
    if (DEBUG==1) printf("Comparing %s,%s\n", copyName, name);
    if (strncmp(copyName, name, MAXFILENAME) == 0) return inode;
  }

  // If no file found then return -1
  return -1;
}
*/


//////////////////// GET INODE FROM NAME /////////////////////
// Get the inode number from the root directory using the name
int get_inode_from_name(char* name){
  // Iterate over the entire directory in memory.
  // If a file exists by that name, return its inode number
  for (int i = 0; i < NUM_INODES; i ++){
    file_map curFile = root_directory[i];

    // If curFile has a null name or inode then clearly invalid
    if (curFile.filename == NULL || curFile.inode <= 0) continue;


    // Compare the two strings, return the inode if there is a match
    if (strncmp(name, curFile.filename, MAXFILENAME) == 0) return curFile.inode;
    if (DEBUG==1) printf("Comparing %s,%s\n", name, curFile.filename);
  }

  // If no file found then return -1
  return -1;
}

// TODO is path different than name?
int sfs_GetFileSize(const char* path) {
  // Get the inode corresponding to the name of the file
  int inode = get_inode_from_name(path);
  if (inode == -1) return -1;
	return inode_table[inode].size;
}

int sfs_fopen(char *name) {
  // See if the name exceeds the max
  // The name passed in will include the extension I believe
  //      i.e. some_name.txt
  if (DEBUG==1) printf("\nOpening %s \n", name);  
  if (strlen(name) >= MAXFILENAME+1) return -1;

  // Find the file in the root directory
  int inodeIdx = get_inode_from_name(name);
  
  // If the inode idx is <= it is either the root dir or invalid
  // Create the file if it doesn't already exist
  if (inodeIdx == -1){
    // Need to create an inode
    if (DEBUG==1) printf("No file found, creating one ");
    inodeIdx = create_inode();
    if (DEBUG==1) printf("at index %d \n", inodeIdx);

    // root dir slot is the node index minus one...
    root_directory[inodeIdx].filename = name;
    root_directory[inodeIdx].inode = inodeIdx;

    if (DEBUG==1) printf("File created at inode %d  \n", inodeIdx);
  }

  // Check to see if the inode already exists in the table, if it does do not open twice
  if (fd_table[inodeIdx].inode <= 0){
    // Set the inode number to be the proper inode
    fd_table[inodeIdx].inode = inodeIdx;
  }

  // Set the rwptr to be the size (assume no empty space in middle, rwptr <= size always)
  fd_table[inodeIdx].rwptr = inode_table[inodeIdx].size;

  // The inode table and root directory were modified, so write these to disk
  write_blocks(1, NUM_INODE_BLOCKS, inode_table);
  write_blocks(1+NUM_INODES, NUM_ROOTDIR_BLOCKS, root_directory);


  if (DEBUG==1) printf("Returning FD %d \n", inodeIdx);
	return inodeIdx;
}

int sfs_fclose(int fileID){
  // If there is no fd_table entry for the given ID then either closed
  // Or the entry otherwise doesn't exist
  if (fd_table[fileID].inode == 0){
    if (DEBUG==1) printf("No such file descriptor entry at index %d \n", fileID);
    return -1;
  }

  // If the entry does exist, reset both of the fields in the fd_table
  // Return 0 for success
  fd_table[fileID].inode = 0;
  fd_table[fileID].rwptr = 0;

	return 0;
}

int get_RW_block(int fileID, int write){
  // This function gets the block index from the rwpointer information (fileID)
  // If the write flag is on then we are in write mode, (write == 1)
  //    Write mode will also allocate the blocks

  // fd and inode use same index, need both of them
  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fileID];

  // Get the current file location to write to based on the rwptr
  int rwOffset = fd->rwptr;
  
  // Get the current block pointed to by the RW pointer
  int blockOffset = rwOffset / BLOCK_SIZE;

  // Get the location of the current block to be written
  int curDataPageIdx = 0;
  
  // If the blockOffset < 12 then the rwOffset points to a direct pointer block
  if (blockOffset < 12){
    if (DEBUG==1) printf("Acquiring data page from direct ptr #%d \n", blockOffset);

    // If it is a direct pointer then the corresponding block can be read directly
    curDataPageIdx = inode->data_ptrs[blockOffset]; 

    // If the index is 0 then it is empty
    // Create a page and point to it if write
    if (curDataPageIdx == 0){
      if (write == 1){
        curDataPageIdx = get_next_free_block();
        inode->data_ptrs[blockOffset] = curDataPageIdx;
        return curDataPageIdx;
      }

      // If trying to read from empty then we have a problem
      if (DEBUG==1) printf("Attempting to read from uninst dir ptr #%d \n", blockOffset);
      return -1;
    }

    return curDataPageIdx;
  }
  else{
    // Reading from indirect pointers

    if (DEBUG==1) printf("Acquiring data page from indirect pointers \n");
    // Indirect pointers
    // The indirect pointer will be the block index of the pointerPage
    // This block will be filled with contiguous pointers to data pages
    int indirPtr = inode->indirect_ptr;
    char *pointerPage = calloc(1,BLOCK_SIZE);
    
    // If the indirect ptr hasn't been set up yet
    // Need to create a pointer page
    // The farthest an RW pointer will be is pointing to this first page
    // Probably has a block offset of 12
    if (indirPtr <=0){
      if (write == 1){
        if (DEBUG==1) printf("No indirect found, creating new indirect for inode %d \n", fileID);

        // get the next free block and set the indirect pointer to be this location
        indirPtr = get_next_free_block();
        inode->indirect_ptr = indirPtr;

        // set up a data page as well
        curDataPageIdx = get_next_free_block();
        // set the first index in the pointer page to be the current data page index
        pointerPage[0] = curDataPageIdx;

        return curDataPageIdx;
      }
      // if trying to read from empty then we have a problem
      if (DEBUG==1) printf("Attempting to read from uninst indir ptr for inode #%d \n", fileID);
      return -1;
    }
    else{
      // Get the indirect ptr
      // Read the ptr page from ptr
      // get the index - 12 th page from the ptr page
      // If write and no index-12 then get a new block and link this page to that
      // else return the index-12
      int *pointerPage = calloc(1,BLOCK_SIZE);
      read_blocks(indirPtr, 1, (void*) pointerPage);
      
      // we know that the block offset is at least 12
      // now have to find the offset on the pointer page
      // todo i think this is causing issues from writing currupted data from disk
      blockOffset -= 12;
      curDataPageIdx = pointerPage[blockOffset];
      if (DEBUG==1) printf("Indirect pointer found at %d \n", blockOffset);

      // if i iterates all the way to block size, then the pointer page is full
      // cannot allocate any memory so quit
      if (blockOffset >= BLOCK_SIZE/PTR_SIZE){
        if (DEBUG==1) printf("Inode is full on inode #%d \n", fileID);
        return -1;
      }

      // If the data page does not exist and there is a write, then create it
      if (curDataPageIdx == 0){
        if (write == 1){
          // Get the next free block
          // Set the proper pointer on the idirect page
          // Write the indirect page back to disk
          curDataPageIdx = get_next_free_block();
          if (DEBUG==1) printf("Create new pointer slot for page %d  \n", curDataPageIdx);
          pointerPage[blockOffset] = curDataPageIdx;
          write_blocks(indirPtr, 1, pointerPage);
          return curDataPageIdx;
        }
        // If trying to read from empty then we have a problem
        if (DEBUG==1) printf("Attempting to read from uninst indir ptr for inode #%d \n", fileID);
        return -1;
      }

      return curDataPageIdx;

    }

    return curDataPageIdx;
  }



}

int sfs_fread(int fileID, char *buf, int length){
  // Want to read from the given fileID at the current offset
  
  // First, get the FD and inodes corresponding to the fileID
  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fd->inode];

  // If the fd's inode is 0 then the fd entry is empty
  if (fd->inode == 0){
    if (DEBUG==1) printf("FD table is empty \n");
    return 0;
  }

  

  // Get the current file location to write to based on the rwptr
  int rwOffset = fd->rwptr;
  if (DEBUG==1) printf("RW offset %d \n", rwOffset);
  

  // fileOffset is the byte location within the current block
  // Also get the block to be read to from the current RW pointer location
  int fileOffset = rwOffset % BLOCK_SIZE;
  int curDataPageIdx = get_RW_block(fileID, 0);
  

  int bufferIdx = 0;
  while(bufferIdx < length){
    // Error checking, if curDataBlockIdx == -1 then out of bounds
    if (curDataPageIdx == -1){
      if (DEBUG==1) printf("Read out of bounds \n");
      return 0;
    }

    char *dataBuf = calloc(1,BLOCK_SIZE);
    read_blocks(curDataPageIdx, 1, (void*) dataBuf);


    // Set the number of characters to copy within the block
    int numCharsToCopy = (BLOCK_SIZE-fileOffset);  
    if ((length-bufferIdx) < numCharsToCopy) numCharsToCopy = length-bufferIdx;

    if (DEBUG==1) printf("Reading %d of %d bytes from block %d \n", numCharsToCopy, length, curDataPageIdx);

    // copy the page into the buffer
    memcpy(buf + bufferIdx, dataBuf + fileOffset, numCharsToCopy);

    // write the blocks to memory
    //write_blocks(curDataPageIdx, 1, (void*) dataBuf);
    write_blocks(curDataPageIdx, 1, dataBuf);

    // Free the source buffer
    free(dataBuf);

    // Update rwptr, the file size, and the current buffer idx
    // If the rwptr has a larger offset than the inode size then size increases
    // Assume optimal file writing
    fd->rwptr += numCharsToCopy;
    bufferIdx += numCharsToCopy;
    fileOffset = 0;

    // Double check and grab next chars
    if (bufferIdx < length) curDataPageIdx = get_RW_block(fileID, 0);


    /*
    // Read the whole block from memory... cannot read a partial block
    char *tempBlock = calloc(1,BLOCK_SIZE);
    read_blocks(curDataBlockIdx, 1, (void*) tempBlock);

    // Iterate over the block starting at the file offset
    // Copy one byte at a time into the buffer
    for (fileOffset = fileOffset; fileOffset < BLOCK_SIZE && bufferIdx < length; fileOffset ++){
      buf[bufferIdx] = tempBlock[fileOffset];
      
      // Advance the rwptr and the buffer
      bufferIdx ++;
      fd->rwptr ++; 
    }

    // Set fileOffset to 0 if filled
    fileOffset = 0;


    // Get new data block
    curDataBlockIdx = get_RW_block(fileID, 0);

*/
  }




	return bufferIdx;
}

int sfs_fwrite(int fileID, const char *buf, int length){
  // Grab both file descriptor entry and the inode
  file_descriptor* fd = &fd_table[fileID];
  inode_t* inode = &inode_table[fd->inode];

  // If the fd's inode is 0 then the fd entry is empty
  if (fd->inode == 0){
    if (DEBUG==1) printf("FD table slot %d is empty \n", fd->inode);
    return -1;
  }
  
  // Get the block that we are going to write to
  int curDataPageIdx = get_RW_block(fileID, 1);
  if (DEBUG==1) printf("Block to write file to is %d \n", curDataPageIdx);  

  // Get the current file location to write to based on the rwptr
  int rwOffset = fd->rwptr;
  if (DEBUG==1) printf("RW offset %d \n", rwOffset);

  // fileOffset is the byte location within the current block
  int fileOffset = rwOffset % BLOCK_SIZE;
  // This is the location within the buffer (how far through the data we are)
  int bufferIdx = 0;

  while (bufferIdx < length){
    if (curDataPageIdx == -1){
      if (DEBUG==1) printf("Could not write \n");
      break;
    }

    if (DEBUG==1) printf("\n\n");
    // read block from current page
    char *dataBuf = calloc(1,BLOCK_SIZE);
    read_blocks(curDataPageIdx, 1, (void*) dataBuf);


    // Set the number of characters to copy within the block
    int numCharsToCopy = (BLOCK_SIZE-fileOffset);  
    if ((length-bufferIdx) < numCharsToCopy) numCharsToCopy = length-bufferIdx;

    if (DEBUG==1) printf("Writing %d of %d bytes to block %d \n", numCharsToCopy, length, curDataPageIdx);

    // copy the page into the buffer
    memcpy(dataBuf + fileOffset, buf + bufferIdx, numCharsToCopy);

    // NOTE getting correct output here... not writing to disk properly though
    //for (int i = 0; i < BLOCK_SIZE; i++){
    //  printf("%c", &dataBuf[i]);
    //}
    //printf("\n\n");

    // write the blocks to memory
    //write_blocks(curDataPageIdx, 1, (void*) dataBuf);
    write_blocks(curDataPageIdx, 1, dataBuf);

    // Free the source buffer
    free(dataBuf);

    // Update rwptr, the file size, and the current buffer idx
    // If the rwptr has a larger offset than the inode size then size increases
    // Assume optimal file writing
    fd->rwptr += numCharsToCopy;
    if (fd->rwptr > inode->size) inode->size = fd->rwptr;
    bufferIdx += numCharsToCopy;
    fileOffset = 0;

    // Double check and grab next chars
    if (bufferIdx < length) curDataPageIdx = get_RW_block(fileID, 1);
  }


	return bufferIdx;
}

int sfs_fseek(int fileID, int loc){
  // Moves the r/w pointer to the given location (nothing to be done on disk)
  //
  // "interesting problem is performing a read or write after moving r/w ptr"
  //    sfs_read and sfs_write both advance the same ptr
  // Could implement with two ptrs?

  // Grab the file descriptor entry
  file_descriptor* fd = &fd_table[fileID];

  // If the fd's inode is 0 then the fd entry is empty
  if (fd->inode == 0){
    if (DEBUG==1) printf("FD table slot %d is empty \n", fd->inode);
    return -1;
  }

  if (loc < 0) {
    if (DEBUG==1) printf("Invalid location %d \n", loc);
    return -1;
  }

  fd_table[fileID].rwptr = loc;

	return 0;
}

int sfs_remove(char *file) {

	//Implement sfs_remove here	
	return 0;
}
