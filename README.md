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


