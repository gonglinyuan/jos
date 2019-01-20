# Lab 3

**Exercise 1.** *Modify `mem_init()` in `kern/pmap.c` to allocate and map the `envs` array. This array consists of exactly `NENV` instances of the `Env` structure allocated much like how you allocated the `pages` array. Also like the `pages` array, the memory backing `envs` should also be mapped user read-only at `UENVS` (defined in `inc/memlayout.h`) so user processes can read from this array.* 

In `mem_init()` , I added code:

```c
envs = (struct Env *) boot_alloc(NENV * sizeof(struct Env));
memset(envs, 0, NENV * sizeof(struct Env));
```

and

```c
boot_map_region(kern_pgdir, UENVS, NENV * sizeof(struct Env), PADDR(envs), PTE_U);
```

**Exercise 2.** *In the file env.c, finish coding the following functions:*

- *`env_init()` : Initialize all of the Env structures in the envs array and add them to the env_free_list. Also calls env_init_percpu, which configures the segmentation hardware with separate segments for privilege level 0 (kernel) and privilege level 3 (user).*

  In `env_init()`, I added code:

  ```c
  env_free_list = NULL;
  for (int i = NENV - 1; i >= 0; --i) {  // reverse order
  	envs[i].env_link = env_free_list;
  	env_free_list = envs + i;
  }
  ```

- *`env_setup_vm()` : Allocate a page directory for a new environment and initialize the kernel portion of the new environment's address space.*

  In `env_setup_vm()`, I added code:

  ```c
  p->pp_ref++;
  e->env_pgdir = (pde_t *) page2kva(p);
  memcpy(e->env_pgdir, kern_pgdir, PGSIZE);
  ```

- *`region_alloc()` : Allocates and maps physical memory for an environment*

  In `region_alloc()`, I added code:

  ```c
  void *va_end = va + len;
  for (va = ROUNDDOWN(va, PGSIZE); va < va_end; va += PGSIZE) {
  	struct PageInfo *pp = page_alloc(0);
  	if (!pp) {
  		panic("region_alloc failed.");
  	}
  	page_insert(e->env_pgdir, pp, va, PTE_U | PTE_W);
  }
  ```

- *`load_icode()` : You will need to parse an ELF binary image, much like the boot loader already does, and load its contents into the user address space of a new environment.*

  In `load_icode()`, I added code:

  ```c
  struct Elf *eh = (struct Elf *) binary;
  if (eh->e_magic != ELF_MAGIC) {
  	panic("load icode error: not a valid ELF");
  }
  
  struct Proghdr *ph = (struct Proghdr *) (binary + eh->e_phoff);
  struct Proghdr *eph = ph + eh->e_phnum;
  
  lcr3(PADDR(e->env_pgdir));  // switch to user env's address space
  for (; ph < eph; ph++) {
  	if (ph->p_type == ELF_PROG_LOAD) {
  		region_alloc(e, (void *) ph->p_va, ph->p_memsz);
  		memset((void *) ph->p_va, 0, ph->p_memsz);
  		memcpy((void *) ph->p_va, binary + ph->p_offset, ph->p_filesz);
  	}
  }
  lcr3(PADDR(kern_pgdir));  // switch back to kernel's address space
  
  e->env_tf.tf_eip = eh->e_entry;  // set saved PC to the entrypoint of the ELF
  ```

  and:

  ```c
  region_alloc(e, (void *) (USTACKTOP - PGSIZE), PGSIZE);
  ```

- *`env_create()` : Allocate an environment with `env_alloc` and call `load_icode` to load an ELF binary into it.*

  In `env_create()`, I added code:

  ```c
  struct Env *e;
  if (env_alloc(&e, 0) < 0) {
  	panic("env_create failed to allocate new environment");
  }
  load_icode(e, binary);
  e->env_type = ENV_TYPE_USER;
  ```

- *`env_run()` : Start a given environment running in user mode.*

  In `env_run()`, I added code:

  ```c
  if (curenv && curenv->env_status == ENV_RUNNING) {
  	curenv->env_status = ENV_RUNNABLE;
  }
  curenv = e;
  e->env_status = ENV_RUNNING;
  e->env_runs++;
  lcr3(PADDR(e->env_pgdir));
  env_pop_tf(&e->env_tf);
  ```

**Exercise 3.** *Read Chapter 9, Exceptions and Interrupts in the 80386 Programmer's Manual (or Chapter 5 of the IA-32 Developer's Manual), if you haven't already.*

**Exercise 4.** *Edit `trapentry.S` and `trap.c` and implement the features described above.*

- *You will need to add an entry point in `trapentry.S` (using those macros) for each trap defined in `inc/trap.h`*

  In `trapentry.S` I added code:

  ```assembly
  TRAPHANDLER_NOEC(handle_divide, T_DIVIDE)
  TRAPHANDLER_NOEC(handle_debug, T_DEBUG)
  TRAPHANDLER_NOEC(handle_nmi, T_NMI)
  TRAPHANDLER_NOEC(handle_brkpt, T_BRKPT)
  TRAPHANDLER_NOEC(handle_oflow, T_OFLOW)
  TRAPHANDLER_NOEC(handle_bound, T_BOUND)
  TRAPHANDLER_NOEC(handle_illop, T_ILLOP)
  TRAPHANDLER_NOEC(handle_device, T_DEVICE)
  TRAPHANDLER(handle_dblflt, T_DBLFLT)
  /* TRAPHANDLER_NOEC(handle_coproc, T_COPROC) */
  TRAPHANDLER(handle_tss, T_TSS)
  TRAPHANDLER(handle_segnp, T_SEGNP)
  TRAPHANDLER(handle_stack, T_STACK)
  TRAPHANDLER(handle_gpflt, T_GPFLT)
  TRAPHANDLER(handle_pgflt, T_PGFLT)
  /* TRAPHANDLER(handle_res, T_RES) */
  TRAPHANDLER_NOEC(handle_fperr, T_FPERR)
  ```

- *and you'll have to provide `_alltraps` which the `TRAPHANDLER` macros refer to.*

  In `trapentry.S` I added code:

  ```assembly
  _alltraps:
  	pushl	%ds
  	pushl	%es
  	pushal
  	pushl	$GD_KD
  	popl	%ds
  	pushl	$GD_KD
  	popl	%es
  	pushl	%esp
  	call	trap
  ```

- *You will also need to modify `trap_init()` to initialize the `idt` to point to each of these entry points defined in `trapentry.S`*

  In `trap_init()` I added code:

  ```c
  void handle_divide();
  void handle_debug();
  void handle_nmi();
  void handle_brkpt();
  void handle_oflow();
  void handle_bound();
  void handle_illop();
  void handle_device();
  void handle_dblflt();
  /* void handle_coproc(); */
  void handle_tss();
  void handle_segnp();
  void handle_stack();
  void handle_gpflt();
  void handle_pgflt();
  /* void handle_res(); */
  void handle_fperr();
  
  SETGATE(idt[T_DIVIDE], 0, GD_KT, handle_divide, 0);
  SETGATE(idt[T_DEBUG], 0, GD_KT, handle_debug, 0);
  SETGATE(idt[T_NMI], 0, GD_KT, handle_nmi, 0);
  SETGATE(idt[T_BRKPT], 0, GD_KT, handle_brkpt, 0);
  SETGATE(idt[T_OFLOW], 0, GD_KT, handle_oflow, 0);
  SETGATE(idt[T_BOUND], 0, GD_KT, handle_bound, 0);
  SETGATE(idt[T_ILLOP], 0, GD_KT, handle_illop, 0);
  SETGATE(idt[T_DEVICE], 0, GD_KT, handle_device, 0);
  SETGATE(idt[T_DBLFLT], 0, GD_KT, handle_dblflt, 0);
  /* SETGATE(idt[T_COPROC], 0, GD_KT, handle_coproc, 0); */
  SETGATE(idt[T_TSS], 0, GD_KT, handle_tss, 0);
  SETGATE(idt[T_SEGNP], 0, GD_KT, handle_segnp, 0);
  SETGATE(idt[T_STACK], 0, GD_KT, handle_stack, 0);
  SETGATE(idt[T_GPFLT], 0, GD_KT, handle_gpflt, 0);
  SETGATE(idt[T_PGFLT], 0, GD_KT, handle_pgflt, 0);
  /* SETGATE(idt[T_RES], 0, GD_KT, handle_res, 0); */
  SETGATE(idt[T_FPERR], 0, GD_KT, handle_fperr, 0);
  ```

**Challenge 1.** *You probably have a lot of very similar code right now, between the lists of `TRAPHANDLER` in `trapentry.S` and their installations in `trap.c`. Clean this up. Change the macros in`trapentry.S` to automatically generate a table for `trap.c` to use.*

In `trapentry.S`, I added code:

```assembly
.data
.global idt_entries
idt_entries:
	.long handle_divide
	.long handle_debug
	.long handle_nmi
	.long handle_brkpt
	.long handle_oflow
	.long handle_bound
	.long handle_illop
	.long handle_device
	.long handle_dblflt
	.long handle_coproc
	.long handle_tss
	.long handle_segnp
	.long handle_stack
	.long handle_gpflt
	.long handle_pgflt
	.long handle_res
	.long handle_fperr
	.long handle_align
	.long handle_mchk
	.long handle_simderr
```

I modified `trap_init()` to be:

```c
extern uint32_t idt_entries[];
for (int i = 0; i < 20; ++i) {
	SETGATE(idt[i], 0, GD_KT, idt_entries[i], 0);
}
```

and eliminated duplicate codes.

**Questions 1.**

1. *What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)*

   If we use an individual handler function for each exception/interrupt, we can safely let user apps deal with some of the exceptions and disallow user apps to catch other exceptions. Therefore, it is easier to provide protections.

2. *Did you have to do anything to make the `user/softint` program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but `softint`'s code says`int $14`. Why should this produce interrupt vector 13? What happens if the kernel actually allows `softint`'s `int $14` instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?*

   The `int 14` produces interrupt vector 13 because the DPL for page fault handler is 0 (kernel privileged). So when CPU finds out that the user is calling `int 14`, it will trigger the general protection fault, which gives a trap number 13.

**Exercise 5.** *Modify `trap_dispatch()` to dispatch page fault exceptions to `page_fault_handler()`.*

In `trap_dispatch()`, I added code:

```c
if (tf->tf_trapno == T_PGFLT) {
	page_fault_handler(tf);
	return;
}
```

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

