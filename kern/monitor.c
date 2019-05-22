// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>
#include <kern/kclock.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmapping", "Display memory mapping of adr_0 to adr_1", mon_showmapping },
	{ "setmapping", "Set the bits on the PTE", mon_setmapping },
	{ "dumpmemory", "Display a range of physical or virtual memory", mon_dumpmemory },
	{ "continue", "Continue running", mon_continue },
	{ "stepi", "step to the next instruction", mon_stepi },
	{ "showtime", "", mon_showtime }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");
	uint32_t ebp = read_ebp();
	while (ebp) {
		uint32_t eip = *(uint32_t *)(ebp + 4);
		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
			ebp,
			eip,
			*(uint32_t *)(ebp + 8),
			*(uint32_t *)(ebp + 12),
			*(uint32_t *)(ebp + 16),
			*(uint32_t *)(ebp + 20),
			*(uint32_t *)(ebp + 24)
		);
		cprintf("         %s:%d: %.*s+%d\n",
			info.eip_file,
			info.eip_line,
			info.eip_fn_namelen,
			info.eip_fn_name,
			eip - info.eip_fn_addr
		);
		ebp = *(uint32_t *)ebp;
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}


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

static int
nvram_read(int r)
{
	return mc146818_read(r) | (mc146818_read(r + 1) << 8);
}

int mon_showtime(int argc, char **argv, struct Trapframe *tf) {
	cprintf("hello\n");
	int year = mc146818_read(9);
	int month = mc146818_read(8);
	int day = mc146818_read(7);
	int hour = mc146818_read(4);
	int min = mc146818_read(2);
	int sec = mc146818_read(0);
	if (0 >= (int) (month -= 2)) {
		month += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	min -= 60;
	hour += 1;
	cprintf("%d %d %d %d %d %d\n", year, month, day, hour, min, sec);
	return 0;
}
