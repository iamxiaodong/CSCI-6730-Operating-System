#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

// Xiaodong
// Date: April. 29, 2017
char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

// indirect block map
InDirectBlockMap inDirectBMap;

int fs_mount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
		int i, index, inode_index = 0;

		// load superblock, inodeMap, blockMap and inodes into the memory
		if(disk_mount(name) == 1) {
				disk_read(0, (char*) &superBlock);
				disk_read(1, inodeMap);
				disk_read(2, blockMap);
				for(i = 0; i < numInodeBlock; i++)
				{
						index = i+3;
						disk_read(index, (char*) (inode+inode_index));
						inode_index += (BLOCK_SIZE / sizeof(Inode));
				}
				// root directory
				curDirBlock = inode[0].directBlock[0];
				disk_read(curDirBlock, (char*)&curDir);

		} else {
		// Init file system superblock, inodeMap and blockMap
				superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
				superBlock.freeInodeCount = MAX_INODE;

				//Init inodeMap
				for(i = 0; i < MAX_INODE / 8; i++)
				{
						set_bit(inodeMap, i, 0);
				}
				//Init blockMap
				for(i = 0; i < MAX_BLOCK / 8; i++)
				{
						if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
						else set_bit(blockMap, i, 0);
				}
				//Init root dir
				int rootInode = get_free_inode();
				curDirBlock = get_free_block();

				inode[rootInode].type =directory;
				inode[rootInode].owner = 0;
				inode[rootInode].group = 0;
				gettimeofday(&(inode[rootInode].created), NULL);
				gettimeofday(&(inode[rootInode].lastAccess), NULL);
				inode[rootInode].size = 1;
				inode[rootInode].blockCount = 1;
				inode[rootInode].directBlock[0] = curDirBlock;

				curDir.numEntry = 1;
				strncpy(curDir.dentry[0].name, ".", 1);
				curDir.dentry[0].name[1] = '\0';
				curDir.dentry[0].inode = rootInode;
				disk_write(curDirBlock, (char*)&curDir);
		}
		return 0;
}

int fs_umount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
		int i, index, inode_index = 0;
		disk_write(0, (char*) &superBlock);
		disk_write(1, inodeMap);
		disk_write(2, blockMap);
		for(i = 0; i < numInodeBlock; i++)
		{
				index = i+3;
				disk_write(index, (char*) (inode+inode_index));
				inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// current directory
		disk_write(curDirBlock, (char*)&curDir);

		disk_umount(name);	
}

int search_cur_dir(char *name)
{
		// return inode. If not exist, return -1
		int i;

		for(i = 0; i < curDir.numEntry; i++)
		{
				if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
		}
		return -1;
}

/**************************************
File create for small and large files
***************************************/
// The original create_file function is to create small files
int file_create(char *name, int size)
{
		int inode;
		if(size >= LARGE_FILE) {
				printf("File create failed:  size over 70,656.\n");
				return -1;
		}

		char *tmp = (char*) malloc(sizeof(int) * size + 1);
		memset(tmp, 0, size + 1);
		rand_string(tmp, size);

		if(size >= SMALL_FILE) {
				inode = file_create_small_helper(name, SMALL_FILE - 1, tmp);
				if(inode < 0) return -1;
				file_create_large_helper(inode, name, size - (SMALL_FILE - 1), tmp);
		} else {
				inode = file_create_small_helper(name, size, tmp);
		}

		free(tmp);
		tmp = NULL;
		printf("file created: %s, inode %d, size %d\n", name, inode, size);
		return 0;
}

int file_create_large_helper(int inodeNum, char *name, int size, char *tmp) 
{
		// only deal with indirect block
		int numInBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numInBlock++;

		if(numInBlock + 1 > superBlock.freeBlockCount) {
				printf("File create failed: not enough space\n");
				return -1;
		}

		// get a free block for indirect block map
		int indirect = get_free_block();
		if(indirect == -1) {
				printf("File_create error: get_free_block failed\n");
				return -1;
		}

		int i;
		for(i = 0; i < numInBlock; i++)
		{
				int block = get_free_block();
				if(block == -1) {
						printf("File_create error: get_free_block failed\n");
						return -1;
				}
				inDirectBMap.indirectBlock[i] = block;
				disk_write(block, tmp + ((i + DIRECT_BLOCK) * BLOCK_SIZE));
		}
		// update info
		inode[inodeNum].size += size;
		inode[inodeNum].blockCount += numInBlock;
		inode[inodeNum].indirectBlock = indirect;

		disk_write(indirect, (char*) &inDirectBMap);
		return 0;
}

int file_create_small_helper(char *name, int size, char *tmp)
{
		int inodeNum = search_cur_dir(name); 
		if(inodeNum >= 0) {
				printf("File create failed:  %s exist.\n", name);
				return -1;
		}

		// BLOCK_SIZE / sizeof(DirectoryEntry) = 25, where sizeof(DirectoryEntry) = 20
		if(curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry))) {
				printf("File create failed: directory is full!\n");
				return -1;
		}

		int numBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numBlock++;

		if(numBlock > superBlock.freeBlockCount) {
				printf("File create failed: not enough space\n");
				return -1;
		}

		if(superBlock.freeInodeCount < 1) {
				printf("File create failed: not enough inode\n");
				return -1;
		}
		
		// get inode and fill it
		inodeNum = get_free_inode();
		if(inodeNum < 0) {
				printf("File_create error: not enough inode.\n");
				return -1;
		}
		
		inode[inodeNum].type = file;
		inode[inodeNum].owner = 1;
		inode[inodeNum].group = 2;
		gettimeofday(&(inode[inodeNum].created), NULL);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		inode[inodeNum].size = size;
		inode[inodeNum].blockCount = numBlock;
		
		// add a new file into the current directory entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = inodeNum;
		curDir.numEntry++;

		// get data blocks
		int i;
		for(i = 0; i < numBlock; i++)
		{
				int block = get_free_block();
				if(block == -1) {
						printf("File_create error: get_free_block failed\n");
						return -1;
				}
				inode[inodeNum].directBlock[i] = block;
				disk_write(block, tmp + (i * BLOCK_SIZE));
		}
		return inodeNum;
}

/**************************************
File cat for small and large files
***************************************/
int file_cat(char *name)
{
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file cat error: file is not exist.\n");
				return -1;
		}

		if(inode[inodeNum].type == directory) {
				printf("file cat error: it is not a file.\n", name);
				return -1;
		}

		if(inode[inodeNum].size >= SMALL_FILE)
		{
				// large file
				return file_cat_helper(inodeNum);
		}

		int numBlock = inode[inodeNum].blockCount;

		char *str = NULL, *nstr = NULL;
		str = (char *)malloc(sizeof(char) * BLOCK_SIZE + 1);
		nstr = (char *)malloc(sizeof(char) * numBlock * BLOCK_SIZE + 1);

		int i;
		for(i = 0; i < numBlock; i++) {
				disk_read(inode[inodeNum].directBlock[i], str);
				memcpy(nstr + i * BLOCK_SIZE, str, BLOCK_SIZE);
		}

		nstr[numBlock * BLOCK_SIZE] = '\0';
		printf("%s\n", nstr);
		free(str);
		str = NULL;
		free(nstr);
		nstr = NULL;
		return 0;
}

// helper function as file_cat for large files
int file_cat_helper(int inodeNum)
{
		InDirectBlockMap ibkMap;
		disk_read(inode[inodeNum].indirectBlock, (char*) &ibkMap);

		int numBlock = inode[inodeNum].blockCount;
		char *str = NULL, *nstr = NULL;
		str = (char *)malloc(sizeof(char) * BLOCK_SIZE + 1);
		nstr = (char *)malloc(sizeof(char) * numBlock * BLOCK_SIZE + 1);
		memset(str, 0, BLOCK_SIZE + 1);
		memset(nstr, 0, numBlock * BLOCK_SIZE + 1);

		// for direct block
		int i;
		for(i = 0; i < DIRECT_BLOCK; i++) {
				disk_read(inode[inodeNum].directBlock[i], str);
				memcpy(nstr + i * BLOCK_SIZE, str, BLOCK_SIZE);
		}

		// for indirect block
		int j;
		for(j = 0; j < (numBlock - DIRECT_BLOCK); j++) {
				disk_read(ibkMap.indirectBlock[j], str);
				memcpy(nstr + (DIRECT_BLOCK + j) * BLOCK_SIZE, str, BLOCK_SIZE);
		}

		nstr[numBlock * BLOCK_SIZE] = '\0';
		printf("%s\n", nstr);

		free(str);
		str = NULL;
		free(nstr);
		nstr = NULL;
		return 0;
}

/**************************************
File read for small and large files
***************************************/
int file_read(char *name, int offset, int size)
{
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) 
		{
				printf("file read error: file is not exist.\n");
				return -1;
		}

		if(inode[inodeNum].type == directory) 
		{
				printf("file read error: it is not a file.\n", name);
				return -1;
		}

		int startBlockNo = offset / BLOCK_SIZE;
		int endBlockNo = (offset + size) / BLOCK_SIZE;

		if(endBlockNo > DIRECT_BLOCK - 1)
		{
				return file_read_helper(inodeNum, offset, size);
		}

		char *str = NULL, *restr = NULL, *nstr = NULL;
		str = (char *)malloc(sizeof(char) * BLOCK_SIZE + 1);
		nstr = (char *)malloc(sizeof(char) * (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		restr = (char *)malloc(sizeof(char) * (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		
		int i;
		for(i = startBlockNo; i <= endBlockNo; i++) {
				disk_read(inode[inodeNum].directBlock[i], str);
				memcpy(nstr + (i - startBlockNo) * BLOCK_SIZE, str, BLOCK_SIZE);
		}
		nstr[(endBlockNo - startBlockNo + 1) * BLOCK_SIZE] = '\0';

		memcpy(restr, nstr + offset % BLOCK_SIZE, size);
		restr[size] = '\0';
		printf("%s\n", restr);

		free(str);
		str = NULL;
		free(restr);
		restr = NULL;
		free(nstr);
		nstr = NULL;
		return 0;
}

// helper function as file_read for large files
int file_read_helper(int inodeNum, int offset, int size)
{
		InDirectBlockMap ibkMap;
		disk_read(inode[inodeNum].indirectBlock, (char*) &ibkMap);
		int numBlock = inode[inodeNum].blockCount;

		int startBlockNo = offset / BLOCK_SIZE;
		int endBlockNo = (offset + size) / BLOCK_SIZE;

		char *str = NULL, *nstr = NULL, *restr = NULL;
		str = (char *)malloc(sizeof(char) * BLOCK_SIZE + 1);
		nstr = (char *)malloc(sizeof(char) * (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		restr = (char *)malloc(sizeof(char) * (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		memset(str, 0, BLOCK_SIZE + 1);
		memset(nstr, 0, (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		memset(restr, 0, (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);

		// direct block
		int i;
		for(i = startBlockNo; i < DIRECT_BLOCK; i++) {
				disk_read(inode[inodeNum].directBlock[i], str);
				memcpy(nstr + (i - startBlockNo) * BLOCK_SIZE, str, BLOCK_SIZE);
		}
		// indirect block
		int j;
		for(j = DIRECT_BLOCK; j <= endBlockNo; j++) {
				disk_read(ibkMap.indirectBlock[j - DIRECT_BLOCK], str);
				memcpy(nstr + (j - startBlockNo) * BLOCK_SIZE, str, BLOCK_SIZE);
		}

		nstr[(endBlockNo - startBlockNo + 1) * BLOCK_SIZE] = '\0';

		memcpy(restr, nstr + offset % BLOCK_SIZE, size);
		restr[size] = '\0';
		printf("%s\n", restr);

		free(str);
		str = NULL;
		free(restr);
		restr = NULL;
		free(nstr);
		nstr = NULL;
		return 0;
}

/**************************************
File write for small and large files
***************************************/
int file_write(char *name, int offset, int size, char *buf)
{	
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file write error: file is not exist.\n");
				return -1;
		}

		int startBlockNo = offset / BLOCK_SIZE;
		int endBlockNo = (offset + size) / BLOCK_SIZE;

		if(endBlockNo > DIRECT_BLOCK - 1)
		{
				return file_write_helper(inodeNum, offset, size, buf);
		}

		char *str = NULL, *nstr = NULL;
		str = (char *)malloc(sizeof(char) * BLOCK_SIZE + 1);
		nstr = (char *)malloc(sizeof(char) * (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		
		int i;
		for(i = startBlockNo; i <= endBlockNo; i++) 
		{
				disk_read(inode[inodeNum].directBlock[i], str);
				memcpy(nstr + (i - startBlockNo) * BLOCK_SIZE, str, BLOCK_SIZE);
		}
		
		// all the contents from involving blocks
		nstr[(endBlockNo - startBlockNo + 1) * BLOCK_SIZE] = '\0';

		if(size != strlen(buf)) {
				printf("file write error: The size of input buf does not match input size.\n");
				return -1;
		}

		char bstr[size + 1];
		memcpy(bstr, nstr + offset % BLOCK_SIZE, size);
		bstr[size] = '\0';
		memcpy(nstr + offset % BLOCK_SIZE, buf, size);
		char astr[size + 1];
		memcpy(astr, nstr + offset % BLOCK_SIZE, size);
		astr[size] = '\0';

		// write back to block
		int j;
		for(j = startBlockNo; j <= endBlockNo; j++) {
				disk_write(inode[inodeNum].directBlock[j], nstr + (j - startBlockNo) * BLOCK_SIZE);
		}
		
		printf("File write success!  %s ----> %s\n", bstr, astr);

		free(str);
		str = NULL;
		free(nstr);
		nstr = NULL;
		return 0;
}

int file_write_helper(int inodeNum, int offset, int size, char *buf)
{
		InDirectBlockMap ibkMap;
		disk_read(inode[inodeNum].indirectBlock, (char*) &ibkMap);
		int numBlock = inode[inodeNum].blockCount;

		int startBlockNo = offset / BLOCK_SIZE;
		int endBlockNo = (offset + size) / BLOCK_SIZE;

		char *str = NULL, *nstr = NULL;
		str = (char *)malloc(sizeof(char) * BLOCK_SIZE + 1);
		nstr = (char *)malloc(sizeof(char) * (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);
		memset(str, 0, BLOCK_SIZE + 1);
		memset(nstr, 0, (endBlockNo - startBlockNo + 1) * BLOCK_SIZE + 1);

		// direct block
		int i;
		for(i = startBlockNo; i < DIRECT_BLOCK; i++) {
				disk_read(inode[inodeNum].directBlock[i], str);
				memcpy(nstr + (i - startBlockNo) * BLOCK_SIZE, str, BLOCK_SIZE);
		}
		
		// indirect block
		int j;
		for(j = DIRECT_BLOCK; j <= endBlockNo; j++) {
				disk_read(ibkMap.indirectBlock[j - DIRECT_BLOCK], str);
				memcpy(nstr + (j - startBlockNo) * BLOCK_SIZE, str, BLOCK_SIZE);
		}

		nstr[(endBlockNo - startBlockNo + 1) * BLOCK_SIZE] = '\0';

		if(size != strlen(buf)) {
				printf("file write error: The size of input buf does not match input size.\n");
				return -1;
		}
		// write to direct block
		for(i = startBlockNo; i < DIRECT_BLOCK; i++) {
				disk_write(inode[inodeNum].directBlock[i], nstr + (i - startBlockNo) * BLOCK_SIZE);
		}
		// write to indirect block
		for(j = DIRECT_BLOCK; j <= endBlockNo; j++) {
				disk_write(ibkMap.indirectBlock[j - DIRECT_BLOCK], nstr + (j - startBlockNo) * BLOCK_SIZE);
		}

		char bstr[size + 1];
		memcpy(bstr, nstr + offset % BLOCK_SIZE, size);
		bstr[size] = '\0';
		memcpy(nstr + offset % BLOCK_SIZE, buf, size);
		char astr[size + 1];
		memcpy(astr, nstr + offset % BLOCK_SIZE, size);
		astr[size] = '\0';
		
		printf("File write success!  %s ----> %s\n", bstr, astr);
		free(str);
		str = NULL;
		free(nstr);
		nstr = NULL;
		return 0;
}

/**************************************
File stat - implemented by Dr. Lee
***************************************/
int file_stat(char *name)
{
		char timebuf[28];
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file cat error: file is not exist.\n");
				return -1;
		}

		printf("Inode = %d\n", inodeNum);
		if(inode[inodeNum].type == file) printf("type = file\n");
		else printf("type = directory\n");
		printf("owner = %d\n", inode[inodeNum].owner);
		printf("group = %d\n", inode[inodeNum].group);
		printf("size = %d\n", inode[inodeNum].size);
		printf("num of block = %d\n", inode[inodeNum].blockCount);
		format_timeval(&(inode[inodeNum].created), timebuf, 28);
		printf("Created time = %s\n", timebuf);
		format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
		printf("Last accessed time = %s\n", timebuf);
}

/**************************************
File Remove function
***************************************/
int file_remove(char *name)
{		
		int i;
		// get requested file inode number
		int inodeNum = search_cur_dir(name);
		
		if(inodeNum < 0) 
		{
				printf("file delete error: file is not exist.\n");
				return -1;
		}

		if(inode[inodeNum].type == directory) {
				printf("file delete error: input name <%s> is a directory.\n", name);
				return -1;
		}

		// now we are ready to delete the inode in inodeMap
		if(free_inode(inodeNum) != 0) 
		{
				printf("file delete error: fail to delete inode.\n");
				return -1;
		}

		InDirectBlockMap ibkMap;
		disk_read(inode[inodeNum].indirectBlock, (char*) &ibkMap);

		int directBlockNum = inode[inodeNum].blockCount;
		if(directBlockNum > DIRECT_BLOCK)
		{
				// for large files
				directBlockNum = DIRECT_BLOCK;

				for(i = 0; i < (inode[inodeNum].blockCount - DIRECT_BLOCK); i++) {
						if(free_block(ibkMap.indirectBlock[i]) != 0) {
								printf("file delete error: fail to delete block.\n");
								return -1;
						}
				}
		}

		// delete n block in blockMap
		for(i = 0; i < directBlockNum; i++) 
		{
				if(free_block(inode[inodeNum].directBlock[i]) != 0) {
						printf("file delete error: fail to delete block.\n");
						return -1;
				}
		}

		curDir.numEntry--;
		// whether delete inode is the last one
		if(inodeNum != curDir.dentry[curDir.numEntry].inode) {
				// dlete the inode, move the last inode to the delete position
				memcpy(curDir.dentry[inodeNum].name, curDir.dentry[curDir.numEntry].name, sizeof(curDir.dentry[curDir.numEntry].name));
				curDir.dentry[inodeNum].name[MAX_FILE_NAME] = '\0';

				curDir.dentry[inodeNum].inode = curDir.dentry[curDir.numEntry].inode;
				inode[inodeNum].size = inode[curDir.numEntry].size;
		}
		
		return 0;
}

/**************************************
Make a directory
***************************************/
int dir_make(char* name)
{
		// get the current directory inode number
		int inodeNum = search_cur_dir(name); 
		if(inodeNum >= 0) 
		{
				printf("Directory create failed:  %s exist.\n", name);
				return -1;
		}

		// BLOCK_SIZE / sizeof(DirectoryEntry) = 25, where sizeof(DirectoryEntry) = 20
		if(curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry))) 
		{
				printf("Directory create failed: directory is full!\n");
				return -1;
		}

		if(superBlock.freeInodeCount < 1) 
		{
				printf("Directory create failed: not enough inode\n");
				return -1;
		}

		if(superBlock.freeBlockCount < 1) 
		{
				printf("Directory create failed: not enough space\n");
				return -1;
		}
		
		// Create a new sub-directory, it is a entry under current directory, then make a block number
		int newSubDirInode = get_free_inode();
		if(newSubDirInode < 0) 
		{
				printf("Directory create error: not enough inode.\n");
				return -1;
		}

		int newDirBlock = get_free_block();
		if(newDirBlock == -1) 
		{
				printf("Directory create error: get_free_block failed\n");
				return -1;
		}

		// update or initialize
		// Initialize the sub-dir properties as a file in cur-dir
		inode[newSubDirInode].type =directory;
		inode[newSubDirInode].owner = 1;
		inode[newSubDirInode].group = 2;
		gettimeofday(&(inode[newSubDirInode].created), NULL);
		gettimeofday(&(inode[newSubDirInode].lastAccess), NULL);
		inode[newSubDirInode].size = 1;
		inode[newSubDirInode].blockCount = 1;
		inode[newSubDirInode].directBlock[0] = newDirBlock;

		// Add new sub-dir into the current dir as one entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = newSubDirInode;

		curDir.numEntry++;

		printf("Directory %s create success! \n", name);

		// Init sub-dir properties as a new dir
		Dentry subDir;
		subDir.numEntry = 2;
		strncpy(subDir.dentry[0].name, ".", strlen("."));
		strncpy(subDir.dentry[1].name, "..", strlen(".."));
		subDir.dentry[0].name[strlen(".")] = '\0';
		subDir.dentry[1].name[strlen("..")] = '\0';

		subDir.dentry[0].inode = newSubDirInode;
		
		// .. is parent dir, first entry of each dentry
		subDir.dentry[1].inode = curDir.dentry[0].inode;
		disk_write(newDirBlock, (char *)&subDir);

		return 0;
}

/**************************************
Remove directory
***************************************/
int dir_remove(char *name)
{
		int inodeNum = search_cur_dir(name); 
		if(inodeNum < 0) 
		{
				printf("Directory delete failed:  %s not exist.\n", name);
				return -1;
		}

		if(inode[inodeNum].type == file) 
		{
				printf("Directory delete error: input name <%s> is a file not a directory.\n", name);
				return -1;
		}

		if(free_inode(inodeNum) != 0) 
		{
				printf("Directory delete error: fail to delete inode.\n");
				return -1;
		}

		// we now delete n block in blockMap
		if(free_block(inode[inodeNum].directBlock[0]) != 0) 
		{
				printf("file delete error: fail to delete block.\n");
				return -1;
		}

		curDir.numEntry--;

		// check if the deleted inode is the last one
		if(inodeNum != curDir.dentry[curDir.numEntry].inode) 
		{
				// dlete the inode, move the last inode to the deleted position
				memcpy(curDir.dentry[inodeNum].name, curDir.dentry[curDir.numEntry].name, sizeof(curDir.dentry[curDir.numEntry].name));
				curDir.dentry[inodeNum].name[MAX_FILE_NAME] = '\0';

				curDir.dentry[inodeNum].inode = curDir.dentry[curDir.numEntry].inode;
				inode[inodeNum].size = inode[curDir.numEntry].size;
		}
		
		return 0;
}


/**************************************
Change directory
***************************************/
int dir_change(char* name)
{
		// save current dir information
		int curDirInode = curDir.dentry[0].inode;
		disk_write(inode[curDirInode].directBlock[0], (char *)&curDir);
		int inodeNum = search_cur_dir(name);
		
		if(inodeNum < 0) 
		{
				printf("Directory change failed:  %s not exist.\n", name);
				return -1;
		}

		if(inode[inodeNum].type == file) 
		{
				printf("Directory change error: input name <%s> is a file not a directory.\n", name);
				return -1;
		}
		
		disk_read(inode[inodeNum].directBlock[0], (char *)&curDir);		
		return 0;
}

/**************************************
Show the content of the current directory
Implemented By Dr. Lee
***************************************/
int ls()
{
		int i;
		for(i = 0; i < curDir.numEntry; i++)
		{
				int n = curDir.dentry[i].inode;
				if(inode[n].type == file) printf("type: file, ");
				else printf("type: dir, ");
				printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
		}

		return 0;
}

int fs_stat()
{
		printf("File System Status: \n");
		printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{
		if(command(comm, "create")) 
		{
				if(numArg < 2) {
						printf("error: create <filename> <size>\n");
						return -1;
				}
				return file_create(arg1, atoi(arg2)); // (filename, size)
		} else if(command(comm, "cat")) {
				if(numArg < 1) {
						printf("error: cat <filename>\n");
						return -1;
				}
				return file_cat(arg1); // file_cat(filename)
		} else if(command(comm, "write")) {
				if(numArg < 4) {
						printf("error: write <filename> <offset> <size> <buf>\n");
						return -1;
				}
				return file_write(arg1, atoi(arg2), atoi(arg3), arg4); // file_write(filename, offset, size, buf);
		}	else if(command(comm, "read")) {
				if(numArg < 3) {
						printf("error: read <filename> <offset> <size>\n");
						return -1;
				}
				return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);
		} else if(command(comm, "rm")) {
				if(numArg < 1) {
						printf("error: rm <filename>\n");
						return -1;
				}
				return file_remove(arg1); //(filename)
		} else if(command(comm, "mkdir")) {
				if(numArg < 1) {
						printf("error: mkdir <dirname>\n");
						return -1;
				}
				return dir_make(arg1); // (dirname)
		} else if(command(comm, "rmdir")) {
				if(numArg < 1) {
						printf("error: rmdir <dirname>\n");
						return -1;
				}
				return dir_remove(arg1); // (dirname)
		} else if(command(comm, "cd")) {
				if(numArg < 1) {
						printf("error: cd <dirname>\n");
						return -1;
				}
				return dir_change(arg1); // (dirname)
		} else if(command(comm, "ls"))  {
				return ls();
		} else if(command(comm, "stat")) {
				if(numArg < 1) {
						printf("error: stat <filename>\n");
						return -1;
				}
				return file_stat(arg1); //(filename)
		} else if(command(comm, "df")) {
				return fs_stat();
		} else {
				fprintf(stderr, "%s: command not found.\n", comm);
				return -1;
		}
		return 0;
}

