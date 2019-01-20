# Lab 4

**Exercise 1.** *Implement `mmio_map_region` in `kern/pmap.c`. To see how this is used, look at the beginning of `lapic_init` in `kern/lapic.c`.*

In `mmio_map_region()` , I added code:

```c
physaddr_t pa_end = ROUNDUP(pa + size, PGSIZE), pa_start = ROUNDDOWN(pa, PGSIZE);
if (base - (pa_end - pa_start) > MMIOLIM || pa_end < pa_start) {
	panic("mmio_map_region overflow %x %x", pa_start, pa_end);
}
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
- When compiling `boot.S`, the linker link its symbols to low addresses. When loading `boot.S`, we can just load it at low addresses and run it. However, `mpentry.S` also loads at low addresses but linked to high addresses, so we need `MPBOOTPHYS()` to map back to low addresses.

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

**Exercise 6.** *Modify `trap_dispatch()` to make breakpoint exceptions invoke the kernel monitor. You should now be able to get make grade to succeed on the `breakpoint` test.*

In `trap_dispatch()`, I added code:

```c
} else if (tf->tf_trapno == T_BRKPT) {
	monitor(tf);
	return;
}
```

To make it callable from user-mode apps, I modified code in `trap_init()` as:

```c
for (int i = 0; i < 20; ++i) {
	if (i == T_BRKPT) {
		SETGATE(idt[i], 0, GD_KT, idt_entries[i], 3);
	} else {  // privileged
		SETGATE(idt[i], 0, GD_KT, idt_entries[i], 0);
	}
}
```

**Challenge 2.** *Modify the JOS kernel monitor so that you can 'continue' execution from the current location (e.g., after the `int3`, if the kernel monitor was invoked via the breakpoint exception), and so that you can single-step one instruction at a time.* 

First, I added two monitor commands: `continue` and `stepi`.

In `monitor.h`, I added code:

```c
int mon_continue(int argc, char **argv, struct Trapframe *tf);
int mon_stepi(int argc, char **argv, struct Trapframe *tf);
```

In `monitor.c`, I added code:

```c
#include <kern/env.h>
```

and

```c
{ "continue", "Continue running", mon_continue },
{ "stepi", "step to the next instruction", mon_stepi }
```

and their definitions:

```c
int mon_continue(int argc, char **argv, struct Trapframe *tf) {
	assert(tf && (tf->tf_trapno == T_BRKPT || tf->tf_trapno == T_DEBUG));
	tf->tf_eflags &= ~0x100;  // clear the trap flag
	env_run(curenv);
	return 0;
}

int mon_stepi(int argc, char **argv, struct Trapframe *tf) {
	assert(tf && (tf->tf_trapno == T_BRKPT || tf->tf_trapno == T_DEBUG));
	tf->tf_eflags |= 0x100;  // set the trap flag
	env_run(curenv);
	return 0;
}
```

If the trap flag in `EFLAGS` is set, the processor will interrupt after executing the next instruction with `T_DEBUG`.

Then, I added trap handler for `T_DEBUG`:

In `trap_dispatch()` in `trap.c`, I modified the code to be:

```c
} else if (tf->tf_trapno == T_BRKPT || tf->tf_trapno == T_DEBUG) {
	monitor(tf);
	return;
}
```

I tested my tiny debugger using `breakpoint` program. I run `make run-breakpoint-nox`, and the output is:

```
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!                                                                                          [40/1896]
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf01c0000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0xeec00000
  es   0x----0023
  ds   0x----0023
  trap 0x00000003 Breakpoint
  err  0x00000000
  eip  0x00800037
  cs   0x----001b
  flag 0x00000082
  esp  0xeebfdfd0
  ss   0x----0023
K> 
```

I typed `stepi` to let it execute the next instruction and stop:

```
K> stepi                                              
Incoming TRAP frame at 0xefffffbc
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf01c0000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdff0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0xeec00000
  es   0x----0023
  ds   0x----0023
  trap 0x00000001 Debug
  err  0x00000000
  eip  0x00800038
  cs   0x----001b
  flag 0x00000182
  esp  0xeebfdfd4
  ss   0x----0023
K>   
```

Then, I typed `continue` to let it continue running:

```
K> continue
Incoming TRAP frame at 0xefffffbc
[00001000] exiting gracefully
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
```

**Questions 2.**

1. *The break point test case will either generate a break point exception or a general protection fault depending on how you initialized the break point entry in the IDT (i.e., your call to`SETGATE` from `trap_init`). Why? How do you need to set it up in order to get the breakpoint exception to work as specified above and what incorrect setup would cause it to trigger a general protection fault?*

   If we pass `0` as `dpl` (privilege level) when calling `SETGATE`, the test case will generate a general protection fault because user-mode apps have no permission to execute `int 03h`. So we modified `dpl` to be `3`, then user-mode apps can enter the trap handler of break point exception correctly.

2. *What do you think is the point of these mechanisms, particularly in light of what the `user/softint` test program does?*

   Because there are protections that prevent user-mode apps from arbitrarily invoking trap handlers in the kernel.

**Exercise 7.** *Add a handler in the kernel for interrupt vector `T_SYSCALL`. Finally, you need to implement `syscall()` in `kern/syscall.c`. Handle all the system calls listed in `inc/syscall.h` by invoking the corresponding kernel function for each call.*

In `trapentry.S`, I added code:

```assembly
TRAPHANDLER_NOEC(handle_syscall, T_SYSCALL)
```

In `trap_init()` of `trap.c`, I added code:

```c
extern void handle_syscall();
SETGATE(idt[T_SYSCALL], 1, GD_KT, handle_syscall, 3);
```

In `trap_dispatch()` of `trap.c`, I added code:

```c
} else if (tf->tf_trapno == T_SYSCALL) {
	tf->tf_regs.reg_eax = syscall(
		tf->tf_regs.reg_eax,  // number
		tf->tf_regs.reg_edx,  // arg 1
		tf->tf_regs.reg_ecx,  // arg 2
		tf->tf_regs.reg_ebx,  // arg 3
		tf->tf_regs.reg_edi,  // arg 4
		tf->tf_regs.reg_esi  // arg 5
	);
    return;
}
```

I modified `syscall()` in `syscall.c` to be:

```c
switch (syscallno) {
case SYS_cputs:
	sys_cputs((const char *) a1, a2);
	return 0;
case SYS_cgetc:
	return sys_cgetc();
case SYS_getenvid:
	return sys_getenvid();
case SYS_env_destroy:
	return sys_env_destroy(a1);
default:
	return -E_INVAL;
}
```

**Exercise 8.** *Add the required code to the user library, then boot your kernel. You should see `user/hello` print "`hello, world`" and then print "`i am environment 00001000`". `user/hello` then attempts to "exit" by calling `sys_env_destroy()` (see `lib/libmain.c` and `lib/exit.c`).*

In `libmain()`, I added code:

```c
thisenv = envs + ENVX(sys_getenvid());
```

**Exercise 9.** 

- *Change `kern/trap.c` to panic if a page fault happens in kernel mode.*

  In `page_fault_handler()`, I added code:

  ```c
  if ((tf->tf_cs & 0x3) == 0) {  // the last 2 bits of CS is the DPL
  	panic("kernel page fault");
  }
  ```

- *Read `user_mem_assert` in `kern/pmap.c` and implement `user_mem_check` in that same file.*

  In `user_mem_check()`, I added code:

  ```c
  const void *va_end = va + len;
  for (void *i = ROUNDDOWN(va, PGSIZE); i < va_end; i += PGSIZE) {
  	if (i >= ULIM) {
  		user_mem_check_addr = (uintptr_t) MIN(i, va);
  		return -E_FAULT;
  	}
  	pte_t *pte_ptr;
  	struct PageInfo *pp = page_lookup(env->env_pgdir, i, &pte_ptr);
  	if (!pp || ((perm & (*pte_ptr)) != perm)) {  // perm in pte
  		user_mem_check_addr = (uintptr_t) MIN(i, va);
  		return -E_FAULT;
  	}
  }
  ```

- *Change `kern/syscall.c` to sanity check arguments to system calls.*

  In `sys_cputs()`, I added code:

  ```c
  user_mem_assert(curenv, (const void *) s, len, PTE_U);
  ```

- *Finally, change `debuginfo_eip` in `kern/kdebug.c` to call `user_mem_check` on `usd`, `stabs`, and `stabstr`.*

  In `debuginfo_eip()`, I added code:

  ```c
  if (user_mem_check(curenv, (const void *) addr, sizeof(struct UserStabData), PTE_U)) {
  	return -1;
  }
  ```

  and

  ```c
  if (user_mem_check(curenv, (const void *) stabs, ((uintptr_t) stab_end) - ((uintptr_t) stabs), PTE_U) ||
  	user_mem_check(curenv, (const void *) stabstr, ((uintptr_t) stabstr_end) - ((uintptr_t) stabstr), PTE_U)) {
  	return -1;
  }
  ```

**Exercise 10.** *Boot your kernel, running `user/evilhello`. The environment should be destroyed, and the kernel should not panic.*

I run `evilhello` with

```
make run-evilhello
```

And the qemu outputs:

```
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
[00001000] user_mem_check assertion failure for va f010000c
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
```

