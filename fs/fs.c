#include <inc/string.h>
#include <inc/partition.h>

#include "fs.h"

// --------------------------------------------------------------
// Super block
// --------------------------------------------------------------

// Validate the file system super-block.
void
check_super(void)
{
	if (super->s_magic != FS_MAGIC)
		panic("bad file system magic number");

	if (super->s_nblocks > DISKSIZE/BLKSIZE)
		panic("file system is too large");

	cprintf("superblock is good\n");
}

// --------------------------------------------------------------
// Free block bitmap
// --------------------------------------------------------------

// Check to see if the block bitmap indicates that block 'blockno' is free.
// Return 1 if the block is free, 0 if not.
bool
block_is_free(uint32_t blockno)
{
	if (super == 0 || blockno >= super->s_nblocks)
		return 0;
	return (blockno >= super->s_cur_blk);
}

// Mark a block free in the bitmap
void
free_block(uint32_t blockno)
{
	// Blockno zero is the null pointer of block numbers.
	if (blockno == 0)
		panic("attempt to free zero block");
}

// Search the bitmap for a free block and allocate it.  When you
// allocate a block, immediately flush the changed bitmap block
// to disk.
//
// Return block number allocated on success,
// -E_NO_DISK if we are out of blocks.
//
// Hint: use free_block as an example for manipulating the bitmap.
int
alloc_block(void)
{
	return super->s_cur_blk++;
}

// Validate the file system bitmap.
//
// Check that all reserved blocks -- 0, 1, and the bitmap blocks themselves --
// are all marked as in-use.
void
check_cur_blk(void)
{
	uint32_t i;

	// Make sure all bitmap blocks are marked in-use
	for (i = 0; i * BLKBITSIZE < super->s_nblocks; i++)
		assert(!block_is_free(2+i));

	// Make sure the reserved and root blocks are marked in-use.
	assert(!block_is_free(0));
	assert(!block_is_free(1));

	cprintf("current block pointer is good\n");
}

// --------------------------------------------------------------
// LFS Buffer
// --------------------------------------------------------------

int
lfs_alloc_data_block(void)
{
	return lfs_data_buf_sz++;
}

struct File *
lfs_imap_get(uint32_t inode_num)
{
	if (inode_num == 0 || inode_num >= INODE_ENT_BLK) {
		return NULL;
	} else if (lfs_imap_dirty[inode_num]) {
		return &lfs_inode_buf[lfs_tmp_imap[inode_num]];
	} else {
		return (struct File *) lfs_tmp_imap[inode_num];
	}
}

int
lfs_alloc_inode(void)
{
	cprintf("lfs_alloc_inode\n");
	// allocate inode number
	uint32_t inode_num = 1;
	while (lfs_imap_get(inode_num)) {
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

// --------------------------------------------------------------
// Inode map
// --------------------------------------------------------------

void
free_inode(uint32_t inode_num)
{
	assert(inode_num > 0 && inode_num < super->s_nblocks);
	// update imap
	lfs_tmp_imap[inode_num] = 0;
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
	while ((inode_num < super->s_nblocks) && lfs_tmp_imap[inode_num]) {
		++inode_num;
	}
	if (inode_num >= super->s_nblocks) {
		return -E_NO_DISK;
	}
	lfs_tmp_imap[inode_num] = (uint32_t) addr;
	return inode_num;
}

// --------------------------------------------------------------
// File system structures
// --------------------------------------------------------------



// Initialize the file system
void
fs_init(void)
{
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
	lfs_sync_from_disk();
}

// Find the disk block number slot for the 'filebno'th block in file 'f'.
// Set '*ppdiskbno' to point to that slot.
// The slot will be one of the f->f_direct[] entries,
// or an entry in the indirect block.
// When 'alloc' is set, this function will allocate an indirect block
// if necessary.
//
// Returns:
//	0 on success (but note that *ppdiskbno might equal 0).
//	-E_NOT_FOUND if the function needed to allocate an indirect block, but
//		alloc was 0.
//	-E_NO_DISK if there's no space on the disk for an indirect block.
//	-E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
//
// Analogy: This is like pgdir_walk for files.
// Hint: Don't forget to clear any block you allocate.
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
	// LAB 5: Your code here.
	int r;
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
}

// Set *blk to the address in memory where the filebno'th
// block of file 'f' would be mapped.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_NO_DISK if a block needed to be allocated but the disk is full.
//	-E_INVAL if filebno is out of range.
//
// Hint: Use file_block_walk and alloc_block.
int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
	// LAB 5: Your code here.
	int r;
	uint32_t *ppdiskbno;
	if ((r = file_block_walk(f, filebno, &ppdiskbno, true)) < 0) {
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
}

// Try to find a file named "name" in dir.  If so, set *file to it.
//
// Returns 0 and sets *file on success, < 0 on error.  Errors are:
//	-E_NOT_FOUND if the file is not found
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
				struct File *pf = lfs_imap_get(f[j]);
				if (strcmp(pf->f_name, name) == 0) {
					*file = pf;
					return 0;
				}
			}
		}
	}
	return -E_NOT_FOUND;
}

// Set *file to point at a free File structure in dir.  The caller is
// responsible for filling in the File fields.
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
				f[j] = lfs_alloc_inode();
				*file = lfs_imap_get(f[j]);
				return 0;
			}
		}
	}
	dir->f_size += BLKSIZE;
	if ((r = file_get_block(dir, i, &blk)) < 0)
		return r;
	memset(blk, 0x00, BLKSIZE);
	f = (uint32_t*) blk;
	f[0] = lfs_alloc_inode();
	*file = lfs_imap_get(f[0]);
	return 0;
}

// Skip over slashes.
static const char*
skip_slash(const char *p)
{
	while (*p == '/')
		p++;
	return p;
}

// Evaluate a path name, starting at the root.
// On success, set *pf to the file we found
// and set *pdir to the directory the file is in.
// If we cannot find the file but find the directory
// it should be in, set *pdir and copy the final path
// element into lastelem.
static int
walk_path(const char *path, struct File **pdir, struct File **pf, char *lastelem)
{
	const char *p;
	char name[MAXNAMELEN];
	struct File *dir, *f;
	int r;

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = skip_slash(path);
	f = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir)
		*pdir = 0;
	*pf = 0;
	while (*path != '\0') {
		dir = f;
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

		if ((r = dir_lookup(dir, name, &f)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir)
					*pdir = dir;
				if (lastelem)
					strcpy(lastelem, name);
				*pf = 0;
			}
			return r;
		}
	}

	if (pdir)
		*pdir = dir;
	*pf = f;
	return 0;
}

// --------------------------------------------------------------
// File operations
// --------------------------------------------------------------

// Create "path".  On success set *pf to point at the file and return 0.
// On error return < 0.
int
file_create(const char *path, struct File **pf)
{
	char name[MAXNAMELEN];
	int r;
	struct File *dir, *f;

	if ((r = walk_path(path, &dir, &f, name)) == 0)
		return -E_FILE_EXISTS;
	if (r != -E_NOT_FOUND || dir == 0)
		return r;
	if ((r = dir_alloc_file(dir, &f)) < 0)
		return r;

	strcpy(f->f_name, name);
	*pf = f;
	file_flush(dir);
	return 0;
}

// Open "path".  On success set *pf to point at the file and return 0.
// On error return < 0.
int
file_open(const char *path, struct File **pf)
{
	return walk_path(path, 0, pf, 0);
}

// Read count bytes from f into buf, starting from seek position
// offset.  This meant to mimic the standard pread function.
// Returns the number of bytes read, < 0 on error.
ssize_t
file_read(struct File *f, void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	if (offset >= f->f_size)
		return 0;

	count = MIN(count, f->f_size - offset);

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(buf, blk + pos % BLKSIZE, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}


// Write count bytes from buf into f, starting at seek position
// offset.  This is meant to mimic the standard pwrite function.
// Extends the file if necessary.
// Returns the number of bytes written, < 0 on error.
int
file_write(struct File *f, const void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	// Extend file if necessary
	if (offset + count > f->f_size)
		if ((r = file_set_size(f, offset + count)) < 0)
			return r;

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(blk + pos % BLKSIZE, buf, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}

// Remove a block from file f.  If it's not there, just silently succeed.
// Returns 0 on success, < 0 on error.
static int
file_free_block(struct File *f, uint32_t filebno)
{
	int r;
	uint32_t *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0)
		return r;
	if (*ptr) {
		free_block(*ptr);
		*ptr = 0;
	}
	return 0;
}

// Remove any blocks currently used by file 'f',
// but not necessary for a file of size 'newsize'.
// For both the old and new sizes, figure out the number of blocks required,
// and then clear the blocks from new_nblocks to old_nblocks.
// If the new_nblocks is no more than NDIRECT, and the indirect block has
// been allocated (f->f_indirect != 0), then free the indirect block too.
// (Remember to clear the f->f_indirect pointer so you'll know
// whether it's valid!)
// Do not change f->f_size.
static void
file_truncate_blocks(struct File *f, off_t newsize)
{
	int r;
	uint32_t bno, old_nblocks, new_nblocks;

	old_nblocks = (f->f_size + BLKSIZE - 1) / BLKSIZE;
	new_nblocks = (newsize + BLKSIZE - 1) / BLKSIZE;
	for (bno = new_nblocks; bno < old_nblocks; bno++)
		if ((r = file_free_block(f, bno)) < 0)
			cprintf("warning: file_free_block: %e", r);

	if (new_nblocks <= NDIRECT && f->f_indirect) {
		free_block(f->f_indirect);
		f->f_indirect = 0;
	}
}

// Set the size of file f, truncating or extending as necessary.
int
file_set_size(struct File *f, off_t newsize)
{
	if (f->f_size > newsize)
		file_truncate_blocks(f, newsize);
	f->f_size = newsize;
	if ((uint32_t) f > DISKMAP) {
		flush_block(f);
	}
	return 0;
}

// Flush the contents and metadata of file f out to disk.
// Loop over all the blocks in file.
// Translate the file block number into a disk block number
// and then check whether that disk block is dirty.  If so, write it out.
void
file_flush(struct File *f)
{
	int i;
	uint32_t *pdiskbno;

	for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
		if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
		    pdiskbno == NULL || *pdiskbno == 0)
			continue;
		flush_block(diskaddr(*pdiskbno));
	}
	if ((uint32_t) f > DISKMAP) {
		flush_block(f);
	}
	if (f->f_indirect)
		flush_block(diskaddr(f->f_indirect));
}


// Sync the entire file system.  A big hammer.
void
fs_sync(void)
{
	int i;
	for (i = 1; i < super->s_nblocks; i++)
		flush_block(diskaddr(i));
}

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
	lfs_data_buf_sz = 0;
	// Write back imap
	memmove(imap, lfs_tmp_imap, sizeof(lfs_tmp_imap));
	memset(lfs_imap_dirty, 0, sizeof(lfs_imap_dirty));
	flush_block(imap);
}
