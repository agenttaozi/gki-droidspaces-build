#include "sc_pci_pub.h"

#include "sc_pci_vnet.h"

#ifdef SC_PCI_ETH_PROC_FS
struct sc_pci_epf_eth_debug_info sc_pci_epf_eth_debug_infos = {0};
#endif
/*
static void dump_pkt_trace(unsigned char *data,int len)
{
	int i;
	len = len > 256? 256 : len;
    printk("********************\n");
	zjl_dbg_pcie("skb_data_vir:0x%llx", data);
	for (i = 0; i < len; i++) {
		printk("%.2x ", data[i]);
		if ((i&0xf) == 0xf)
			printk("\n");
	}
	printk("\n");
    printk("********************\n");
}
*/

static inline bool sc_vnet_ring_full(struct sc_vnet_counter *counter)
{
	u32 rd, wr;

	wr = READ_ONCE(*counter->wr);
	rd = READ_ONCE(*counter->rd);

	return wr - rd >= SC_RING_COUNT;
}

static inline bool sc_vnet_free_ring_full(struct sc_vnet_counter *counter)
{
	u32 rd, wr;

	wr = READ_ONCE(*counter->wr);
	rd = READ_ONCE(*counter->rd);

	return wr - rd >= SC_FREE_RING_COUNT;
}


static inline bool sc_vnet_ring_empty(struct sc_vnet_counter *counter)
{
	u32 rd, wr;

	wr = READ_ONCE(*counter->wr);
	rd = READ_ONCE(*counter->rd);

	if (wr - rd > SC_RING_COUNT)
		return true;

	return wr == rd;
}


static inline void sc_vnet_ring_advance_rd(struct sc_vnet_counter *counter)
{
	WRITE_ONCE(*counter->rd, READ_ONCE(*counter->rd) + 1);

	/* BAR0 mmio address is wc mem, add mb to make sure cnts are updated */
	smp_mb(); //?
}


static inline u32 sc_vnet_ring_get_wr_cnt(struct sc_vnet_counter *counter)
{
	return READ_ONCE(*counter->wr);
}

static inline u32 sc_vnet_ring_get_rd_cnt(struct sc_vnet_counter *counter)
{
	return READ_ONCE(*counter->rd);
}


static inline void sc_vnet_ring_advance_wr(struct sc_vnet_counter *counter)
{
	WRITE_ONCE(*counter->wr, READ_ONCE(*counter->wr) + 1);

	/* BAR0 mmio address is wc mem, add mb to make sure cnts are updated */
	smp_mb(); //?
}

static inline u32 sc_vnet_ring_rd_available(struct sc_vnet_counter *counter)
{
	u32 rd, wr;

	wr = READ_ONCE(*counter->wr);
	rd = READ_ONCE(*counter->rd);

	return wr - rd;
}

static inline u32 sc_pci_vnet_endpoint_bar_readl(struct sc_pci_vnet *sc_pci_vnet_priv,
					      int bar, int offset)
{
	return readl(sc_pci_vnet_priv->mmio_bar5 + offset);
}

static inline void sc_pci_vnet_endpoint_bar_writel(struct sc_pci_vnet *sc_pci_vnet_priv,
						int bar, u32 offset, u32 value)
{
	writel(value, sc_pci_vnet_priv->mmio_bar5 + offset);
}



static void sc_pci_vnet_raise_ep_ctrl_irq(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	u32 val_int = sc_pci_vnet_endpoint_bar_readl(sc_pci_vnet_priv, BAR_5, 0x224);
	val_int |= 0x400;
	sc_pci_vnet_endpoint_bar_writel(sc_pci_vnet_priv, BAR_5, 0x224, val_int);
	sc_pci_epf_eth_debug_infos.raise_sgi0_counts++;
}

static void sc_pci_vnet_raise_ep_data_irq(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	u32 val_int = sc_pci_vnet_endpoint_bar_readl(sc_pci_vnet_priv, BAR_5, 0x224);
	val_int |= 0x800;
	sc_pci_vnet_endpoint_bar_writel(sc_pci_vnet_priv, BAR_5, 0x224, val_int);
}



static void sc_pci_vnet_clear_data_msg_counters(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf;
	struct host_own_cnt *host_cnt = host_ring_buf->host_cnt;

	zjl_dbg_pcie();

	host_cnt->host_empty_skb_rd_cnt = 0;
	host_cnt->host_empty_skb_wr_cnt = 0;
	host_cnt->h2ep_xfer_element_wr_cnt = 0;
	host_cnt->h2ep_xfer_element_rd_cnt = 0;
	
	host_cnt->host_skb_free_info_rd_cnt = 0;
	host_cnt->host_skb_free_info_wr_cnt = 0;
	sc_pci_epf_eth_debug_infos.host_netif_rx_hwirq_counts = 0;
#ifdef SC_PCI_ETH_PROC_FS
	sc_pci_epf_eth_debug_infos.napi_sirq_count = 0;
	sc_pci_epf_eth_debug_infos.work_done_per_sirq = 0;
	sc_pci_epf_eth_debug_infos.wait_to_poll_in_sirq = 0;
	sc_pci_epf_eth_debug_infos.sirq_delta0_time = 0;
	sc_pci_epf_eth_debug_infos.sirq_delta1_time = 0;
	sc_pci_epf_eth_debug_infos.sirq_delta2_time = 0;
	sc_pci_epf_eth_debug_infos.sirq_delta3_time = 0;
	sc_pci_epf_eth_debug_infos.sirq_delta4_time = 0;
	sc_pci_epf_eth_debug_infos.sirq_delta5_time = 0;
#endif
	sc_pci_epf_eth_debug_infos.xmit_counts = 0;
	sc_pci_epf_eth_debug_infos.drop_nomem_counts = 0;
	sc_pci_epf_eth_debug_infos.drop_free_slow_counts = 0;
	sc_pci_epf_eth_debug_infos.drop_ep_skb_empty_counts = 0;
	sc_pci_epf_eth_debug_infos.drop_h2ep_xfer_element_empty_counts = 0;
	sc_pci_epf_eth_debug_infos.drop_host_xmit_dma_map_err_counts = 0;
	sc_pci_epf_eth_debug_infos.ctrl_hirq_counts = 0;
	sc_pci_epf_eth_debug_infos.host_free_skb_counts = 0;
	sc_pci_epf_eth_debug_infos.host_free_skb_found_counts = 0;
	sc_pci_epf_eth_debug_infos.ep2h_xfer_element_counts = 0;
	sc_pci_epf_eth_debug_infos.host_netif_rx_counts = 0;
	sc_pci_epf_eth_debug_infos.host_netif_rx_found_counts = 0;
	sc_pci_epf_eth_debug_infos.raise_sgi0_counts = 0;

}


/*  host local recvie skb buffer
	vir_skb_addr: host local skb addr
	phy_skb_base: host local skb->data physical addr(pcie addr in rc)
	len:allocater buffer len
*/
static void sc_pci_vnet_alloc_empty_buffers(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct net_device *ndev = sc_pci_vnet_priv->ndev;
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf;
	struct host_empty_skb *host_empty_skb = host_ring_buf->host_empty_skbs;
	struct host_empty_list *host_empty_ptr;
	struct device *d = &sc_pci_vnet_priv->pdev->dev;
	
	zjl_dbg_pcie("start");

	while (!sc_vnet_ring_full(&sc_pci_vnet_priv->host_empty_skb_count)) {
		dma_addr_t host_skb_data_pa;
		struct sk_buff *skb;
		int len = GMAC_FRAME_LEN + NET_IP_ALIGN; //?
		unsigned long flags;
		u32 idx;

		skb = netdev_alloc_skb(ndev, len);
		if (!skb) {
			pr_err("%s: alloc skb failed\n", __func__);
			break;
		}

		skb_reserve(skb, NET_IP_ALIGN);

		host_skb_data_pa = dma_map_single(d, skb->data, len, DMA_FROM_DEVICE);//none dma may not need
		if (dma_mapping_error(d, host_skb_data_pa)) {
			pr_err("%s: dma map failed\n", __func__);
			dev_kfree_skb_any(skb);
			break;
		}

		host_empty_ptr = kmalloc(sizeof(*host_empty_ptr), GFP_KERNEL);
		if (!host_empty_ptr) {
			dma_unmap_single(d, host_skb_data_pa, len, DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			break;
		}

		host_empty_ptr->vir_skb_addr = skb;
		host_empty_ptr->phy_skb_base = host_skb_data_pa;
		host_empty_ptr->buffer_len = len;
		spin_lock_irqsave(&sc_pci_vnet_priv->host_empty_lock, flags);
		list_add_tail(&host_empty_ptr->list, &sc_pci_vnet_priv->host_empty_list);
		spin_unlock_irqrestore(&sc_pci_vnet_priv->host_empty_lock, flags);

		idx = sc_vnet_ring_get_wr_cnt(&sc_pci_vnet_priv->host_empty_skb_count) % SC_RING_COUNT;
		host_empty_skb[idx].phy_skb_base = host_skb_data_pa;
		host_empty_skb[idx].vir_skb_addr = skb;
		host_empty_skb[idx].buffer_len = len;
		zjl_dbg_pcie("idx:%u, phy_skb_base:0x%llx, vir_skb_addr:0x%llx, vir_skb_data_addr:0x%llx, buffer_len:0x%x", 
						idx, host_skb_data_pa, skb, skb->data, len);
		sc_vnet_ring_advance_wr(&sc_pci_vnet_priv->host_empty_skb_count);

		zjl_dbg_pcie("raise ctrl irq");
	    sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);

	};
	zjl_dbg_pcie("end");

}


static int sc_pci_vnet_write_ctrl_msg(struct sc_pci_vnet *sc_pci_vnet_priv, 
							struct h2ep_ctrl_msg *msg)
{
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf;
	struct h2ep_ctrl_msg *h2ep_ctrl_msg = host_ring_buf->h2ep_ctrl_msgs;
	u32 idx;

	zjl_dbg_pcie("start");

	if (sc_vnet_ring_full(&sc_pci_vnet_priv->h2ep_ctrl_msg_count)) {
		/* Raise an interrupt to let host process EP2H ring */
	    sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);
		pr_info("%s: EP2H ctrl ring full\n", __func__);
		return -EAGAIN;
	}

	idx = sc_vnet_ring_get_wr_cnt(&sc_pci_vnet_priv->h2ep_ctrl_msg_count) % SC_RING_COUNT;
	zjl_dbg_pcie("idx:%d", idx);
	memcpy(&h2ep_ctrl_msg[idx], msg, sizeof(*msg));
	/* BAR0 mmio address is wc mem, add mb to make sure ctrl msg is written
	 * before updating counters.
	 */
	mb();
	sc_vnet_ring_advance_wr(&sc_pci_vnet_priv->h2ep_ctrl_msg_count);
	sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);
	zjl_dbg_pcie("end");

	return 0;

}

static void sc_pci_vnet_read_ctrl_msg(struct sc_pci_vnet *sc_pci_vnet_priv,
				     struct ep2h_ctrl_msg *msg)
{
	struct ep_ring_buf *ep_ring_buf = &sc_pci_vnet_priv->ep_ring_buf;
	struct ep2h_ctrl_msg *ep2h_ctrl_msg = ep_ring_buf->ep2h_ctrl_msgs;
	u32 idx;

	if (sc_vnet_ring_empty(&sc_pci_vnet_priv->ep2h_ctrl_msg_count)) {
		pr_err("%s: EP2H ctrl ring is empty\n", __func__);
		return;
	}

	idx = sc_vnet_ring_get_rd_cnt(&sc_pci_vnet_priv->ep2h_ctrl_msg_count) % SC_RING_COUNT;
	memcpy(msg, &ep2h_ctrl_msg[idx], sizeof(*msg));
	sc_vnet_ring_advance_rd(&sc_pci_vnet_priv->ep2h_ctrl_msg_count);
}


static void sc_pci_vnet_update_link_state(struct net_device *ndev,
				    enum os_link_state state)
{
	if (state == OS_LINK_STATE_UP) {
		netif_start_queue(ndev);
		netif_carrier_on(ndev);
		zjl_dbg_pcie("up");
	} else if (state == OS_LINK_STATE_DOWN) {
		netif_carrier_off(ndev);
		netif_stop_queue(ndev);
		zjl_dbg_pcie("down");
	} else {
		pr_err("%s: invalid sate: %d\n", __func__, state);
	}

	pr_info("%s: link sate: %d\n", __func__, state);
}


/* OS link state machine */
static void sc_pci_vnet_update_link_sm(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct net_device *ndev = sc_pci_vnet_priv->ndev;
	enum os_link_state old_state = sc_pci_vnet_priv->os_link_state;

	zjl_dbg_pcie("local_link_state:0x%x, remote_link_state:0x%x", sc_pci_vnet_priv->local_link_state, sc_pci_vnet_priv->remote_link_state);

	if ((sc_pci_vnet_priv->local_link_state == DIR_LINK_STATE_UP) &&
		(sc_pci_vnet_priv->remote_link_state == DIR_LINK_STATE_UP))
		sc_pci_vnet_priv->os_link_state = OS_LINK_STATE_UP;
	else
		sc_pci_vnet_priv->os_link_state = OS_LINK_STATE_DOWN;

	if (sc_pci_vnet_priv->os_link_state != old_state)
		sc_pci_vnet_update_link_state(ndev, sc_pci_vnet_priv->os_link_state);

}

/* One way link state machine*/
static void sc_pci_vnet_user_link_up_req(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct h2ep_ctrl_msg msg;
	zjl_dbg_pcie("start");

	sc_pci_vnet_clear_data_msg_counters(sc_pci_vnet_priv);
	sc_pci_vnet_alloc_empty_buffers(sc_pci_vnet_priv);
	msg.msg_id = CTRL_MSG_LINK_UP;
	sc_pci_vnet_write_ctrl_msg(sc_pci_vnet_priv, &msg);
	sc_pci_vnet_priv->local_link_state = DIR_LINK_STATE_UP;
	sc_pci_vnet_update_link_sm(sc_pci_vnet_priv);
	
	zjl_dbg_pcie("end");
}


static int sc_pci_vnet_open(struct net_device *ndev)
{
	struct sc_pci_vnet *sc_pci_vnet_priv = netdev_priv(ndev);

	zjl_dbg_pcie("local_link_state:0x%x, remote_link_state:0x%x", sc_pci_vnet_priv->local_link_state, sc_pci_vnet_priv->remote_link_state);
	mutex_lock(&sc_pci_vnet_priv->link_state_lock);
	if (sc_pci_vnet_priv->local_link_state == DIR_LINK_STATE_DOWN)
		sc_pci_vnet_user_link_up_req(sc_pci_vnet_priv);
	napi_enable(&sc_pci_vnet_priv->napi);
	mutex_unlock(&sc_pci_vnet_priv->link_state_lock);
	zjl_dbg_pcie("success");

	return 0;
}

static void sc_pci_vnet_user_link_down_req(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct h2ep_ctrl_msg msg;

	sc_pci_vnet_priv->local_link_state = DIR_LINK_STATE_SENT_DOWN;
	msg.msg_id = CTRL_MSG_LINK_DOWN;
	sc_pci_vnet_write_ctrl_msg(sc_pci_vnet_priv, &msg);
	sc_pci_vnet_update_link_sm(sc_pci_vnet_priv);
}


static int sc_pci_vnet_close(struct net_device *ndev)
{
	struct sc_pci_vnet *sc_pci_vnet_priv = netdev_priv(ndev);
	int ret = 0;

	mutex_lock(&sc_pci_vnet_priv->link_state_lock);
	napi_disable(&sc_pci_vnet_priv->napi);
	if (sc_pci_vnet_priv->local_link_state == DIR_LINK_STATE_UP)
		sc_pci_vnet_user_link_down_req(sc_pci_vnet_priv);

	ret = wait_event_interruptible_timeout(sc_pci_vnet_priv->link_state_wq,
						   (sc_pci_vnet_priv->local_link_state ==
						   DIR_LINK_STATE_DOWN),
						   msecs_to_jiffies(SC_LINK_TIMEOUT));
	ret = (ret > 0) ? 0 : -ETIMEDOUT;
	if (ret < 0) {
		pr_err("%s: link state machine failed: tx_state: %d rx_state: %d err: %d",
			   __func__, sc_pci_vnet_priv->remote_link_state, sc_pci_vnet_priv->local_link_state,
			   ret);
		sc_pci_vnet_priv->local_link_state = DIR_LINK_STATE_UP;
	}

	
	mutex_unlock(&sc_pci_vnet_priv->link_state_lock);
	return 0;
}

static netdev_tx_t sc_pci_vnet_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct sc_pci_vnet *sc_pci_vnet_priv = netdev_priv(ndev);
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf;
	struct h2ep_xfer_element *h2ep_xfer_element = host_ring_buf->h2ep_xfer_elems;
	struct ep_ring_buf *ep_ring_buf = &sc_pci_vnet_priv->ep_ring_buf;
	struct ep_empty_skb *ep_empty_skb = ep_ring_buf->ep_empty_skbs;
	struct host_to_free_skb_list *host_to_free_skb_ptr;

	struct skb_shared_info *info = skb_shinfo(skb);
	struct device *d = &sc_pci_vnet_priv->pdev->dev;

	
	int len;
	dma_addr_t host_skb_data_pa;
	phys_addr_t ep_skb_data_pa;
	void __iomem *ep_skb_va;
	u32 rd_idx;
	u32 wr_idx;
	int ep_buffer_len;
	unsigned long flags;

	zjl_dbg_pcie("start xmit");
	
	sc_pci_epf_eth_debug_infos.xmit_counts++;
	/* TODO Not expecting skb frags, remove this after testing */
	WARN_ON(info->nr_frags);

	host_to_free_skb_ptr = kmalloc(sizeof(*host_to_free_skb_ptr), GFP_KERNEL);
	if (!host_to_free_skb_ptr) {
		pr_err("%s: No enough mem\n", __func__);
		sc_pci_epf_eth_debug_infos.drop_nomem_counts++;
		return NETDEV_TX_BUSY;
	}

	if (sc_vnet_free_ring_full(&sc_pci_vnet_priv->host_skb_free_info_count)) {
//		sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);
		pr_err("%s: host skb free too slow:%u\n", __func__, sc_vnet_ring_rd_available(&sc_pci_vnet_priv->host_skb_free_info_count));
		sc_pci_epf_eth_debug_infos.drop_free_slow_counts++;

//		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}
	
	/* Check if EP EMPTY SKB available to read */
	if (!sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep_empty_skb_count)) {
		sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);
		pr_err("%s: No EP empty skb, stop tx\n", __func__);
		sc_pci_epf_eth_debug_infos.drop_ep_skb_empty_counts++;
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	/* Check if H2EP xfer element buf available to write */
	if (sc_vnet_ring_full(&sc_pci_vnet_priv->h2ep_xfer_element_count)) {
//		sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);
		sc_pci_vnet_raise_ep_data_irq(sc_pci_vnet_priv);

		pr_err("%s: No H2EP xfer element buf, stop tx\n", __func__);
		sc_pci_epf_eth_debug_infos.drop_h2ep_xfer_element_empty_counts++;
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	len = MIN(skb->len, GMAC_FRAME_LEN - NET_IP_ALIGN);

	host_skb_data_pa = dma_map_single(d, skb->data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(d, host_skb_data_pa)) {
		pr_err("%s: dma_map_single failed\n", __func__);
		sc_pci_epf_eth_debug_infos.drop_host_xmit_dma_map_err_counts++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Get EP EMPTY SKB */
	rd_idx = sc_vnet_ring_get_rd_cnt(&sc_pci_vnet_priv->ep_empty_skb_count) %
				SC_RING_COUNT;
	ep_skb_data_pa = ep_empty_skb[rd_idx].phy_skb_base;
	ep_buffer_len = ep_empty_skb[rd_idx].buffer_len;
	ep_skb_va =  ep_empty_skb[rd_idx].vir_skb_addr;

	
	zjl_dbg_pcie("ep rd_idx:%u, ep_skb_data_pa:0x%llx, ep_skb_va:0x%llx, ep_buffer_len:0x%llx", rd_idx, ep_skb_data_pa, ep_skb_va, ep_buffer_len);
	/* Advance read count after all failure cases complated, to avoid
	 * dangling buffer at endpoint.
	 */
	sc_vnet_ring_advance_rd(&sc_pci_vnet_priv->ep_empty_skb_count);
	/* Raise an interrupt to let EP populate H2EP_EMPTY_BUF ring */
	if (sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep_empty_skb_count) < 20) {
		sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);
	}
	
	/* Push dst to H2EP full ring */
	wr_idx = sc_vnet_ring_get_wr_cnt(&sc_pci_vnet_priv->h2ep_xfer_element_count) %
				SC_RING_COUNT;
	h2ep_xfer_element[wr_idx].xfer_len = len;
	h2ep_xfer_element[wr_idx].src_pcie_addr = host_skb_data_pa;
	h2ep_xfer_element[wr_idx].src_vir_skb_addr = skb;
	h2ep_xfer_element[wr_idx].dst_phy_skb_base = ep_skb_data_pa;
	h2ep_xfer_element[wr_idx].dst_vir_skb_addr = ep_skb_va;
	/* BAR0 mmio address is wc mem, add mb to make sure that full
	 * buffer is written before updating counters.
	 */

	/* to avoid panic because of free too early */
	host_to_free_skb_ptr->phy_skb_data_base = host_skb_data_pa;
	host_to_free_skb_ptr->vir_skb_addr = skb;
	host_to_free_skb_ptr->xfer_len = len;
	
	zjl_dbg_pcie("wait free:host_skb_data_pa:0x%llx, skb:0x%llx, len:0x%llx", 
		host_to_free_skb_ptr->phy_skb_data_base, 
		host_to_free_skb_ptr->vir_skb_addr, 
		host_to_free_skb_ptr->xfer_len);
	
	spin_lock_irqsave(&sc_pci_vnet_priv->host_to_free_skb_lock, flags);
	list_add_tail(&host_to_free_skb_ptr->list, &sc_pci_vnet_priv->host_to_free_skb_list);
	spin_unlock_irqrestore(&sc_pci_vnet_priv->host_to_free_skb_lock, flags);

	ndev->stats.tx_bytes 		+= len;
	ndev->stats.tx_packets++;
	
	mb();
	sc_vnet_ring_advance_wr(&sc_pci_vnet_priv->h2ep_xfer_element_count);
	sc_pci_vnet_raise_ep_data_irq(sc_pci_vnet_priv);

	

	/* Free skb */
//	dma_unmap_single(d, src_iova, len, DMA_TO_DEVICE);
//	dev_kfree_skb_any(skb);
#if 0
	host_to_free_skb_ptr->phy_skb_data_base = host_skb_data_pa;
	host_to_free_skb_ptr->vir_skb_addr = skb;
	host_to_free_skb_ptr->xfer_len = len;
	
	zjl_dbg_pcie("wait free:host_skb_data_pa:0x%llx, skb:0x%llx, len:0x%llx", 
		host_to_free_skb_ptr->phy_skb_data_base, 
		host_to_free_skb_ptr->vir_skb_addr, 
		host_to_free_skb_ptr->xfer_len);
	spin_lock_irqsave(&sc_pci_vnet_priv->host_to_free_skb_lock, flags);
	list_add_tail(&host_to_free_skb_ptr->list, &sc_pci_vnet_priv->host_to_free_skb_list);
	spin_unlock_irqrestore(&sc_pci_vnet_priv->host_to_free_skb_lock, flags);

	ndev->stats.tx_bytes 		+= len;
	ndev->stats.tx_packets++;
#endif

	zjl_dbg_pcie("end xmit");


	return NETDEV_TX_OK;
}


static int sc_pci_vnet_change_mtu(struct net_device *ndev, int new_mtu)
{
	return 0;
}




static const struct net_device_ops sc_pci_vnet_netdev_ops = {
	.ndo_open = sc_pci_vnet_open,
	.ndo_stop = sc_pci_vnet_close,
	.ndo_start_xmit	= sc_pci_vnet_start_xmit,
	.ndo_change_mtu = sc_pci_vnet_change_mtu,
};


/*  host local recvie skb buffer
	vir_skb_addr: host local skb addr
	phy_skb_base: host local skb->data physical addr(pcie addr in rc)
	len:allocater buffer len

	
*/

u64 the_same_counts = 0;
u64 the_last_stop_time = -1;
u64 the_current_stop_time = 0;

static int sc_pci_vnet_process_ep2h_msg(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct ep_ring_buf *ep_ring_buf = &sc_pci_vnet_priv->ep_ring_buf;
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf;
	struct ep2h_xfer_element *ep2h_xfer_elem = ep_ring_buf->ep2h_xfer_elems;
	struct device *d = &sc_pci_vnet_priv->pdev->dev;
	struct host_empty_list *host_empty_ptr;
	struct net_device *ndev = sc_pci_vnet_priv->ndev;
	int count = 0;

#ifdef SC_PCI_ETH_PROC_FS
	u64 end_sirq, mid0, mid1, mid2, mid3, mid4, mid5;
	u64 start_sirq = local_clock();
	sc_pci_epf_eth_debug_infos.napi_sirq_count++;
#endif


	while ((count < SC_VNET_NAPI_WEIGHT) &&
	       (sc_pci_epf_eth_debug_infos.ep2h_xfer_element_counts = sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep2h_xfer_element_count))) {
		struct sk_buff *skb;
		u64 host_skb_data_pa;
		void __iomem *host_skb_va;
		u32 xfer_len;
		int idx, found = 0;
		unsigned long flags;

		the_current_stop_time = host_ring_buf->end->xfer_len;
		if (the_last_stop_time != the_current_stop_time) {
			printk("last_stop:%llu, times:%llu\n", the_last_stop_time, the_same_counts);
			the_same_counts = 0;
			the_last_stop_time = the_current_stop_time;
		}
		the_same_counts++;

		sc_pci_epf_eth_debug_infos.host_netif_rx_counts++;

#ifdef SC_PCI_ETH_PROC_FS
		mid0 = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_delta0_time = mid0 - start_sirq;
#endif

		/* Read EP2H full msg */
		idx = sc_vnet_ring_get_rd_cnt(&sc_pci_vnet_priv->ep2h_xfer_element_count) %
					SC_RING_COUNT;
		xfer_len = ep2h_xfer_elem[idx].xfer_len;
		host_skb_data_pa = ep2h_xfer_elem[idx].dst_pcie_addr;
		host_skb_va = ep2h_xfer_elem[idx].host_skb_va;

		zjl_dbg_pcie("idx:%d, xfer_len:0x%x, host_skb_data_pa:0x%llx, host_skb_va:0x%llx",
						idx, xfer_len, host_skb_data_pa, host_skb_va);
#ifdef SC_PCI_ETH_PROC_FS
		mid1 = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_delta1_time = mid1 - mid0;
#endif

		spin_lock_irqsave(&sc_pci_vnet_priv->host_empty_lock, flags);
		list_for_each_entry(host_empty_ptr, &sc_pci_vnet_priv->host_empty_list,
				    list) {
			if (host_empty_ptr->phy_skb_base == host_skb_data_pa) {
				sc_pci_epf_eth_debug_infos.host_netif_rx_found_counts++;
				found = 1;
				break;
			}
		}
		zjl_dbg_pcie("found:%d", found);
		WARN_ON(!found);
		list_del(&host_empty_ptr->list);
		spin_unlock_irqrestore(&sc_pci_vnet_priv->host_empty_lock, flags);

#ifdef SC_PCI_ETH_PROC_FS
		mid2 = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_delta2_time = mid2 - mid1;
#endif


		/* Advance H2EP full buffer after search in local list */
		sc_vnet_ring_advance_rd(&sc_pci_vnet_priv->ep2h_xfer_element_count);
		/* If EP2H network queue is stopped due to lack of EP2H_FULL
		 * queue, raising ctrl irq will help.
		 */
#ifdef SC_PCI_ETH_PROC_FS
		mid3 = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_delta3_time = mid3 - mid2;
#endif
		sc_pci_vnet_raise_ep_ctrl_irq(sc_pci_vnet_priv);

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += xfer_len;

#ifdef SC_PCI_ETH_PROC_FS
		mid4 = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_delta4_time = mid4 - mid3;
#endif


		dma_unmap_single(d, host_skb_data_pa, GMAC_FRAME_LEN + NET_IP_ALIGN, DMA_FROM_DEVICE);
		skb = (struct sk_buff *)host_skb_va;
		skb_put(skb, xfer_len);
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&sc_pci_vnet_priv->napi, skb);

		/* Free EP2H empty list element */
		kfree(host_empty_ptr);
		count++;
#ifdef SC_PCI_ETH_PROC_FS
		mid5 = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_delta5_time = mid5 - mid4;
#endif
	}

#ifdef SC_PCI_ETH_PROC_FS
		end_sirq = local_clock();
		sc_pci_epf_eth_debug_infos.sirq_eaten_time= end_sirq - start_sirq;
#endif


	return count;
}


static int sc_pci_vnet_poll(struct napi_struct *napi, int budget)
{

	struct sc_pci_vnet *sc_pci_vnet_priv = container_of(napi, struct sc_pci_vnet, napi);
	int work_done;

	work_done = sc_pci_vnet_process_ep2h_msg(sc_pci_vnet_priv);
#ifdef SC_PCI_ETH_PROC_FS
	sc_pci_epf_eth_debug_infos.work_done_per_sirq = work_done;
#endif
//	printk("work_done: %d budget: %d\n", work_done, budget);
//	trace_printk("work_done: %d budget: %d\n", work_done, budget);
	if (work_done < budget) {
		napi_complete(napi);
		enable_irq(pci_irq_vector(sc_pci_vnet_priv->pdev, 1));
//		mmiowb();
	}

	return work_done;
}

static void sc_pci_vnet_rcv_link_up_msg(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	sc_pci_vnet_priv->remote_link_state = DIR_LINK_STATE_UP;
	sc_pci_vnet_update_link_sm(sc_pci_vnet_priv);
}

static void sc_pci_vnet_stop_tx_queue(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct net_device *ndev = sc_pci_vnet_priv->ndev;
	
	netif_stop_queue(ndev);
	/* Get tx lock to make sure that there is no ongoing xmit */
	netif_tx_lock(ndev);
	netif_tx_unlock(ndev);

}





static void sc_pci_vnet_rcv_link_down_msg(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct h2ep_ctrl_msg h2ep_ctrl_msg;

	/* Stop using empty buffers of remote system */
	sc_pci_vnet_stop_tx_queue(sc_pci_vnet_priv);
	h2ep_ctrl_msg.msg_id = CTRL_MSG_LINK_DOWN_ACK;
	sc_pci_vnet_write_ctrl_msg(sc_pci_vnet_priv, &h2ep_ctrl_msg);
	sc_pci_vnet_priv->remote_link_state = DIR_LINK_STATE_DOWN;
	sc_pci_vnet_update_link_sm(sc_pci_vnet_priv);
}

static void sc_pci_vnet_free_empty_buffers(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct host_empty_list *host_empty_ptr, *temp;
	struct device *d = &sc_pci_vnet_priv->pdev->dev;
	unsigned long flags;
	int i = 0;

	zjl_dbg_pcie("start");

	spin_lock_irqsave(&sc_pci_vnet_priv->host_empty_lock, flags);
	list_for_each_entry_safe(host_empty_ptr, temp, &sc_pci_vnet_priv->host_empty_list,
				 list) {
		list_del(&host_empty_ptr->list);
		dma_unmap_single(d, host_empty_ptr->phy_skb_base, host_empty_ptr->buffer_len,
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(host_empty_ptr->vir_skb_addr);
		kfree(host_empty_ptr);
		i++;
	}
	spin_unlock_irqrestore(&sc_pci_vnet_priv->host_empty_lock, flags);
	
	zjl_dbg_pcie("end, count:%d", i);
}


static void sc_pci_vnet_rcv_link_down_ack(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	/* Stop using empty buffers(which are full in rx) of local system */
//	sc_pci_vnet_stop_rx_work(sc_pci_vnet_priv);
	sc_pci_vnet_free_empty_buffers(sc_pci_vnet_priv);
	sc_pci_vnet_priv->local_link_state = DIR_LINK_STATE_DOWN;
	wake_up_interruptible(&sc_pci_vnet_priv->link_state_wq);
	sc_pci_vnet_update_link_sm(sc_pci_vnet_priv);
}


static void sc_pci_vnet_process_ctrl_msg(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct ep2h_ctrl_msg ep2h_ctrl_msg;
	
	zjl_dbg_pcie("start");
	
	while (sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep2h_ctrl_msg_count)) {
		sc_pci_vnet_read_ctrl_msg(sc_pci_vnet_priv, &ep2h_ctrl_msg);

		zjl_dbg_pcie("msg_id:0x%x", ep2h_ctrl_msg.msg_id);
	
		if (ep2h_ctrl_msg.msg_id == CTRL_MSG_LINK_UP)
			sc_pci_vnet_rcv_link_up_msg(sc_pci_vnet_priv);
		else if (ep2h_ctrl_msg.msg_id == CTRL_MSG_LINK_DOWN)
			sc_pci_vnet_rcv_link_down_msg(sc_pci_vnet_priv);
		else if (ep2h_ctrl_msg.msg_id == CTRL_MSG_LINK_DOWN_ACK)
			sc_pci_vnet_rcv_link_down_ack(sc_pci_vnet_priv);
	}
	zjl_dbg_pcie("end");

}

static irqreturn_t sc_pci_vnet_irq_ctrl(int irq, void *data)
{
	struct net_device *ndev = data;
	struct sc_pci_vnet *sc_pci_vnet_priv = netdev_priv(ndev);

	zjl_dbg_pcie("irq:%d", irq);
	sc_pci_epf_eth_debug_infos.ctrl_hirq_counts++;

	if (netif_queue_stopped(ndev)) {
		if ((sc_pci_vnet_priv->os_link_state == OS_LINK_STATE_UP) &&
		    sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep_empty_skb_count) &&
		    !sc_vnet_ring_full(&sc_pci_vnet_priv->h2ep_xfer_element_count)) {
			pr_info("%s: wake net tx queue\n", __func__);
			zjl_dbg_pcie("wake net tx queue\n");
			netif_wake_queue(ndev);
		}
	}

	if (sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep2h_ctrl_msg_count))
		sc_pci_vnet_process_ctrl_msg(sc_pci_vnet_priv);

	if (!sc_vnet_ring_full(&sc_pci_vnet_priv->host_empty_skb_count) &&
	    (sc_pci_vnet_priv->os_link_state == OS_LINK_STATE_UP))
		sc_pci_vnet_alloc_empty_buffers(sc_pci_vnet_priv);

	return IRQ_HANDLED;
}

static irqreturn_t sc_pci_vnet_irq_data(int irq, void *data)
{
	struct net_device *ndev = data;
	struct sc_pci_vnet *sc_pci_vnet_priv = netdev_priv(ndev);
	u32 wait_to_poll_in_sirq;
	zjl_dbg_pcie("irq:%d", irq);

	sc_pci_epf_eth_debug_infos.host_netif_rx_hwirq_counts++;


	wait_to_poll_in_sirq = sc_vnet_ring_rd_available(&sc_pci_vnet_priv->ep2h_xfer_element_count);
	if (wait_to_poll_in_sirq) {
#ifdef SC_PCI_ETH_PROC_FS
		sc_pci_epf_eth_debug_infos.wait_to_poll_in_sirq  = wait_to_poll_in_sirq;
#endif
		disable_irq_nosync(pci_irq_vector(sc_pci_vnet_priv->pdev, 1));
		napi_schedule(&sc_pci_vnet_priv->napi);
	}

	return IRQ_HANDLED;
}

static irqreturn_t sc_pci_vnet_free_skb(int irq, void *data)
{
	struct net_device *ndev = data;
	struct sc_pci_vnet *sc_pci_vnet_priv = netdev_priv(ndev);
	
	zjl_dbg_pcie("irq:%d", irq);

	up(&sc_pci_vnet_priv->free_host_xmit_skb_sem);
	disable_irq_nosync(irq);

	zjl_dbg_pcie("up and disable irq");

	return IRQ_HANDLED;
}




static void sc_pci_vnet_setup_bar1_md(struct sc_pci_vnet *sc_pci_vnet_priv)
{
	struct ep_ring_buf *ep_ring_buf = &sc_pci_vnet_priv->ep_ring_buf;
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf; 

	ep_ring_buf->ep_cnt = (struct ep_own_cnt *)sc_pci_vnet_priv->mmio_bar1;
	ep_ring_buf->ep_empty_skbs = (struct ep_empty_skb *)(ep_ring_buf->ep_cnt + 1);// 1 ep_own_cnt space
	ep_ring_buf->ep2h_xfer_elems = (struct ep2h_xfer_element *)(ep_ring_buf->ep_empty_skbs + SC_RING_COUNT);
	ep_ring_buf->ep2h_ctrl_msgs = (struct ep2h_ctrl_msg *)(ep_ring_buf->ep2h_xfer_elems + SC_RING_COUNT);
	ep_ring_buf->end = (struct ep2h_ctrl_msg *)(ep_ring_buf->ep2h_ctrl_msgs + SC_RING_COUNT);
	zjl_dbg_pcie("ep_cnt:0x%llx, ep_empty_skbs:0x%llx, ep2h_xfer_elems:0x%llx, ep2h_ctrl_msgs:0x%llx, end:0x%llx",
			ep_ring_buf->ep_cnt, ep_ring_buf->ep_empty_skbs, ep_ring_buf->ep2h_xfer_elems, ep_ring_buf->ep2h_ctrl_msgs, ep_ring_buf->end);
//	memset(ep_ring_buf->ep_cnt, 0, sizeof(struct ep_own_cnt));

	host_ring_buf->host_cnt = (struct host_own_cnt *)ep_ring_buf->end;
	host_ring_buf->host_empty_skbs = (struct host_empty_skb *)(host_ring_buf->host_cnt + 1);
	host_ring_buf->h2ep_xfer_elems = (struct h2ep_xfer_element *)(host_ring_buf->host_empty_skbs + SC_RING_COUNT);
	host_ring_buf->h2ep_ctrl_msgs = (struct h2ep_ctrl_msg *)(host_ring_buf->h2ep_xfer_elems + SC_RING_COUNT);
	host_ring_buf->host_skb_free_infos = (struct host_skb_free_info*)(host_ring_buf->h2ep_ctrl_msgs + SC_RING_COUNT);
	host_ring_buf->end = (struct host_skb_free_info *)(host_ring_buf->host_skb_free_infos + SC_FREE_RING_COUNT);
	zjl_dbg_pcie("host_cnt:0x%llx, host_empty_skbs:0x%llx, h2ep_xfer_elems:0x%llx, h2ep_ctrl_msgs:0x%llx, host_skb_free_infos:0x%llx, end:0x%llx",
			host_ring_buf->host_cnt, host_ring_buf->host_empty_skbs, host_ring_buf->h2ep_xfer_elems, host_ring_buf->h2ep_ctrl_msgs, host_ring_buf->host_skb_free_infos, host_ring_buf->end);
//	memset(host_ring_buf->host_cnt, 0, sizeof(struct host_own_cnt));

	sc_pci_vnet_priv->ep_empty_skb_count.rd = &ep_ring_buf->ep_cnt->ep_empty_skb_rd_cnt;
	sc_pci_vnet_priv->ep_empty_skb_count.wr = &ep_ring_buf->ep_cnt->ep_empty_skb_wr_cnt;
	sc_pci_vnet_priv->ep2h_xfer_element_count.rd  = &ep_ring_buf->ep_cnt->ep2h_xfer_element_rd_cnt;
	sc_pci_vnet_priv->ep2h_xfer_element_count.wr  = &ep_ring_buf->ep_cnt->ep2h_xfer_element_wr_cnt;
	sc_pci_vnet_priv->ep2h_ctrl_msg_count.rd = &ep_ring_buf->ep_cnt->ep2h_ctrl_msg_rd_cnt;
	sc_pci_vnet_priv->ep2h_ctrl_msg_count.wr = &ep_ring_buf->ep_cnt->ep2h_ctrl_msg_wr_cnt;
	
	sc_pci_vnet_priv->host_empty_skb_count.rd = &host_ring_buf->host_cnt->host_empty_skb_rd_cnt;
	sc_pci_vnet_priv->host_empty_skb_count.wr = &host_ring_buf->host_cnt->host_empty_skb_wr_cnt;
	sc_pci_vnet_priv->h2ep_xfer_element_count.rd = &host_ring_buf->host_cnt->h2ep_xfer_element_rd_cnt;
	sc_pci_vnet_priv->h2ep_xfer_element_count.wr = &host_ring_buf->host_cnt->h2ep_xfer_element_wr_cnt;
	sc_pci_vnet_priv->h2ep_ctrl_msg_count.rd = &host_ring_buf->host_cnt->h2ep_ctrl_msg_rd_cnt;
	sc_pci_vnet_priv->h2ep_ctrl_msg_count.wr = &host_ring_buf->host_cnt->h2ep_ctrl_msg_wr_cnt;
	
	sc_pci_vnet_priv->host_skb_free_info_count.rd = &host_ring_buf->host_cnt->host_skb_free_info_rd_cnt;
	sc_pci_vnet_priv->host_skb_free_info_count.wr = &host_ring_buf->host_cnt->host_skb_free_info_wr_cnt;
}

int sc_pci_vnet_free_xmit_skb(void *dev)
{
	struct sc_pci_vnet *sc_pci_vnet_priv = (struct sc_pci_vnet *)dev;
	struct device *d = &sc_pci_vnet_priv->pdev->dev;
	struct host_ring_buf *host_ring_buf = &sc_pci_vnet_priv->host_ring_buf;
	struct host_skb_free_info *host_skb_free_info = host_ring_buf->host_skb_free_infos;
	struct host_to_free_skb_list *host_to_free_skb_ptr;
	unsigned long flags;

	do {
		down(&sc_pci_vnet_priv->free_host_xmit_skb_sem);
		zjl_dbg_pcie("down");
		while (sc_vnet_ring_rd_available(&sc_pci_vnet_priv->host_skb_free_info_count)) {
			int idx;
			void __iomem *host_skb_va;
			phys_addr_t host_skb_data_pa;
			u32 len;
			int found = 0;

			sc_pci_epf_eth_debug_infos.host_free_skb_counts++;

			idx = sc_vnet_ring_get_rd_cnt(&sc_pci_vnet_priv->host_skb_free_info_count) % SC_FREE_RING_COUNT;
			len = host_skb_free_info[idx].xfer_len;
			host_skb_va = host_skb_free_info[idx].host_skb_va;
			host_skb_data_pa = host_skb_free_info[idx].host_skb_data_pa;
			zjl_dbg_pcie("idx:%d, len:0x%x, host_skb_va:0x%llx, host_skb_data_pa:0x%llx",
				idx, len, host_skb_va, host_skb_data_pa);
			

			spin_lock_irqsave(&sc_pci_vnet_priv->host_to_free_skb_lock, flags);
			list_for_each_entry(host_to_free_skb_ptr, &sc_pci_vnet_priv->host_to_free_skb_list,
						list) {
				if (host_to_free_skb_ptr->vir_skb_addr == host_skb_va) {
					found = 1;
					sc_pci_epf_eth_debug_infos.host_free_skb_found_counts++;
					zjl_dbg_pcie("found");
					break;
				}
			}
			WARN_ON(!found);
			list_del(&host_to_free_skb_ptr->list);
			spin_unlock_irqrestore(&sc_pci_vnet_priv->host_to_free_skb_lock, flags);
			sc_vnet_ring_advance_rd(&sc_pci_vnet_priv->host_skb_free_info_count);

			zjl_dbg_pcie("free skb");
			dma_unmap_single(d, host_skb_data_pa, len, DMA_TO_DEVICE);
			dev_kfree_skb_any(host_skb_va);

			kfree(host_to_free_skb_ptr);

		};

    	enable_irq(pci_irq_vector(sc_pci_vnet_priv->pdev, 2));
//		sc_pci_vnet_priv->flag_in_free_skb = 0;

		zjl_dbg_pcie("enable irq");
	} while(1);

	return 0;
	
}


static int sc_pci_epf_eth_xirq_count_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m,
				   "host_netif_rx_hwirq_counts:0x%llx\n"
#ifdef SC_PCI_ETH_PROC_FS		 
				   "rx sirq count:0x%llx\n"
				   "work_done_per_sirq:0x%llx\n"
				   "wait_to_poll_in_sirq:0x%llx\n"
				   "sirq_eaten_time:%llu\n"
				   "sirq_delta0_time:%llu\n"
				   "sirq_delta1_time:%llu\n"
				   "sirq_delta2_time:%llu\n"
				   "sirq_delta3_time:%llu\n"
				   "sirq_delta4_time:%llu\n"
				   "sirq_delta5_time:%llu\n"
#endif
				   "xmit_counts:%llu\n"
				   "drop_nomem_counts:%llu\n"
				   "drop_free_slow_counts:%llu\n"
				   "drop_ep_skb_empty_counts:%llu\n"
				   "drop_h2ep_xfer_element_empty_counts:%llu\n"
				   "drop_host_xmit_dma_map_err_counts:%llu\n"
				   "ctrl_hirq_counts:%llu\n"
				   "host_free_skb_counts:%llu\n"
				   "host_free_skb_found_counts:%llu\n"
				   "ep2h_xfer_element_counts:%llu\n"
				   "host_netif_rx_counts:%llu\n"
				   "host_netif_rx_found_counts:%llu\n"
				   "raise_sgi0_counts:%llu\n",
				   sc_pci_epf_eth_debug_infos.host_netif_rx_hwirq_counts,
#ifdef SC_PCI_ETH_PROC_FS		 
				   sc_pci_epf_eth_debug_infos.napi_sirq_count,
				   sc_pci_epf_eth_debug_infos.work_done_per_sirq,
				   sc_pci_epf_eth_debug_infos.wait_to_poll_in_sirq,
				   sc_pci_epf_eth_debug_infos.sirq_eaten_time,
				   sc_pci_epf_eth_debug_infos.sirq_delta0_time,
				   sc_pci_epf_eth_debug_infos.sirq_delta1_time,
				   sc_pci_epf_eth_debug_infos.sirq_delta2_time,
				   sc_pci_epf_eth_debug_infos.sirq_delta3_time,
				   sc_pci_epf_eth_debug_infos.sirq_delta4_time,
				   sc_pci_epf_eth_debug_infos.sirq_delta5_time,		   
#endif
				   sc_pci_epf_eth_debug_infos.xmit_counts,
				   sc_pci_epf_eth_debug_infos.drop_nomem_counts,
				   sc_pci_epf_eth_debug_infos.drop_free_slow_counts,
	               sc_pci_epf_eth_debug_infos.drop_ep_skb_empty_counts,
	               sc_pci_epf_eth_debug_infos.drop_h2ep_xfer_element_empty_counts,
	               sc_pci_epf_eth_debug_infos.drop_host_xmit_dma_map_err_counts,
	               sc_pci_epf_eth_debug_infos.ctrl_hirq_counts,
	               sc_pci_epf_eth_debug_infos.host_free_skb_counts,
	               sc_pci_epf_eth_debug_infos.host_free_skb_found_counts,
	               sc_pci_epf_eth_debug_infos.ep2h_xfer_element_counts,
				   sc_pci_epf_eth_debug_infos.host_netif_rx_counts,
				   sc_pci_epf_eth_debug_infos.host_netif_rx_found_counts,
				   sc_pci_epf_eth_debug_infos.raise_sgi0_counts);
		return 0;
}

static int sc_pci_epf_eth_proc_init(void)
{
	struct proc_dir_entry *base;
	
	base = proc_mkdir("sc_pci_epf_eth", NULL);
	if (!base) {
		zjl_dbg_pcie("mkdir epf eth proc failed");
		return -1;
	}

	proc_create_single("xirq_count", 0, base, sc_pci_epf_eth_xirq_count_proc_show);

	return 0;

}



int sc_pci_vnet_probe(struct pci_dev *pdev)
{
	struct sc_pci_pub *sc_pci_pub_priv = pci_get_drvdata(pdev);
	struct sc_pci_vnet *sc_pci_vnet_priv;
	struct net_device *ndev;
	int ret;
	
	ndev = alloc_etherdev(sizeof(struct sc_pci_vnet));
	if (!ndev) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "alloc_etherdev() failed");
		goto fail;
	}

	eth_hw_addr_random(ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &sc_pci_vnet_netdev_ops;
	sc_pci_vnet_priv = netdev_priv(ndev);
	sc_pci_vnet_priv->ndev = ndev;
	sc_pci_vnet_priv->pdev = pdev;

	/* get pci resource */
	sc_pci_vnet_priv->mmio_bar1 = pci_ioremap_bar(pdev, BAR_1);/* ctrl space */
	if (!sc_pci_vnet_priv->mmio_bar1) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Failed to read BAR%d\n", BAR_1);
		goto pci_disable;
	}
	zjl_dbg_pcie("bar%d:base:0x%llx, size:%x",
					BAR_1, sc_pci_vnet_priv->mmio_bar1, pci_resource_len(pdev, BAR_1));

	sc_pci_vnet_priv->mmio_bar5 = sc_pci_pub_priv->mmio_bar5; /* int space */


	/* set bar0 meta data */
	sc_pci_vnet_setup_bar1_md(sc_pci_vnet_priv);
	

	netif_napi_add(ndev, &sc_pci_vnet_priv->napi, sc_pci_vnet_poll, SC_VNET_NAPI_WEIGHT);

	ndev->mtu = 1500;

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "register_netdev() fail: %d\n", ret);
		goto pci_disable;
	}
	netif_carrier_off(ndev);
	/* link status */
	sc_pci_vnet_priv->local_link_state = DIR_LINK_STATE_DOWN;
	sc_pci_vnet_priv->remote_link_state = DIR_LINK_STATE_DOWN;
	sc_pci_vnet_priv->os_link_state = OS_LINK_STATE_DOWN;
	mutex_init(&sc_pci_vnet_priv->link_state_lock);
	init_waitqueue_head(&sc_pci_vnet_priv->link_state_wq);

	
	ret = request_irq(pci_irq_vector(pdev, 0), sc_pci_vnet_irq_ctrl, 0,
				  ndev->name, ndev);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq() fail: %d\n", ret);
			goto disable_msi;
		}

		ret = request_irq(pci_irq_vector(pdev, 1), sc_pci_vnet_irq_data, 0,
				  ndev->name, ndev);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq() fail: %d\n", ret);
			goto fail_request_irq_ctrl;
		}

		ret = request_irq(pci_irq_vector(pdev, 2), sc_pci_vnet_free_skb, 0,
				  ndev->name, ndev);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq() fail: %d\n", ret);
			goto fail_request_irq_ctrl;
		}

		INIT_LIST_HEAD(&sc_pci_vnet_priv->host_empty_list);
		spin_lock_init(&sc_pci_vnet_priv->host_empty_lock);

		INIT_LIST_HEAD(&sc_pci_vnet_priv->host_to_free_skb_list);
		spin_lock_init(&sc_pci_vnet_priv->host_to_free_skb_lock);
		skb_queue_head_init(&sc_pci_vnet_priv->tx_queue);
		sema_init(&sc_pci_vnet_priv->free_host_xmit_skb_sem, 0);
		sc_pci_vnet_priv->sc_pci_vnet_free_worker = kthread_create(sc_pci_vnet_free_xmit_skb, (void *)sc_pci_vnet_priv, "sc_pci_vnet_free_worker");
		wake_up_process(sc_pci_vnet_priv->sc_pci_vnet_free_worker);

#ifdef SC_PCI_ETH_PROC_FS
		ret = sc_pci_epf_eth_proc_init();
		if (ret < 0) {
			dev_err(&pdev->dev, "sc_pci_epf_eth_proc_init() failed: %d\n", ret);
			goto fail_request_irq_ctrl;
		}
#endif

	return 0;

fail_request_irq_ctrl:
	free_irq(pci_irq_vector(pdev, 0), ndev);
disable_msi:
	pci_free_irq_vectors(pdev);
	unregister_netdev(ndev);
pci_disable:
	netif_napi_del(&sc_pci_vnet_priv->napi);
	pci_disable_device(pdev);	
	free_netdev(ndev);
fail:
	return ret;

}
EXPORT_SYMBOL_GPL(sc_pci_vnet_probe);


