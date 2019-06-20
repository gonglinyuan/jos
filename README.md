# Extra Lab

龚林源 1600012714

**Introduction.**

In this extra lab, I implement a "[Log-Structured File System](<https://people.eecs.berkeley.edu/~brewer/cs262/LFS.pdf>) (**LFS**)" for JOS, which is firstly developed by Mendel Rosenblum and John K. Ousterhout. Log-structured file system focus on write performance, and try to make use of the sequential bandwidth of the disk. Moreover, it would perform well on common workloads that not only write out data but also update on-disk metadata structures frequently.

When writing to disk, LFS first *buffers* all updates (including *both data and metadata*) in an in memory **segment**; when the segment is full, it is written to disk in one long, *sequential* transfer to an *unused* part of the disk: Because segments are large, the disk is used efficiently, and performance of the file system approaches its zenith.

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Allocate Region for Inodes.**

In the lab 5 of JOS, we implement a simple file system, where the metadata of a file is stored in the data blocks of its parent directory. However, in Unix, and also in LFS, each file has an *inode*, which is stored in a place which is separate from data blocks. Therefore, before implementing the LFS, I first implement the inode data structure for the JOS file system.

I changed the file system in these ways:

1. I added a *inode map (imap)* after the superblock (block 1) and the free block bitmap (block 2) of the file system. Imap can map an inode number to the disk address of the inode, and each entry takes `4B` of space. The imap occupies `1` block, so it is `1 * 4KB = 4KB`  is size; hence, it can contain up to `4KB / 4B = 1024` imap entries, so our file system can support up to `1024` inodes.

   In `inc/fs.h`, I defined the number of imap entries per block:

   ```c
   #define INODE_ENT_BLK	(BLKSIZE / 4)
   ```

   In `fs/fs.h`, I defined the variable `imap`:

   ```c
   uint32_t *imap;		// inode map mapped in memory
   ```

   In `opendisk()` of `fs/fsformat.c`, I added:

   ```c
   imap = alloc((nblocks / INODE_ENT_BLK) * BLKSIZE);
   memset(imap, 0x00, (nblocks / INODE_ENT_BLK) * BLKSIZE);
   ```
   
   In `fs_init()` of `fs/fs.c`, I added:
   
   ```c
   // Set "imap" to the inode map
   imap = diskaddr(3);
   ```

2. I added some functions to allocate and free inode.

   In `fs/fs.c`, I added:

   ```c
   void
   free_inode(uint32_t inode_num)
   {
   	assert(inode_num > 0 && inode_num < super->s_nblocks);
   	// update imap
   	imap[inode_num] = 0;
   	flush_block(&imap[inode_num]);
   }
   
   int
   alloc_inode(void)
   {
   	int r;
   	// allocate block and store inode
   	if ((r = alloc_block()) < 0) {
   		return r;
   	}
   	struct File *addr = diskaddr(r);
   	// allocate inode number and update imap
   	uint32_t inode_num = 1;
   	while ((inode_num < super->s_nblocks) && imap[inode_num]) {
   		++inode_num;
   	}
   	if (inode_num >= super->s_nblocks) {
   		return -E_NO_DISK;
   	}
   	imap[inode_num] = (uint32_t) addr;
   	flush_block(&imap[inode_num]);
   	return inode_num;
   }
   ```

3. I changed the data structure of directories. In JOS, a directory is just an array of `struct File`s. Now we have inodes stored in other places on the disk, so we regard a directory as an array of *inode number*s instead.

   I changed `dir_lookup()` of `fs/fs.c` to be:

   ```c
   int r;
   uint32_t i, j, nblock;
   char *blk;
   uint32_t *f;
   
   // Search dir for name.
   // We maintain the invariant that the size of a directory-file
   // is always a multiple of the file system's block size.
   assert((dir->f_size % BLKSIZE) == 0);
   nblock = dir->f_size / BLKSIZE;
   for (i = 0; i < nblock; i++) {
       if ((r = file_get_block(dir, i, &blk)) < 0)
           return r;
       f = (uint32_t*) blk;
       for (j = 0; j < INODE_ENT_BLK; ++j) {
           if (f[j]) {
               struct File *pf = (struct File*) imap[f[j]];
               if (strcmp(pf->f_name, name) == 0) {
                   *file = pf;
                   return 0;
               }
           }
       }
   }
   return -E_NOT_FOUND;
   ```

   I changed `dir_alloc_file()` of `fs/fs.c` to be:

   ```c
   int r;
   uint32_t nblock, i, j;
   char *blk;
   uint32_t *f;
   
   assert((dir->f_size % BLKSIZE) == 0);
   nblock = dir->f_size / BLKSIZE;
   for (i = 0; i < nblock; i++) {
       if ((r = file_get_block(dir, i, &blk)) < 0)
           return r;
       f = (uint32_t*) blk;
       for (j = 0; j < INODE_ENT_BLK; ++j) {
           if (!f[j]) {
               f[j] = alloc_inode();
               *file = (struct File*) imap[f[j]];
               return 0;
           }
       }
   }
   dir->f_size += BLKSIZE;
   if ((r = file_get_block(dir, i, &blk)) < 0)
       return r;
   memset(blk, 0x00, BLKSIZE);
   f = (uint32_t*) blk;
   f[0] = alloc_inode();
   *file = (struct File*) imap[f[0]];
   return 0;
   ```
   
   I changed `finishdir()` of `fs/fsformat.c` to be:

   ```c
int size = d->n * sizeof(struct File);
   struct File *start = alloc(size);
   uint32_t *start2 = alloc(d->n * sizeof(uint32_t));
   memmove(start, d->ents, size);
   for (uint32_t i = 0; i < d->n; ++i) {
       imap[i + 1] = (uint32_t)(start + i) - (uint32_t) diskmap + 0x10000000;
       start2[i] = i + 1;
   }
   finishfile(d->f, blockof(start2), ROUNDUP(d->n * sizeof(uint32_t), BLKSIZE));
   free(d->ents);
   d->ents = NULL;
   ```
   

**File Updates Buffering.**

In this stage, we implement an in-memory buffer for file updates. This buffer keeps track of all the updates to the file system. After the buffer is full or after some time interval, the updates in the buffer is written to the hard disk. In the current stage, the performance of our file system cannot benefit from this buffering mechanic because the file updates are still multiple random writes to the disk. However, after we implement *persistent file updates* in the next stage, our FS will benefit a lot from the buffering. 



**Persistent File Updates.**

Traditional file systems (such as old Unix file system and the file system in the lab 5 of JOS) implement implement in place file modifications: for example, when you change some bytes of a file, the file system modifies the data block and the inode of the file exactly in its original place. In order to keep the file system consistent from accidental crashes, these traditional file systems usually introduce **Journaling**: keep an extra "log" for pending changes to the file system. However, in a LFS, we do not keep a file journal in somewhere else: the file system itself is a "log". Therefore, we do not modify anything in place in a LFS; instead, we *append* the changes to the file system.

