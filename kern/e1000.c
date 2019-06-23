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
