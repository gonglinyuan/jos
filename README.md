# Lab 6

龚林源 1600012714

**Environment.**

- **CPU:** Intel(R) Core(TM) i7-6700HQ CPU @ 2.60GHz
- **Vendor:** VirtualBox
- **Platform:** i686 (32-bit)
- **OS:** Ubuntu 16.04.5 LTS
- **OS Kernel:** Linux ubuntu-xenial 4.4.0-141-generic
- **C Compiler:** gcc version 5.4.0 20160609 (Ubuntu 5.4.0-6ubuntu1~16.04.10)
- **QEMU:** https://github.com/mit-pdos/6.828-qemu.git

**Exercise 1.** *Add a call to `time_tick` for every clock interrupt in `kern/trap.c`. Implement `sys_time_msec` and add it to `syscall` in `kern/syscall.c` so that user space has access to the time.*

In `kern/trap.c`, I modified how the kernel handles `IRQ_OFFSET + IRQ_TIMER`:

```c
lapic_eoi();

// Add time tick increment to clock interrupts.
// Be careful! In multiprocessors, clock interrupts are
// triggered on every CPU.
// LAB 6: Your code here.
if (cpunum() == 0) {
    time_tick();
}

sched_yield();
return;
```

In `kern/syscall.c`, I implemented `sys_time_msec()`:

```c
// LAB 6: Your code here.
return time_msec();
```

I also modified `syscall()` to dispatch the system call:

```c
case SYS_time_msec:
	return sys_time_msec();
```

**Exercise 2.** *Browse Intel's [Software Developer's Manual](https://pdos.csail.mit.edu/6.828/2018/readings/hardware/8254x_GBe_SDM.pdf) for the E1000. This manual covers several closely related Ethernet controllers. QEMU emulates the 82540EM.*

**Exercise 3.** *Implement an attach function to initialize the E1000. Add an entry to the `pci_attach_vendor` array in `kern/pci.c` to trigger your function if a matching PCI device is found (be sure to put it before the `{0, 0, 0}` entry that mark the end of the table). You can find the vendor ID and device ID of the 82540EM that QEMU emulates in section 5.2. You should also see these listed when JOS scans the PCI bus while booting.*

First, I added some error codes for the E1000 driver; in `inc/error.h`, I added:

```c
// E1000 error codes
E_NO_TX_DESC,   // No free transmit descriptors
E_TX_TOO_LONG,  // Packet too long
E_RX_NO_PKT,    // No more packet to read
```

To handle some corner cases, I added a new status for environments (processes).

In `inc/env.h`, I added a entry to the `enum`:

```c
ENV_SLEEPING
```

In `kern/sched.c`, I modified the conditions of `sched_halt()`:

```c
for (i = 0; i < NENV; i++) {
    if ((envs[i].env_status == ENV_RUNNABLE ||
         envs[i].env_status == ENV_RUNNING ||
         envs[i].env_status == ENV_DYING ||
         envs[i].env_status == ENV_SLEEPING))
        break;
```

Then, I implemented the e1000 driver in `kern/e1000.h` and `kern/e1000.c`:

```c
#include <kern/pci.h>

struct tx_desc
{
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
};

struct rx_desc
{
	uint64_t addr;
	uint16_t length;
	uint16_t checksum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
};

int e1000_func_enable(struct pci_func *f);
```

```c
#include <kern/e1000.h>
#include <kern/pmap.h>
#include <kern/picirq.h>
#include <kern/sched.h>
#include <kern/env.h>
#include <inc/error.h>
#include <inc/string.h>

/* Configuration constants */
#define E1000_CONF_TX_TDNUM 32
#define E1000_CONF_TX_TDLEN (E1000_CONF_TX_TDNUM * 16)
#define E1000_CONF_TX_BUFSIZE 2048
#define E1000_CONF_TX_BUF_PER_PAGE (PGSIZE / E1000_CONF_TX_BUFSIZE)
#define E1000_CONF_RX_RDNUM 256
#define E1000_CONF_RX_RDLEN (E1000_CONF_RX_RDNUM * 16)
#define E1000_CONF_RX_BUFSIZE 2048
#define E1000_CONF_RX_BUF_PER_PAGE (PGSIZE / E1000_CONF_RX_BUFSIZE)
#define E1000_CONF_RX_RAL_VAL 0x12005452
#define E1000_CONF_RX_RAH_VAL 0x00005634


/* E1000 memory mapped offset */
#define E1000_MM_TDBAL          0x03800    /* TX Descriptor Base Address Low - RW */
#define E1000_MM_TDBAH          0x03804    /* TX Descriptor Base Address High - RW */
#define E1000_MM_TDLEN          0x03808    /* TX Descriptor Length - RW */
#define E1000_MM_TDH            0x03810    /* TX Descriptor Head - RW */
#define E1000_MM_TDT            0x03818    /* TX Descripotr Tail - RW */
#define E1000_MM_TCTL           0x00400    /* TX Control - RW */
#define E1000_MM_TCTL_EN        0x00000002 /* enable tx */
#define E1000_MM_TCTL_PSP       0x00000008 /* pad short packets */
#define E1000_MM_TCTL_CT        0x00000ff0 /* collision threshold */
#define E1000_MM_TCTL_COLD      0x003ff000 /* collision distance */
#define E1000_MM_TIPG           0x00410    /* TX Inter-packet gap -RW */
#define E1000_MM_TIPG_IPGT      0x000003ff 
#define E1000_MM_TIPG_IPGR1     0x000ffc00
#define E1000_MM_TIPG_IPGR2     0x3ff00000
#define E1000_MM_TIPG_RES       0xc0000000
#define E1000_MM_TXD_CMD_RS     (0x08000000 >> 24) /* Report Status */
#define E1000_MM_TXD_STAT_DD    0x00000001 /* Descriptor Done */
#define E1000_MM_TXD_CMD_EOP    (0x01000000 >> 24) /* End of Packet */
#define E1000_MM_RAL             0x05400  /* Receive Address Low - RW Array [0] */
#define E1000_MM_RAH             0x05404  /* Receive Address High - RW Array [0] */
#define E1000_MM_RAH_AV          0x80000000        /* Receive descriptor valid */
#define E1000_MM_MTA_ADDR        0x05200  /* Multicast Table Array - RW Array */
#define E1000_MM_MTA_LEN         128
#define E1000_MM_IMS             0x000D0  /* Interrupt Mask Set - RW */
#define E1000_MM_RXD_STAT_DD     0x01    /* Descriptor Done */
#define E1000_MM_RXD_STAT_EOP    0x02    /* End of Packet */
#define E1000_MM_RDBAL           0x02800  /* RX Descriptor Base Address Low - RW */
#define E1000_MM_RDBAH           0x02804  /* RX Descriptor Base Address High - RW */
#define E1000_MM_RDLEN           0x02808  /* RX Descriptor Length - RW */
#define E1000_MM_RDH             0x02810  /* RX Descriptor Head - RW */
#define E1000_MM_RDT             0x02818  /* RX Descriptor Tail - RW */
#define E1000_MM_RCTL            0x00100  /* RX Control - RW */
#define E1000_MM_RCTL_EN         0x00000002    /* enable */
#define E1000_MM_RCTL_BAM        0x00008000    /* broadcast enable */
#define E1000_MM_RCTL_SECRC      0x04000000    /* Strip Ethernet CRC */



#define GET8B(ptr, offset) (*(uint64_t *)((uint8_t *)(ptr) + (offset)))
#define GET4B(ptr, offset) (*(uint32_t *)((uint8_t *)(ptr) + (offset)))
#define GET2B(ptr, offset) (*(uint16_t *)((uint8_t *)(ptr) + (offset)))

static volatile uint8_t *mme1000; // memory mapped e1000
static volatile struct tx_desc *ptx_desc;   // pointer to tx descriptors
static uintptr_t txbuf_addr[E1000_CONF_TX_TDNUM]; // physical addr of tx buf
static volatile struct rx_desc *prx_desc;   // pointer to rx descriptors
static uintptr_t rxbuf_addr[E1000_CONF_RX_RDNUM];

extern void pci_func_enable(struct pci_func *f);

int e1000_func_enable(struct pci_func *f)
{
    /* Enable E1000 and set up memory mapped region. */
    pci_func_enable(f);

    /* Serve as a flush. */
    cprintf("e1000: tx descriptor initialized at %p\n", 
            GET4B(mme1000, E1000_MM_TDBAL));

    return 0;
}
```

Finally, I added it to `pci_attach_vendor` of `kern/pci.c`:

```c
struct pci_driver pci_attach_vendor[] = {
	{ 0x8086, 0x100e, e1000_func_enable},
	{ 0, 0, 0 },
};
```

**Exercise 4.** *In your attach function, create a virtual memory mapping for the E1000's BAR 0 by calling `mmio_map_region` (which you wrote in lab 4 to support memory-mapping the LAPIC).*

In `e1000_func_enable()` of `kern/e1000.c`, I added:

```c
mme1000 = (uint8_t *) mmio_map_region(f->reg_base[0], f->reg_size[0]);
```

**Exercise 5.** *Perform the initialization steps described in section 14.5 (but not its subsections). Use section 13 as a reference for the registers the initialization process refers to and sections 3.3.3 and 3.4 for reference to the transmit descriptors and transmit descriptor array.*

I added `enable_transmit()` to `kern/e1000.c`:

```c
static void enable_transmit(void)
{
    /* Allocate memory for tx descriptors. */
    assert(E1000_CONF_TX_TDNUM * sizeof(struct tx_desc) <= PGSIZE);
    struct PageInfo *pp_tx_des;
    int ret_val;
    pp_tx_des = page_alloc(ALLOC_ZERO);
	if (!pp_tx_des)
		panic("enable_transmit: failed to allocate memory for tx descriptors!");
    ptx_desc = (struct tx_desc *) page2kva(pp_tx_des);

    /* Allocate memory for buffers, and link them to descriptors. */
    for (int tdnum = 0; tdnum < E1000_CONF_TX_TDNUM; tdnum += E1000_CONF_TX_BUF_PER_PAGE) {
        struct PageInfo *pp_buf;
        pp_buf = page_alloc(ALLOC_ZERO);
        
        if (!pp_buf)
            panic("enable_transmit: failed to allocate memory for buffers!");
        physaddr_t phyaddr = page2pa(pp_buf);
        for (int i = 0; i < E1000_CONF_TX_BUF_PER_PAGE; ++i) {
            ptx_desc[tdnum + i].addr = phyaddr + i * E1000_CONF_TX_BUFSIZE;
            txbuf_addr[tdnum + i] = phyaddr + i * E1000_CONF_TX_BUFSIZE;
            ptx_desc[tdnum + i].length = E1000_CONF_TX_BUFSIZE;
            ptx_desc[tdnum + i].cmd |= E1000_MM_TXD_CMD_RS;
            ptx_desc[tdnum + i].status |= E1000_MM_TXD_STAT_DD;
        }
    }

    /* Transmit initialization. */
    GET4B(mme1000, E1000_MM_TDBAL) = page2pa(pp_tx_des);
    GET4B(mme1000, E1000_MM_TDBAH) = 0;
    GET4B(mme1000, E1000_MM_TDLEN) = E1000_CONF_TX_TDLEN;
    GET4B(mme1000, E1000_MM_TDH) = 0;
    GET4B(mme1000, E1000_MM_TDT) = 0;
    uint32_t tipg;
    tipg = (10 & E1000_MM_TIPG_IPGT) | ((8 << 10) & E1000_MM_TIPG_IPGR1)
           | ((6 << 20) & E1000_MM_TIPG_IPGR2);
    GET4B(mme1000, E1000_MM_TIPG) = tipg;
    uint32_t tctl = GET4B(mme1000, E1000_MM_TCTL);
    tctl |= (E1000_MM_TCTL_EN | E1000_MM_TCTL_PSP);
    tctl = (tctl & ~E1000_MM_TCTL_CT) | (0x10 << 4);
    tctl = (tctl & ~E1000_MM_TCTL_COLD) | (0x40 << 12);
    GET4B(mme1000, E1000_MM_TCTL) = tctl;
}
```

I also call this function in `e1000_func_enable()` of `kern/e1000.c`:

```c
enable_transmit();
```

**Exercise 6.** *Write a function to transmit a packet by checking that the next descriptor is free, copying the packet data into the next descriptor, and updating TDT. Make sure you handle the transmit queue being full.*

In `kern/e1000.c`, I added:

```c
int e1000_transmit(const void *data, uint32_t len)
{
    int tdh = GET4B(mme1000, E1000_MM_TDH);
    int tdt = GET4B(mme1000, E1000_MM_TDT);

    if (len > E1000_CONF_TX_BUFSIZE)
        return -E_TX_TOO_LONG;

    if ((tdt + 1) % E1000_CONF_TX_TDNUM == tdh)
        return -E_NO_TX_DESC;
    
    if (!(ptx_desc[tdt].status & E1000_MM_TXD_STAT_DD))
        return -E_NO_TX_DESC;
    
    memmove(KADDR(txbuf_addr[tdt]), data, len);
    ptx_desc[tdt].addr = txbuf_addr[tdt];
    ptx_desc[tdt].length = len;
    ptx_desc[tdt].cmd = E1000_MM_TXD_CMD_RS | E1000_MM_TXD_CMD_EOP;
    ptx_desc[tdt].status = 0;
    
    GET4B(mme1000, E1000_MM_TDT) = (tdt + 1) % E1000_CONF_TX_TDNUM;

    return 0;
}
```

I also added the signature of this function to `kern/e1000.h`.

**Exercise 7.** *Add a system call that lets you transmit packets from user space.*

In `inc/syscall.h`, I allocated system call number to our new system call:

```c
SYS_send_frame,
```

In `kern/syscall.c`, I implemented `sys_send_frame()`:

```c
static int
sys_send_frame(const void *data, uint32_t len)
{
	user_mem_assert(curenv, data, len, PTE_P | PTE_U);
	return e1000_transmit(data, len);
}
```

In `syscall()` of `kern/syscall.c`, I added two entries to dispatch these two system calls:

```c
case SYS_send_frame:
	return sys_send_frame((const void *) a1, (uint32_t) a2);
```

In `lib/syscall.c`, I added:

```c
int
sys_send_frame(const void *data, uint32_t len)
{
	return syscall(SYS_send_frame, 0, (uint32_t) data, len, 0, 0, 0);
}
```

In `inc/lib.h`, I added:

```c
int sys_send_frame(const void *data, uint32_t len);
```

**Exercise 8.** *Implement `net/output.c`.*

In `ns/ns.h`, I defined `NS_IPC_PAGE_ADDR`:

```C
#define NS_IPC_PAGE_ADDR 0xc0000000
```

In `output()` of `net/output.c`, I added:

```c
void *in_page = (void *) NS_IPC_PAGE_ADDR;
while (true) {
    sys_ipc_recv(in_page);
    struct jif_pkt *ppkt = (struct jif_pkt *) in_page;
    if (thisenv->env_ipc_value != NSREQ_OUTPUT)
        continue;
    if ((thisenv->env_ipc_perm & (PTE_P | PTE_U)) != (PTE_P | PTE_U))
        continue;

    // Try to send a frame. Only retry once.
    if (sys_send_frame(ppkt->jp_data, ppkt->jp_len)) {
        sys_yield();
        sys_send_frame(ppkt->jp_data, ppkt->jp_len);
    }
}
```

**Question 1.** *How did you structure your transmit implementation? In particular, what do you do if the transmit ring is full?*

The user program first calls library function `sys_send_frame()`. This library function invokes a syscall `SYS_send_frame`, and trap into the kernel. The kernel checks the arguments, and calls `e1000_transmit()` in the driver.

Look at the code in `ns/output.c`:

```c
// Try to send a frame. Only retry once.
if (sys_send_frame(ppkt->jp_data, ppkt->jp_len)) {
    sys_yield();
    sys_send_frame(ppkt->jp_data, ppkt->jp_len);
}
```

If the transmit ring is full, the user program should retry exactly once. This guarantees that we do not drop packets too often, and we do not keep the packet for too long time such that may cause the connection RTT to blow up.

**Exercise 9.** *Read section 3.2. You can ignore anything about interrupts and checksum offloading (you can return to these sections if you decide to use these features later), and you don't have to be concerned with the details of thresholds and how the card's internal caches work.*

**Exercise 10.** *Set up the receive queue and configure the E1000 by following the process in section 14.4. You don't have to support "long packets" or multicast. For now, don't configure the card to use interrupts; you can change that later if you decide to use receive interrupts. Also, configure the E1000 to strip the Ethernet CRC, since the grade script expects it to be stripped.*

I added `enable_receive()` to `kern/e1000.c`:

```c
static void enable_receive(void)
{
    /* Allocate memory for tx descriptors. */
    assert(E1000_CONF_RX_RDNUM * sizeof(struct tx_desc) <= PGSIZE);
    struct PageInfo *pp_rx_des;
    int ret_val;
    pp_rx_des = page_alloc(ALLOC_ZERO);
	if (!pp_rx_des)
		panic("enable_receive: failed to allocate memory for rx descriptors!");
    prx_desc = (struct rx_desc *) page2kva(pp_rx_des);

    /* Allocate memory for buffers, and link them to descriptors. */
    for (int rdnum = 0; rdnum < E1000_CONF_RX_RDNUM; rdnum += E1000_CONF_RX_BUF_PER_PAGE) {
        struct PageInfo *pp_buf;
        pp_buf = page_alloc(ALLOC_ZERO);
        
        if (!pp_buf)
            panic("enable_receive: failed to allocate memory for buffers!");
        physaddr_t phyaddr = page2pa(pp_buf);
        for (int i = 0; i < E1000_CONF_RX_BUF_PER_PAGE; ++i) {
            prx_desc[rdnum + i].addr = phyaddr + i * E1000_CONF_RX_BUFSIZE;
            rxbuf_addr[rdnum + i] = phyaddr + i * E1000_CONF_RX_BUFSIZE;
        }
    }

    /* Receiving initialization. */
    GET4B(mme1000, E1000_MM_RAL) = E1000_CONF_RX_RAL_VAL;
    GET4B(mme1000, E1000_MM_RAH) = E1000_CONF_RX_RAH_VAL | E1000_MM_RAH_AV;
    for (int i = 0; i < E1000_MM_MTA_LEN; i += 4)
        GET4B(mme1000, E1000_MM_MTA_ADDR + i) = 0;
    GET4B(mme1000, E1000_MM_IMS) = 0;
    GET4B(mme1000, E1000_MM_RDBAL) = page2pa(pp_rx_des);
    GET4B(mme1000, E1000_MM_RDBAH) = 0;
    GET4B(mme1000, E1000_MM_RDLEN) = E1000_CONF_RX_RDLEN;
    GET4B(mme1000, E1000_MM_RDH) = 0;
    GET4B(mme1000, E1000_MM_RDT) = E1000_CONF_RX_RDNUM - 1;
    GET4B(mme1000, E1000_MM_RCTL) = E1000_MM_RCTL_EN | E1000_MM_RCTL_BAM
                                    | E1000_MM_RCTL_SECRC;
}
```

In `e1000_func_enable` of `kern/e1000.c`, I added:

```c
enable_receive();
```

**Exercise 11.** *Write a function to receive a packet from the E1000 and expose it to user space by adding a system call.*

In `kern/e1000.c`, I added:

```c
int e1000_receive(void *data, uint32_t len)
{
    int rdh = GET4B(mme1000, E1000_MM_RDH);
    int rdt = GET4B(mme1000, E1000_MM_RDT);
    int try = (rdt + 1) % E1000_CONF_RX_RDNUM;
    if (prx_desc[try].status & E1000_MM_RXD_STAT_DD) {
        uint32_t recv_len;
        recv_len = prx_desc[try].length;
        len = len < recv_len ? len : recv_len;
        memmove(data, KADDR(rxbuf_addr[try]), len);
        GET4B(mme1000, E1000_MM_RDT) = try;
        return len;
    }

    return -E_RX_NO_PKT;
}
```

I also added the function signature in `kern/e1000.h`:

```c
int e1000_receive(void *data, uint32_t len);
```

In `inc/syscall.h`, I added:

```c
SYS_receive_frame,
```

In `kern/syscall.c`, I added:

```c
static int
sys_receive_frame(void *data, uint32_t len)
{
	user_mem_assert(curenv, data, len, PTE_P | PTE_U | PTE_W);
	return e1000_receive(data, len);
}
```

In `syscall()` of `kern/syscall.c`, I added:

```c
case SYS_receive_frame:
	return sys_receive_frame((void *) a1, (uint32_t) a2);
```

In `lib/syscall.c`, I added:

```c
int
sys_receive_frame(void *data, uint32_t len)
{
	return syscall(SYS_receive_frame, 0, (uint32_t) data, (uint32_t) len, 0, 0, 0);
}
```

In `inc/lib.h`, I added:

```c
int sys_receive_frame(void *data, uint32_t len);
```

**Exercise 12.** *Implement `net/input.c`.*

In `input()` of `net/input.c`, I added:

```c
while (true) {
    void *new_page = (void *) NS_IPC_PAGE_ADDR;
    while (sys_page_alloc(0, new_page, PTE_U | PTE_W | PTE_P)) {
        cprintf("failed to alloc page, try again ...\n");
        sys_yield();
    }
    struct jif_pkt *pjif = (struct jif_pkt *) new_page;
    int32_t recv_len;
    while ((recv_len = sys_receive_frame(pjif->jp_data, 2048)) < 0) {
        if (recv_len != -E_RX_NO_PKT)
            cprintf("error occured when receiving frame\n");
        sys_yield();
    }
    pjif->jp_len = recv_len;
    ipc_send(ns_envid, NSREQ_INPUT, new_page, PTE_U | PTE_W | PTE_P);
    sys_page_unmap(0, new_page);
}
```

**Question 2.** *How did you structure your receive implementation? In particular, what do you do if the receive queue is empty and a user environment requests the next incoming packet?*

The user program first calls library function `sys_receive_frame()`. This library function invokes a syscall `SYS_receive_frame`, and trap into the kernel. The kernel checks the arguments, and calls `e1000_receive()` in the driver.

Look at the code in `ns/input.c`:

```c
while ((recv_len = sys_receive_frame(pjif->jp_data, 2048)) < 0) {
    if (recv_len != -E_RX_NO_PKT)
        cprintf("error occured when receiving frame\n");
    sys_yield();
}
```

If the receive queue is empty, the user program should retry until sucessfully received something. Note that if `-E_RX_NO_PKT` is returned from `sys_receive_frame()`, there must be an error occurred when receiving the frame.

**Exercise 13.** *The web server is missing the code that deals with sending the contents of a file back to the client. Finish the web server by implementing `send_file` and `send_data`.*

I implemented `send_data()` in `user/httpd.c`:

```c
char buf[256];
int r;
while ((r = read(fd, buf, 256)) > 0) {
    if (write(req->sock, buf, r) != r)
        return -1;
}
if (r != 0)
    return -1;
return 0;
```

I implemented `send_file()` in `user/httpd.c`:

```c
struct Stat statbuf;
if (stat(req->url, &statbuf) < 0) {
    send_error(req, 404);
    return -1;
}
if (statbuf.st_isdir) {
    send_error(req, 404);
    return -1;
}
file_size = statbuf.st_size;
fd = open(req->url, O_RDONLY);
```



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

