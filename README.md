# Lab 5

龚林源 1600012714

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Exercise 1.** *`i386_init` identifies the file system environment by passing the type `ENV_TYPE_FS` to your environment creation function, `env_create`. Modify `env_create` in `env.c`, so that it gives the file system environment I/O privilege, but never gives that privilege to any other environment.*

In `env_create()` of `env.c`, I changed `e->env_type = ENV_TYPE_USER` to `e->env_type = type`, and added following to set `IOPL` to `3`:

```c
if (type == ENV_TYPE_FS) {
    e->env_tf.tf_eflags |= FL_IOPL_3;
}
```

**Question 1.** *Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?*

No. Because `IOPL` is in `EFLAGS`, which will be properly saved and restored every time we switch from one environment to another. When an interrupt happens, the hardware will automatically save `EFLAGS` in the kernel stack; in `env_pop_tf()`, the `iret` instruction will restore the previously saved `EFLAGS`. 

**Exercise 2.** *Implement the `bc_pgfault` and `flush_block` functions in `fs/bc.c`. `bc_pgfault` is a page fault handler, just like the one your wrote in the previous lab for copy-on-write fork, except that its job is to load pages in from the disk in response to a page fault. When writing this, keep in mind that (1) `addr` may not be aligned to a block boundary and (2) `ide_read` operates in sectors, not blocks.*

*The `flush_block` function should write a block out to disk if necessary. `flush_block` shouldn't do anything if the block isn't even in the block cache (that is, the page isn't mapped) or if it's not dirty. After writing the block to disk, `flush_block` should clear the `PTE_D` bit using `sys_page_map`.*

In `bc_pgfault()` of `bc.c`, I added:

```c
addr = ROUNDDOWN(addr, PGSIZE);
if ((r = sys_page_alloc(0, addr, PTE_U | PTE_W)) < 0) {
	panic("in bc_pgfault, sys_page_alloc: %e", r);
}
if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0) {
	panic("in bc_pgfault, ide_read returns %d", r);
}
```

In `flush_block()` of `bc.c`, I added:

```c
int r;
addr = ROUNDDOWN(addr, BLKSIZE);
if (!(va_is_mapped(addr) && va_is_dirty(addr))) {
    return;
}
if ((r = ide_write(blockno * BLKSECTS, addr, BLKSECTS)) < 0) {
    panic("in bc_pgfault, ide_write returns %d", r);
}
// Remap to clear PTE_D
if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0) {
    panic("in bc_pgfault, sys_page_map: %e", r);
}
```

**Challenge 2.** *The block cache has no eviction policy. Once a block gets faulted in to it, it never gets removed and will remain in memory forevermore. Add eviction to the buffer cache. Using the `PTE_A` "accessed" bits in the page tables, which the hardware sets on any access to a page, you can track approximate usage of disk blocks without the need to modify every place in the code that accesses the disk map region. Be careful with dirty blocks.*

In `bc.c`, I added:

```c
// Challenge 2:
#define MAX_CACHED_BLOCKS 256 // Maximum cached blocks.
static uint32_t cached_block_num = 0; // The number of cached blocks.
static void *cached_block[MAX_CACHED_BLOCKS] = {NULL}; // The vaddr of cached blocks.
static uint32_t cached_block_next = 0; // The next block to evict.

// Return whether the accessed bit is true or not.
bool
va_is_accessed(void *va)
{
	return (uvpt[PGNUM(va)] & PTE_A) != 0;
}
```

In `bc_pgfault()` of `bc.c`, I added:

```c
// Challenge 2:
int i;
void *tmp_addr;
if (cached_block_num == MAX_CACHED_BLOCKS) {
    // If the cache is full, evict one page
    for (;; cached_block_next = (cached_block_next + 1) % MAX_CACHED_BLOCKS) {
        tmp_addr = cached_block[cached_block_next];
        if (((uint32_t) tmp_addr - DISKMAP) / BLKSIZE <= 1) {
            // Skip superblock
            continue;
        }
        assert(va_is_mapped(tmp_addr));
        // If it is dirty, flush (because the following step will clear the dirty bit)
        if (va_is_dirty(tmp_addr)) {
            flush_block(tmp_addr);
        }
        if (va_is_accessed(tmp_addr)) {
            // Clear the access bit (and the dirty bit)
            if ((r = sys_page_map(0, tmp_addr, 0, tmp_addr, uvpt[PGNUM(tmp_addr)] & PTE_SYSCALL)) < 0)
                panic("in bc_pgfault, sys_page_map: %e", r);
        } else {
            // Evict this page
            // cprintf("evict %x\n", (uint32_t) tmp_addr);
            if ((r = sys_page_unmap(0, tmp_addr)) < 0)
                panic("in bc_pgfault, sys_page_unmap: %e", r);
            cached_block[cached_block_next] = NULL;
            --cached_block_num;
            break;
        }
    }
    i = cached_block_next;
    cached_block_next = (cached_block_next + 1) % MAX_CACHED_BLOCKS;
} else {
    // Otherwise, find a slot in cached_block array
    i = (cached_block_next - 1 + MAX_CACHED_BLOCKS) % MAX_CACHED_BLOCKS;
    while ((i != MAX_CACHED_BLOCKS) && cached_block[i]) {
        i = (i - 1 + MAX_CACHED_BLOCKS) % MAX_CACHED_BLOCKS;
    }
    assert(!cached_block[i]);
}
cached_block[i] = addr;
++cached_block_num;
// cprintf("cached block num = %d\n", cached_block_num);
```

To test its functionality, I added a large file `largefile` to `fs` directory, and added it into `fs\Makefrag` to put it into `disk 1` of JOS. Then, I run `make qemu-nox`, and typed `cat largefile > largefile2`. The program runs correctly with page eviction enabled.

**Exercise 3.** *Use `free_block` as a model to implement `alloc_block` in `fs/fs.c`, which should find a free disk block in the bitmap, mark it used, and return the number of that block. When you allocate a block, you should immediately flush the changed bitmap block to disk with `flush_block`, to help file system consistency.*

In `alloc_block()` of `fs.c`, I added:

```c
uint32_t blockno = 0;
while ((blockno < super->s_nblocks) && !bitmap[blockno / 32]) {
    blockno += 32;
}
while ((blockno < super->s_nblocks) && !(bitmap[blockno / 32] & (1 << (blockno % 32)))) {
    blockno += 1;
}
if (blockno >= super->s_nblocks) {
    return -E_NO_DISK;
}
bitmap[blockno / 32] &= ~(1 << (blockno % 32));
flush_block(&bitmap[blockno / 32]);
return blockno;
```

**Exercise 4.** *Implement `file_block_walk` and `file_get_block`. `file_block_walk` maps from a block offset within a file to the pointer for that block in the `struct File` or the indirect block, very much like what `pgdir_walk` did for page tables. `file_get_block` goes one step further and maps to the actual disk block, allocating a new one if necessary.*

In `file_block_walk()` of `fs.c`, I added:

```c
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
```

In `file_get_block()` of `fs.c`, I added:

```c
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
```

**Exercise 5.** *Implement `serve_read` in `fs/serv.c`.*

In `serve_read()` of `fs.c`, I added:

```c
struct OpenFile *o;
int r;
if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0) {
    return r;
}
if ((r = file_read(o->o_file, ret->ret_buf, MIN(req->req_n, sizeof(ret->ret_buf)), o->o_fd->fd_offset)) < 0) {
    return r;
}
o->o_fd->fd_offset += r;
return r;
```

I make sure that the number of bytes we read from the file is no more than the size of the provided buffer.

**Exercise 6.** *Implement `serve_write` in `fs/serv.c` and `devfile_write` in `lib/file.c`.*

In `serve_write()` of `fs.c`, I added:

```c
struct OpenFile *o;
int r;
if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0) {
	return r;
}
if ((r = file_write(o->o_file, req->req_buf, MIN(req->req_n, sizeof(req->req_buf)), o->o_fd->fd_offset)) < 0) {
	return r;
}
o->o_fd->fd_offset += r;
return r;
```

In `devfile_write()` of `file.c`, I added:

```c
fsipcbuf.write.req_fileid = fd->fd_file.id;
n = MIN(n, sizeof(fsipcbuf.write.req_buf));
fsipcbuf.write.req_n = n;
memmove(fsipcbuf.write.req_buf, buf, n);
return fsipc(FSREQ_WRITE, NULL);
```

**Exercise 7.** *`spawn` relies on the new syscall `sys_env_set_trapframe` to initialize the state of the newly created environment. Implement `sys_env_set_trapframe` in `kern/syscall.c` (don't forget to dispatch the new system call in `syscall()`).*

*Test your code by running the `user/spawnhello` program from `kern/init.c`, which will attempt to spawn `/hello` from the file system.*

In `sys_env_set_trapframe()` of `syscall.c`, I added:

```c
struct Env *env_ptr;
int r;
if ((r = envid2env(envid, &env_ptr, true)) < 0) {
    return r;
}
user_mem_assert(curenv, (const void *)tf, sizeof(struct Trapframe), 0);
env_ptr->env_tf = *tf;
// Set IOPL to 0
env_ptr->env_tf.tf_eflags &= ~FL_IOPL_MASK;
// Enable interrupts
env_ptr->env_tf.tf_eflags |= FL_IF;
// Set CPL to 3
env_ptr->env_tf.tf_cs |= 0x3;
return 0;
```

Then I tested it with `make run-spawnhello-nox`, and the output is:

```
vagrant@ubuntu-xenial:~/jos$ make run-spawnhello-nox
make[1]: Entering directory '/home/vagrant/jos'
make[1]: 'obj/fs/fs.img' is up to date.
make[1]: Leaving directory '/home/vagrant/jos'
qemu-system-i386 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 -drive file=obj/fs/fs.img,index=1,media=disk,format=raw
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2 4
i am parent environment 00001001
FS is running
FS can do I/O
Device 1 presence: 1
block cache is good
superblock is good
bitmap is good
alloc_block is good
file_open is good
file_get_block is good
file_flush is good
file_truncate is good
file rewrite is good
hello, world
i am environment 00001002
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

**Exercise 8.** *Change `duppage` in `lib/fork.c` to follow the new convention. If the page table entry has the `PTE_SHARE` bit set, just copy the mapping directly.*

*Likewise, implement `copy_shared_pages` in `lib/spawn.c`. It should loop through all page table entries in the current process (just like `fork` did), copying any page mappings that have the`PTE_SHARE` bit set into the child process.*

I changed `duppage()` in `fork.c` to be:

```c
uintptr_t addr = pn * PGSIZE;
if (((uvpt[pn] & PTE_COW) || (uvpt[pn] & PTE_W)) && !(uvpt[pn] & PTE_SHARE)) {
    r = sys_page_map(0, (void *) addr, envid, (void *) addr, ((uvpt[pn] & PTE_SYSCALL) & (~PTE_W)) | PTE_COW);
    if (r < 0) return r;
    r = sys_page_map(0, (void *) addr, 0, (void *) addr, ((uvpt[pn] & PTE_SYSCALL) & (~PTE_W)) | PTE_COW);
    if (r < 0) return r;
} else {
    r = sys_page_map(0, (void *) addr, envid, (void *) addr, (uvpt[pn] & PTE_SYSCALL));
    if (r < 0) return r;
}
return 0;
```

In `copy_shared_pages()` of `spawn.c`, I added:

```c
int r;
for (uintptr_t addr = 0; addr < USTACKTOP; addr += PGSIZE) {
    if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_U) && (uvpt[PGNUM(addr)] & PTE_SHARE)) {
        if ((r = sys_page_map(0, (void *) addr, child, (void *) addr, (uvpt[PGNUM(addr)] & PTE_SYSCALL))) < 0) {
            return r;
        }
    }
}
```

**Exercise 9.** *In your `kern/trap.c`, call `kbd_intr` to handle trap `IRQ_OFFSET+IRQ_KBD` and `serial_intr` to handle trap `IRQ_OFFSET+IRQ_SERIAL`.*

In `trap_init()` of `trap.c`, I added:

```c
SETGATE(idt[IRQ_OFFSET + IRQ_KBD], 0, GD_KT, idt_entries[IRQ_OFFSET + IRQ_KBD], 0);
SETGATE(idt[IRQ_OFFSET + IRQ_SERIAL], 0, GD_KT, idt_entries[IRQ_OFFSET + IRQ_SERIAL], 0);
```

In `trap_dispatch()` of `trap.c`, I added:

```c
if (tf->tf_trapno == IRQ_OFFSET + IRQ_KBD) {
    kbd_intr();
    return;
}

if (tf->tf_trapno == IRQ_OFFSET + IRQ_SERIAL) {
    serial_intr();
    return;
}
```

**Exercise 10.** *Add I/O redirection for < to `user/sh.c`.*

*Test your implementation by typing sh <script into your shell*

*Run make run-testshell to test your shell.*

In `runcmd()` of `sh.c`, I added:

```c
if ((fd = open(t, O_RDONLY)) < 0) {
    cprintf("open %s for read: %e", t, fd);
    exit();
}
if (fd != 0) {
    dup(fd, 0);
    close(fd);
}
```

I run `make qemu-nox`, and type `sh < script` into the console. The output is:

```
$ sh < script
This is from the script.
    1 Lorem ipsum dolor sit amet, consectetur
    2 adipisicing elit, sed do eiusmod tempor
    3 incididunt ut labore et dolore magna
    4 aliqua. Ut enim ad minim veniam, quis
    5 nostrud exercitation ullamco laboris
    6 nisi ut aliquip ex ea commodo consequat.
    7 Duis aute irure dolor in reprehenderit
    8 in voluptate velit esse cillum dolore eu
    9 fugiat nulla pariatur. Excepteur sint
   10 occaecat cupidatat non proident, sunt in
   11 culpa qui officia deserunt mollit anim
   12 id est laborum.
These are my file descriptors.
fd 0: name script isdir 0 size 132 dev file
fd 1: name <cons> isdir 0 size 0 dev cons
This is the end of the script.
$ 
```

I run `make run-testshell-nox`, and the output is:

```
vagrant@ubuntu-xenial:~/jos$ make run-testshell-nox
make[1]: Entering directory '/home/vagrant/jos'
make[1]: 'obj/fs/fs.img' is up to date.
make[1]: Leaving directory '/home/vagrant/jos'
qemu-system-i386 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 -drive file=obj/fs/fs.img,index=1,media=disk,format=raw
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2 4
FS is running
FS can do I/O
Device 1 presence: 1
block cache is good
superblock is good
bitmap is good
alloc_block is good
file_open is good
file_get_block is good
file_flush is good
file_truncate is good
file rewrite is good
running sh -x < testshell.sh | cat
shell ran correctly
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf029707c from CPU 0
  edi  0x00000b64
  esi  0x00000b65
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0xeebfde58
  ecx  0x00000014
  eax  0x00000014
  es   0x----0023
  ds   0x----0023
  trap 0x00000003 Breakpoint
  err  0x00000000
  eip  0x008002f6
  cs   0x----001b
  flag 0x00000292
  esp  0xeebfdf88
  ss   0x----0023
K>
```

**Grading.** This is the output of `make grade`:

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
internal FS tests [fs/test.c]: OK (1.7s)
  fs i/o: OK
  check_bc: OK
  check_super: OK
  check_bitmap: OK
  alloc_block: OK
  file_open: OK
  file_get_block: OK
  file_flush/file_truncate/file rewrite: OK
testfile: OK (2.3s)
  serve_open/file_stat/file_close: OK
  file_read: OK
  file_write: OK
  file_read after file_write: OK
  open: OK
  large file: OK
spawn via spawnhello: OK (1.5s)
Protection I/O space: OK (1.4s)
PTE_SHARE [testpteshare]: OK (2.4s)
PTE_SHARE [testfdsharing]: OK (1.4s)
start the shell [icode]: Timeout! OK (31.1s)
testshell: OK (3.9s)
primespipe: OK (14.5s)
Score: 100% (150/150)
```

