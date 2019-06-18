# Extra Lab

龚林源 1600012714

**Introduction.**

In this extra lab, I implement a "[Log-Structured File System](<https://people.eecs.berkeley.edu/~brewer/cs262/LFS.pdf>) (**LFS**)" for JOS, which is firstly developed by Mendel Rosenblum and John K. Ousterhout. Log-structured file system focus on write performance, and try to make use of the sequential bandwidth of the disk. Moreover, it would perform well on common workloads that not only write out data but also update on-disk metadata structures frequently.

When writing to disk, LFS first *buffers* all updates (including *both data and metadata*) in an in memory **segment**; when the segment is full, it is written to disk in one long, *sequential* transfer to an *unused* part of the disk: Because segments are large, the disk is used efficiently, and performance of the file system approaches its zenith.

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

