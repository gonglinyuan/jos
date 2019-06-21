#include <inc/x86.h>
#include <inc/string.h>

#include "fs.h"

static char *msg = "This is the NEW message of the day!\n\n";

void
fs_test(void)
{
	struct File *f;
	int r;
	char *blk;
	uint32_t *bits, inode_num_file;

	uint32_t last_cur_blk = super->s_cur_blk;
	// allocate block
	if ((r = alloc_block()) < 0)
		panic("alloc_block: %e", r);
	// check that block was free
	assert(r >= last_cur_blk);
	// and is not free any more
	assert(r < super->s_cur_blk);
	cprintf("alloc_block is good\n");

	if ((r = file_open("/not-found", &inode_num_file)) < 0 && r != -E_NOT_FOUND)
		panic("file_open /not-found: %e", r);
	else if (r == 0)
		panic("file_open /not-found succeeded!");
	if ((r = file_open("/newmotd", &inode_num_file)) < 0)
		panic("file_open /newmotd: %e", r);
	f = (struct File *) lfs_tmp_imap[inode_num_file];
	cprintf("file_open is good\n");

	if ((r = file_get_block(inode_num_file, 0, &blk)) < 0)
		panic("file_get_block: %e", r);
	if (strcmp(blk, msg) != 0)
		panic("file_get_block returned wrong data");
	cprintf("file_get_block is good\n");

	*(volatile char*)blk = *(volatile char*)blk;
	assert((uvpt[PGNUM(blk)] & PTE_D));
	file_flush(inode_num_file);
	assert(!(uvpt[PGNUM(blk)] & PTE_D));
	cprintf("file_flush is good\n");

	if ((r = file_set_size(inode_num_file, 0)) < 0)
		panic("file_set_size: %e", r);
	assert(f->f_direct[0] == 0);
	assert(!(uvpt[PGNUM(f)] & PTE_D));
	cprintf("file_truncate is good\n");

	if ((r = file_set_size(inode_num_file, strlen(msg))) < 0)
		panic("file_set_size 2: %e", r);
	assert(!(uvpt[PGNUM(f)] & PTE_D));
	if ((r = file_get_block(inode_num_file, 0, &blk)) < 0)
		panic("file_get_block 2: %e", r);
	strcpy(blk, msg);
	assert((uvpt[PGNUM(blk)] & PTE_D));
	file_flush(inode_num_file);
	assert(!(uvpt[PGNUM(blk)] & PTE_D));
	assert(!(uvpt[PGNUM(f)] & PTE_D));
	cprintf("file rewrite is good\n");
}
