#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

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

#endif  // SOL >= 6
