
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/kthread.h>

#define MAX_PCI_SERIAL_PORTS 4
#define PCI_SERIAL_WR_MEMROY_SIZE 0x1000
#define PCI_SERIAL_RD_MEMROY_SIZE 0x1000

#define	PCI_SERIAL_RESERVED_BAR_SIZE 0x400
#define CTRL_RING_COUNT 128

#define PCI_SERIAL_ALIGN 0x4   
#define ALIGN_PCI_SERIAL(size,align) (((unsigned int)size + align - 1)&(~(align - 1)))

#define PCI_READ_SERIAL_FLAG  0xa5a0
#define PCI_WRITE_SERIAL_FLAG  0x0a5a

#define PCI_SERIAL_WRITE_BUF_SIZE	0x2000
#define PCI_SERIAL_READ_BUF_SIZE 0x4000
#define PCI_SERIAL_READ_ONE_PACKET_SIZE 0x1000

#define PCI_EP_NOTIFY_IRQ_OFFSET 0x224
#define PCI_EP_NOTIFY_IRQ_VALUE 0x4000
#define PCI_EP_NOTIFY_CTRL 0x2000
#define PCI_EP_NOTIFY_DATA 0x4000



enum ctrl_msg_type {
	CTRL_MSG_RESERVED,
	CTRL_MSG_LINK_OPEN,
	CTRL_MSG_LINK_ALREADY_OPEN,
	CTRL_MSG_LINK_CLOSE,
	CTRL_MSG_LINK_CLOSE_ACK,
	CTRL_MSG_LINK_ALREADY_CLOSE,
	CTRL_MSG_LINK_DISCONNECT,
	CTRL_MSG_LINK_DISCONNECT_ACK,
	CTRL_MSG_HEAD_ERROR,
	CTRL_MSG_WRITE_FULL_ERROR,
};

struct pci_ctrl_msg {
	u32 ps_id;	//当前的 ps_port的index
	u32 msg_id; /* enum ctrl_msg_type */
};

struct pci_ctrl_cnt {
	u32 h2ep_ctrl_rd_cnt;
	u32 ep2h_ctrl_wr_cnt;
	u32 h2ep_ctrl_wr_cnt;
	u32 ep2h_ctrl_rd_cnt;
};


struct pci_ctrl_buf {
	struct pci_ctrl_msg * ctrl_rd_msgs;
	struct pci_ctrl_msg *ctrl_wr_msgs;
	struct pci_ctrl_cnt *ctrl_cnt;
	unsigned int ctrl_rd_offset;
	unsigned int ctrl_wr_offset;
	unsigned int ctrl_cnt_offset;
	unsigned int ctrl_rd_size;
	unsigned int ctrl_wr_size;
	
};


enum dir_link_state {
	DIR_LINK_STATE_CLOSE,
	DIR_LINK_STATE_OPEN,
	DIR_LINK_STATE_SENT_CLOSE,//表明本侧发起的close
	DIR_LINK_STATE_SENT_OPEN,//防止在打开串口时，立刻关闭了串口，只有等当前打开串口动作结束，才能进行close操作
	DIR_LINK_STATE_DISCONNECT,
	DIR_LINK_STATE_RESERVED,
};

enum os_link_state {
	OS_LINK_STATE_CLOSE,
	OS_LINK_STATE_OPEN,
	OS_LINK_STATE_DISCONNECT, //表明对方断开连接
};


struct serial_header_fmt
{
	unsigned int 	flag;
	unsigned int 	len;
};

struct ps_icount {
	u32		rx;
	u32		tx;
};

struct pci_port {
	struct tty_port		port;
	spinlock_t		port_lock;	/* guard port_* access */
	struct mutex	lock;			/* protect open/close */
	void 				*private_data;
	bool			openclose;	/* open/close in progress */
	bool			used;
	u8			port_num;
	bool		wait_read;
	bool		wait_write;
	bool		ep2host_open;
	bool 		cp_state;
	bool		reading;
	enum dir_link_state tx_link_state;
	enum dir_link_state rx_link_state;
	enum os_link_state os_link_state;
	struct device *dev;
	struct kfifo		port_write_buf;
	struct kfifo		port_read_buf;
	struct work_struct online_inform_work;	 /* inform pcie link state( online ) */
	struct work_struct offline_inform_work;
	struct pci_ctrl_buf *ctrl_mem;
	wait_queue_head_t	drain_wait;	/* wait while writes drain */
	wait_queue_head_t	close_wait;
	wait_queue_head_t	open_wait;
	unsigned long 		flags;
	struct ps_icount	icount;
	#define EVENT_TTY_WAKEUP	0
	#define EVENT_RX_STALL	1
	#define PCI_THROTTLED	2

	struct mutex mutex;
	u8 			minor;
};

//判断空,读指针==写指针, 满, 即将写指针+1%size == 即将读指针
struct pci_serial_ctrl {
	unsigned int ctrl_offset;  //当前控制通道对应整个bar的偏移
	unsigned int data_offset;  //当前tty data对应整个bar的偏移
	unsigned int sendPos;	//buffer的写指针,表明当前data内存写到何处,即将写
	unsigned int recvPos;	// ep buffer的读指针，即将读     sendPos和recvPos表示的是同一块内存
	unsigned int size;  //当前整个tty的写内存size
};

struct pci_serial_data {
	unsigned int offset;    //当前tty内存，写到了什么地方
	unsigned int sendPos;
	unsigned int recvPos;
	unsigned int size;		//累计传输的size	
};

struct pci_serial {
	struct serial_header_fmt head_fmt;
	struct pci_serial_ctrl ctrl_wr;
	struct pci_serial_ctrl ctrl_rd;
	struct pci_serial_data data_wr;
	struct pci_serial_data data_rd;
	unsigned int port_num;
	struct pci_port	*port;
	struct task_struct	*rd_work_thread;
	wait_queue_head_t work_wait;
	atomic_t wakeup_flag;
	struct device* tty_dev;
	spinlock_t rd_lock;
	spinlock_t wr_lock;
	struct mutex rd_mutex;
	struct mutex wr_mutex;
};



struct pci_serial_config {
	//struct serial_ctrl_info ctrl_info[2 * MAX_PCI_SERIAL_PORTS];
	//struct serial_data_info data_info[2 * MAX_PCI_SERIAL_PORTS];

	struct pci_serial ps[MAX_PCI_SERIAL_PORTS];
	unsigned int	offset;  //整个bar，目前初始化使用到什么位置，默认从4k开始，之前的为预留配置控制空间
	struct pci_ctrl_buf ctrl_mem;
	unsigned int	freeSize;
	struct mutex	lock;
	void __iomem	*base;
};

#define SET_TTYDEV_DEV(tty, pdev)	((tty)->parent = (pdev))

static inline bool pci_serial_ctrl_rd_empty(struct pci_ctrl_cnt* ctrl_cnt)
{
	return ctrl_cnt->h2ep_ctrl_rd_cnt == ctrl_cnt->ep2h_ctrl_wr_cnt; //此时为空
	
}

static inline bool pci_serial_ctrl_wr_full(struct pci_ctrl_cnt* ctrl_cnt)
{
	unsigned int wr = ctrl_cnt->h2ep_ctrl_wr_cnt;
	unsigned int rd = ctrl_cnt->ep2h_ctrl_rd_cnt;
	if(wr >= rd)
		return (wr + 1 ) % CTRL_RING_COUNT == rd;
	
	return (rd + 1 ) % CTRL_RING_COUNT == wr;	
}

static inline void pci_serial_update_ctrl_wr(struct pci_ctrl_cnt* ctrl_cnt)
{
	ctrl_cnt->h2ep_ctrl_wr_cnt = (ctrl_cnt->h2ep_ctrl_wr_cnt + 1 ) % CTRL_RING_COUNT;

	smp_mb();
}

static inline void pci_serial_update_ctrl_rd(struct pci_ctrl_cnt* ctrl_cnt)
{
	ctrl_cnt->h2ep_ctrl_rd_cnt = (ctrl_cnt->h2ep_ctrl_rd_cnt + 1 ) % CTRL_RING_COUNT;

	smp_mb();
}

static inline void pci_serial_recal_ctrl_pos(struct pci_ctrl_cnt* ctrl_cnt)
{
	ctrl_cnt->h2ep_ctrl_rd_cnt = 0;
	ctrl_cnt->ep2h_ctrl_rd_cnt = 0;
	ctrl_cnt->ep2h_ctrl_wr_cnt = 0;
	ctrl_cnt->h2ep_ctrl_wr_cnt = 0;

	smp_mb();
}



