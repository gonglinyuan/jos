#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

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
}
