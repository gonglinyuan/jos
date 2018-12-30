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


