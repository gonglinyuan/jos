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
   

**Remove Bitmap Structure.**

The JOS file system uses a free block *bitmap* to record which disk block is in use and which disk block is free.  When the file system needs a new block, it will look up the bitmap to find an empty slot. However, LFS appends everything right after the used part of the disk, so we do not need the free block bitmap any longer: what we need is just a pointer to mark the start of the unused part of the disk.

In `inc/fs.h`, I added a pointer to the `Super` struct:

```c
struct Super {
	uint32_t s_magic;		// Magic number: FS_MAGIC
	uint32_t s_nblocks;		// Total number of blocks on disk
	struct File s_root;		// Root directory node
	uint32_t s_cur_blk;		// (LFS) Current disk block 
};
```

In `fs/fs.h`, I removed the definition of `bitmap` variable.

In `fs/fs.c`, I changed the `block_is_free()` function:

```c
bool
block_is_free(uint32_t blockno)
{
	if (super == 0 || blockno >= super->s_nblocks)
		return 0;
	return (blockno >= super->s_cur_blk);
}
```

In `fs/fs.c`, I commented out the content of `free_block()` function because it now does nothing.

I changed `alloc_block()` of `fs/fs.c` to be:

```c
int
alloc_block(void)
{
	return super->s_cur_blk++;
}
```

I changed `fs_init()` of `fs/fs.c` to be:

```c
static_assert(sizeof(struct File) == 256);

// Find a JOS disk.  Use the second IDE disk (number 1) if available
if (ide_probe_disk1())
    ide_set_disk(1);
else
    ide_set_disk(0);
bc_init();

// Set "super" to point to the super block.
super = diskaddr(1);
check_super();

// Set "bitmap" to the beginning of the first bitmap block.
check_cur_blk();

// Set "imap" to the inode map
imap = diskaddr(2);
```

In `fs/fsformat.c`, I removed the definition of `bitmap` variable.

I changed `opendisk()` of `fs/fsformat.c` to be:

```c
int r, diskfd;

if ((diskfd = open(name, O_RDWR | O_CREAT, 0666)) < 0)
    panic("open %s: %s", name, strerror(errno));

if ((r = ftruncate(diskfd, 0)) < 0
    || (r = ftruncate(diskfd, nblocks * BLKSIZE)) < 0)
    panic("truncate %s: %s", name, strerror(errno));

if ((diskmap = mmap(NULL, nblocks * BLKSIZE, PROT_READ|PROT_WRITE,
                    MAP_SHARED, diskfd, 0)) == MAP_FAILED)
    panic("mmap %s: %s", name, strerror(errno));

close(diskfd);

diskpos = diskmap;
alloc(BLKSIZE);
super = alloc(BLKSIZE);
super->s_magic = FS_MAGIC;
super->s_nblocks = nblocks;
super->s_root.f_type = FTYPE_DIR;
strcpy(super->s_root.f_name, "/");

imap = alloc((nblocks / INODE_ENT_BLK) * BLKSIZE);
memset(imap, 0x00, (nblocks / INODE_ENT_BLK) * BLKSIZE);
```

I changed `finishdisk()` of `fs/fsformat.c` to be:

```c
int r;

super->s_cur_blk = blockof(diskpos);

if ((r = msync(diskmap, nblocks * BLKSIZE, MS_SYNC)) < 0)
    panic("msync: %s", strerror(errno));
```

**Imap Buffering.**

In LFS, we will implement an in-memory buffer for file updates. This buffer keeps track of all the updates to the file system. After the buffer is full or after some time interval, the updates in the buffer is written to the hard disk. In this state, we implement the simplest part of the buffer: the Inode map buffering.

I defined some data structures in `fs/fs.h`:

```c
uint32_t lfs_tmp_imap[INODE_ENT_BLK];
```

In `fs/fs.c`, I changed all updates to `imap` into updates to `lsf_tmp_imap`:

```c
void
free_inode(uint32_t inode_num)
{
	assert(inode_num > 0 && inode_num < super->s_nblocks);
	// update imap
	lfs_tmp_imap[inode_num] = 0;
}
```

```c
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
	while ((inode_num < super->s_nblocks) && lfs_tmp_imap[inode_num]) {
		++inode_num;
	}
	if (inode_num >= super->s_nblocks) {
		return -E_NO_DISK;
	}
	lfs_tmp_imap[inode_num] = (uint32_t) addr;
	return inode_num;
}
```

```c
static int
dir_lookup(struct File *dir, const char *name, struct File **file)
{
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
				struct File *pf = (struct File*) lfs_tmp_imap[f[j]];
				if (strcmp(pf->f_name, name) == 0) {
					*file = pf;
					return 0;
				}
			}
		}
	}
	return -E_NOT_FOUND;
}
```

```c
static int
dir_alloc_file(struct File *dir, struct File **file)
{
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
				*file = (struct File*) lfs_tmp_imap[f[j]];
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
	*file = (struct File*) lfs_tmp_imap[f[0]];
	return 0;
}
```

Then, in `fs/fs.c`, I defined two functions `lfs_sync_from_disk()` and `lfs_sync_to_disk()` to synchronize the imap buffer with the disk:

```c
void
lfs_sync_from_disk(void)
{
	memmove(lfs_tmp_imap, imap, sizeof(lfs_tmp_imap));
}

void
lfs_sync_to_disk(void)
{
	memmove(imap, lfs_tmp_imap, sizeof(lfs_tmp_imap));
	flush_block(imap);
}
```

I added a call to `lfs_sync_from_disk()` at the end of `fs_init()` in `fs/fs.c`; I also added a call to `lfs_sync_to_disk()`  at the end of the main loop of `serve()` in `fs/serv.c`.

**Use Inode Numbers in File System APIs.**

In the JOS file system, `fs/fs.h` exposes some file APIs to `fs/serv.c`. They are: `file_get_block`, `file_create`, `file_open`, `file_read`, `file_write`, `file_set_size`, and `file_flush`. In the lab 5 of JOS, all these APIs use `struct File *` as arguments to pass file handlers. However, to make sure that we can implement LFS in next stages, and because we have already implemented a Unix-like inode structure, we have to change these APIs to use *inode number*s rather than `struct File *`s as arguments.

In `fs/fs.h`, I changed all the function signatures of these functions:

```c
int file_get_block(uint32_t inode_num, uint32_t filebno, char **blk);
int file_create(const char *path, uint32_t *p_inode_num);
int file_open(const char *path, uint32_t *p_inode_num);
ssize_t file_read(uint32_t inode_num, void *buf, size_t count, off_t offset);
int	file_write(uint32_t inode_num, const void *buf, size_t count, off_t offset);
int	file_set_size(uint32_t inode_num, off_t newsize);
void	file_flush(uint32_t inode_num);
```

In `fs/fs.c`, I modified `file_block_walk()`:

```c
int r;
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

if (filebno < NDIRECT) {
    *ppdiskbno = f->f_direct + filebno;
} else if (filebno < NDIRECT + NINDIRECT) {
    if (!f->f_indirect) {
        if (alloc) {
            if ((r = alloc_block()) < 0) {
                return r;
            }
            f->f_indirect = r;
        } else {
            return -E_NOT_FOUND;
        }
    }
    *ppdiskbno = ((uint32_t *) diskaddr(f->f_indirect)) + (filebno - NDIRECT);
} else { // filebno >= NDIRECT + NINDIRECT
    return -E_INVAL;
}
return 0;
```

In `fs/fs.c`, I modified `file_get_block()`:

```c
int r;
uint32_t *ppdiskbno;
if ((r = file_block_walk(inode_num, filebno, &ppdiskbno, true)) < 0) {
    return r;
}
if (!(*ppdiskbno)) {
    if ((r = alloc_block()) < 0) {
        return r;
    }
    *ppdiskbno = r;
}
*blk = diskaddr(*ppdiskbno);
return 0;
```

In `fs/fs.c`, I modified `dir_lookup()`:

```c
int r;
uint32_t i, j, nblock;
char *blk;
uint32_t *f;
struct File *dir;

dir = (struct File *) lfs_tmp_imap[inode_num_dir];

// Search dir for name.
// We maintain the invariant that the size of a directory-file
// is always a multiple of the file system's block size.
assert((dir->f_size % BLKSIZE) == 0);
nblock = dir->f_size / BLKSIZE;
for (i = 0; i < nblock; i++) {
    if ((r = file_get_block(inode_num_dir, i, &blk)) < 0)
        return r;
    f = (uint32_t*) blk;
    for (j = 0; j < INODE_ENT_BLK; ++j) {
        if (f[j]) {
            struct File *pf = (struct File*) lfs_tmp_imap[f[j]];
            if (strcmp(pf->f_name, name) == 0) {
                *p_inode_num_file = f[j];
                return 0;
            }
        }
    }
}
return -E_NOT_FOUND;
```

In `fs/fs.c`, I modified `dir_alloc_file()`:

```c
int r;
uint32_t nblock, i, j;
char *blk;
uint32_t *f;
struct File *dir;

dir = (struct File *) lfs_tmp_imap[inode_num_dir];

assert((dir->f_size % BLKSIZE) == 0);
nblock = dir->f_size / BLKSIZE;
for (i = 0; i < nblock; i++) {
    if ((r = file_get_block(inode_num_dir, i, &blk)) < 0)
        return r;
    f = (uint32_t*) blk;
    for (j = 0; j < INODE_ENT_BLK; ++j) {
        if (!f[j]) {
            f[j] = alloc_inode();
            *p_inode_num_file = f[j];
            return 0;
        }
    }
}
dir->f_size += BLKSIZE;
if ((r = file_get_block(inode_num_dir, i, &blk)) < 0)
    return r;
memset(blk, 0x00, BLKSIZE);
f = (uint32_t*) blk;
f[0] = alloc_inode();
*p_inode_num_file = f[0];
return 0;
```

In `fs/fs.c`, I modified `walk_path()`:

```c
const char *p;
char name[MAXNAMELEN];
struct File *dir;
uint32_t inode_num_dir, inode_num_file;
int r;

// if (*path != '/')
//	return -E_BAD_PATH;
path = skip_slash(path);
inode_num_file = super->s_root;
inode_num_dir = 0;
dir = 0;
name[0] = 0;

if (p_inode_num_dir)
    *p_inode_num_dir = 0;
*p_inode_num_file = 0;
while (*path != '\0') {
    inode_num_dir = inode_num_file;
    dir = (struct File *) lfs_tmp_imap[inode_num_dir];
    p = path;
    while (*path != '/' && *path != '\0')
        path++;
    if (path - p >= MAXNAMELEN)
        return -E_BAD_PATH;
    memmove(name, p, path - p);
    name[path - p] = '\0';
    path = skip_slash(path);

    if (dir->f_type != FTYPE_DIR)
        return -E_NOT_FOUND;

    if ((r = dir_lookup(inode_num_dir, name, &inode_num_file)) < 0) {
        if (r == -E_NOT_FOUND && *path == '\0') {
            if (p_inode_num_dir)
                *p_inode_num_dir = inode_num_dir;
            if (lastelem)
                strcpy(lastelem, name);
            *p_inode_num_file = 0;
        }
        return r;
    }
}

if (p_inode_num_dir)
    *p_inode_num_dir = inode_num_dir;
*p_inode_num_file = inode_num_file;
return 0;
```

In `fs/fs.c`, I modified `file_create()`:

```c
char name[MAXNAMELEN];
int r;
struct File *f;
uint32_t inode_num_dir, inode_num_file;

if ((r = walk_path(path, &inode_num_dir, &inode_num_file, name)) == 0)
    return -E_FILE_EXISTS;
if (r != -E_NOT_FOUND || inode_num_dir == 0)
    return r;
if ((r = dir_alloc_file(inode_num_dir, &inode_num_file)) < 0)
    return r;
f = (struct File *) lfs_tmp_imap[inode_num_file];

strcpy(f->f_name, name);
*p_inode_num = inode_num_file;
file_flush(inode_num_dir);
return 0;
```

In `fs/fs.c`, I modified `file_open()`:

```c
return walk_path(path, 0, p_inode_num, 0);
```

In `fs/fs.c`, I modified `file_read()`:

```c
int r, bn;
off_t pos;
char *blk;
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

if (offset >= f->f_size)
    return 0;

count = MIN(count, f->f_size - offset);

for (pos = offset; pos < offset + count; ) {
    if ((r = file_get_block(inode_num, pos / BLKSIZE, &blk)) < 0)
        return r;
    bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
    memmove(buf, blk + pos % BLKSIZE, bn);
    pos += bn;
    buf += bn;
}

return count;
```

In `fs/fs.c`, I modified `file_write()`:

```c
int r, bn;
off_t pos;
char *blk;
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

// Extend file if necessary
if (offset + count > f->f_size)
    if ((r = file_set_size(inode_num, offset + count)) < 0)
        return r;

for (pos = offset; pos < offset + count; ) {
    if ((r = file_get_block(inode_num, pos / BLKSIZE, &blk)) < 0)
        return r;
    bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
    memmove(blk + pos % BLKSIZE, buf, bn);
    pos += bn;
    buf += bn;
}

return count;
```

In `fs/fs.c`, I modified `file_free_block()`:

```c
int r;
uint32_t *ptr;
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

if ((r = file_block_walk(inode_num, filebno, &ptr, 0)) < 0)
    return r;
if (*ptr) {
    free_block(*ptr);
    *ptr = 0;
}
return 0;
```

In `fs/fs.c`, I modified `file_truncate_blocks()`:

```c
int r;
uint32_t bno, old_nblocks, new_nblocks;
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

old_nblocks = (f->f_size + BLKSIZE - 1) / BLKSIZE;
new_nblocks = (newsize + BLKSIZE - 1) / BLKSIZE;
for (bno = new_nblocks; bno < old_nblocks; bno++)
    if ((r = file_free_block(inode_num, bno)) < 0)
        cprintf("warning: file_free_block: %e", r);

if (new_nblocks <= NDIRECT && f->f_indirect) {
    free_block(f->f_indirect);
    f->f_indirect = 0;
}
```

In `fs/fs.c`, I modified `file_set_size()`:

```c
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

if (f->f_size > newsize)
    file_truncate_blocks(inode_num, newsize);
f->f_size = newsize;
flush_block(f);
return 0;
```

In `fs/fs.c`, I modified `file_flush()`:

```c
int i;
uint32_t *pdiskbno;
struct File *f;

f = (struct File *) lfs_tmp_imap[inode_num];

for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
    if (file_block_walk(inode_num, i, &pdiskbno, 0) < 0 ||
        pdiskbno == NULL || *pdiskbno == 0)
        continue;
    flush_block(diskaddr(*pdiskbno));
}
flush_block(f);
if (f->f_indirect)
    flush_block(diskaddr(f->f_indirect));
```

In `fs/serv.c`, I modified all calls to these functions to use inode numbers instead of `struct File *`s.

The changing of APIs also implies that we should use inode numbers in the *open file table*.

In `fs/serv.c`, I modified the definition of `struct OpenFile`:

```c
struct OpenFile {
	uint32_t o_fileid;	// file id
	uint32_t o_file;	// inode num for open file
	int o_mode;		// open mode
	struct Fd *o_fd;	// Fd page
};
```

In `serve_open()` of `fs/serv.c`, I changed how the `o->o_file` is set:

```c
// Save the file pointer
o->o_file = inode_num;
```

In `fs/serve.c`, I modified `serve_stat()`:

```c
struct Fsreq_stat *req = &ipc->stat;
struct Fsret_stat *ret = &ipc->statRet;
struct OpenFile *o;
struct File *f;
int r;

if (debug)
    cprintf("serve_stat %08x %08x\n", envid, req->req_fileid);

if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
    return r;

f = (struct File *) lfs_tmp_imap[o->o_file];
strcpy(ret->ret_name, f->f_name);
ret->ret_size = f->f_size;
ret->ret_isdir = (f->f_type == FTYPE_DIR);
return 0;
```

Moreover, we move the inode of root directory (`s_root`) out of the `Super` structure, and store the inode number of the root directory in the `Super` block instead. By default, the inode number of the root directory is `1`.

In `inc/fs.h`, I changed the definition of `struct Super`:

```c
struct Super {
	uint32_t s_magic;		// Magic number: FS_MAGIC
	uint32_t s_nblocks;		// Total number of blocks on disk
	uint32_t s_root;		// Inode num of root directory
	uint32_t s_cur_blk;		// (LFS) Current disk block 
};
```

In `fs/fsformat.c`, I modified `opendisk()` and make it return the pointer to the inode of root directory:

```c
struct File *
opendisk(const char *name)
{
	int r, diskfd;

	if ((diskfd = open(name, O_RDWR | O_CREAT, 0666)) < 0)
		panic("open %s: %s", name, strerror(errno));

	if ((r = ftruncate(diskfd, 0)) < 0
	    || (r = ftruncate(diskfd, nblocks * BLKSIZE)) < 0)
		panic("truncate %s: %s", name, strerror(errno));

	if ((diskmap = mmap(NULL, nblocks * BLKSIZE, PROT_READ|PROT_WRITE,
			    MAP_SHARED, diskfd, 0)) == MAP_FAILED)
		panic("mmap %s: %s", name, strerror(errno));

	close(diskfd);

	diskpos = diskmap;
	alloc(BLKSIZE);
	super = alloc(BLKSIZE);
	super->s_magic = FS_MAGIC;
	super->s_nblocks = nblocks;
	super->s_root = 1;

	imap = alloc((nblocks / INODE_ENT_BLK) * BLKSIZE);
	memset(imap, 0x00, (nblocks / INODE_ENT_BLK) * BLKSIZE);

	struct File *f_root = alloc(BLKSIZE);
	f_root->f_type = FTYPE_DIR;
	strcpy(f_root->f_name, "/");

	return f_root;
}
```

In `fs/fsformat.c`, I modified `finishdir()`:

```c
int size = d->n * sizeof(struct File);
struct File *start = alloc(size);
uint32_t *start2 = alloc(d->n * sizeof(uint32_t));
memmove(start, d->ents, size);
for (uint32_t i = 0; i < d->n; ++i) {
    imap[i + 2] = (uint32_t)(start + i) - (uint32_t) diskmap + 0x10000000;
    start2[i] = i + 2;
}
finishfile(d->f, blockof(start2), ROUNDUP(d->n * sizeof(uint32_t), BLKSIZE));
free(d->ents);
d->ents = NULL;
```

In `main()` of `fs/fsformat.c`, I set the imap correctly:

```c
struct File *f_root = opendisk(argv[1]);

startdir(f_root, &root);
for (i = 3; i < argc; i++)
    writefile(&root, argv[i]);
finishdir(&root);
imap[1] = (uint32_t) f_root - (uint32_t) diskmap + 0x10000000;
```

**Log-structured Inode System.**

In this stage, we are ready to implement the LFS-like inode operations. We never overwrite any inodes in place; instead, we append all updated inodes after the end of the used part of the disk, and we also update imaps to point to the new inodes. In this way, our file system has some good *crash-recovery* characteristics: all the histories of inode updates are kept: whenever the machine crashes, we can still recover a consistent file system structure. In addition, the write speed of our file system will increase, since we buffer inode updates and write them as a *sequential* segment together into the disk.

In `fs/fs.h`, I defined some data structures:

```c
#define LFS_BUFSIZE	(BLKSIZE / 256)	    // size of LFS file update buffer

bool lfs_imap_dirty[INODE_ENT_BLK];
struct File lfs_inode_buf[LFS_BUFSIZE];
uint32_t lfs_inode_buf_sz;

const struct File *lfs_imap_get_for_read(uint32_t inode_num);
```

In `fs/fs.c`, I defined some utility functions to manage the LFS buffer:

```c
// --------------------------------------------------------------
// LFS Inode Buffer
// --------------------------------------------------------------

const struct File *
lfs_imap_get_for_read(uint32_t inode_num)
{
	if (inode_num == 0 || inode_num >= INODE_ENT_BLK) {
		return NULL;
	} else if (lfs_imap_dirty[inode_num]) {
		return (const struct File *) &lfs_inode_buf[lfs_tmp_imap[inode_num]];
	} else {
		return (const struct File *) lfs_tmp_imap[inode_num];
	}
}

struct File *
lfs_imap_get_for_write(uint32_t inode_num)
{
	if (inode_num == 0 || inode_num >= INODE_ENT_BLK) {
		return NULL;
	}
	if (!lfs_imap_dirty[inode_num]) {
		lfs_inode_buf[lfs_inode_buf_sz] = *(struct File *) lfs_tmp_imap[inode_num];
		lfs_tmp_imap[inode_num] = lfs_inode_buf_sz;
		lfs_imap_dirty[inode_num] = true;
		lfs_inode_buf_sz++;
	}
	return &lfs_inode_buf[lfs_tmp_imap[inode_num]];
}

uint32_t
lfs_alloc_inode(void)
{
	// allocate inode number
	uint32_t inode_num = 1;
	while (lfs_imap_get_for_read(inode_num)) {
		++inode_num;
	}
	if (inode_num >= super->s_nblocks) {
		return -E_NO_DISK;
	}
	// allocate a slot in the inode buf and update tmp imap
	lfs_tmp_imap[inode_num] = lfs_inode_buf_sz;
	lfs_imap_dirty[inode_num] = true;
	lfs_inode_buf_sz++;
	return inode_num;
}

void
lfs_free_inode(uint32_t inode_num)
{
	assert(inode_num > 0 && inode_num < super->s_nblocks);
	// update imap
	lfs_tmp_imap[inode_num] = 0;
	lfs_imap_dirty[inode_num] = false;
}
```

The old function to manages inodes, `alloc_inode()` and `free_inode()`, are now deprecated and removed from the source code.

In `fs/fs.c`, I also changed the functions to synchronize LFS and the disk: I make them also write back the buffered inode updates to the disk and clear the dirty bits in the array:

```c
void
lfs_sync_from_disk(void)
{
	lfs_inode_buf_sz = 0;
	memmove(lfs_tmp_imap, imap, sizeof(lfs_tmp_imap));
	memset(lfs_imap_dirty, 0, sizeof(lfs_imap_dirty));
}

void
lfs_sync_to_disk(void)
{
	// Write back inode
	if (lfs_inode_buf_sz) {
		struct File *addr = diskaddr(super->s_cur_blk++);
		memmove(addr, lfs_inode_buf, sizeof(lfs_inode_buf));
		flush_block(addr);
		for (uint32_t i = 0; i < INODE_ENT_BLK; ++i) {
			if (lfs_imap_dirty[i]) {
				lfs_tmp_imap[i] = (uint32_t)(addr + lfs_tmp_imap[i]);
				lfs_imap_dirty[i] = false;
			}
		}
		lfs_inode_buf_sz = 0;
	}
	// Write back imap
	memmove(imap, lfs_tmp_imap, sizeof(lfs_tmp_imap));
	memset(lfs_imap_dirty, 0, sizeof(lfs_imap_dirty));
	flush_block(imap);
}
```

In `fs/fs.c` and `fs/serv.c`, I also changed all the references to `lfs_tmp_imap[]` to function calls to `lfs_imap_get_for_read()` or `lfs_imap_get_for_write()`.

Another thing to notice is that we cannot "flush" a buffered inode since it is not in the disk. So I add some wrappers to `flush_block(f)` as a sanity check:

```c
if ((uint32_t) f > DISKMAP) {
	flush_block(f);
}
```

**Log-structured File System.**

Finally, we are able to move all data block updates into the log. Whenever we update a data block, we never overwrite the previous location; instead, we append the updated data block to the end of the used part of the disk. The appended inode and data together make up a "log" of disk updates, so our file system is called a *log-structured file system* (**LFS**).

In `fs/fs.h`, we define the buffer for data block updates:

```c
#define MAX_BLOCKNO 2048

char lfs_data_buf[LFS_BUFSIZE][BLKSIZE];
uint32_t lfs_data_buf_sz;

uint32_t lfs_alloc_data(void);
```

In `fs/fs.c`, we define some utility functions to manage this data block buffer:

```c
// --------------------------------------------------------------
// LFS data buffer
// --------------------------------------------------------------

const void *
lfs_data_get_for_read(uint32_t blockno)
{
	assert(blockno > 0);
	if (blockno >= super->s_cur_blk) {
		memset(lfs_data_buf[blockno - super->s_cur_blk], 0x00, BLKSIZE);
		return (const void *) lfs_data_buf[blockno - super->s_cur_blk];
	} else {
		return (const void *) diskaddr(blockno);
	}
}

uint32_t
lfs_alloc_data(void)
{
	uint32_t blockno = super->s_cur_blk + lfs_data_buf_sz;
	lfs_data_buf_sz++;
	return blockno;
}
```

The old function to manages data blocks, `alloc_block()`, is now deprecated and removed from the source code.

In `fs/fs.c`, I also changed the functions to synchronize LFS and the disk: I make them also write back the buffered block updates to the disk:

```c
void
lfs_sync_from_disk(void)
{
	lfs_inode_buf_sz = 0;
	lfs_data_buf_sz = 0;
	memmove(lfs_tmp_imap, imap, sizeof(lfs_tmp_imap));
	memset(lfs_imap_dirty, 0, sizeof(lfs_imap_dirty));
}

void
lfs_sync_to_disk(void)
{
	// Write back data
	if (lfs_data_buf_sz) {
		char *addr = diskaddr(super->s_cur_blk);
		super->s_cur_blk += lfs_data_buf_sz;
		memmove(addr, lfs_data_buf, BLKSIZE * lfs_data_buf_sz);
		for (uint32_t i = 0; i < lfs_data_buf_sz; ++i) {
			flush_block(addr + (i * BLKSIZE));
		}
		lfs_data_buf_sz = 0;
	}
	// Write back inode
	if (lfs_inode_buf_sz) {
		struct File *addr = diskaddr(super->s_cur_blk++);
		memmove(addr, lfs_inode_buf, sizeof(lfs_inode_buf));
		flush_block(addr);
		for (uint32_t i = 0; i < INODE_ENT_BLK; ++i) {
			if (lfs_imap_dirty[i]) {
				lfs_tmp_imap[i] = (uint32_t)(addr + lfs_tmp_imap[i]);
				lfs_imap_dirty[i] = false;
			}
		}
		lfs_inode_buf_sz = 0;
	}
	// Write back imap
	memmove(imap, lfs_tmp_imap, sizeof(lfs_tmp_imap));
	memset(lfs_imap_dirty, 0, sizeof(lfs_imap_dirty));
	flush_block(imap);
}
```

In `fs/fs.c`, I also changed all the references to data blocks to function calls to `lfs_data_get_for_read()`. 

I also wrapped some calls to `flush_block()` to avoid flushing anything in the buffer.

```c
if ((uint32_t) addr > DISKMAP) {
    flush_block((void *) addr);
}
```

**Conclusion.**

In this extra lab, I implemented a minimal version *log-structured file system*. It has most basic properties of a LFS, such as write buffering and persistence. It make use of the fact that sequential writing to a hard disk is substantially faster than random writing, to improve the performance of the file system. However, there are still many things to accomplish:

1.  In a real LFS, because the disk space to manage is usually very large, the imap cannot fit into one or two blocks like our JOS file system; therefore, imap of a real LFS is scattered across the entire disk rather than stored in a fixed place. If we want to use this LFS in a larger system, we should implement this feature.

2. Our implementation has some performance issues, because we use software to monitor each read and write to the hard disk, instead of using hardware features such as `PTE_D`. A better implementation is possible if we want make our LFS faster.

3. Our implementation of LFS does not have a garbage collection system. Therefore, the disk space utilization is not as high as a standard LFS. However, this implementation also has an advantage in crash-recovery, since it keeps more file histories.

This is a statistic of my effort:

```
$ git diff --stat 99729321db8c8c368890763cce2b9bd32b90012d extralab
 README.md      | 1075 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 fs/bc.c        |    2 +-
 fs/fs.c        |  359 +++++++++++++------
 fs/fs.h        |   32 +-
 fs/fsformat.c  |   37 +-
 fs/serv.c      |   24 +-
 fs/test.c      |   42 ++-
 grade-lab5     |    4 +-
 gradelib.py    |    6 +-
 inc/fs.h       |    4 +-
 kern/monitor.c |    3 +-
 11 files changed, 1421 insertions(+), 167 deletions(-)
```

I changed 513 lines of code in total.

**Grading.**

I run the lab 5 grading system for my extra lab. Due to some discrepancies in the file API, I made some minor modifications to the test scripts. The grading result is:

```
vagrant@ubuntu-xenial:~/jos$ make grade
make clean
make[1]: Entering directory '/home/vagrant/jos'
rm -rf obj .gdbinit jos.in qemu.log
make[1]: Leaving directory '/home/vagrant/jos'
./grade-lab5
make[1]: Entering directory '/home/vagrant/jos'
+ as kern/entry.S
+ cc kern/entrypgdir.c
+ cc kern/init.c
+ cc kern/console.c
+ cc kern/monitor.c
+ cc kern/pmap.c
+ cc kern/env.c
+ cc kern/kclock.c
+ cc kern/picirq.c
+ cc kern/printf.c
+ cc kern/trap.c
+ as kern/trapentry.S
+ cc kern/sched.c
+ cc kern/syscall.c
+ cc kern/kdebug.c
+ cc lib/printfmt.c
+ cc lib/readline.c
+ cc lib/string.c
+ as kern/mpentry.S
+ cc kern/mpconfig.c
+ cc kern/lapic.c
+ cc kern/spinlock.c
+ as[USER] lib/entry.S
+ cc[USER] lib/console.c
+ cc[USER] lib/libmain.c
+ cc[USER] lib/exit.c
+ cc[USER] lib/panic.c
+ cc[USER] lib/printf.c
+ cc[USER] lib/printfmt.c
+ cc[USER] lib/readline.c
+ cc[USER] lib/string.c
+ cc[USER] lib/syscall.c
+ cc[USER] lib/pgfault.c
+ as[USER] lib/pfentry.S
+ cc[USER] lib/fork.c
+ cc[USER] lib/ipc.c
+ cc[USER] lib/args.c
+ cc[USER] lib/fd.c
+ cc[USER] lib/file.c
+ cc[USER] lib/fprintf.c
+ cc[USER] lib/pageref.c
+ cc[USER] lib/spawn.c
+ cc[USER] lib/pipe.c
+ cc[USER] lib/wait.c
+ ar obj/lib/libjos.a
ar: creating obj/lib/libjos.a
+ cc[USER] user/hello.c
+ ld obj/user/hello
+ cc[USER] user/buggyhello.c
+ ld obj/user/buggyhello
+ cc[USER] user/buggyhello2.c
+ ld obj/user/buggyhello2
+ cc[USER] user/evilhello.c
+ ld obj/user/evilhello
+ cc[USER] user/testbss.c
+ ld obj/user/testbss
+ cc[USER] user/divzero.c
+ ld obj/user/divzero
+ cc[USER] user/breakpoint.c
+ ld obj/user/breakpoint
+ cc[USER] user/softint.c
+ ld obj/user/softint
+ cc[USER] user/badsegment.c
+ ld obj/user/badsegment
+ cc[USER] user/faultread.c
+ ld obj/user/faultread
+ cc[USER] user/faultreadkernel.c
+ ld obj/user/faultreadkernel
+ cc[USER] user/faultwrite.c
+ ld obj/user/faultwrite
+ cc[USER] user/faultwritekernel.c
+ ld obj/user/faultwritekernel
+ cc[USER] user/idle.c
+ ld obj/user/idle
+ cc[USER] user/yield.c
+ ld obj/user/yield
+ cc[USER] user/dumbfork.c
+ ld obj/user/dumbfork
+ cc[USER] user/stresssched.c
+ ld obj/user/stresssched
+ cc[USER] user/faultdie.c
+ ld obj/user/faultdie
+ cc[USER] user/faultregs.c
+ ld obj/user/faultregs
+ cc[USER] user/faultalloc.c
+ ld obj/user/faultalloc
+ cc[USER] user/faultallocbad.c
+ ld obj/user/faultallocbad
+ cc[USER] user/faultnostack.c
+ ld obj/user/faultnostack
+ cc[USER] user/faultbadhandler.c
+ ld obj/user/faultbadhandler
+ cc[USER] user/faultevilhandler.c
+ ld obj/user/faultevilhandler
+ cc[USER] user/forktree.c
+ ld obj/user/forktree
+ cc[USER] user/sendpage.c
+ ld obj/user/sendpage
+ cc[USER] user/spin.c
+ ld obj/user/spin
+ cc[USER] user/fairness.c
+ ld obj/user/fairness
+ cc[USER] user/pingpong.c
+ ld obj/user/pingpong
+ cc[USER] user/pingpongs.c
+ ld obj/user/pingpongs
+ cc[USER] user/primes.c
+ ld obj/user/primes
+ cc[USER] user/faultio.c
+ ld obj/user/faultio
+ cc[USER] user/spawnfaultio.c
+ ld obj/user/spawnfaultio
+ cc[USER] user/testfile.c
+ ld obj/user/testfile
+ cc[USER] user/spawnhello.c
+ ld obj/user/spawnhello
+ cc[USER] user/icode.c
+ ld obj/user/icode
+ cc[USER] fs/ide.c
+ cc[USER] fs/bc.c
+ cc[USER] fs/fs.c
+ cc[USER] fs/serv.c
+ cc[USER] fs/test.c
+ ld obj/fs/fs
+ cc[USER] user/testpteshare.c
+ ld obj/user/testpteshare
+ cc[USER] user/testfdsharing.c
+ ld obj/user/testfdsharing
+ cc[USER] user/testpipe.c
+ ld obj/user/testpipe
+ cc[USER] user/testpiperace.c
+ ld obj/user/testpiperace
+ cc[USER] user/testpiperace2.c
+ ld obj/user/testpiperace2
+ cc[USER] user/primespipe.c
+ ld obj/user/primespipe
+ cc[USER] user/testkbd.c
+ ld obj/user/testkbd
+ cc[USER] user/testshell.c
+ ld obj/user/testshell
+ ld obj/kern/kernel
+ as boot/boot.S
+ cc -Os boot/main.c
+ ld boot/boot
boot block is 414 bytes (max 510)
+ mk obj/kern/kernel.img
+ mk obj/fs/fsformat
+ cc[USER] user/init.c
+ ld obj/user/init
+ cc[USER] user/cat.c
+ ld obj/user/cat
+ cc[USER] user/echo.c
+ ld obj/user/echo
+ cc[USER] user/ls.c
+ ld obj/user/ls
+ cc[USER] user/lsfd.c
+ ld obj/user/lsfd
+ cc[USER] user/num.c
+ ld obj/user/num
+ cc[USER] user/sh.c
+ ld obj/user/sh
+ mk obj/fs/clean-fs.img
+ cp obj/fs/clean-fs.img obj/fs/fs.img
make[1]: Leaving directory '/home/vagrant/jos'
internal FS tests [fs/test.c]: OK (1.6s)
  fs i/o: OK
  check_bc: OK
  check_super: OK
  check_cur_blk: OK
  alloc_block: OK
  file_open: OK
  file_get_block: OK
  file_flush/file_truncate/file rewrite: OK
testfile: OK (3.7s)
  serve_open/file_stat/file_close: OK
  file_read: OK
  file_write: OK
  file_read after file_write: OK
  open: OK
  large file: OK
spawn via spawnhello: OK (1.3s)
Protection I/O space: OK (1.4s)
PTE_SHARE [testpteshare]: OK (2.4s)
PTE_SHARE [testfdsharing]: OK (1.4s)
start the shell [icode]: Timeout! OK (61.2s)
testshell: OK (9.7s)
primespipe: OK (13.0s)
Score: 100% (150/150)
```

  