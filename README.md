# Lab 1

龚林源 1600012714

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Exercise 1.** *Familiarize yourself with the assembly language materials available on the 6.828 reference page. You don't have to read them now, but you'll almost certainly want to refer to some of this material when reading and writing x86 assembly.*
*We do recommend reading the section "The Syntax" in Brennan's Guide to Inline Assembly. It gives a good (and quite brief) description of the AT&T assembly syntax we'll be using with the GNU assembler in JOS.*

**Exercise 2.** *Use GDB's si (Step Instruction) command to trace into the ROM BIOS for a few more instructions, and try to guess what it might be doing. You might want to look at Phil Storrs I/O Ports Description, as well as other materials on the 6.828 reference materials page. No need to figure out all the details - just the general idea of what the BIOS is doing first.*

```assembly
[f000:fff0]    0xffff0: ljmp   $0xf000,$0xe05b
# Jump to a lower address to continue execution (because addresses after 0x100000 is not in BIOS ROM)
[f000:e05b]    0xfe05b: cmpl   $0x0,%cs:0x6ac8
[f000:e062]    0xfe062: jne    0xfd2e1
[f000:e066]    0xfe066: xor    %dx,%dx
[f000:e068]    0xfe068: mov    %dx,%ss
# Set up the stack-segment register to 0
[f000:e06a]    0xfe06a: mov    $0x7000,%esp
# Set up the stack pointer to [0x0000:0x7000] = 0x7c00
[f000:e070]    0xfe070: mov    $0xf34c2,%edx
[f000:e076]    0xfe076: jmp    0xfd15c
[f000:d15c]    0xfd15c: mov    %eax,%ecx
[f000:d15f]    0xfd15f: cli
# Disable hardware interrupts
[f000:d160]    0xfd160: cld
# Clear the DF flag (for string operations) in EFLAGS
[f000:d161]    0xfd161: mov    $0x8f,%eax
[f000:d167]    0xfd167: out    %al,$0x70
# Disable NMI (Non-Maskable Interrupts) and select the 15th CMOS register
[f000:d169]    0xfd169: in     $0x71,%al
# Read this CMOS register (Shutdown Status Byte) into %al
[f000:d16b]    0xfd16b: in     $0x92,%al
[f000:d16d]    0xfd16d: or     $0x2,%al
[f000:d16f]    0xfd16f: out    %al,$0x92
# Try to use FAST A20 gate to quickly enable A20 line
[f000:d171]    0xfd171: lidtw  %cs:0x6ab8
[f000:d177]    0xfd177: lgdtw  %cs:0x6a74
# Set up IDT and GDT
[f000:d17d]    0xfd17d: mov    %cr0,%eax
[f000:d180]    0xfd180: or     $0x1,%eax
[f000:d184]    0xfd184: mov    %eax,%cr0
# Switch from real mode to protected mode
[f000:d187]    0xfd187: ljmpl  $0x8,$0xfd18f
# Long jump to 32-bit code segment
=> 0xfd18f:     mov    $0x10,%eax
=> 0xfd194:     mov    %eax,%ds
=> 0xfd196:     mov    %eax,%es
=> 0xfd198:     mov    %eax,%ss
=> 0xfd19a:     mov    %eax,%fs
=> 0xfd19c:     mov    %eax,%gs
# Set up segment registers
...
```

Then, BIOS ROM:

1. Initializes system devices such as VGA
2. Outputs to VGA port and print "SeaBIOS (Version ...)" to the screen
3. Loads the first sector of the disk into physical address `0x7c00`, checks that the last 2 bytes are  `0x55 0xAA`
4. Jumps to the boot loader (0x7c00)

**Exercise 3.** *Take a look at the lab tools guide, especially the section on GDB commands. Even if you're familiar with GDB, this includes some esoteric GDB commands that are useful for OS work.*

*Set a breakpoint at address 0x7c00, which is where the boot sector will be loaded. Continue execution until that breakpoint. Trace through the code in boot/boot.S, using the source code and the disassembly file obj/boot/boot.asm to keep track of where you are. Also use the x/i command in GDB to disassemble sequences of instructions in the boot loader, and compare the original boot loader source code with both the disassembly in obj/boot/boot.asm and GDB.*

*Trace into bootmain() in boot/main.c, and then into readsect(). Identify the exact assembly instructions that correspond to each of the statements in readsect(). Trace through the rest of readsect() and back out into bootmain(), and identify the begin and end of the for loop that reads the remaining sectors of the kernel from the disk. Find out what code will run when the loop is finished, set a breakpoint there, and continue to that breakpoint. Then step through the remainder of the boot loader.*

Here, the processor start executing 32-bit mode:

```asm
7c2d:   ljmp    $PROT_MODE_CSEG, $protcseg
```

Readsect:

```c
waitdisk();
    call   7c6a <waitdisk>
outb(0x1F2, 1);
    mov    $0x1f2,%edx
    mov    $0x1,%al
    out    %al,(%dx)
outb(0x1F3, offset);
    mov    $0x1f3,%edx
    mov    %cl,%al
    out    %al,(%dx)
outb(0x1F4, offset >> 8);
    mov    %ecx,%eax
    mov    $0x1f4,%edx
    shr    $0x8,%eax
    out    %al,(%dx)
outb(0x1F5, offset >> 16);
    mov    %ecx,%eax
    mov    $0x1f5,%edx
    shr    $0x10,%eax
    out    %al,(%dx)
outb(0x1F6, (offset >> 24) | 0xE0);
    mov    %ecx,%eax
    mov    $0x1f6,%edx
    shr    $0x18,%eax
    or     $0xffffffe0,%eax
    out    %al,(%dx)
outb(0x1F7, 0x20);
    mov    $0x1f7,%edx
    mov    $0x20,%al
    out    %al,(%dx)
waitdisk();
	call   7c6a <waitdisk>
insl(0x1F0, dst, SECTSIZE/4);
    mov    0x8(%ebp),%edi
    mov    $0x80,%ecx
    mov    $0x1f0,%edx
    cld    
    repnz insl (%dx),%es:(%edi)
```

Firstly, bootmain() read the length of the kernel in the ELF header to determine how many sectors it should read: 

```assembly
7d3f:   movzwl 0x1002c,%esi
7d46:   lea    0x10000(%eax),%ebx
7d4c:   shl    $0x5,%esi
7d4f:   add    %ebx,%esi
```

The loop body of reading sectors is: 

```c
7d51:   cmp    %esi,%ebx
7d53:   jae    7d6b <bootmain+0x56>
7d55:   pushl  0x4(%ebx)
7d58:   pushl  0x14(%ebx)
7d5b:   add    $0x20,%ebx
7d5e:   pushl  -0x14(%ebx)
7d61:   call   7cdc <readseg>
7d66:   add    $0xc,%esp
7d69:   jmp    7d51 <bootmain+0x3c>
```

After the loop body, the code will call the entry point from the ELF header: 

```assembly
call   *0x10018
```

This is the last instruction executed by the bootloader. The first instruction executed by the kernel is:

```assembly
f010000c:   movw   $0x1234,0x472
```

**Exercise 4.** *Read about programming with pointers in C. The best reference for the C language is The C Programming Language by Brian Kernighan and Dennis Ritchie (known as 'K&R'). We recommend that students purchase this book (here is an Amazon Link) or find one of MIT's 7 copies.*

*Read 5.1 (Pointers and Addresses) through 5.5 (Character Pointers and Functions) in K&R. Then download the code for pointers.c, run it, and make sure you understand where all of the printed values come from. In particular, make sure you understand where the pointer addresses in printed lines 1 and 6 come from, how all the values in printed lines 2 through 4 get there, and why the values printed in line 5 are seemingly corrupted.*

*There are other references on pointers in C (e.g., A tutorial by Ted Jensen that cites K&R heavily), though not as strongly recommended.*

**Exercise 5.** *Trace through the first few instructions of the boot loader again and identify the first instruction that would "break" or otherwise do the wrong thing if you were to get the boot loader's link address wrong. Then change the link address in boot/Makefrag to something wrong, run make clean, recompile the lab with make, and trace into the boot loader again to see what happens. Don't forget to change the link address back and make clean again afterward!*

I changed the link address to 0x7c01, and the program stuck at:

```asm
7c30:  ljmp   $0x8,$0x7c36
```

**Exercise 6.** *We can examine memory using GDB's x command. The GDB manual has full details, but for now, it is enough to know that the command x/Nx ADDR prints N words of memory at ADDR. (Note that both 'x's in the command are lowercase.) Warning: The size of a word is not a universal standard. In GNU assembly, a word is two bytes (the 'w' in xorw, which stands for word, means 2 bytes).*

*Reset the machine (exit QEMU/GDB and start them again). Examine the 8 words of memory at 0x00100000 at the point the BIOS enters the boot loader, and then again at the point the boot loader enters the kernel. Why are they different? What is there at the second breakpoint? (You do not really need to use QEMU to answer this question. Just think.)*

At the beginning of the bootloader, the output of GDB is:

```
0x100000:       0x00000000      0x00000000      0x00000000      0x00000000
0x100010:       0x00000000      0x00000000      0x00000000      0x00000000
```

At the beginning of the kernel, the output of GDB is:

```
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x0000b812      0x220f0011      0xc0200fd8
```

Because the ELF binary executable of the kernel is loaded into 0x100000. 

**Exercise 7.** *Use QEMU and GDB to trace into the JOS kernel and stop at the movl %eax, %cr0. Examine memory at 0x00100000 and at 0xf0100000. Now, single step over that instruction using the stepi GDB command. Again, examine memory at 0x00100000 and at 0xf0100000. Make sure you understand what just happened.*

*What is the first instruction after the new mapping is established that would fail to work properly if the mapping weren't in place? Comment out the movl %eax, %cr0 in kern/entry.S, trace into it, and see if you were right.*

Before executing this instruction:

```
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x0000b812      0x220f0011      0xc0200fd8
0xf0100000 <_start+4026531828>: 0x00000000      0x00000000      0x00000000      0x00000000
0xf0100010 <entry+4>:   0x00000000      0x00000000      0x00000000      0x00000000
```

After executing this instruction:

```
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x0000b812      0x220f0011      0xc0200fd8
0xf0100000 <_start+4026531828>: 0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0xf0100010 <entry+4>:   0x34000004      0x0000b812      0x220f0011      0xc0200fd8
```

If this instruction is commented out, the first instruction that will fail is: 

```assembly
jmp	*%eax
```

Because $relocated is assigned to EAX. The virtual memory is not mapped, so EAX points to zeros. The error message is:

```
qemu: fatal: Trying to execute code outside RAM or ROM at 0xf010002c
```

**Exercise 8.** *We have omitted a small fragment of code - the code necessary to print octal numbers using patterns of the form "%o". Find and fill in this code fragment.*

```c
num = getuint(&ap, lflag);
base = 8;
goto number;
```

1. *Explain the interface between printf.c and console.c. Specifically, what function does console.c export? How is this function used by printf.c?*

   console.c exports cputchar() function.

   printf.c use cputchar() in putch() function.

   printf.c pass the function pointer of putch() to printfmt.c and the latter file will call this function by this pointer.

2. *Explain the following from console.c:*

   ```c
   // if putting next char will exceed the width of the screen
   if (crt_pos >= CRT_SIZE) {
       int i;
       // move output one line upward
       memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
       // clear the next line
       for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
           crt_buf[i] = 0x0700 | ' ';
       // reset the current column number
       crt_pos -= CRT_COLS;
   }
   ```

3. *In the call to cprintf(), to what does fmt point? To what does ap point?*

   fmt points to the address where the format string starts.

   ap points to where the function stack starts and hold the rest of the arguments.

4. *List (in order of execution) each call to cons_putc, va_arg, and vcprintf. For cons_putc, list its argument as well. For va_arg, list what ap points to before and after the call. For vcprintf list the values of its two arguments.*

   ```
   vcprintf(fmt=0xf0101b6e, ap=0xf010ff54)
       >>> x/s 0xf0101b6e:    "x %d, y %x, z %d\n"
       >>> x/3dw 0xf010ff54:  1       3       4
   
       cons_putc(c=120)
           >>> p/c c: 120 'x'
   
       cons_putc(c=32)
           >>> p/c c: 32 ' '
   
       getint(ap=0xf010ff54, 0)
           >>> p *0xf010ff54: 1
           va_arg(*ap=1, int)
               >>> p ap: 0xf010ff58
               >>> p *0xf010ff58: 3
   
       cons_putc(c=49)
           >>> p/c c: 49 '1'
   
       cons_putc(c=44)
           >>> p/c c: 44 ','
   
       cons_putc(c=32)
           >>> p/c c: 32 ' '
   
       cons_putc(c=121)
           >>> p/c c: 121 'y'
   
       cons_putc(c=32)
           >>> p/c c: 32 ' '
   
       getuint(ap=0xf010ff58, 0)
           >>> p *0xf010ff58: 3
           va_arg(*ap=3, unsigned int)
               >>> p ap: 0xf010ff5c
               >>> p *0xf010ff5c: 4
   
       cons_putc(c=51)
           >>> p/c c: 41 '3'
   
       cons_putc(c=44)
           >>> p/c c: 44 ','
   
       cons_putc(c=32)
           >>> p/c c: 32 ' '
   
       cons_putc(c=122)
           >>> p/c c: 122 'z'
   
       cons_putc(c=32)
           >>> p/c c: 32 ' '
   
       getint(ap=0xf010ff5c, 0)
           >>> p *0xf010ff5c: 4
           va_arg(*ap=4, int)
               >>> p ap: 0xf010ff60
               >>> p *0xf010ff60: 4027645792
   
       cons_putc(c=52)
           >>> p/c c: 52 '4'
   
       cons_putc(c=10)
           >>> p/c c: 10 '\n'
   ```

5. *What is the output? Explain how this output is arrived at in the step-by-step manner of the previous exercise. Here's an ASCII table that maps bytes to characters.*

   The output is He110 World.

   e110 is hex representation of decimal 57616. 0x00646c72 is hex representation of \0dlr

6. *The output depends on that fact that the x86 is little-endian. If the x86 were instead big-endian what would you set i to in order to yield the same output? Would you need to change 57616 to a different value?*

   On a big-endian machine value of i would be 0x726c6400, but the 57616 would not change, because it's hex representation is the same

7. *In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen?*

   Output is x=3, y=-1. At first ap points to 3, then for the next value it points to (&ap) + sizeof(int), which can be undefined, but in this case is value -1. 

8. *Let's say that GCC changed its calling convention so that it pushed arguments on the stack in declaration order, so that the last argument is pushed last. How would you have to change cprintf or its interface so that it would still be possible to pass it a variable number of arguments?*

   Push an integer after the last argument indicating the number of arguments.

**Exercise 9.** *Determine where the kernel initializes its stack, and exactly where in memory its stack is located. How does the kernel reserve space for its stack? And at which "end" of this reserved area is the stack pointer initialized to point to?*

The kernel initializes its stack at:

```assembly
f0100034:   mov    $0xf0110000,%esp
```

In memory, the stack is located at 0x110000.

The memory for the stack is 0x000000 to 0x110000.

**Exercise 10.** *To become familiar with the C calling conventions on the x86, find the address of the test_backtrace function in obj/kern/kernel.asm, set a breakpoint there, and examine what happens each time it gets called after the kernel starts. How many 32-bit words does each recursive nesting level of test_backtrace push on the stack, and what are those words?*

When first calling test_backtrace, ESP = 0xf010ffdc. The next time, ESP = 0xf010ffbc. And the next time after the next time, ESP = 0xf010ff9c. Therefore, each time 2*16/4=8 32-bit words are push on the stack.

**Exercise 11.** *Implement the backtrace function as specified above. Use the same format as in the example, since otherwise the grading script will be confused. When you think you have it working right, run make grade to see if its output conforms to what our grading script expects, and fix it if it doesn't. After you have handed in your Lab 1 code, you are welcome to change the output format of the backtrace function any way you like.*

```c
cprintf("Stack backtrace:\n");
uint32_t ebp = read_ebp();
while (ebp) {
    cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
        ebp,
        *(uint32_t *)(ebp + 4),
        *(uint32_t *)(ebp + 8),
        *(uint32_t *)(ebp + 12),
        *(uint32_t *)(ebp + 16),
        *(uint32_t *)(ebp + 20),
        *(uint32_t *)(ebp + 24)
    );
    ebp = *(uint32_t *)ebp;
}
```

**Exercise 12**. *Modify your stack backtrace function to display, for each eip, the function name, source file name, and line number corresponding to that eip.*

In kdebug.c:

```c
stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
if (lline == 0) {
    return -1;
}
info->eip_line = stabs[lline].n_desc;
```

Note that the line number is not the n_value field, but the n_desc field.

In monitor.c:

```c
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
```

The output is:

```
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
Stack backtrace:
  ebp f010ff18  eip f010007b  args 00000000 00000000 00000000 00000000 f0100949
         kern/init.c:18: test_backtrace+59
  ebp f010ff38  eip f0100068  args 00000000 00000001 f010ff78 00000000 f0100949
         kern/init.c:16: test_backtrace+40
  ebp f010ff58  eip f0100068  args 00000001 00000002 f010ff98 00000000 f0100949
         kern/init.c:16: test_backtrace+40
  ebp f010ff78  eip f0100068  args 00000002 00000003 f010ffb8 00000000 f0100949
         kern/init.c:16: test_backtrace+40
  ebp f010ff98  eip f0100068  args 00000003 00000004 00000000 00000000 00000000
         kern/init.c:16: test_backtrace+40
  ebp f010ffb8  eip f0100068  args 00000004 00000005 00000000 00010094 00010094
         kern/init.c:16: test_backtrace+40
  ebp f010ffd8  eip f01000d4  args 00000005 00001aac 00000640 00000000 00000000
         kern/init.c:39: i386_init+64
  ebp f010fff8  eip f010003e  args 00111021 00000000 00000000 00000000 00000000
         kern/entry.S:83: <unknown>+0
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
```

**Challenge**. *Enhance the console to allow text to be printed in different colors.*

1. Add a global variable in console.c

   ```c
   int cga_text_color = 0x700;
   ```

2. Use this color in cga_putc() function in console.c

   ```c
   if (!(c & ~0xFF))
       c |= cga_text_color;
   ```

3. Reference to cga_text_color variable in printfmt.c

   ```c
   extern int cga_text_color;
   ```

4. Add a branch to process color. Here we use %m as the symbol for colors

   ```c
   // color
   case 'm':
       num = getint(&ap, lflag);
       cga_text_color = num;
       break;
   ```

5. When encountered the end of a string, reset the color to white: 

   ```c
   if (ch == '\0') {
       cga_text_color = 0x700;
       return;
   }
   ```

6. Print one line to test color functionalities in monitor.c 

   ```c
   cprintf("Test colors: %mRed %mGreen %mBlue\n", 0x100, 0x200, 0x400);
   ```


The result is:

![1551756043842](C:\Users\gonglinyuan\AppData\Roaming\Typora\typora-user-images\1551756043842.png)

**Grading.** This is the output of `make grade`:

```
vagrant@ubuntu-xenial:~/jos$ make grade
make clean
make[1]: Entering directory '/home/vagrant/jos'
rm -rf obj .gdbinit jos.in qemu.log
make[1]: Leaving directory '/home/vagrant/jos'
./grade-lab1
make[1]: Entering directory '/home/vagrant/jos'
+ as kern/entry.S
+ cc kern/entrypgdir.c
+ cc kern/init.c
+ cc kern/console.c
+ cc kern/monitor.c
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
running JOS: (0.5s)
  printf: OK
  backtrace count: OK
  backtrace arguments: OK
  backtrace symbols: OK
  backtrace lines: OK
Score: 100% (50/50)
```

