# Lab 4

龚林源 1600012714

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Exercise 1.** *Implement `mmio_map_region` in `kern/pmap.c`. To see how this is used, look at the beginning of `lapic_init` in `kern/lapic.c`.*

In `mmio_map_region()` , I added code:

```c
physaddr_t pa_end = ROUNDUP(pa + size, PGSIZE), pa_start = ROUNDDOWN(pa, PGSIZE);
if (base + (pa_end - pa_start) > MMIOLIM || pa_end < pa_start) {  // overflow or underflow
	panic("mmio_map_region overflow %x %x", pa_start, pa_end);
}
// disable cache and write-through
boot_map_region(kern_pgdir, base, pa_end - pa_start, pa_start, PTE_PCD | PTE_PWT | PTE_W);
base += pa_end - pa_start;
return (void *)(base - (pa_end - pa_start));
```

**Exercise 2.** *Read `boot_aps()` and `mp_main()` in `kern/init.c`, and the assembly code in `kern/mpentry.S`. Make sure you understand the control flow transfer during the bootstrap of APs. Then modify your implementation of `page_init()` in `kern/pmap.c` to avoid adding the page at `MPENTRY_PADDR` to the free list, so that we can safely copy and run AP bootstrap code at that physical address.*

In `page_init`, I modified the condition to add a free page to be:

```c
if ((PGSIZE <= pa && pa < npages_basemem * PGSIZE && (pa != MPENTRY_PADDR)) || (pa > kern_memory_end)) {
```

**Question 1.** *Compare `kern/mpentry.S` side by side with `boot/boot.S`. Bearing in mind that `kern/mpentry.S` is compiled and linked to run above `KERNBASE` just like everything else in the kernel, what is the purpose of macro `MPBOOTPHYS`? Why is it necessary in `kern/mpentry.S` but not in `boot/boot.S`? In other words, what could go wrong if it were omitted in `kern/mpentry.S`?*

- `MPBOOTPHYS()` maps the relative address in `mpentry.S` to its absolute address starting from `MPENTRY_PADDR`  at compile time, because `mpentry.S` will be loaded to `MPENTRY_PADDR`.
- When compiling `boot.S`, the linker link its symbols to low addresses. When loading `boot.S`, we can just load it at low addresses and run it. However, `mpentry.S` also loads at low addresses (`MPENTRY_PADDR`) but linked to high addresses (`mpentry_start`), so we need `MPBOOTPHYS()` to map back to low addresses.

**Exercise 3.** *Modify `mem_init_mp()` (in `kern/pmap.c`) to map per-CPU stacks starting at `KSTACKTOP`, as shown in `inc/memlayout.h`. The size of each stack is `KSTKSIZE` bytes plus `KSTKGAP` bytes of unmapped guard pages.*

In `mem_init_mp()`, I added code:

```c
for (int i = 0; i < NCPU; ++i) {
	uintptr_t kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
	boot_map_region(kern_pgdir, kstacktop_i - KSTKSIZE, KSTKSIZE, PADDR(percpu_kstacks[i]), PTE_W);
}
```

**Exercise 4.** *The code in `trap_init_percpu()` (`kern/trap.c`) initializes the TSS and TSS descriptor for the BSP. It worked in Lab 3, but is incorrect when running on other CPUs. Change the code so that it can work on all CPUs.*

I modified `trap_init_per_cpu()` to be:

```c
uintptr_t kstacktop_i = KSTACKTOP - cpunum() * (KSTKSIZE + KSTKGAP);
thiscpu->cpu_ts.ts_esp0 = kstacktop_i;
thiscpu->cpu_ts.ts_ss0 = GD_KD;
thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

// Initialize the TSS slot of the gdt.
gdt[(GD_TSS0 >> 3) + cpunum()] = SEG16(STS_T32A, (uint32_t) (&(thiscpu->cpu_ts)), sizeof(struct Taskstate) - 1, 0);
gdt[(GD_TSS0 >> 3) + cpunum()].sd_s = 0;

// Load the TSS selector (like other segment selectors, the
// bottom three bits are special; we leave them 0)
ltr(GD_TSS0 + (cpunum() << 3));

// Load the IDT
lidt(&idt_pd);
```

I ran `make qemu-nox CPUS=4`, the output is:

```
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 4 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
SMP: CPU 2 starting
SMP: CPU 3 starting
[00000000] new env 00001000
```

**Exercise 5.** *Apply the big kernel lock as described above, by calling `lock_kernel()` and `unlock_kernel()` at the proper locations.*

I added `lock_kernel()` at:

1. `i386_init()` in `init.c`, before `boot_aps()`
2. `mp_main()` in `init.c`, before `for (;;);`. I also added `sched_yield()`
3. `trap()` in `trap.c`, after `assert(curenv)`.

I added `unlock_kernel()` at:

1. `env_run()` in `env.c`, before `env_pop_tf()`

**Question 2.** *It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock.*

When an interrupt occurs, CPU will push things into the current kernel stack. This step is before any lock acquiring and cannot be protected by the big kernel lock. Therefore,  we need to separate the kernel stacks of different CPUs.

**Exercise 6.** *Implement round-robin scheduling in `sched_yield()` as described above. Don't forget to modify `syscall()` to dispatch `sys_yield()`.*

*Make sure to invoke `sched_yield()` in `mp_main`.*

*Modify `kern/init.c` to create three (or more!) environments that all run the program `user/yield.c`.*

In `sched_yield()` of `sched.c`, I added code:

```c
envid_t curenv_id = curenv ? ENVX(curenv->env_id) : 0;
for (envid_t i = curenv_id + 1; i < NENV; ++i) {
	if (envs[i].env_status == ENV_RUNNABLE) {
		env_run(envs + i);
	}
}
for (envid_t i = 0; i <= curenv_id; ++i) {
	if (envs[i].env_status == ENV_RUNNABLE) {
		env_run(envs + i);
	}
}
// If no other environment can run, run itself again
if (curenv && curenv->env_status == ENV_RUNNING) {
	env_run(curenv);
}
```

In `syscall()` of `syscall.c`, I added code:

```c
case SYS_yield:
	sys_yield();
	return 0;
```

In `mp_main()` of `init.c`, I removed:

```c
// Remove this after you finish Exercise 6
for (;;);
```

In `i386_init()` of `init.c`, I replaced `ENV_CREATE(user_primes, ENV_TYPE_USER)` with:

```c
ENV_CREATE(user_yield, ENV_TYPE_USER);
ENV_CREATE(user_yield, ENV_TYPE_USER);
ENV_CREATE(user_yield, ENV_TYPE_USER);
```

When I run `make qemu-nox CPUS=2`, the output is:

```
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 2 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
[00000000] new env 00001000
[00000000] new env 00001001
[00000000] new env 00001002
Hello, I am environment 00001001.
Hello, I am environment 00001002.
Back in environment 00001001, iteration 0.
Hello, I am environment 00001000.
Back in environment 00001002, iteration 0.
Back in environment 00001001, iteration 1.
Back in environment 00001000, iteration 0.
Back in environment 00001002, iteration 1.
Back in environment 00001001, iteration 2.
Back in environment 00001000, iteration 1.
Back in environment 00001002, iteration 2.
Back in environment 00001001, iteration 3.
Back in environment 00001000, iteration 2.
Back in environment 00001002, iteration 3.
Back in environment 00001001, iteration 4.
Back in environment 00001000, iteration 3.
All done in environment 00001001.
[00001001] exiting gracefully
[00001001] free env 00001001
Back in environment 00001002, iteration 4.
Back in environment 00001000, iteration 4.
All done in environment 00001002.
All done in environment 00001000.
[00001002] exiting gracefully
[00001002] free env 00001002
[00001000] exiting gracefully
[00001000] free env 00001000
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
```

**Question 3.** *In your implementation of `env_run()` you should have called `lcr3()`. Before and after the call to `lcr3()`, your code makes references (at least it should) to the variable `e`, the argument to `env_run`. Upon loading the `%cr3` register, the addressing context used by the MMU is instantly changed. But a virtual address (namely `e`) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer `e` be dereferenced both before and after the addressing switch?*

Because `e` points to an address above `UENVS`, which in the kernel virtual memory. When mapping memories for user environments, we mapped kernel virtual memory (all addresses above `UTOP`) identically as in `kern_pgdir`.

**Question 4.** *Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?*

Because otherwise the local variables of the previous environment kept in the register will be corrupted by the new environment. It happens in `trap()` of `trap.c`:

```c
// Copy trap frame (which is currently on the stack)
// into 'curenv->env_tf', so that running the environment
// will restart at the trap point.
curenv->env_tf = *tf;
// The trapframe on the stack should be ignored from here on.
tf = &curenv->env_tf;
```

**Exercise 7.** *Implement the system calls described above in `kern/syscall.c` and make sure `syscall()` calls them. You will need to use various functions in `kern/pmap.c` and `kern/env.c`, particularly `envid2env()`. For now, whenever you call `envid2env()`, pass 1 in the `checkperm` parameter. Be sure you check for any invalid system call arguments, returning `-E_INVAL` in that case. Test your JOS kernel with `user/dumbfork` and make sure it works before proceeding.*

In `sys_exofork()`, I added:

```c
struct Env *env_ptr;
int ret = env_alloc(&env_ptr, curenv->env_id);
if (ret < 0) {  // on error
	return ret;
}
env_ptr->env_status = ENV_NOT_RUNNABLE;
env_ptr->env_tf = curenv->env_tf;
// make child return 0
env_ptr->env_tf.tf_regs.reg_eax = 0;
return env_ptr->env_id;
```

In `sys_env_set_status`, I added:

```c
if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
	return -E_INVAL;
}
struct Env *env_ptr;
int r = envid2env(envid, &env_ptr, 1);
if (r < 0) {
	return r;
}
env_ptr->env_status = status;
return 0;
```

In `sys_page_alloc()`, I added:

```c
struct Env *env_ptr;
int r = envid2env(envid, &env_ptr, 1);
if (r < 0) {
	return r;
}
if (va != ROUNDDOWN(va, PGSIZE) || (uintptr_t) va >= UTOP || ((perm & PTE_SYSCALL) != perm)) {
	return -E_INVAL;
}
struct PageInfo *pp = page_alloc(perm);
if (pp == NULL) {
	return -E_NO_MEM;
}
r = page_insert(env_ptr->env_pgdir, pp, va, perm);
if (r < 0) {
	return r;
}
return 0;
```

In `sys_page_map()`, I added:

```c
struct Env *srcenv_ptr, *dstenv_ptr;
int r = envid2env(srcenvid, &srcenv_ptr, 1);
if (r < 0) {
	return r;
}
r = envid2env(dstenvid, &dstenv_ptr, 1);
if (r < 0) {
	return r;
}
if (srcva != ROUNDDOWN(srcva, PGSIZE) || (uintptr_t) srcva >= UTOP || dstva != ROUNDDOWN(dstva, PGSIZE) || (uintptr_t) dstva >= UTOP || ((perm & PTE_SYSCALL) != perm)) {
	return -E_INVAL;
}
pte_t *pte_ptr;
struct PageInfo *pp = page_lookup(srcenv_ptr->env_pgdir, srcva, &pte_ptr);
if (pp == NULL || ((perm & PTE_W) && !(*pte_ptr & PTE_W))) {
	return -E_INVAL;
}
r = page_insert(dstenv_ptr->env_pgdir, pp, dstva, perm);
if (r < 0) {
	return r;
}
return 0;
```

In `sys_page_unmap()`, I added:

```c
struct Env *env_ptr;
int r = envid2env(envid, &env_ptr, 1);
if (r < 0) {
	return r;
}
if (va != ROUNDDOWN(va, PGSIZE) || (uintptr_t) va >= UTOP) {
	return -E_INVAL;
}
page_remove(env_ptr->env_pgdir, va);
return 0;
```

In `syscall()`, I added:

```c
case SYS_exofork:
	return sys_exofork();
case SYS_env_set_status:
	return sys_env_set_status(a1, a2);
case SYS_page_alloc:
	return sys_page_alloc(a1, (void *) a2, a3);
case SYS_page_map:
	return sys_page_map(a1, (void *) a2, a3, (void *) a4, a5);
case SYS_page_unmap:
	return sys_page_unmap(a1, (void *) a2);
```

**Exercise 8.** *Implement the `sys_env_set_pgfault_upcall` system call. Be sure to enable permission checking when looking up the environment ID of the target environment, since this is a "dangerous" system call*

In `sys_env_set_pgfault_upcall()`, I added code:

```c
struct Env *env_ptr;
int r = envid2env(envid, &env_ptr, 1);
if (r < 0) {
	return r;
}
env_ptr->env_pgfault_upcall = func;
// Assert that the user has permission to execute at func
user_mem_assert(env_ptr, func, 1, 0);
return 0;
```

In `syscall()`, I added code:

```c
case SYS_env_set_pgfault_upcall:
	return sys_env_set_pgfault_upcall(a1, (void *) a2);
```

**Exercise 9.** *Implement the code in `page_fault_handler` in `kern/trap.c` required to dispatch page faults to the user-mode handler. Be sure to take appropriate precautions when writing into the exception stack.*

In `page_fault_handler()`, I added code:

```c
if (curenv->env_pgfault_upcall != NULL) {
	uintptr_t utf_addr;
	if (UXSTACKTOP - PGSIZE <= tf->tf_esp && tf->tf_esp < UXSTACKTOP) {
		// recursive
		utf_addr = tf->tf_esp - sizeof(struct UTrapframe) - sizeof(uintptr_t);
	} else {
		// non-recursive
		utf_addr = UXSTACKTOP - sizeof(struct UTrapframe);
	}
	user_mem_assert(curenv, (const void *) utf_addr, sizeof(struct UTrapframe), PTE_W);
	struct UTrapframe *utf_ptr = (struct UTrapframe *) utf_addr;
	utf_ptr->utf_eflags = tf->tf_eflags;
	utf_ptr->utf_eip = tf->tf_eip;
	utf_ptr->utf_err = tf->tf_err;
	utf_ptr->utf_regs = tf->tf_regs;
	utf_ptr->utf_esp = tf->tf_esp;
	utf_ptr->utf_fault_va = fault_va;
	curenv->env_tf.tf_eip = (uintptr_t) curenv->env_pgfault_upcall;
	curenv->env_tf.tf_esp = utf_addr;
	env_run(curenv);
}
```

**Exercise 10.** *Implement the `_pgfault_upcall` routine in `lib/pfentry.S`. The interesting part is returning to the original point in the user code that caused the page fault. You'll return directly there, without going back through the kernel. The hard part is simultaneously switching stacks and re-loading the EIP.*

In `pentry.S`, I added code:

```assembly
// Throughout the remaining code, think carefully about what
// registers are available for intermediate calculations.  You
// may find that you have to rearrange your code in non-obvious
// ways as registers become unavailable as scratch space.
//
// LAB 4: Your code here.
// Next 2 lines: *trap_time_esp -= 4
movl 0x28(%esp), %eax
subl $0x4, 0x30(%esp)
// Next 2 lines: *trap_time_esp = trap_time_eip
movl 0x30(%esp), %ecx
movl %eax, (%ecx)

// Restore the trap-time registers.  After you do this, you
// can no longer modify any general-purpose registers.
// LAB 4: Your code here.
// Skip fault_va and tf_err
addl $0x8, %esp
popal

// Restore eflags from the stack.  After you do this, you can
// no longer use arithmetic operations or anything else that
// modifies eflags.
// LAB 4: Your code here.
// Skip eip
addl $0x4, %esp
popfl

// Switch back to the adjusted trap-time stack.
// LAB 4: Your code here.
popl %esp

// Return to re-execute the instruction that faulted.
// LAB 4: Your code here.
ret
```

**Exercise 11.**  *Finish `set_pgfault_handler()` in `lib/pgfault.c`.*

In `set_pgfault_handler()`, I added code:

```c
r = sys_page_alloc(env_id, (void *) (UXSTACKTOP - PGSIZE), PTE_W | PTE_U);
if (r < 0) {
	sys_env_destroy(env_id);
}
r = sys_env_set_pgfault_upcall(env_id, _pgfault_upcall);
if (r < 0) {
	sys_env_destroy(env_id);
}
```

**Exercise 12.** *Implement `fork`, `duppage` and `pgfault` in `lib/fork.c`.*

In `pgfault()`, I added code:

```c
if (!((err & FEC_PR) && (uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_COW))) {
	panic("not COW pagefault");
}
```

and:

```c
addr = ROUNDDOWN(addr, PGSIZE);
sys_page_alloc(0, PFTEMP, PTE_W | PTE_U);
memcpy(PFTEMP, addr, PGSIZE);
sys_page_map(0, PFTEMP, 0, addr, (uvpt[PGNUM(addr)] & PTE_SYSCALL & ~PTE_COW) | PTE_W);
sys_page_unmap(0, PFTEMP);
```

In `duppage()`, I added code:

```c
uintptr_t addr = pn * PGSIZE;
if ((uvpt[pn] & PTE_COW) || (uvpt[pn] & PTE_W)) {
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

In `fork()`, I added code:

```c
set_pgfault_handler(pgfault);
envid_t child_id = sys_exofork();
if (child_id < 0) {
	// Error
	return child_id;
} else if (child_id == 0) {
	// I am the child
	set_pgfault_handler(pgfault);
	thisenv = envs + ENVX(sys_getenvid());
	return 0;
}
for (uintptr_t addr = 0; addr < USTACKTOP; addr += PGSIZE) {
	if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_U)) {
		duppage(child_id, PGNUM(addr));
	}
}
// Exception stack
int r = sys_page_alloc(child_id, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_W);
if (r < 0) return r;
extern void _pgfault_upcall();
sys_env_set_pgfault_upcall(child_id, _pgfault_upcall);
r = sys_env_set_status(child_id, ENV_RUNNABLE);
if (r < 0) return r;
return child_id;
```

**Exercise 13.** *Modify `kern/trapentry.S` and `kern/trap.c` to initialize the appropriate entries in the IDT and provide handlers for IRQs 0 through 15. Then modify the code in `env_alloc()`in `kern/env.c` to ensure that user environments are always run with interrupts enabled.*

*Also uncomment the `sti` instruction in `sched_halt()` so that idle CPUs unmask interrupts.*

In `env_alloc()`, I added:

```c
e->env_tf.tf_eflags |= FL_IF;
```

**Exercise 14.** *Modify the kernel's `trap_dispatch()` function so that it calls `sched_yield()` to find and run a different environment whenever a clock interrupt takes place.*

In `trap_init()`, I added:

```c
SETGATE(idt[IRQ_OFFSET + IRQ_TIMER], 0, GD_KT, idt_entries[IRQ_OFFSET + IRQ_TIMER], 0);
```

In `trap_dispatch`, I added:

```c
if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER) {
	lapic_eoi();
	sched_yield();
	return;
}
```

**Exercise 15.** *Implement `sys_ipc_recv` and `sys_ipc_try_send` in `kern/syscall.c`. Read the comments on both before implementing them, since they have to work together. When you call `envid2env` in these routines, you should set the `checkperm` flag to 0, meaning that any environment is allowed to send IPC messages to any other environment, and the kernel does no special permission checking other than verifying that the target envid is valid.*

*Then implement the `ipc_recv` and `ipc_send` functions in `lib/ipc.c`.*

In `sys_ipc_try_send()`, I added code:

```c
struct Env *env;
if (envid2env(envid, &env, 0)) {
	return -E_BAD_ENV;
}
if (!env->env_ipc_recving) {
	return -E_IPC_NOT_RECV;
}
if ((uintptr_t)srcva < UTOP) {
	if (ROUNDUP(srcva, PGSIZE) == srcva && (perm & PTE_SYSCALL) == perm ) {
		struct PageInfo * page;
		pte_t * pte;
		if ((page = page_lookup(curenv->env_pgdir, srcva, &pte))) {
			if (!(perm & PTE_W) || (*pte & PTE_W)) {
				if((uintptr_t)env->env_ipc_dstva < UTOP && page_insert(env->env_pgdir, page, env->env_ipc_dstva, perm)) {
					return -E_NO_MEM;
				}
				env->env_ipc_perm = perm;
			}
			else {
				return -E_INVAL;
			}
		}
		else {
			return -E_INVAL;
		}
	} else {
		return -E_INVAL;
	}
} else {
	env->env_ipc_perm = 0;
}
env->env_ipc_recving = false;
env->env_ipc_from = curenv->env_id;
env->env_ipc_value = value;
env->env_status = ENV_RUNNABLE;
return 0;
```

In `sys_ipc_recv()`, I added:

```c
curenv->env_ipc_recving = true;
if ((uintptr_t)dstva < UTOP) {
	if (ROUNDUP(dstva, PGSIZE) != dstva) {
		return -E_INVAL;
	}
	curenv->env_ipc_dstva = dstva;
} else {
	curenv->env_ipc_dstva = (void *)0xFFFFFFFF;
}
curenv->env_status = ENV_NOT_RUNNABLE;
curenv->env_tf.tf_regs.reg_eax = 0;
sched_yield();
```

In `syscall()`, I added:

```c
case SYS_ipc_try_send:
	return sys_ipc_try_send(a1, a2, (void *) a3, a4);
case SYS_ipc_recv:
	return sys_ipc_recv((void *) a1);
```

In `ipc_recv()`, I added code:

```c
if (!pg) {
	pg = (void *) 0xFFFFFFFF;
}
if (sys_ipc_recv(pg)) {
	*from_env_store = 0;
	*perm_store = 0;
	return -E_INVAL;
}
if (perm_store) {
	*perm_store = thisenv->env_ipc_perm;
}
if (from_env_store) {
	*from_env_store = thisenv->env_ipc_from;
}
return thisenv->env_ipc_value;
```

In `ipc_send()`, I added code:

```c
int result;
while ((result = sys_ipc_try_send(to_env, val, pg, perm)) == -E_IPC_NOT_RECV) {
	sys_yield();
}
if (result) {
	panic("ipc_send: %e", result);
}
```

