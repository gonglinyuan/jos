#include <inc/fs.h>
#include <inc/lib.h>

#define SECTSIZE	512			// bytes per disk sector
#define BLKSECTS	(BLKSIZE / SECTSIZE)	// sectors per block
#define LFS_BUFSIZE	(BLKSIZE / 256)	    // size of LFS file update buffer

/* Disk block n, when in memory, is mapped into the file system
 * server's address space at DISKMAP + (n*BLKSIZE). */
#define DISKMAP		0x10000000

/* Maximum disk size we can handle (3GB) */
#define DISKSIZE	0xC0000000

struct Super *super;		// superblock
uint32_t *imap;		// inode map mapped in memory

uint32_t lfs_tmp_imap[INODE_ENT_BLK];
bool lfs_imap_dirty[INODE_ENT_BLK];
struct File lfs_inode_buf[LFS_BUFSIZE];
uint32_t lfs_inode_buf_sz;

const struct File *lfs_imap_get_for_read(uint32_t inode_num);

void lfs_sync_from_disk(void);
void lfs_sync_to_disk(void);

/* ide.c */
bool	ide_probe_disk1(void);
void	ide_set_disk(int diskno);
void	ide_set_partition(uint32_t first_sect, uint32_t nsect);
int	ide_read(uint32_t secno, void *dst, size_t nsecs);
int	ide_write(uint32_t secno, const void *src, size_t nsecs);

/* bc.c */
void*	diskaddr(uint32_t blockno);
bool	va_is_mapped(void *va);
bool	va_is_dirty(void *va);
void	flush_block(void *addr);
void	bc_init(void);

/* fs.c */
void	fs_init(void);
int file_get_block(uint32_t inode_num, uint32_t filebno, char **blk);
int file_create(const char *path, uint32_t *p_inode_num);
int file_open(const char *path, uint32_t *p_inode_num);
ssize_t file_read(uint32_t inode_num, void *buf, size_t count, off_t offset);
int	file_write(uint32_t inode_num, const void *buf, size_t count, off_t offset);
int	file_set_size(uint32_t inode_num, off_t newsize);
void	file_flush(uint32_t inode_num);
int	file_remove(const char *path);
void	fs_sync(void);

/* int	map_block(uint32_t); */
bool	block_is_free(uint32_t blockno);
int	alloc_block(void);

/* test.c */
void	fs_test(void);

