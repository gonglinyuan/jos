#include "ns.h"
#include <inc/syscall.h>

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver

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
}
