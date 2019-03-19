# Lab 2

龚林源 1600012714

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Exercise 1.** *In the file `kern/pmap.c`, you must implement code for the following functions (probably in the order given).* 

In `boot_alloc()` , I added code:

```c
if (n > 0) {
	n = ROUNDUP(n, PGSIZE);
	void *res = (void *) nextfree;
	nextfree = (char *) ((uint32_t) nextfree + n);
	return res;
} else if (n == 0) {
	return (void *) nextfree;
} else {
	panic("call boot_alloc with negative n");
}
```

In `mem_init()` , I added these code to allocate memory for `pages` :

```c
pages = (struct PageInfo *) boot_alloc(npages * sizeof(struct PageInfo));
memset(pages, 0, npages * sizeof(struct PageInfo));
```

In `page_init()` , I modified the code to be:

```c
assert(page_free_list == NULL);
physaddr_t kern_memory_end = PADDR(boot_alloc(0));  // end of kernel's memory
for (size_t i = 0; i < npages; i++) {
	physaddr_t pa = page2pa(pages + i);
	// [PGSIZE, npages_basemem * PGSIZE) or (kern_memory_end, inf) 
	if ((PGSIZE <= pa && pa < npages_basemem * PGSIZE) || (pa > kern_memory_end)) {
		assert(i > 0);
		pages[i].pp_ref = 0;
		pages[i].pp_link = page_free_list;
		page_free_list = &pages[i];
	}
}
```

In `page_alloc()` , I wrote:

```c
struct PageInfo *pp = page_free_list;
if (!pp) {
	return NULL;
}
page_free_list = pp->pp_link;  // remove the first element of the free list
pp->pp_link = NULL;
pp->pp_ref = 0;
if (alloc_flags & ALLOC_ZERO) {
	memset(page2kva(pp), 0, PGSIZE);
}
return pp;
```

In `page_free()` , I wrote:

```c
assert((pp->pp_ref == 0) && (pp->pp_link == NULL));
pp->pp_link = page_free_list;
page_free_list = pp;  // add it to the front of the free list
```

**Exercise 2.** *Look at chapters 5 and 6 of the [Intel 80386 Reference Manual](https://pdos.csail.mit.edu/6.828/2014/readings/i386/toc.htm), if you haven't done so already. Read the sections about page translation and page-based protection closely (5.2 and 6.4). We recommend that you also skim the sections about segmentation; while JOS uses paging for virtual memory and protection, segment translation and segment-based protection cannot be disabled on the x86, so you will need a basic understanding of it.*

**Exercise 3.** *While GDB can only access QEMU's memory by virtual address, it's often useful to be able to inspect physical memory while setting up virtual memory. Review the QEMU [monitor commands](https://pdos.csail.mit.edu/6.828/2014/labguide.html#qemu) from the lab tools guide, especially the `xp` command, which lets you inspect physical memory. To access the QEMU monitor, press Ctrl-a c in the terminal (the same binding returns to the serial console).*

**Question.** *Assuming that the following JOS kernel code is correct, what type should variable `x` have, `uintptr_t` or `physaddr_t`?* 

```c
mystery_t x;
char* value = return_a_pointer();
*value = 10;
x = (mystery_t) value;
```

The type of `x` should be `uintptr_t` . Because `value` is a pointer, it is a virtual memory address. So, `x` should also be a virtual memory address rather than a physical memory address. Hence, it is `uintptr_t` .

**Exercise 4.** *In the file `kern/pmap.c`, you must implement code for the following functions.*

In `pgdir_walk()` , I wrote:

```c
pde_t *pde_ptr = pgdir + PDX(va);  // pointer to PDE
if (!((*pde_ptr) & PTE_P)) {  // if PT not present
	if (create) {  // create PT
		struct PageInfo *pp = page_alloc(ALLOC_ZERO);
		if (!pp) {
			return NULL;
		}
		pp->pp_ref++;
		*pde_ptr = PTE_P | PTE_W | PTE_U | page2pa(pp);  // create PTE
	} else {
		return NULL;
	}
}
pte_t *pgtable = (pte_t *) KADDR(PTE_ADDR(*pde_ptr));
return pgtable + PTX(va);  // return PTE
```

In `boot_map_region()` , I wrote:

```c
assert((va % PGSIZE == 0) && (pa % PGSIZE == 0) && (size % PGSIZE == 0));  // make sure it's aligned
for (size_t i = 0; i < size; i += PGSIZE) {
	pte_t *pte_ptr = pgdir_walk(pgdir, (void *)(va + i), 1);
	*pte_ptr = (pa + i) | perm | PTE_P;  // edit PTE
}
```
In `page_lookup()` , I wrote:

```c
pte_t *pte_ptr = pgdir_walk(pgdir, va, 0);  // pointer to PTE
if (!pte_ptr || !(*pte_ptr & PTE_P)) {  // PT not present or PTE not present
	return NULL;
}
if (pte_store) {
	*pte_store = pte_ptr;
}
return pa2page(PTE_ADDR(*pte_ptr));
```

In `page_remove()` , I wrote:

```c
pte_t *pte_ptr;
struct PageInfo *page_ptr = page_lookup(pgdir, va, &pte_ptr);
if (page_ptr == NULL) {  // no such page
	return;  // exit silently
}
*pte_ptr = 0;
tlb_invalidate(pgdir, va);
page_decref(page_ptr);
```

In `page_insert()`, I wrote:

```c
pte_t *pte_ptr = pgdir_walk(pgdir, va, 1);
if (!pte_ptr) {  // allocate for new page table failed
	return -E_NO_MEM;
}
pp->pp_ref++;  // increment ref count first
if ((*pte_ptr) & PTE_P) {
	page_remove(pgdir, va);
}
*pte_ptr = page2pa(pp) | perm | PTE_P;
tlb_invalidate(pgdir, va);
return 0;
```

To prevent the problem that when the same `va` is `page_insert()`ed twice, the physical page will be accidentally `page_free()`ed, I increment `pp->pp_ref` before calling `page_remove()`. In this way, `page_remove()` will not decrement `pp->pp_ref` to `0` when calling `page_decref()`, and hence `page_free(pp)` will not be accidentally triggered.

**Exercise 5.** *Fill in the missing code in `mem_init()` after the call to `check_page()`.*

To map the `pages` data structure (physical free page list and reference counts) to `UPAGES`, and to make it visible to users but read-only:

```c
assert(npages * sizeof(struct PageInfo) <= PTSIZE);
boot_map_region(kern_pgdir, UPAGES, npages * sizeof(struct PageInfo), PADDR(pages), PTE_U);
```

To map the memory of the kernel stack to the address space below `KSTACKTOP`, and to make it only visible to the kernel:

```c
boot_map_region(kern_pgdir, KSTACKTOP - KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W);
```

To map the whole physical memory to address space above `KERNBASE`, and to make it only visible to the kernel:

```c
boot_map_region(kern_pgdir, KERNBASE, -KERNBASE, 0, PTE_W);
// note that for uint32, -KERNBASE == 2^32 - KERNBASE
```

**Question.**

1. *What entries (rows) in the page directory have been filled in at this point? What addresses do they map and where do they point? In other words, fill out this table as much as possible:*

   - 957-th entry, `0xEF400000` , points to the page directory (UVPT)
   - 956-th entry, `0xEF000000` , points to the page table of user's read-only image of `pages` array
   - 959-th entry, `0xEFF00000` , points to the page table containing the kernel stack
   - 960-th entry to 1023-th entry, `0xF0000000` to `0xFFC00000` , points to the page table for bottom `4MB` to top `4MB` of physical memory (suppose the physical memory is `256MB`)

2. *We have placed the kernel and user environment in the same address space. Why will user programs not be able to read or write the kernel's memory? What specific mechanisms protect the kernel memory?*

   Because we set the permission bits in the PTE. When `PTE_U` is unset, users cannot read or write the memory in this page; when `PTE_U` is set but `PTE_W` is unset, this virtual page will be read-only to users.

3. *What is the maximum amount of physical memory that this operating system can support? Why?*

   If the total physical memory is `m` (in bytes)

   Because `totalmem` is in `KB`, so `m = totalmem * 1024` .

   The space between `UPAGES` and `UVPT` is `PTSIZE = 4MB` , i.e. only `4MB` of `pages` is mapped. So,

   `npages * sizeof(struct PageInfo) = totalmem / (PGSIZE / 1024) * sizeof(struct PageInfo) = totalmem / 4 * 8 <= 4MB`

   Therefore, `m / 1024 / 4 * 8 <= 4MB`, i.e. `m <= 2048MB = 2GB`

4. *How much space overhead is there for managing memory, if we actually had the maximum amount of physical memory? How is this overhead broken down?*

   The `pages` data structure (free list) occupies `ROUND(totalmem / (PGSIZE / 1024) * sizeof(struct PageInfo), PGSIZE) = ROUND(totalmem / 4 * 8, PGSIZE)` . 

   `totalmem` can be at most `2097152`, so the overhead here is at most `4194304B = 4MB` .

   `kern_pgdir` occupies 1 page, which is `4096` bytes.

   When all page tables is full, there are `1024` page tables, so the overhead here is `1024 * 4K = 4MB`

   So the total overhead is at most `4MB + 4KB + 4MB = 8196KB`

5. *Revisit the page table setup in `kern/entry.S` and `kern/entrypgdir.c`. Immediately after we turn on paging, EIP is still a low number (a little over 1MB). At what point do we transition to running at an EIP above KERNBASE? What makes it possible for us to continue executing at a low EIP between when we enable paging and when we begin running at an EIP above KERNBASE? Why is this transition necessary?*

   In `entry.S`,

   ```assembly
   movl $(RELOC(entry_pgdir)), %eax
   movl %eax, %cr3
   ```

   It sets the page directory base register (PDBR) `CR3` to `entry_pgdir`, and make it the page directory. It maps both low addresses and high addresses to the same physical memory. 

   ```javascript
   mov $relocated, %eax
   jmp *%eax
   ```

   Jumps to high addresses.

   This transition is necessary because we will use our `kern_pgdir` defined in this lab instead of `entry_pgdir`. In `kern_pgdir` , the low memory is no longer mapped to the lowest `4MB` of the virtual address space.

**Challenge 2.** *Extend the JOS kernel monitor with commands to:*

- *Display in a useful and easy-to-read format all of the physical page mappings (or lack thereof) that apply to a particular range of virtual/linear addresses in the currently active address space.* 

  I wrote two helper functions in `monitor.c` :

  ```c
  uintptr_t hex_2_ptr(char *s) {
  	if (s[0] == '0' && s[1] == 'x') {
  		s += 2;
  	}
  	uintptr_t res = 0;
  	for (; *s; ++s) {
  		if ('0' <= *s && *s <= '9') {
  			res = res * 16 + (*s) - '0';
  		} else if ('a' <= *s && *s <= 'f') {
  			res = res * 16 + (*s) - 'a' + 10;
  		} else if ('A' <= *s && *s <= 'F') {
  			res = res * 16 + (*s) - 'A' + 10;
  		} else {
  			panic("invalid input hex address");
  		}
  	}
  	return res;
  }
  
  // pretty print of a PTE
  void print_pte(pte_t pte) {
  	if (pte & PTE_P) {
  		cprintf(
  			"%08x %c %s P\n",
  			PTE_ADDR(pte),
  			(pte & PTE_U) ? 'U' : 'S',
  			(pte & PTE_W) ? "RW" : "R-"
  		);
  	} else {
  		cprintf("00000000 - -- -\n");
  	}
  }
  ```

  Then I wrote `mon_showmapping()` :

  ```c
  int mon_showmapping(int argc, char **argv, struct Trapframe *tf) {
  	assert(argc == 3);
  	uintptr_t adr_0 = ROUNDDOWN(hex_2_ptr(argv[1]), PGSIZE), adr_1 = ROUNDDOWN(hex_2_ptr(argv[2]), PGSIZE);
  	for (uintptr_t i = adr_0; i <= adr_1; i += PGSIZE) {
  		pte_t *pte_ptr = pgdir_walk(kern_pgdir, (void *) i, 0);
  		if (pte_ptr) {
  			cprintf("%08x ", i);
  			print_pte(*pte_ptr);
  		}
  	}
  	return 0;
  }
  ```

  It takes 2 arguments `adr_0` and `adr_1` , and prints virtual memory mapping between these two addresses.

  To make `pgdir_walk()` work, I had to include a header file:

  ```c
  #include <kern/pmap.h>
  ```

  Then, register this monitor command. in `monitor.h` :

  ```c
  int mon_showmapping(int argc, char **argv, struct Trapframe *tf);
  ```

  In `monitor.c` :

  ```c
  static struct Command commands[] = {
  	{ "help", "Display this list of commands", mon_help },
  	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
  	{ "showmapping", "Display memory mapping of adr_0 to adr_1", mon_showmapping }
  };
  ```

  Result:

  ```
  K> showmapping 0xf011a000 0xf012a000
  f011a000 0011a000 S RW P
  f011b000 0011b000 S RW P
  f011c000 0011c000 S RW P
  f011d000 0011d000 S RW P
  f011e000 0011e000 S RW P
  f011f000 0011f000 S RW P
  f0120000 00120000 S RW P
  f0121000 00121000 S RW P
  f0122000 00122000 S RW P
  f0123000 00123000 S RW P
  f0124000 00124000 S RW P
  f0125000 00125000 S RW P
  f0126000 00126000 S RW P
  f0127000 00127000 S RW P
  f0128000 00128000 S RW P
  f0129000 00129000 S RW P
  f012a000 0012a000 S RW P
  K> showmapping 0xef000000 0xef400000
  ef000000 00119000 U R- P
  ef001000 0011a000 U R- P
  ef002000 0011b000 U R- P
  ef003000 0011c000 U R- P
  ef004000 0011d000 U R- P
  ef005000 0011e000 U R- P
  ef006000 0011f000 U R- P
  ef007000 00120000 U R- P
  ef008000 00121000 U R- P
  ef009000 00122000 U R- P
  ef00a000 00123000 U R- P
  ef00b000 00124000 U R- P
  ef00c000 00125000 U R- P
  ef00d000 00126000 U R- P
  ef00e000 00127000 U R- P
  ef00f000 00128000 U R- P
  ef010000 00129000 U R- P
  ef011000 0012a000 U R- P
  ef012000 0012b000 U R- P
  ef013000 0012c000 U R- P
  ef014000 0012d000 U R- P
  ef015000 0012e000 U R- P
  ef016000 0012f000 U R- P
  ef017000 00130000 U R- P
  ef018000 00131000 U R- P
  ef019000 00132000 U R- P
  ef01a000 00133000 U R- P
  ef01b000 00134000 U R- P
  ef01c000 00135000 U R- P
  ef01d000 00136000 U R- P
  ef01e000 00137000 U R- P
  ef01f000 00138000 U R- P
  ef020000 00139000 U R- P
  ef021000 0013a000 U R- P
  ef022000 0013b000 U R- P
  ef023000 0013c000 U R- P
  ef024000 0013d000 U R- P
  ef025000 0013e000 U R- P
  ef026000 0013f000 U R- P
  ef027000 00140000 U R- P
  ef028000 00141000 U R- P
  ef029000 00142000 U R- P
  ef02a000 00143000 U R- P
  ef02b000 00144000 U R- P
  ef02c000 00145000 U R- P
  ef02d000 00146000 U R- P
  ef02e000 00147000 U R- P
  ef02f000 00148000 U R- P
  ef030000 00149000 U R- P
  ef031000 0014a000 U R- P
  ef032000 0014b000 U R- P
  ef033000 0014c000 U R- P
  ef034000 0014d000 U R- P
  ef035000 0014e000 U R- P
  ef036000 0014f000 U R- P
  ef037000 00150000 U R- P
  ef038000 00151000 U R- P
  ef039000 00152000 U R- P
  ef03a000 00153000 U R- P
  ef03b000 00154000 U R- P
  ef03c000 00155000 U R- P
  ef03d000 00156000 U R- P
  ef03e000 00157000 U R- P
  ef03f000 00158000 U R- P
  ef040000 00000000 - -- -
  ef041000 00000000 - -- -
  ef042000 00000000 - -- -
  ...
  ```

- *Explicitly set, clear, or change the permissions of any mapping in the current address space.*

  ```c
  int mon_setmapping(int argc, char **argv, struct Trapframe *tf) {
  	assert(argc == 5);
  	uintptr_t addr = ROUNDDOWN(hex_2_ptr(argv[1]), PGSIZE);
  	pte_t pte_u = argv[2][0] == 'U' ? PTE_U : 0,
  		  pte_w = argv[3][1] == 'W' ? PTE_W : 0,
  		  pte_p = argv[4][0] == 'P' ? PTE_P : 0;
  	pte_t *pte_ptr = pgdir_walk(kern_pgdir, (void *) addr, 1);
  	if (pte_ptr) {
  		*pte_ptr = PTE_ADDR(*pte_ptr) | pte_u | pte_w | pte_p;
  		cprintf("%08x ", addr);
  		print_pte(*pte_ptr);
  	} else {
  		panic("no enough memory for the page table");
  	}
  	return 0;
  }
  ```

  Result:

  ```
  K> showmapping 0xef000000 0xef000000
  ef000000 00119000 U R- P
  K> setmapping 0xef000000 S RW P
  ef000000 00119000 S RW P
  K> showmapping 0xef000000 0xef000000
  ef000000 00119000 S RW P
  ```

- *Dump the contents of a range of memory given either a virtual or physical address range. Be sure the dump code behaves correctly when the range extends across page boundaries!*

  Code:

  ```c
  int mon_dumpmemory(int argc, char **argv, struct Trapframe *tf) {
  	assert(argc == 4);
  	uintptr_t vaddr_0, vaddr_1;
  	if (argv[1][0] == 'V') {
  		vaddr_0 = ROUNDDOWN(hex_2_ptr(argv[2]), 4);
  		vaddr_1 = ROUNDDOWN(hex_2_ptr(argv[3]), 4);
  	} else if (argv[1][0] == 'P') {
  		vaddr_0 = (uintptr_t) KADDR(ROUNDDOWN(hex_2_ptr(argv[2]), 4));
  		vaddr_1 = (uintptr_t) KADDR(ROUNDDOWN(hex_2_ptr(argv[3]), 4));
  	} else {
  		panic("the first argument should be V or P");
  	}
  	for (uintptr_t i = vaddr_0; i <= vaddr_1; i += 4) {
  		cprintf("%08x: %08x\n", i, *((uint32_t *) i));
  	}
  	return 0;
  }
  ```

  Result:

  ```
  K> dumpmemory V 0xef000000 0xef000010
  ef000000: 00000000
  ef000004: 00000000
  ef000008: f0158ff8
  ef00000c: 00000000
  ef000010: f0119008
  K> showmapping 0xef000000 0xef000010
  ef000000 00119000 U R- P
  K> dumpmemory P 0x00119000 0x00119010
  f0119000: 00000000
  f0119004: 00000000
  f0119008: f0158ff8
  f011900c: 00000000
  f0119010: f0119008
  ```

**Grading.** This is the output of `make grade`:

```
vagrant@ubuntu-xenial:~/jos$ make grade
make clean
make[1]: Entering directory '/home/vagrant/jos'
rm -rf obj .gdbinit jos.in qemu.log
make[1]: Leaving directory '/home/vagrant/jos'
./grade-lab2
make[1]: Entering directory '/home/vagrant/jos'
+ as kern/entry.S
+ cc kern/entrypgdir.c
+ cc kern/init.c
+ cc kern/console.c
+ cc kern/monitor.c
+ cc kern/pmap.c
+ cc kern/kclock.c
+ cc kern/printf.c
+ cc kern/kdebug.c
+ cc lib/printfmt.c
+ cc lib/readline.c
+ cc lib/string.c
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ as boot/boot.S
+ cc -Os boot/main.c
+ ld boot/boot
boot block is 390 bytes (max 510)
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/vagrant/jos'
running JOS: (0.9s)
  Physical page allocator: OK
  Page management: OK
  Kernel page directory: OK
  Page management 2: OK
Score: 100% (70/70)
```

