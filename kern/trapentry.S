/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
	TRAPHANDLER_NOEC(handle_divide, T_DIVIDE)
	TRAPHANDLER_NOEC(handle_debug, T_DEBUG)
	TRAPHANDLER_NOEC(handle_nmi, T_NMI)
	TRAPHANDLER_NOEC(handle_brkpt, T_BRKPT)
	TRAPHANDLER_NOEC(handle_oflow, T_OFLOW)
	TRAPHANDLER_NOEC(handle_bound, T_BOUND)
	TRAPHANDLER_NOEC(handle_illop, T_ILLOP)
	TRAPHANDLER_NOEC(handle_device, T_DEVICE)
	TRAPHANDLER(handle_dblflt, T_DBLFLT)
	TRAPHANDLER_NOEC(handle_coproc, 9)
	TRAPHANDLER(handle_tss, T_TSS)
	TRAPHANDLER(handle_segnp, T_SEGNP)
	TRAPHANDLER(handle_stack, T_STACK)
	TRAPHANDLER(handle_gpflt, T_GPFLT)
	TRAPHANDLER(handle_pgflt, T_PGFLT)
	TRAPHANDLER(handle_res, 15)
	TRAPHANDLER_NOEC(handle_fperr, T_FPERR)
	TRAPHANDLER_NOEC(handle_align, T_ALIGN)
	TRAPHANDLER_NOEC(handle_mchk, T_MCHK)
	TRAPHANDLER_NOEC(handle_simderr, T_SIMDERR)
	TRAPHANDLER_NOEC(thdlr20, 20)
	TRAPHANDLER_NOEC(thdlr21, 21)
	TRAPHANDLER_NOEC(thdlr22, 22)
	TRAPHANDLER_NOEC(thdlr23, 23)
	TRAPHANDLER_NOEC(thdlr24, 24)
	TRAPHANDLER_NOEC(thdlr25, 25)
	TRAPHANDLER_NOEC(thdlr26, 26)
	TRAPHANDLER_NOEC(thdlr27, 27)
	TRAPHANDLER_NOEC(thdlr28, 28)
	TRAPHANDLER_NOEC(thdlr29, 29)
	TRAPHANDLER_NOEC(thdlr30, 30)
	TRAPHANDLER_NOEC(thdlr31, 31)
	TRAPHANDLER_NOEC(thdlr32, 32)
	TRAPHANDLER_NOEC(thdlr33, 33)
	TRAPHANDLER_NOEC(thdlr34, 34)
	TRAPHANDLER_NOEC(thdlr35, 35)
	TRAPHANDLER_NOEC(thdlr36, 36)
	TRAPHANDLER_NOEC(thdlr37, 37)
	TRAPHANDLER_NOEC(thdlr38, 38)
	TRAPHANDLER_NOEC(thdlr39, 39)
	TRAPHANDLER_NOEC(thdlr40, 40)
	TRAPHANDLER_NOEC(thdlr41, 41)
	TRAPHANDLER_NOEC(thdlr42, 42)
	TRAPHANDLER_NOEC(thdlr43, 43)
	TRAPHANDLER_NOEC(thdlr44, 44)
	TRAPHANDLER_NOEC(thdlr45, 45)
	TRAPHANDLER_NOEC(thdlr46, 46)
	TRAPHANDLER_NOEC(thdlr47, 47)
	
	TRAPHANDLER_NOEC(handle_syscall, T_SYSCALL)


/*
 * Lab 3: Your code here for _alltraps
 */

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
	.long thdlr20
    .long thdlr21
    .long thdlr22
    .long thdlr23
    .long thdlr24
    .long thdlr25
    .long thdlr26
    .long thdlr27
    .long thdlr28
    .long thdlr29
    .long thdlr30
    .long thdlr31
    .long thdlr32
    .long thdlr33
    .long thdlr34
    .long thdlr35
    .long thdlr36
    .long thdlr37
    .long thdlr38
    .long thdlr39
    .long thdlr40
    .long thdlr41
    .long thdlr42
    .long thdlr43
    .long thdlr44
    .long thdlr45
    .long thdlr46
    .long thdlr47
