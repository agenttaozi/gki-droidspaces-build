// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012-2015 Spreadtrum Communications Inc.
 */
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/sprd-dma.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_wakeup.h>
static struct wakeup_source *zsk_ztp_wakelock;
/* device name */
#define UART_NR_MAX		8
#define ZSK_TTY_NAME		"ttyS"
#define ZSK_FIFO_SIZE		128
#define ZSK_DEF_RATE		26000000
#define ZSK_BAUD_IO_LIMIT	3000000
#define ZSK_TIMEOUT		256000
/* the offset of serial registers and BITs for them */
/* data registers */
#define ZSK_TXD		0x0000
#define ZSK_RXD		0x0004
/* line status register and its BITs  */
#define ZSK_LSR		0x0008
#define ZSK_LSR_OE		BIT(4)
#define ZSK_LSR_FE		BIT(3)
#define ZSK_LSR_PE		BIT(2)
#define ZSK_LSR_BI		BIT(7)
#define ZSK_LSR_TX_OVER	BIT(15)
/* data number in TX and RX fifo */
#define ZSK_STS1		0x000C
#define ZSK_RX_FIFO_CNT_MASK	GENMASK(7, 0)
#define ZSK_TX_FIFO_CNT_MASK	GENMASK(15, 8)
/* interrupt enable register and its BITs */
#define ZSK_IEN		0x0010
#define ZSK_IEN_RX_FULL	BIT(0)
#define ZSK_IEN_TX_EMPTY	BIT(1)
#define ZSK_IEN_BREAK_DETECT	BIT(7)
#define ZSK_IEN_TIMEOUT	BIT(13)
/* interrupt clear register */
#define ZSK_ICLR		0x0014
#define ZSK_ICLR_TIMEOUT	BIT(13)
/* line control register */
#define ZSK_LCR		0x0018
#define ZSK_LCR_STOP_1BIT	0x10
#define ZSK_LCR_STOP_2BIT	0x30
#define ZSK_LCR_DATA_LEN	(BIT(2) | BIT(3))
#define ZSK_LCR_DATA_LEN5	0x0
#define ZSK_LCR_DATA_LEN6	0x4
#define ZSK_LCR_DATA_LEN7	0x8
#define ZSK_LCR_DATA_LEN8	0xc
#define ZSK_LCR_PARITY		(BIT(0) | BIT(1))
#define ZSK_LCR_PARITY_EN	0x2
#define ZSK_LCR_EVEN_PAR	0x0
#define ZSK_LCR_ODD_PAR	0x1
/* control register 1 */
#define ZSK_CTL1		0x001C
#define ZSK_DMA_EN		BIT(15)
#define ZSK_LOOPBACK_EN	BIT(14)
#define RX_HW_FLOW_CTL_THLD	BIT(6)
#define RX_HW_FLOW_CTL_EN	BIT(7)
#define TX_HW_FLOW_CTL_EN	BIT(8)
#define RX_TOUT_THLD_DEF	0x3E00
#define RX_HFC_THLD_DEF		0x40
/* fifo threshold register */
#define ZSK_CTL2		0x0020
#define THLD_TX_EMPTY		0x40
#define THLD_TX_EMPTY_SHIFT	8
#define THLD_RX_FULL		0x40
#define THLD_RX_FULL_MASK	GENMASK(6, 0)
/* config baud rate register */
#define ZSK_CLKD0		0x0024
#define ZSK_CLKD0_MASK		GENMASK(15, 0)
#define ZSK_CLKD1		0x0028
#define ZSK_CLKD1_MASK		GENMASK(20, 16)
#define ZSK_CLKD1_SHIFT	16
/* interrupt mask status register */
#define ZSK_IMSR		0x002C
#define ZSK_IMSR_RX_FIFO_FULL	BIT(0)
#define ZSK_IMSR_TX_FIFO_EMPTY	BIT(1)
#define ZSK_IMSR_BREAK_DETECT	BIT(7)
#define ZSK_IMSR_TIMEOUT	BIT(13)
#define ZSK_DEFAULT_SOURCE_CLK	26000000
#define ZSK_RX_DMA_STEP	1
#define ZSK_RX_FIFO_FULL	1
#define ZSK_TX_FIFO_FULL	0x20
#define ZSK_UART_RX_SIZE	(UART_XMIT_SIZE / 4)
struct zsk_uart_dma {
	struct dma_chan *chn;
	unsigned char *virt;
	dma_addr_t phys_addr;
	dma_cookie_t cookie;
	u32 trans_len;
	bool enable;
};
struct zsk_uart_port {
	struct uart_port port;
	char name[16];
	struct clk *clk;
	struct zsk_uart_dma tx_dma;
	struct zsk_uart_dma rx_dma;
	dma_addr_t pos;
	unsigned char *rx_buf_tail;
};
static struct zsk_uart_port *zsk_port[UART_NR_MAX];
static int zsk_ports_num;
static int zsk_start_dma_rx(struct uart_port *port);
static int zsk_tx_dma_config(struct uart_port *port);
static inline unsigned int serial_in(struct uart_port *port,
				     unsigned int offset) {
	return readl_relaxed(port->membase + offset);
}
static inline void serial_out(struct uart_port *port, unsigned int offset,
			      int value) {
	writel_relaxed(value, port->membase + offset);
}
static unsigned int zsk_tx_empty(struct uart_port *port) {
	if (serial_in(port, ZSK_STS1) & ZSK_TX_FIFO_CNT_MASK)
		return 0;
	else
		return TIOCSER_TEMT;
}
static unsigned int zsk_get_mctrl(struct uart_port *port) {
	return TIOCM_DSR | TIOCM_CTS;
}
static void zsk_set_mctrl(struct uart_port *port, unsigned int mctrl) {
	u32 val = serial_in(port, ZSK_CTL1);

	if (mctrl & TIOCM_LOOP)
		val |= ZSK_LOOPBACK_EN;
	else
		val &= ~ZSK_LOOPBACK_EN;
	serial_out(port, ZSK_CTL1, val);
}
static void zsk_stop_rx(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	unsigned int ien, iclr;

	if (sp->rx_dma.enable)
		dmaengine_terminate_all(sp->rx_dma.chn);
	iclr = serial_in(port, ZSK_ICLR);
	ien = serial_in(port, ZSK_IEN);
	ien &= ~(ZSK_IEN_RX_FULL | ZSK_IEN_BREAK_DETECT);
	iclr |= ZSK_IEN_RX_FULL | ZSK_IEN_BREAK_DETECT;
	serial_out(port, ZSK_IEN, ien);
	serial_out(port, ZSK_ICLR, iclr);
}
static void zsk_uart_dma_enable(struct uart_port *port, bool enable) {
	u32 val = serial_in(port, ZSK_CTL1);

	if (enable)
		val |= ZSK_DMA_EN;
	else
		val &= ~ZSK_DMA_EN;
	serial_out(port, ZSK_CTL1, val);
}
static void zsk_stop_tx_dma(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;
	struct dma_tx_state state;
	u32 trans_len;

	dmaengine_pause(sp->tx_dma.chn);
	dmaengine_tx_status(sp->tx_dma.chn, sp->tx_dma.cookie, &state);
	if (state.residue) {
		trans_len = state.residue - sp->tx_dma.phys_addr;
		xmit->tail = (xmit->tail + trans_len) & (UART_XMIT_SIZE - 1);
		port->icount.tx += trans_len;
		dma_unmap_single(port->dev, sp->tx_dma.phys_addr,
				 sp->tx_dma.trans_len, DMA_TO_DEVICE);
	}
	dmaengine_terminate_all(sp->tx_dma.chn);
	sp->tx_dma.trans_len = 0;
}
static int zsk_tx_buf_remap(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;

	sp->tx_dma.trans_len =
		CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
	sp->tx_dma.phys_addr = dma_map_single(port->dev,
					      (void *)&(xmit->buf[xmit->tail]),
					      sp->tx_dma.trans_len,
					      DMA_TO_DEVICE);
	return dma_mapping_error(port->dev, sp->tx_dma.phys_addr);
}
static void zsk_complete_tx_dma(void *data) {
	struct uart_port *port = (struct uart_port *)data;
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	dma_unmap_single(port->dev, sp->tx_dma.phys_addr,
			 sp->tx_dma.trans_len, DMA_TO_DEVICE);
	xmit->tail = (xmit->tail + sp->tx_dma.trans_len) & (UART_XMIT_SIZE - 1);
	port->icount.tx += sp->tx_dma.trans_len;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (uart_circ_empty(xmit) || zsk_tx_buf_remap(port) ||
	    zsk_tx_dma_config(port))
		sp->tx_dma.trans_len = 0;
	spin_unlock_irqrestore(&port->lock, flags);
}
static int zsk_uart_dma_submit(struct uart_port *port,
				struct zsk_uart_dma *ud, u32 trans_len,
				enum dma_transfer_direction direction,
				dma_async_tx_callback callback) {
	struct dma_async_tx_descriptor *dma_des;
	unsigned long flags;

	flags = SPRD_DMA_FLAGS(SPRD_DMA_CHN_MODE_NONE,
			       SPRD_DMA_NO_TRG,
			       SPRD_DMA_FRAG_REQ,
			       SPRD_DMA_TRANS_INT);

	dma_des = dmaengine_prep_slave_single(ud->chn, ud->phys_addr, trans_len,
					      direction, flags);
	if (!dma_des)
		return -ENODEV;
	dma_des->callback = callback;
	dma_des->callback_param = port;
	ud->cookie = dmaengine_submit(dma_des);
	if (dma_submit_error(ud->cookie))
		return dma_submit_error(ud->cookie);
	dma_async_issue_pending(ud->chn);
	return 0;
}
static int zsk_tx_dma_config(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	u32 burst = sp->tx_dma.trans_len > ZSK_TX_FIFO_FULL ?
		ZSK_TX_FIFO_FULL : sp->tx_dma.trans_len;
	int ret;
	struct dma_slave_config cfg = {
		.dst_addr = port->mapbase + ZSK_TXD,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.src_maxburst = burst,
	};
	ret = dmaengine_slave_config(sp->tx_dma.chn, &cfg);
	if (ret < 0)
		return ret;
	return zsk_uart_dma_submit(port, &sp->tx_dma, sp->tx_dma.trans_len,
				    DMA_MEM_TO_DEV, zsk_complete_tx_dma);
}
static void zsk_start_tx_dma(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		serial_out(port, ZSK_TXD, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		zsk_stop_tx_dma(port);
		return;
	}
	if (sp->tx_dma.trans_len)
		return;
	if (zsk_tx_buf_remap(port) || zsk_tx_dma_config(port))
		sp->tx_dma.trans_len = 0;
}
static void zsk_rx_full_thld(struct uart_port *port, u32 thld) {
	u32 val = serial_in(port, ZSK_CTL2);

	val &= ~THLD_RX_FULL_MASK;
	val |= thld & THLD_RX_FULL_MASK;
	serial_out(port, ZSK_CTL2, val);
}
static int zsk_rx_alloc_buf(struct zsk_uart_port *sp) {
	sp->rx_dma.virt = dma_alloc_coherent(sp->port.dev, ZSK_UART_RX_SIZE,
					     &sp->rx_dma.phys_addr, GFP_KERNEL);
	if (!sp->rx_dma.virt)
		return -ENOMEM;
	return 0;
}
static void zsk_rx_free_buf(struct zsk_uart_port *sp) {
	if (sp->rx_dma.virt)
		dma_free_coherent(sp->port.dev, ZSK_UART_RX_SIZE,
				  sp->rx_dma.virt, sp->rx_dma.phys_addr);

}
static int zsk_rx_dma_config(struct uart_port *port, u32 burst) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct dma_slave_config cfg = {
		.src_addr = port->mapbase + ZSK_RXD,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.src_maxburst = burst,
	};
	return dmaengine_slave_config(sp->rx_dma.chn, &cfg);
}
static void zsk_uart_dma_rx(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct tty_port *tty = &port->state->port;

	port->icount.rx += sp->rx_dma.trans_len;
	tty_insert_flip_string(tty, sp->rx_buf_tail, sp->rx_dma.trans_len);
	tty_flip_buffer_push(tty);
}
static void zsk_uart_dma_irq(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(sp->rx_dma.chn,
				     sp->rx_dma.cookie, &state);
	if (status == DMA_ERROR)
		zsk_stop_rx(port);
	if (!state.residue && sp->pos == sp->rx_dma.phys_addr)
		return;
	if (!state.residue) {
		sp->rx_dma.trans_len = ZSK_UART_RX_SIZE +
			sp->rx_dma.phys_addr - sp->pos;
		sp->pos = sp->rx_dma.phys_addr;
	} else {
		sp->rx_dma.trans_len = state.residue - sp->pos;
		sp->pos = state.residue;
	}
	zsk_uart_dma_rx(port);
	sp->rx_buf_tail += sp->rx_dma.trans_len;
}
static void zsk_complete_rx_dma(void *data) {
	struct uart_port *port = (struct uart_port *)data;
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	struct dma_tx_state state;
	enum dma_status status;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	status = dmaengine_tx_status(sp->rx_dma.chn,
				     sp->rx_dma.cookie, &state);
	if (status != DMA_COMPLETE) {
		zsk_stop_rx(port);
		spin_unlock_irqrestore(&port->lock, flags);
		return;
	}
	if (sp->pos != sp->rx_dma.phys_addr) {
		sp->rx_dma.trans_len =  ZSK_UART_RX_SIZE +
			sp->rx_dma.phys_addr - sp->pos;
		zsk_uart_dma_rx(port);
		sp->rx_buf_tail += sp->rx_dma.trans_len;
	}
	if (zsk_start_dma_rx(port))
		zsk_stop_rx(port);
	spin_unlock_irqrestore(&port->lock, flags);
}
static int zsk_start_dma_rx(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);
	int ret;

	if (!sp->rx_dma.enable)
		return 0;
	sp->pos = sp->rx_dma.phys_addr;
	sp->rx_buf_tail = sp->rx_dma.virt;
	zsk_rx_full_thld(port, ZSK_RX_FIFO_FULL);
	ret = zsk_rx_dma_config(port, ZSK_RX_DMA_STEP);
	if (ret)
		return ret;
	return zsk_uart_dma_submit(port, &sp->rx_dma, ZSK_UART_RX_SIZE,
				    DMA_DEV_TO_MEM, zsk_complete_rx_dma);
}
static void zsk_release_dma(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);

	zsk_uart_dma_enable(port, false);
	if (sp->rx_dma.enable)
		dma_release_channel(sp->rx_dma.chn);
	if (sp->tx_dma.enable)
		dma_release_channel(sp->tx_dma.chn);
	sp->tx_dma.enable = false;
	sp->rx_dma.enable = false;
}
static void zsk_request_dma(struct uart_port *port) {
	struct zsk_uart_port *sp =
		container_of(port, struct zsk_uart_port, port);

	sp->tx_dma.enable = true;
	sp->rx_dma.enable = true;
	sp->tx_dma.chn = dma_request_chan(port->dev, "tx");
	if (IS_ERR(sp->tx_dma.chn)) {
		dev_err(port->dev, "request TX DMA channel failed, ret = %ld\n",
			PTR_ERR(sp->tx_dma.chn));
		sp->tx_dma.enable = false;
	}
	sp->rx_dma.chn = dma_request_chan(port->dev, "rx");
	if (IS_ERR(sp->rx_dma.chn)) {
		dev_err(port->dev, "request RX DMA channel failed, ret = %ld\n",
			PTR_ERR(sp->rx_dma.chn));
		sp->rx_dma.enable = false;
	}
}
static void zsk_stop_tx(struct uart_port *port) {
	struct zsk_uart_port *sp = container_of(port, struct zsk_uart_port,
						 port);
	unsigned int ien, iclr;

	if (sp->tx_dma.enable) {
		zsk_stop_tx_dma(port);
		return;
	}
	iclr = serial_in(port, ZSK_ICLR);
	ien = serial_in(port, ZSK_IEN);
	iclr |= ZSK_IEN_TX_EMPTY;
	ien &= ~ZSK_IEN_TX_EMPTY;
	serial_out(port, ZSK_IEN, ien);
	serial_out(port, ZSK_ICLR, iclr);
}
static void zsk_start_tx(struct uart_port *port) {
	struct zsk_uart_port *sp = container_of(port, struct zsk_uart_port,
						 port);
	unsigned int ien;

	if (sp->tx_dma.enable) {
		zsk_start_tx_dma(port);
		return;
	}
	ien = serial_in(port, ZSK_IEN);
	if (!(ien & ZSK_IEN_TX_EMPTY)) {
		ien |= ZSK_IEN_TX_EMPTY;
		serial_out(port, ZSK_IEN, ien);
	}
}
/* The Zsk serial does not support this function. */
static void zsk_break_ctl(struct uart_port *port, int break_state) {
	/* nothing to do */
}
static int handle_lsr_errors(struct uart_port *port,
			     unsigned int *flag,
			     unsigned int *lsr) {
	int ret = 0;

	/* statistics */
	if (*lsr & ZSK_LSR_BI) {
		*lsr &= ~(ZSK_LSR_FE | ZSK_LSR_PE);
		port->icount.brk++;
		ret = uart_handle_break(port);
		if (ret)
			return ret;
	} else if (*lsr & ZSK_LSR_PE)
		port->icount.parity++;
	else if (*lsr & ZSK_LSR_FE)
		port->icount.frame++;
	if (*lsr & ZSK_LSR_OE)
		port->icount.overrun++;
	/* mask off conditions which should be ignored */
	*lsr &= port->read_status_mask;
	if (*lsr & ZSK_LSR_BI)
		*flag = TTY_BREAK;
	else if (*lsr & ZSK_LSR_PE)
		*flag = TTY_PARITY;
	else if (*lsr & ZSK_LSR_FE)
		*flag = TTY_FRAME;
	return ret;
}
static void zte_clear_lsr_bit(struct uart_port *port, int lsr_bit) {
	unsigned int ien, iclr;

	iclr = serial_in(port, ZSK_ICLR);
	ien = serial_in(port, ZSK_IEN);
	ien &= ~(lsr_bit);
	iclr |= lsr_bit;
	serial_out(port, ZSK_IEN, ien);
	serial_out(port, ZSK_ICLR, iclr);
}
static inline void zsk_rx(struct uart_port *port) {
	struct zsk_uart_port *sp = container_of(port, struct zsk_uart_port,
						 port);
	struct tty_port *tty = &port->state->port;
	unsigned int ch, flag, lsr, max_count = ZSK_TIMEOUT;

	if (sp->rx_dma.enable) {
		zsk_uart_dma_irq(port);
		return;
	}
	while ((serial_in(port, ZSK_STS1) & ZSK_RX_FIFO_CNT_MASK) &&
	       max_count--) {
		lsr = serial_in(port, ZSK_LSR);
		ch = serial_in(port, ZSK_RXD);
		flag = TTY_NORMAL;
		port->icount.rx++;
		if (lsr & (ZSK_LSR_BI | ZSK_LSR_PE |
			   ZSK_LSR_FE | ZSK_LSR_OE))
			if (handle_lsr_errors(port, &flag, &lsr))
				continue;
		if ((strcmp(port->name, "ttyS2") == 0) && ((lsr & ZSK_LSR_OE) == ZSK_LSR_OE)) {
			zte_clear_lsr_bit(port, ZSK_LSR_OE);
			lsr &= ~(ZSK_LSR_OE);
		}
		if (uart_handle_sysrq_char(port, ch))
			continue;
		uart_insert_char(port, lsr, ZSK_LSR_OE, ch, flag);
	}
	tty_flip_buffer_push(tty);
}
static inline void zsk_tx(struct uart_port *port) {
	struct circ_buf *xmit = &port->state->xmit;
	int count;

	if (port->x_char) {
		serial_out(port, ZSK_TXD, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		zsk_stop_tx(port);
		return;
	}
	count = THLD_TX_EMPTY;
	do {
		serial_out(port, ZSK_TXD, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (uart_circ_empty(xmit))
		zsk_stop_tx(port);
}
/* this handles the interrupt from one port */
static irqreturn_t zsk_handle_irq(int irq, void *dev_id) {
	struct uart_port *port = dev_id;
	unsigned int ims;

	spin_lock(&port->lock);
	ims = serial_in(port, ZSK_IMSR);
	if (!ims) {
		spin_unlock(&port->lock);
		return IRQ_NONE;
	}
	if (ims & ZSK_IMSR_TIMEOUT)
		serial_out(port, ZSK_ICLR, ZSK_ICLR_TIMEOUT);
	if (ims & ZSK_IMSR_BREAK_DETECT)
		serial_out(port, ZSK_ICLR, ZSK_IMSR_BREAK_DETECT);
	if (ims & (ZSK_IMSR_RX_FIFO_FULL | ZSK_IMSR_BREAK_DETECT |
		   ZSK_IMSR_TIMEOUT))
		zsk_rx(port);
	if (ims & ZSK_IMSR_TX_FIFO_EMPTY)
		zsk_tx(port);
	spin_unlock(&port->lock);
	return IRQ_HANDLED;
}
static void zsk_uart_dma_startup(struct uart_port *port,
				  struct zsk_uart_port *sp) {
	int ret;

	zsk_request_dma(port);
	if (!(sp->rx_dma.enable || sp->tx_dma.enable))
		return;
	ret = zsk_start_dma_rx(port);
	if (ret) {
		sp->rx_dma.enable = false;
		dma_release_channel(sp->rx_dma.chn);
		dev_warn(port->dev, "fail to start RX dma mode\n");
	}
	zsk_uart_dma_enable(port, true);
}
static int zsk_startup(struct uart_port *port) {
	int ret = 0;
	unsigned int ien, fc;
	unsigned int timeout;
	struct zsk_uart_port *sp;
	unsigned long flags;

	serial_out(port, ZSK_CTL2,
		   THLD_TX_EMPTY << THLD_TX_EMPTY_SHIFT | THLD_RX_FULL);
	/* clear rx fifo */
	timeout = ZSK_TIMEOUT;
	while (timeout-- && serial_in(port, ZSK_STS1) & ZSK_RX_FIFO_CNT_MASK)
		serial_in(port, ZSK_RXD);
	/* clear tx fifo */
	timeout = ZSK_TIMEOUT;
	while (timeout-- && serial_in(port, ZSK_STS1) & ZSK_TX_FIFO_CNT_MASK)
		cpu_relax();
	/* clear interrupt */
	serial_out(port, ZSK_IEN, 0);
	serial_out(port, ZSK_ICLR, ~0);
	/* allocate irq */
	sp = container_of(port, struct zsk_uart_port, port);
	snprintf(sp->name, sizeof(sp->name), "zsk_serial%d", port->line);
	zsk_uart_dma_startup(port, sp);
	ret = devm_request_irq(port->dev, port->irq, zsk_handle_irq,
			       IRQF_SHARED, sp->name, port);
	if (ret) {
		dev_err(port->dev, "fail to request serial irq %d, ret=%d\n",
			port->irq, ret);
		return ret;
	}
	fc = serial_in(port, ZSK_CTL1);
	fc |= RX_TOUT_THLD_DEF | RX_HFC_THLD_DEF;
	serial_out(port, ZSK_CTL1, fc);
	/* enable interrupt */
	spin_lock_irqsave(&port->lock, flags);
	ien = serial_in(port, ZSK_IEN);
	ien |= ZSK_IEN_BREAK_DETECT | ZSK_IEN_TIMEOUT;
	if (!sp->rx_dma.enable)
		ien |= ZSK_IEN_RX_FULL;
	serial_out(port, ZSK_IEN, ien);
	spin_unlock_irqrestore(&port->lock, flags);
	return 0;
}
static void zsk_shutdown(struct uart_port *port) {
	zsk_release_dma(port);
	serial_out(port, ZSK_IEN, 0);
	serial_out(port, ZSK_ICLR, ~0);
	devm_free_irq(port->dev, port->irq, port);
}
static void zsk_set_termios(struct uart_port *port,
			     struct ktermios *termios,
			     struct ktermios *old) {
	unsigned int baud, quot;
	unsigned int lcr = 0, fc;
	unsigned long flags;

	/* ask the core to calculate the divisor for us */
	baud = uart_get_baud_rate(port, termios, old, 0, ZSK_BAUD_IO_LIMIT);
	quot = port->uartclk / baud;
	/* set data length */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr |= ZSK_LCR_DATA_LEN5;
		break;
	case CS6:
		lcr |= ZSK_LCR_DATA_LEN6;
		break;
	case CS7:
		lcr |= ZSK_LCR_DATA_LEN7;
		break;
	case CS8:
	default:
		lcr |= ZSK_LCR_DATA_LEN8;
		break;
	}
	/* calculate stop bits */
	lcr &= ~(ZSK_LCR_STOP_1BIT | ZSK_LCR_STOP_2BIT);
	if (termios->c_cflag & CSTOPB)
		lcr |= ZSK_LCR_STOP_2BIT;
	else
		lcr |= ZSK_LCR_STOP_1BIT;
	/* calculate parity */
	lcr &= ~ZSK_LCR_PARITY;
	termios->c_cflag &= ~CMSPAR;	/* no support mark/space */
	if (termios->c_cflag & PARENB) {
		lcr |= ZSK_LCR_PARITY_EN;
		if (termios->c_cflag & PARODD)
			lcr |= ZSK_LCR_ODD_PAR;
		else
			lcr |= ZSK_LCR_EVEN_PAR;
	}
	spin_lock_irqsave(&port->lock, flags);
	/* update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);
	port->read_status_mask = ZSK_LSR_OE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= ZSK_LSR_FE | ZSK_LSR_PE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		port->read_status_mask |= ZSK_LSR_BI;
	/* characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= ZSK_LSR_PE | ZSK_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= ZSK_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= ZSK_LSR_OE;
	}
	/* flow control */
	fc = serial_in(port, ZSK_CTL1);
	fc &= ~(RX_HW_FLOW_CTL_THLD | RX_HW_FLOW_CTL_EN | TX_HW_FLOW_CTL_EN);
	if (termios->c_cflag & CRTSCTS) {
		fc |= RX_HW_FLOW_CTL_THLD;
		fc |= RX_HW_FLOW_CTL_EN;
		fc |= TX_HW_FLOW_CTL_EN;
	}
	/* clock divider bit0~bit15 */
	serial_out(port, ZSK_CLKD0, quot & ZSK_CLKD0_MASK);
	/* clock divider bit16~bit20 */
	serial_out(port, ZSK_CLKD1,
		   (quot & ZSK_CLKD1_MASK) >> ZSK_CLKD1_SHIFT);
	serial_out(port, ZSK_LCR, lcr);
	fc |= RX_TOUT_THLD_DEF | RX_HFC_THLD_DEF;
	serial_out(port, ZSK_CTL1, fc);
	spin_unlock_irqrestore(&port->lock, flags);
	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
}
static const char *zsk_type(struct uart_port *port) {
	return "SPX";
}
static void zsk_release_port(struct uart_port *port) {
	/* nothing to do */
}
static int zsk_request_port(struct uart_port *port) {
	return 0;
}
static void zsk_config_port(struct uart_port *port, int flags) {
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_SPRD;
}
static int zsk_verify_port(struct uart_port *port, struct serial_struct *ser) {
	if (ser->type != PORT_SPRD)
		return -EINVAL;
	if (port->irq != ser->irq)
		return -EINVAL;
	if (port->iotype != ser->io_type)
		return -EINVAL;
	return 0;
}
static void zsk_pm(struct uart_port *port, unsigned int state,
		unsigned int oldstate) {
	struct zsk_uart_port *sup =
		container_of(port, struct zsk_uart_port, port);

	switch (state) {
	case UART_PM_STATE_ON:
		clk_prepare_enable(sup->clk);
		break;
	case UART_PM_STATE_OFF:
		clk_disable_unprepare(sup->clk);
		break;
	}
}
static const struct uart_ops serial_zsk_ops = {
	.tx_empty = zsk_tx_empty,
	.get_mctrl = zsk_get_mctrl,
	.set_mctrl = zsk_set_mctrl,
	.stop_tx = zsk_stop_tx,
	.start_tx = zsk_start_tx,
	.stop_rx = zsk_stop_rx,
	.break_ctl = zsk_break_ctl,
	.startup = zsk_startup,
	.shutdown = zsk_shutdown,
	.set_termios = zsk_set_termios,
	.type = zsk_type,
	.release_port = zsk_release_port,
	.request_port = zsk_request_port,
	.config_port = zsk_config_port,
	.verify_port = zsk_verify_port,
	.pm = zsk_pm,
};
#define ZSK_CONSOLE		NULL
static struct uart_driver zsk_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "zsk_serial",
	.dev_name = ZSK_TTY_NAME,
	.major = 0,
	.minor = 0,
	.nr = UART_NR_MAX,
	.cons = ZSK_CONSOLE,
};
static int zsk_remove(struct platform_device *dev) {
	struct zsk_uart_port *sup = platform_get_drvdata(dev);

	if (sup) {
		uart_remove_one_port(&zsk_uart_driver, &sup->port);
		zsk_port[sup->port.line] = NULL;
		zsk_rx_free_buf(sup);
		zsk_ports_num--;
	}
	if (!zsk_ports_num)
		uart_unregister_driver(&zsk_uart_driver);
	wakeup_source_remove(zsk_ztp_wakelock);
	return 0;
}
static int zsk_clk_init(struct uart_port *uport) {
	struct clk *clk_uart, *clk_parent;
	struct zsk_uart_port *u = zsk_port[uport->line];

	clk_uart = devm_clk_get(uport->dev, "uart");
	if (IS_ERR(clk_uart)) {
		dev_warn(uport->dev, "uart%d can't get uart clock\n",
			 uport->line);
		clk_uart = NULL;
	}
	clk_parent = devm_clk_get(uport->dev, "source");
	if (IS_ERR(clk_parent)) {
		dev_warn(uport->dev, "uart%d can't get source clock\n",
			 uport->line);
		clk_parent = NULL;
	}
	if (!clk_uart || clk_set_parent(clk_uart, clk_parent))
		uport->uartclk = ZSK_DEFAULT_SOURCE_CLK;
	else
		uport->uartclk = clk_get_rate(clk_uart);
	u->clk = devm_clk_get(uport->dev, "enable");
	if (IS_ERR(u->clk)) {
		if (PTR_ERR(u->clk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_warn(uport->dev, "uart%d can't get enable clock\n",
			uport->line);
		/* To keep console alive even if the error occurred */
		return PTR_ERR(u->clk);
	}
	return 0;
}

static irqreturn_t zsk_uart_callback(int irq, void *dev_id) {
	struct uart_port *port = dev_id;
	long timeout = 5 * HZ;

	uart_resume_port(&zsk_uart_driver, port);
	__pm_wakeup_event(zsk_ztp_wakelock, jiffies_to_msecs(timeout));
	//pr_emerg("light2: uart%d eic wake up\n", port->line);
	return IRQ_HANDLED;
}

static int eic_irq;
static int zsk_probe(struct platform_device *pdev) {
	struct resource *res;
	struct uart_port *up;
	int irq;
	int index;
	int ret;
	struct gpio_desc *irq_wake;

	index = of_alias_get_id(pdev->dev.of_node, "serial");
	if (index < 0 || index >= ARRAY_SIZE(zsk_port)) {
		dev_err(&pdev->dev, "got a wrong serial alias id %d\n", index);
		return -EINVAL;
	}
	zsk_port[index] = devm_kzalloc(&pdev->dev, sizeof(*zsk_port[index]),
					GFP_KERNEL);
	if (!zsk_port[index])
		return -ENOMEM;
	up = &zsk_port[index]->port;
	up->dev = &pdev->dev;
	up->line = index;
	up->type = PORT_SPRD;
	up->iotype = UPIO_MEM;
	up->uartclk = ZSK_DEF_RATE;
	up->fifosize = ZSK_FIFO_SIZE;
	up->ops = &serial_zsk_ops;
	up->flags = UPF_BOOT_AUTOCONF;
	up->has_sysrq = IS_ENABLED(CONFIG_SERIAL_ZSK_CONSOLE);
	ret = zsk_clk_init(up);
	if (ret)
		return ret;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	up->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(up->membase))
		return PTR_ERR(up->membase);
	up->mapbase = res->start;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	up->irq = irq;
	zsk_ztp_wakelock = wakeup_source_create("zsk_ztp_wakelock");
	wakeup_source_add(zsk_ztp_wakelock);
	/*eic wakeup sys*/
	irq_wake = devm_gpiod_get(&pdev->dev, "uart", GPIOD_IN);
	if (IS_ERR(irq_wake)) {
		dev_err(&pdev->dev, "Failed to get uart GPIO\n");
	}
	ret = gpiod_get_raw_value(irq_wake);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get uart gpio state\n");
	}
	eic_irq = gpiod_to_irq(irq_wake);
	if (eic_irq < 0) {
		dev_err(&pdev->dev, "Failed to translate uart_GPIO to IRQ\n");
	}
	ret = devm_request_threaded_irq(&pdev->dev, eic_irq, NULL,
					zsk_uart_callback,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					pdev->name, up);
	/*
	 * Allocate one dma buffer to prepare for receive transfer, in case
	 * memory allocation failure at runtime.
	 */
	ret = zsk_rx_alloc_buf(zsk_port[index]);
	if (ret)
		return ret;
	if (!zsk_ports_num) {
		ret = uart_register_driver(&zsk_uart_driver);
		if (ret < 0) {
			pr_err("Failed to register ZSK-UART driver\n");
			return ret;
		}
	}
	zsk_ports_num++;
	ret = uart_add_one_port(&zsk_uart_driver, up);
	if (ret)
		zsk_remove(pdev);
	platform_set_drvdata(pdev, up);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int zsk_suspend(struct device *dev) {
	struct zsk_uart_port *sup = dev_get_drvdata(dev);

	enable_irq(eic_irq);
	enable_irq_wake(eic_irq);
	uart_suspend_port(&zsk_uart_driver, &sup->port);
	return 0;
}

static int zsk_resume(struct device *dev) {
	struct zsk_uart_port *sup = dev_get_drvdata(dev);

	uart_resume_port(&zsk_uart_driver, &sup->port);
	disable_irq_wake(eic_irq);
	disable_irq(eic_irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(zsk_pm_ops, zsk_suspend, zsk_resume);
static const struct of_device_id serial_ids[] = {
	{.compatible = "zsk,sc9836-uart",},
	{}
};
MODULE_DEVICE_TABLE(of, serial_ids);

static struct platform_driver zsk_platform_driver = {
	.probe		= zsk_probe,
	.remove		= zsk_remove,
	.driver		= {
		.name	= "zsk_serial",
		.of_match_table = of_match_ptr(serial_ids),
		.pm	= &zsk_pm_ops,
	},
};

module_platform_driver(zsk_platform_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Spreadtrum SoC Zsk serial driver series");
