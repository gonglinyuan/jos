// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

// Pointer to currently installed C-language pgfault handler.
void (*_pgfault_handler)(struct UTrapframe *utf);

//
// Set the page fault handler function.
// If there isn't one yet, _pgfault_handler will be 0.
// The first time we register a handler, we need to
// allocate an exception stack (one page of memory with its top
// at UXSTACKTOP), and tell the kernel to call the assembly-language
// _pgfault_upcall routine when a page fault occurs.
//
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	int env_id = sys_getenvid();
	// cprintf("envid = %d\n", env_id);
	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		r = sys_page_alloc(env_id, (void *) (UXSTACKTOP - PGSIZE), PTE_W | PTE_U);
		if (r < 0) {
			// sys_env_destroy(env_id);
			panic("fail 1");
		}
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
	r = sys_env_set_pgfault_upcall(env_id, handler);
	if (r < 0) {
		// sys_env_destroy(env_id);
		panic("fail 2");
	}
	// cprintf("ddd %u\n", (uintptr_t) _pgfault_handler);
}
