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

**Grading.** This is the output of `make grade`:

```
vagrant@ubuntu-xenial:~/jos$ make grade
make clean
make[1]: Entering directory '/home/vagrant/jos'
rm -rf obj .gdbinit jos.in qemu.log
make[1]: Leaving directory '/home/vagrant/jos'
./grade-lab4
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
+ ar obj/lib/libjos.a
ar: creating obj/lib/libjos.a
+ cc[USER] user/hello.c
+ as[USER] lib/entry.S
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
+ ld obj/kern/kernel
+ as boot/boot.S
+ cc -Os boot/main.c
+ ld boot/boot
boot block is 414 bytes (max 510)
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/vagrant/jos'
dumbfork: OK (1.5s)
Part A score: 5/5

faultread: OK (1.4s)
faultwrite: OK (1.3s)
faultdie: OK (1.4s)
faultregs: OK (1.4s)
faultalloc: OK (2.2s)
faultallocbad: OK (1.3s)
faultnostack: OK (2.1s)
faultbadhandler: OK (2.3s)
faultevilhandler: OK (2.3s)
forktree: OK (2.4s)
Part B score: 50/50

spin: OK (2.2s)
stresssched: OK (2.4s)
sendpage: OK (2.2s)
pingpong: OK (2.4s)
primes: OK (6.5s)
Part C score: 25/25

Score: 100% (80/80)
```

