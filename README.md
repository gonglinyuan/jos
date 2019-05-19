# Lab 5

龚林源 1600012714

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Exercise 1.** *`i386_init` identifies the file system environment by passing the type `ENV_TYPE_FS` to your environment creation function, `env_create`. Modify `env_create` in `env.c`, so that it gives the file system environment I/O privilege, but never gives that privilege to any other environment.*



**Grading.** This is the output of `make grade`:

```
vagrant@ubuntu-xenial:~/jos$ make grade
make clean
make[1]: Entering directory '/home/vagrant/jos'
rm -rf obj .gdbinit jos.in qemu.log
make[1]: Leaving directory '/home/vagrant/jos'
./grade-lab4
make[1]: Entering directory '/home/vagrant/jos'
+ as kern/entry.S
+ cc kern/entrypgdir.c
+ cc kern/init.c
+ cc kern/console.c
+ cc kern/monitor.c
+ cc kern/pmap.c
+ cc kern/env.c
+ cc kern/kclock.c
+ cc kern/picirq.c
+ cc kern/printf.c
+ cc kern/trap.c
+ as kern/trapentry.S
+ cc kern/sched.c
+ cc kern/syscall.c
+ cc kern/kdebug.c
+ cc lib/printfmt.c
+ cc lib/readline.c
+ cc lib/string.c
+ as kern/mpentry.S
+ cc kern/mpconfig.c
+ cc kern/lapic.c
+ cc kern/spinlock.c
+ cc[USER] lib/console.c
+ cc[USER] lib/libmain.c
+ cc[USER] lib/exit.c
+ cc[USER] lib/panic.c
+ cc[USER] lib/printf.c
+ cc[USER] lib/printfmt.c
+ cc[USER] lib/readline.c
+ cc[USER] lib/string.c
+ cc[USER] lib/syscall.c
+ cc[USER] lib/pgfault.c
+ as[USER] lib/pfentry.S
+ cc[USER] lib/fork.c
+ cc[USER] lib/ipc.c
+ ar obj/lib/libjos.a
ar: creating obj/lib/libjos.a
+ cc[USER] user/hello.c
+ as[USER] lib/entry.S
+ ld obj/user/hello
+ cc[USER] user/buggyhello.c
+ ld obj/user/buggyhello
+ cc[USER] user/buggyhello2.c
+ ld obj/user/buggyhello2
+ cc[USER] user/evilhello.c
+ ld obj/user/evilhello
+ cc[USER] user/testbss.c
+ ld obj/user/testbss
+ cc[USER] user/divzero.c
+ ld obj/user/divzero
+ cc[USER] user/breakpoint.c
+ ld obj/user/breakpoint
+ cc[USER] user/softint.c
+ ld obj/user/softint
+ cc[USER] user/badsegment.c
+ ld obj/user/badsegment
+ cc[USER] user/faultread.c
+ ld obj/user/faultread
+ cc[USER] user/faultreadkernel.c
+ ld obj/user/faultreadkernel
+ cc[USER] user/faultwrite.c
+ ld obj/user/faultwrite
+ cc[USER] user/faultwritekernel.c
+ ld obj/user/faultwritekernel
+ cc[USER] user/idle.c
+ ld obj/user/idle
+ cc[USER] user/yield.c
+ ld obj/user/yield
+ cc[USER] user/dumbfork.c
+ ld obj/user/dumbfork
+ cc[USER] user/stresssched.c
+ ld obj/user/stresssched
+ cc[USER] user/faultdie.c
+ ld obj/user/faultdie
+ cc[USER] user/faultregs.c
+ ld obj/user/faultregs
+ cc[USER] user/faultalloc.c
+ ld obj/user/faultalloc
+ cc[USER] user/faultallocbad.c
+ ld obj/user/faultallocbad
+ cc[USER] user/faultnostack.c
+ ld obj/user/faultnostack
+ cc[USER] user/faultbadhandler.c
+ ld obj/user/faultbadhandler
+ cc[USER] user/faultevilhandler.c
+ ld obj/user/faultevilhandler
+ cc[USER] user/forktree.c
+ ld obj/user/forktree
+ cc[USER] user/sendpage.c
+ ld obj/user/sendpage
+ cc[USER] user/spin.c
+ ld obj/user/spin
+ cc[USER] user/fairness.c
+ ld obj/user/fairness
+ cc[USER] user/pingpong.c
+ ld obj/user/pingpong
+ cc[USER] user/pingpongs.c
+ ld obj/user/pingpongs
+ cc[USER] user/primes.c
+ ld obj/user/primes
+ ld obj/kern/kernel
+ as boot/boot.S
+ cc -Os boot/main.c
+ ld boot/boot
boot block is 414 bytes (max 510)
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/vagrant/jos'
dumbfork: OK (1.5s)
Part A score: 5/5

faultread: OK (1.4s)
faultwrite: OK (1.3s)
faultdie: OK (1.4s)
faultregs: OK (1.4s)
faultalloc: OK (2.2s)
faultallocbad: OK (1.3s)
faultnostack: OK (2.1s)
faultbadhandler: OK (2.3s)
faultevilhandler: OK (2.3s)
forktree: OK (2.4s)
Part B score: 50/50

spin: OK (2.2s)
stresssched: OK (2.4s)
sendpage: OK (2.2s)
pingpong: OK (2.4s)
primes: OK (6.5s)
Part C score: 25/25

Score: 100% (80/80)
```

