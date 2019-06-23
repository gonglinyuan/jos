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
int e1000_transmit(const void *data, uint32_t len);
int e1000_receive(void *data, uint32_t len);
int e1000_interrupt_handler();
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
#define E1000_MM_RDTR            0x02820  /* RX Delay Timer - RW */
#define E1000_MM_ICR             0x000C0  /* Interrupt Cause Read - R/clr */
#define E1000_MM_ITR             0x000C4  /* Interrupt Throttling Rate - RW */
#define E1000_MM_ICS             0x000C8  /* Interrupt Cause Set - WO */
#define E1000_MM_IMS             0x000D0  /* Interrupt Mask Set - RW */
#define E1000_MM_IMC             0x000D8  /* Interrupt Mask Clear - WO */
#define E1000_MM_ICR_RXT0        0x00000080 /* rx timer intr (ring 0) */
#define E1000_MM_IMS_RXT0        E1000_MM_ICR_RXT0      /* rx timer intr */
#define E1000_MM_ICR_TXDW        0x00000001 /* Transmit desc written back */
#define E1000_MM_IMS_TXDW        E1000_MM_ICR_TXDW      /* Transmit desc written back */



#define GET8B(ptr, offset) (*(uint64_t *)((uint8_t *)(ptr) + (offset)))
#define GET4B(ptr, offset) (*(uint32_t *)((uint8_t *)(ptr) + (offset)))
#define GET2B(ptr, offset) (*(uint16_t *)((uint8_t *)(ptr) + (offset)))

static volatile uint8_t *mme1000; // memory mapped e1000
static volatile struct tx_desc *ptx_desc;   // pointer to tx descriptors
static uintptr_t txbuf_addr[E1000_CONF_TX_TDNUM]; // physical addr of tx buf
static volatile struct rx_desc *prx_desc;   // pointer to rx descriptors
static uintptr_t rxbuf_addr[E1000_CONF_RX_RDNUM];

static struct Env *pBlkTxEnv, *pBlkRxEnv;

extern void pci_func_enable(struct pci_func *f);

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
    for (int tdnum = 0; tdnum < E1000_CONF_TX_TDNUM; tdnum += E1000_CONF_TX_BUF_PER_PAGE)
    {
        struct PageInfo *pp_buf;
        pp_buf = page_alloc(ALLOC_ZERO);
        
        if (!pp_buf)
            panic("enable_transmit: failed to allocate memory for buffers!");
        physaddr_t phyaddr = page2pa(pp_buf);
        for (int i = 0; i < E1000_CONF_TX_BUF_PER_PAGE; ++i)
        {
            ptx_desc[tdnum + i].addr = phyaddr + i * E1000_CONF_TX_BUFSIZE;
            txbuf_addr[tdnum + i] = phyaddr + i * E1000_CONF_TX_BUFSIZE;
            ptx_desc[tdnum + i].length = E1000_CONF_TX_BUFSIZE;
            ptx_desc[tdnum + i].cmd |= E1000_MM_TXD_CMD_RS;
            ptx_desc[tdnum + i].status |= E1000_MM_TXD_STAT_DD;
        }
    }

    /* Transmit interrupt initialization. */
    GET4B(mme1000, E1000_MM_IMS) = E1000_MM_IMS_TXDW;
    if (GET4B(mme1000, E1000_MM_IMS) & E1000_MM_IMS_TXDW)
        cprintf("TX interrupt mask set!\n");

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
    for (int rdnum = 0; rdnum < E1000_CONF_RX_RDNUM; rdnum += E1000_CONF_RX_BUF_PER_PAGE)
    {
        struct PageInfo *pp_buf;
        pp_buf = page_alloc(ALLOC_ZERO);
        
        if (!pp_buf)
            panic("enable_receive: failed to allocate memory for buffers!");
        physaddr_t phyaddr = page2pa(pp_buf);
        for (int i = 0; i < E1000_CONF_RX_BUF_PER_PAGE; ++i)
        {
            prx_desc[rdnum + i].addr = phyaddr + i * E1000_CONF_RX_BUFSIZE;
            rxbuf_addr[rdnum + i] = phyaddr + i * E1000_CONF_RX_BUFSIZE;
        }
    }
    
    /* Receiving interrupt initialization. */
    GET4B(mme1000, E1000_MM_RDTR) = 0;
    GET4B(mme1000, E1000_MM_IMS) = E1000_MM_IMS_RXT0;
    if (GET4B(mme1000, E1000_MM_IMS) & E1000_MM_IMS_RXT0)
        cprintf("RX interrupt mask set!\n");

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

int e1000_transmit(const void *data, uint32_t len)
{
    struct Env *self;
    int tdh = GET4B(mme1000, E1000_MM_TDH);
    int tdt = GET4B(mme1000, E1000_MM_TDT);

    if (len > E1000_CONF_TX_BUFSIZE)
        return -E_TX_TOO_LONG;

    if ((tdt + 1) % E1000_CONF_TX_TDNUM == tdh)
        goto tx_pending;
    
    if (!(ptx_desc[tdt].status & E1000_MM_TXD_STAT_DD))
        goto tx_pending;
    
    memmove(KADDR(txbuf_addr[tdt]), data, len);
    ptx_desc[tdt].addr = txbuf_addr[tdt];
    ptx_desc[tdt].length = len;
    ptx_desc[tdt].cmd = E1000_MM_TXD_CMD_RS | E1000_MM_TXD_CMD_EOP;
    ptx_desc[tdt].status = 0;
    
    GET4B(mme1000, E1000_MM_TDT) = (tdt + 1) % E1000_CONF_TX_TDNUM;

    return 0;

tx_pending:
    if (envid2env(0, &self, 1) < 0)
        panic("e1000_transmit: cannot get current Env's pointer");
    self->env_tf.tf_regs.reg_eax = -E_NO_TX_DESC;
    self->env_status = ENV_SLEEPING;
    pBlkTxEnv = self;
    sched_yield();

    panic("e1000_transmit: should not reach here!");
    return -E_NO_TX_DESC;
}

int e1000_receive(void *data, uint32_t len)
{
    int rdh = GET4B(mme1000, E1000_MM_RDH);
    int rdt = GET4B(mme1000, E1000_MM_RDT);
    int try = (rdt + 1) % E1000_CONF_RX_RDNUM;
    if (prx_desc[try].status & E1000_MM_RXD_STAT_DD)
    {
        uint32_t recv_len;
        recv_len = prx_desc[try].length;
        len = len < recv_len ? len : recv_len;
        memmove(data, KADDR(rxbuf_addr[try]), len);
        GET4B(mme1000, E1000_MM_RDT) = try;

        if (GET4B(mme1000, E1000_MM_ICR) & E1000_MM_ICR_RXT0
            & GET4B(mme1000, E1000_MM_IMS))
            cprintf("See receive interrupt.\n");

        return len;
    }

    struct Env *self;
    if (envid2env(0, &self, 1) < 0)
        panic("e1000_receive: cannot get current Env's pointer");
    self->env_tf.tf_regs.reg_eax = -E_RX_NO_PKT;
    self->env_status = ENV_SLEEPING;
    pBlkRxEnv = self;
    sched_yield();

    panic("e1000_receive: should not reach here!");
    return -E_RX_NO_PKT;
}

int e1000_interrupt_handler()
{
    uint32_t intvec = GET4B(mme1000, E1000_MM_ICR);
    if (pBlkRxEnv && (intvec & E1000_MM_ICR_RXT0))
    {
        cprintf("Handling RX interrupt!\n");
        pBlkRxEnv->env_status = ENV_RUNNABLE;
        pBlkRxEnv = NULL;
    }

    if (pBlkTxEnv && (intvec & E1000_MM_ICR_TXDW))
    {
        cprintf("Handling TX interrupt!\n");
        pBlkTxEnv->env_status = ENV_RUNNABLE;
        pBlkTxEnv = NULL;
    }

    return 0;
}

int e1000_func_enable(struct pci_func *f)
{
    /* Enable E1000 and set up memory mapped region. */
    irq_setmask_8259A(irq_mask_8259A & ~(1<<11));
    pci_func_enable(f);
    mme1000 = (uint8_t *) mmio_map_region(f->reg_base[0], f->reg_size[0]);

    /* Clear all interrupts. */
    GET4B(mme1000, E1000_MM_IMC) = ~(uint32_t)0;

    /* Enable transmit and receive function. */
    enable_transmit();
    enable_receive();

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

**Question 1.** *Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?*

No. Because `IOPL` is in `EFLAGS`, which will be properly saved and restored every time we switch from one environment to another. When an interrupt happens, the hardware will automatically save `EFLAGS` in the kernel stack; in `env_pop_tf()`, the `iret` instruction will restore the previously saved `EFLAGS`. 

**Exercise 2.** *Browse Intel's [Software Developer's Manual](https://pdos.csail.mit.edu/6.828/2018/readings/hardware/8254x_GBe_SDM.pdf) for the E1000. This manual covers several closely related Ethernet controllers. QEMU emulates the 82540EM.*

*The `flush_block` function should write a block out to disk if necessary. `flush_block` shouldn't do anything if the block isn't even in the block cache (that is, the page isn't mapped) or if it's not dirty. After writing the block to disk, `flush_block` should clear the `PTE_D` bit using `sys_page_map`.*

In `bc_pgfault()` of `bc.c`, I added:

```c
addr = ROUNDDOWN(addr, PGSIZE);
if ((r = sys_page_alloc(0, addr, PTE_U | PTE_W)) < 0) {
	panic("in bc_pgfault, sys_page_alloc: %e", r);
}
if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0) {
	panic("in bc_pgfault, ide_read returns %d", r);
}
```

In `flush_block()` of `bc.c`, I added:

```c
int r;
addr = ROUNDDOWN(addr, BLKSIZE);
if (!(va_is_mapped(addr) && va_is_dirty(addr))) {
    return;
}
if ((r = ide_write(blockno * BLKSECTS, addr, BLKSECTS)) < 0) {
    panic("in bc_pgfault, ide_write returns %d", r);
}
// Remap to clear PTE_D
if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0) {
    panic("in bc_pgfault, sys_page_map: %e", r);
}
```

**Exercise 3.** *Use `free_block` as a model to implement `alloc_block` in `fs/fs.c`, which should find a free disk block in the bitmap, mark it used, and return the number of that block. When you allocate a block, you should immediately flush the changed bitmap block to disk with `flush_block`, to help file system consistency.*

In `alloc_block()` of `fs.c`, I added:

```c
uint32_t blockno = 0;
while ((blockno < super->s_nblocks) && !bitmap[blockno / 32]) {
    blockno += 32;
}
while ((blockno < super->s_nblocks) && !(bitmap[blockno / 32] & (1 << (blockno % 32)))) {
    blockno += 1;
}
if (blockno >= super->s_nblocks) {
    return -E_NO_DISK;
}
bitmap[blockno / 32] &= ~(1 << (blockno % 32));
flush_block(&bitmap[blockno / 32]);
return blockno;
```

**Exercise 4.** *Implement `file_block_walk` and `file_get_block`. `file_block_walk` maps from a block offset within a file to the pointer for that block in the `struct File` or the indirect block, very much like what `pgdir_walk` did for page tables. `file_get_block` goes one step further and maps to the actual disk block, allocating a new one if necessary.*

In `file_block_walk()` of `fs.c`, I added:

```c
int r;
if (filebno < NDIRECT) {
    *ppdiskbno = f->f_direct + filebno;
} else if (filebno < NDIRECT + NINDIRECT) {
    if (!f->f_indirect) {
        if (alloc) {
            if ((r = alloc_block()) < 0) {
                return r;
            }
            f->f_indirect = r;
        } else {
            return -E_NOT_FOUND;
        }
    }
    *ppdiskbno = ((uint32_t *) diskaddr(f->f_indirect)) + (filebno - NDIRECT);
} else { // filebno >= NDIRECT + NINDIRECT
    return -E_INVAL;
}
return 0;
```

In `file_get_block()` of `fs.c`, I added:

```c
int r;
uint32_t *ppdiskbno;
if ((r = file_block_walk(f, filebno, &ppdiskbno, true)) < 0) {
    return r;
}
if (!(*ppdiskbno)) {
    if ((r = alloc_block()) < 0) {
        return r;
    }
    *ppdiskbno = r;
}
*blk = diskaddr(*ppdiskbno);
return 0;
```

**Exercise 5.** *Implement `serve_read` in `fs/serv.c`.*

In `serve_read()` of `fs.c`, I added:

```c
struct OpenFile *o;
int r;
if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0) {
    return r;
}
if ((r = file_read(o->o_file, ret->ret_buf, MIN(req->req_n, sizeof(ret->ret_buf)), o->o_fd->fd_offset)) < 0) {
    return r;
}
o->o_fd->fd_offset += r;
return r;
```

I make sure that the number of bytes we read from the file is no more than the size of the provided buffer.

**Exercise 6.** *Implement `serve_write` in `fs/serv.c` and `devfile_write` in `lib/file.c`.*

In `serve_write()` of `fs.c`, I added:

```c
struct OpenFile *o;
int r;
if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0) {
	return r;
}
if ((r = file_write(o->o_file, req->req_buf, MIN(req->req_n, sizeof(req->req_buf)), o->o_fd->fd_offset)) < 0) {
	return r;
}
o->o_fd->fd_offset += r;
return r;
```

In `devfile_write()` of `file.c`, I added:

```c
fsipcbuf.write.req_fileid = fd->fd_file.id;
n = MIN(n, sizeof(fsipcbuf.write.req_buf));
fsipcbuf.write.req_n = n;
memmove(fsipcbuf.write.req_buf, buf, n);
return fsipc(FSREQ_WRITE, NULL);
```

**Exercise 7.** *`spawn` relies on the new syscall `sys_env_set_trapframe` to initialize the state of the newly created environment. Implement `sys_env_set_trapframe` in `kern/syscall.c` (don't forget to dispatch the new system call in `syscall()`).*

*Test your code by running the `user/spawnhello` program from `kern/init.c`, which will attempt to spawn `/hello` from the file system.*

In `sys_env_set_trapframe()` of `syscall.c`, I added:

```c
struct Env *env_ptr;
int r;
if ((r = envid2env(envid, &env_ptr, true)) < 0) {
    return r;
}
user_mem_assert(curenv, (const void *)tf, sizeof(struct Trapframe), 0);
env_ptr->env_tf = *tf;
// Set IOPL to 0
env_ptr->env_tf.tf_eflags &= ~FL_IOPL_MASK;
// Enable interrupts
env_ptr->env_tf.tf_eflags |= FL_IF;
// Set CPL to 3
env_ptr->env_tf.tf_cs |= 0x3;
return 0;
```

Then I tested it with `make run-spawnhello-nox`, and the output is:

```
vagrant@ubuntu-xenial:~/jos$ make run-spawnhello-nox
make[1]: Entering directory '/home/vagrant/jos'
make[1]: 'obj/fs/fs.img' is up to date.
make[1]: Leaving directory '/home/vagrant/jos'
qemu-system-i386 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 -drive file=obj/fs/fs.img,index=1,media=disk,format=raw
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2 4
i am parent environment 00001001
FS is running
FS can do I/O
Device 1 presence: 1
block cache is good
superblock is good
bitmap is good
alloc_block is good
file_open is good
file_get_block is good
file_flush is good
file_truncate is good
file rewrite is good
hello, world
i am environment 00001002
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

**Exercise 8.** *Change `duppage` in `lib/fork.c` to follow the new convention. If the page table entry has the `PTE_SHARE` bit set, just copy the mapping directly.*

*Likewise, implement `copy_shared_pages` in `lib/spawn.c`. It should loop through all page table entries in the current process (just like `fork` did), copying any page mappings that have the`PTE_SHARE` bit set into the child process.*

I changed `duppage()` in `fork.c` to be:

```c
uintptr_t addr = pn * PGSIZE;
if (((uvpt[pn] & PTE_COW) || (uvpt[pn] & PTE_W)) && !(uvpt[pn] & PTE_SHARE)) {
    r = sys_page_map(0, (void *) addr, envid, (void *) addr, ((uvpt[pn] & PTE_SYSCALL) & (~PTE_W)) | PTE_COW);
    if (r < 0) return r;
    r = sys_page_map(0, (void *) addr, 0, (void *) addr, ((uvpt[pn] & PTE_SYSCALL) & (~PTE_W)) | PTE_COW);
    if (r < 0) return r;
} else {
    r = sys_page_map(0, (void *) addr, envid, (void *) addr, (uvpt[pn] & PTE_SYSCALL));
    if (r < 0) return r;
}
return 0;
```

In `copy_shared_pages()` of `spawn.c`, I added:

```c
int r;
for (uintptr_t addr = 0; addr < USTACKTOP; addr += PGSIZE) {
    if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_U) && (uvpt[PGNUM(addr)] & PTE_SHARE)) {
        if ((r = sys_page_map(0, (void *) addr, child, (void *) addr, (uvpt[PGNUM(addr)] & PTE_SYSCALL))) < 0) {
            return r;
        }
    }
}
```

**Exercise 9.** *In your `kern/trap.c`, call `kbd_intr` to handle trap `IRQ_OFFSET+IRQ_KBD` and `serial_intr` to handle trap `IRQ_OFFSET+IRQ_SERIAL`.*

In `trap_init()` of `trap.c`, I added:

```c
SETGATE(idt[IRQ_OFFSET + IRQ_KBD], 0, GD_KT, idt_entries[IRQ_OFFSET + IRQ_KBD], 0);
SETGATE(idt[IRQ_OFFSET + IRQ_SERIAL], 0, GD_KT, idt_entries[IRQ_OFFSET + IRQ_SERIAL], 0);
```

In `trap_dispatch()` of `trap.c`, I added:

```c
if (tf->tf_trapno == IRQ_OFFSET + IRQ_KBD) {
    kbd_intr();
    return;
}

if (tf->tf_trapno == IRQ_OFFSET + IRQ_SERIAL) {
    serial_intr();
    return;
}
```

**Exercise 10.** *Add I/O redirection for < to `user/sh.c`.*

*Test your implementation by typing sh <script into your shell*

*Run make run-testshell to test your shell.*



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

