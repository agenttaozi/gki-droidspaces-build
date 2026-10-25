#ifndef _SC_PCI_VNET_H
#define _SC_PCI_VNET_H

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/pci_regs.h>
#include <uapi/linux/pcitest.h>


#define GMAC_FRAME_LEN               1528
#define SC_RING_COUNT                   72
#define SC_FREE_RING_COUNT              1024
/* Network link timeout 5 sec */
#define SC_LINK_TIMEOUT 5000
#define MIN(a,b)   					((a) < (b) ? (a) : (b))
#define SC_PCI_ETH_PROC_FS
#define SC_VNET_NAPI_WEIGHT	64

struct sc_pci_epf_eth_debug_info {
		u64 host_netif_rx_hwirq_counts;
#ifdef SC_PCI_ETH_PROC_FS
		u64 napi_sirq_count;
	
		u32 wait_to_poll_in_sirq;
		int work_done_per_sirq; 
		u64 sirq_eaten_time;
		u64 sirq_delta0_time;
		u64 sirq_delta1_time;
		u64 sirq_delta2_time;
		u64 sirq_delta3_time;
		u64 sirq_delta4_time;
		u64 sirq_delta5_time;
#endif
	
		u64 xmit_counts;
		u64 drop_nomem_counts;
		u64 drop_free_slow_counts;
		u64 drop_ep_skb_empty_counts;
		u64 drop_h2ep_xfer_element_empty_counts;
		u64 drop_host_xmit_dma_map_err_counts;
		u64 ctrl_hirq_counts;
		u64 host_free_skb_counts;
		u64 host_free_skb_found_counts;
		u64 ep2h_xfer_element_counts;
		u64 host_netif_rx_counts;
		u64 host_netif_rx_found_counts;
		u64 raise_sgi0_counts;
};

struct ep_own_cnt {
	u32 ep_empty_skb_rd_cnt;
	u32 ep_empty_skb_wr_cnt;
	u32 ep2h_xfer_element_rd_cnt;
	u32 ep2h_xfer_element_wr_cnt;
	u32 ep2h_ctrl_msg_rd_cnt;
	u32 ep2h_ctrl_msg_wr_cnt;
};


struct ep_empty_skb {
	phys_addr_t phy_skb_base;
	void __iomem *vir_skb_addr;
	u32 buffer_len;
};


struct ep2h_xfer_element {
	phys_addr_t src_phy_skb_addr;
	void __iomem *src_vir_skb_addr;
	u32 xfer_len;
		
	phys_addr_t dst_pcie_addr;
	void __iomem *host_skb_va;
};



struct ep2h_ctrl_msg {
	u32 msg_id;
};


struct ep_ring_buf {
	struct ep_own_cnt *ep_cnt;
	/* Endpoint written message buffers */
	struct ep_empty_skb *ep_empty_skbs;
	struct ep2h_xfer_element *ep2h_xfer_elems;
	struct ep2h_ctrl_msg *ep2h_ctrl_msgs;

	struct ep2h_ctrl_msg * end; //just a flag
};


struct host_own_cnt {
	u32 host_empty_skb_rd_cnt;
	u32 host_empty_skb_wr_cnt;
	u32 h2ep_xfer_element_rd_cnt;
	u32 h2ep_xfer_element_wr_cnt;
	u32 h2ep_ctrl_msg_rd_cnt;
	u32 h2ep_ctrl_msg_wr_cnt;
	
	u32 host_skb_free_info_rd_cnt;// to free host xmit skb
	u32 host_skb_free_info_wr_cnt;
};


struct host_empty_skb {
	phys_addr_t phy_skb_base;
	void __iomem *vir_skb_addr;
	u32 buffer_len;
};


struct h2ep_xfer_element {
	phys_addr_t src_pcie_addr;
	void __iomem *src_vir_skb_addr;
	u32 xfer_len;
	
	phys_addr_t dst_phy_skb_base;
	void __iomem *dst_vir_skb_addr;
};


struct h2ep_ctrl_msg {
	u32 msg_id;
};


struct host_skb_free_info {
	phys_addr_t host_skb_data_pa;
	void __iomem *host_skb_va;
	u32 xfer_len;;
};


struct host_ring_buf {
	struct host_own_cnt *host_cnt;
	/* Host written message buffers */
	struct host_empty_skb *host_empty_skbs;
	struct h2ep_xfer_element *h2ep_xfer_elems;
	struct h2ep_ctrl_msg *h2ep_ctrl_msgs;
	struct host_skb_free_info *host_skb_free_infos;
	
	struct host_skb_free_info * end; //just a flag
};

struct sc_vnet_counter {
	u32 *rd;
	u32 *wr;
};

struct host_empty_list {
	phys_addr_t phy_skb_base;
	void __iomem *vir_skb_addr;
	u32 buffer_len; 
	struct list_head list;
};


enum dir_link_state {
	DIR_LINK_STATE_DOWN,
	DIR_LINK_STATE_UP,
	DIR_LINK_STATE_SENT_DOWN,
};

enum os_link_state {
	OS_LINK_STATE_UP,
	OS_LINK_STATE_DOWN,
};

enum ctrl_msg_type {
	CTRL_MSG_RESERVED,
	CTRL_MSG_LINK_UP,
	CTRL_MSG_LINK_DOWN,
	CTRL_MSG_LINK_DOWN_ACK,
};


struct host_to_free_skb_list {
	phys_addr_t phy_skb_data_base;
	void __iomem *vir_skb_addr;
	u32 xfer_len;
	struct list_head list;
};


struct sc_pci_vnet {
	struct net_device *ndev;
	struct napi_struct napi;
	struct pci_dev *pdev;

	void __iomem *mmio_bar1;
	void __iomem *mmio_bar2;
	void __iomem *mmio_bar5;

	struct ep_ring_buf ep_ring_buf;
	struct host_ring_buf host_ring_buf;

	struct sc_vnet_counter ep_empty_skb_count;
	struct sc_vnet_counter ep2h_xfer_element_count;
	struct sc_vnet_counter ep2h_ctrl_msg_count;
	struct sc_vnet_counter host_empty_skb_count;
	struct sc_vnet_counter h2ep_xfer_element_count;
	struct sc_vnet_counter h2ep_ctrl_msg_count;
	struct sc_vnet_counter host_skb_free_info_count;

	struct list_head host_empty_list;
	spinlock_t host_empty_lock;

	enum dir_link_state local_link_state;
	enum dir_link_state remote_link_state;
	enum os_link_state os_link_state;
	struct mutex link_state_lock;
	wait_queue_head_t link_state_wq;

	struct task_struct *sc_pci_vnet_free_worker;//to free skb
	struct semaphore free_host_xmit_skb_sem;
	struct sk_buff_head tx_queue;
	struct list_head host_to_free_skb_list;
	spinlock_t host_to_free_skb_lock;

	int flag_in_free_skb;
};

#ifdef SC_PCIE_ETH_DEBUG
#define zjl_dbg_pcie(fmt, ...) printk("[zjl_dbg_pcie rc]:[%s-%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define zjl_dbg_pcie(fmt, ...)
#endif


extern int sc_pci_vnet_probe(struct pci_dev *pdev);

#endif
