/****************************************************************************/
/*
 *    sc-pci-serial.c   sanchips
 *
 *	
 */

/****************************************************************************/

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
#include <linux/pci_regs.h>

#include "sc_pci_pub.h"
#include <sc_pci_serial.h>

//#define DRV_MODULE_NAME				"pci-endpoint-serial"

#define SC_PCIE_SERIAL_DEBUG
#ifdef SC_PCIE_SERIAL_DEBUG
#define sc_pcie_serial_dbg(fmt, ...) printk("[sc_pcie_serial_dbg rc]:[%s-%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define sc_pcie_serial_dbg(fmt, ...)
#endif



#define PCI_SERIAL_AP_NOTIFY_CP 0


#define IRQ_TYPE_UNDEFINED			-1
#define IRQ_TYPE_LEGACY				0
#define IRQ_TYPE_MSI				1
#define IRQ_TYPE_MSIX				2

static unsigned char read_buffer[MAX_PCI_SERIAL_PORTS][PCI_SERIAL_READ_ONE_PACKET_SIZE];

#define PCI_SERIAL_CTRL_WR_MEM_SIZE CTRL_RING_COUNT*sizeof(struct pci_ctrl_msg)
#define PCI_SERIAL_CTRL_RD_MEM_SIZE CTRL_RING_COUNT*sizeof(struct pci_ctrl_msg)
#define PS_CLOSE_TIMEOUT		1		/* seconds */

#if 0
enum pci_barno {
	BAR_0,
	BAR_1,
	BAR_2,
	BAR_3,
	BAR_4,
	BAR_5,
};
#endif

struct pci_endpoint_serial {
	struct pci_dev	*pdev;
	void __iomem	*base;
	void __iomem	*int_base;
	void __iomem	*bar[6];
	int		num_irqs;
	int		irq_type;
	/* mutex to protect the ioctls */
	struct mutex	mutex;
	struct pci_serial_config ps_config;
	//enum pci_barno test_reg_bar;
	enum pci_barno tty_reg_bar;
	struct device* fdev;
	size_t alignment;
};

struct pci_endpoint_serial_data {
	enum pci_barno tty_reg_bar;
	size_t alignment;
	int irq_type;
};

static struct pci_endpoint_serial *g_ep_serial;
static struct tty_driver *pci_tty_driver;
static void pci_ep_ker_link_open_req(struct pci_port *port);
int pci_epf_rx_work(void *ptr);


static ssize_t serial_state_show(struct device *_dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
	unsigned char* port_num = dev_get_drvdata(_dev);
	struct pci_port* port = ep_serial->ps_config.ps[*port_num].port;
	return sprintf(buf, "serial_state = %d\n",port->cp_state);
}

/**
 * Store the ep serial attribure.
 * 0:  ep pci serial is close 
 * 1:  ep pci serial is open
 */
static ssize_t serial_state_store(struct device *_dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
    struct pci_endpoint_serial *ep_serial = g_ep_serial;
	unsigned char* port_num = dev_get_drvdata(_dev);
	struct pci_port* port = ep_serial->ps_config.ps[*port_num].port;
	printk(" tty:%d serial state is read only, state:%s\n", *port_num, port->cp_state? "open":"close");

	return count;
}

//DEVICE_ATTR(serial_state, S_IRUGO, serial_state_show,
//	 serial_state_store);
DEVICE_ATTR(serial_state, S_IRUGO | S_IWUSR, serial_state_show,
  serial_state_store);

static ssize_t ps_dbg_info_show(struct device *_dev,
			  struct device_attribute *attr, char *buf)
{
  struct pci_endpoint_serial *ep_serial = g_ep_serial;
  unsigned char* port_num = dev_get_drvdata(_dev);
  struct pci_serial_ctrl* ctrl_wr = &ep_serial->ps_config.ps[*port_num].ctrl_wr;
  struct pci_serial_ctrl* ctrl_rd = &ep_serial->ps_config.ps[*port_num].ctrl_rd;
  struct pci_serial_ctrl *mem_ctrl_rd = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_rd->ctrl_offset);
  struct pci_serial_ctrl *mem_ctrl_wr = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_wr->ctrl_offset);
  struct pci_port* port = ep_serial->ps_config.ps[*port_num].port;
  struct pci_ctrl_cnt *ctrl_cnt = port->ctrl_mem->ctrl_cnt;
  return sprintf(buf, "rx:%d, tx:%d, open count:%d\nh2ep_ctrl_rd:%d, ep2h_ctrl_wr:%d, h2ep_ctrl_wr:%d, ep2h_ctrl_rd:%d\n"
  		"h2ep_data_rd:%d, ep2h_data_wr:%d, h2ep_data_wr:%d, ep2h_data_rd:%d\n",port->icount.rx, port->icount.tx, port->port.count,
  		ctrl_cnt->h2ep_ctrl_rd_cnt, ctrl_cnt->ep2h_ctrl_wr_cnt, ctrl_cnt->h2ep_ctrl_wr_cnt, ctrl_cnt->ep2h_ctrl_rd_cnt,
  		mem_ctrl_rd->recvPos, mem_ctrl_rd->sendPos, mem_ctrl_wr->sendPos, mem_ctrl_wr->recvPos);
}

static ssize_t ps_dbg_info_store(struct device *_dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
  int value;
  //struct pci_endpoint_serial *ep_serial = g_ep_serial;
  unsigned char* port_num = dev_get_drvdata(_dev);
  value = simple_strtoul(buf, NULL, 10);
  printk(" tty:%d dbg_info is read only, state:%s\n", *port_num, !!value? "open":"close");

  return count;
}

DEVICE_ATTR(dbginfo, S_IRUGO | S_IWUSR, ps_dbg_info_show,
ps_dbg_info_store);
/* Ended by AICoder, pid:ufc1emef79e25531434509a2d05c6968ba899b02 */


static DEFINE_IDR(pci_minors);
static DEFINE_MUTEX(pci_minors_lock);

typedef struct __PCI_SERIAL_STATIC {
	int cnt;
	char head[16];
	int flag; // 0表示data, 非0表示ctrl
	unsigned long long s_time;
	int func_step;
	unsigned int flg_enter_th;
	//unsigned int ep2h_rd; // ep read pos
	unsigned int ep2h_wr; // ep write pos
	unsigned int h2ep_rd; // host read pos
	//unsigned int h2ep_wr; // host write pos
}ps_static;

#define PCI_SERIAL_STATIC_COUNT 1024

ps_static g_ps_static[MAX_PCI_SERIAL_PORTS][PCI_SERIAL_STATIC_COUNT] = {0};
int g_ps_static_cnt[MAX_PCI_SERIAL_PORTS] = {0};
static unsigned int g_flg_enter_th = 0;

void pci_serial_cal_static(int port, char *buf, unsigned int cnt, int steps)
{
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
	struct pci_serial_ctrl* ctrl_rd;
	struct pci_serial_ctrl *mem_ctrl_rd;
	
	if(buf){
		if(cnt >= 16){
			memcpy(g_ps_static[port][g_ps_static_cnt[port]].head, buf, 16);
		}else{
			memcpy(g_ps_static[port][g_ps_static_cnt[port]].head, buf, cnt);
		}

		ctrl_rd = &ep_serial->ps_config.ps[port].ctrl_rd;
		mem_ctrl_rd = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_rd->ctrl_offset);
	
		g_ps_static[port][g_ps_static_cnt[port]].ep2h_wr = mem_ctrl_rd->sendPos;
		g_ps_static[port][g_ps_static_cnt[port]].h2ep_rd = mem_ctrl_rd->recvPos;
		
	}
	
	g_ps_static[port][g_ps_static_cnt[port]].flag = 0;
	//struct serial_ctrl_info* ctrl_wr = &epf_serial->ps_config.ps_info[port].ctrl_wr;
	g_ps_static[port][g_ps_static_cnt[port]].flg_enter_th = g_flg_enter_th;
	
	g_ps_static[port][g_ps_static_cnt[port]].cnt = cnt;
	g_ps_static[port][g_ps_static_cnt[port]].s_time = local_clock();
	g_ps_static[port][g_ps_static_cnt[port]].func_step = steps;
	
	if(++g_ps_static_cnt[port] >= PCI_SERIAL_STATIC_COUNT)
		g_ps_static_cnt[port] = 0;
}


static struct pci_port *pci_get_by_minor(unsigned int minor)
{
	struct pci_port *port;

	mutex_lock(&pci_minors_lock);
	port = idr_find(&pci_minors, minor);
	if (port) {
		mutex_lock(&port->mutex);
		tty_port_get(&port->port);
		mutex_unlock(&port->mutex);
		
	}
	mutex_unlock(&pci_minors_lock);
	return port;
}

/*
 * Try to find an available minor number and if found, associate it with 'pci_port'.
 */
static int pci_alloc_minor(struct pci_port *port)
{
	int minor;

	mutex_lock(&pci_minors_lock);
	minor = idr_alloc(&pci_minors, port, 0, MAX_PCI_SERIAL_PORTS, GFP_KERNEL);
	mutex_unlock(&pci_minors_lock);

	return minor;
}

/* Release the minor number associated with 'pci_port'.  */
static void pci_release_minor(struct pci_port *port)
{
	mutex_lock(&pci_minors_lock);
	idr_remove(&pci_minors, port->minor);
	mutex_unlock(&pci_minors_lock);
}

static int pci_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct pci_port *port;
	int retval;

	port = pci_get_by_minor(tty->index);
	if (!port)
		return -ENODEV;

	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error_init_termios;
	port->port_num = tty->index;
	tty->driver_data = port;

	return 0;

error_init_termios:
	tty_port_put(&port->port);
	return retval;
}

static void pci_port_destruct(struct tty_port *port)
{
	struct pci_port *pp = container_of(port, struct pci_port, port);

	if (pp->minor != MAX_PCI_SERIAL_PORTS)
		pci_release_minor(pp);
	kfree(pp);
}

/**
static int pci_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct pci_port *port = tty->driver_data;
	port->used = true;

	return tty_port_open(&port->port, tty, filp);
}

static int pci_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct pci_port *port = tty->driver_data;

	return tty_port_close(&port->port, tty, filp);
}
**/

static void pci_tty_throttle(struct tty_struct *tty)
{
	struct pci_port *port = tty->driver_data;

	set_bit(PCI_THROTTLED, &port->flags);
}

static void pci_tty_unthrottle(struct tty_struct *tty)
{
	struct pci_port *port = tty->driver_data;

	clear_bit(PCI_THROTTLED, &port->flags);

	/* Matches the smp_mb__after_atomic() in acm_read_bulk_callback(). */
	smp_mb();
}

static inline u32 pci_endpoint_serial_readl(struct pci_endpoint_serial *test,
					  u32 offset)
{
	return readl(test->base + offset);
}

static inline void pci_endpoint_serial_writel(struct pci_endpoint_serial *test,
					    u32 offset, u32 value)
{
	writel(value, test->base + offset);
}

static inline u32 pci_endpoint_serial_bar_readl(struct pci_endpoint_serial *test,
					      int bar, int offset)
{
	return readl(test->bar[bar] + offset);
}

static inline void pci_endpoint_serial_bar_writel(struct pci_endpoint_serial *test,
						int bar, u32 offset, u32 value)
{
	writel(value, test->bar[bar] + offset);
}


static void pci_ep_irq_notify_epf(void __iomem* base, u32 offset, u32 value)
{
	writel(value, base + offset);
	return;
}

static void pci_endpoint_serial_free_irq_vectors(struct pci_endpoint_serial *ep_serial)
{
	struct pci_dev *pdev = ep_serial->pdev;

	pci_free_irq_vectors(pdev);
	ep_serial->irq_type = IRQ_TYPE_UNDEFINED;
}

/*
static bool pci_endpoint_serial_alloc_irq_vectors(struct pci_endpoint_serial *ep_serial,
						int type)
{
	int irq = -1;
	struct pci_dev *pdev = ep_serial->pdev;
	struct device *dev = &pdev->dev;
	bool res = true;

	switch (type) {
	case IRQ_TYPE_LEGACY:
		irq = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_LEGACY);
		if (irq < 0)
			dev_err(dev, "Failed to get Legacy interrupt\n");
		break;
	case IRQ_TYPE_MSI:
		irq = pci_alloc_irq_vectors(pdev, 1, 32, PCI_IRQ_MSI);
		if (irq < 0)
			dev_err(dev, "Failed to get MSI interrupts\n");
		break;
	case IRQ_TYPE_MSIX:
		irq = pci_alloc_irq_vectors(pdev, 1, 2048, PCI_IRQ_MSIX);
		if (irq < 0)
			dev_err(dev, "Failed to get MSI-X interrupts\n");
		break;
	default:
		dev_err(dev, "Invalid IRQ type selected\n");
	}

	if (irq < 0) {
		irq = 0;
		res = false;
	}

	ep_serial->irq_type = type;
	ep_serial->num_irqs = irq;

	return res;
}
*/

static void pci_endpoint_serial_release_irq(struct pci_endpoint_serial *ep_serial)
{
	struct pci_dev *pdev = ep_serial->pdev;
	struct device *dev = &pdev->dev;
	
	devm_free_irq(dev, pci_irq_vector(pdev, 3), ep_serial);
	devm_free_irq(dev, pci_irq_vector(pdev, 4), ep_serial);
	ep_serial->num_irqs = 0;
}


#define CUR_SEND_POS(info) 		(info)->sendPos
#define CUR_RECV_POS(info) 		(info)->recvPos

/**
#define UPDATE_MEM_SEND_POS(base, ctrl_wr, value) ( do{	\
		void *_base = (base);	\
		typeof(*ctrl_wr)* _ctrl_wr = (typeof(*ctrl_wr)*)(_base + (ctrl_wr)->(ctrl_offset));		\
		_ctrl_wr->sendPos = (value)	\
	}while(0))
#define UPDATE_MEM_RECV_POS(base, ctrl_rd, value)  ( do{	\
		void *_base = (base);	\
		typeof(*ctrl_rd) *_ctrl_wr = (typeof(*ctrl_rd) *)(_base + (ctrl_rd)->(ctrl_offset));		\
		ctrl_rd->sendPos = (value)	\
	}while(0))
**/

static const struct tty_port_operations pci_port_ops = {
	//.shutdown = pci_port_shutdown,
	//.activate = pci_port_activate,
	.destruct = pci_port_destruct,
};

static void pserial_inform_online_work(struct work_struct *online_inform_work)
{
  struct pci_port  *port = container_of(online_inform_work, struct pci_port, online_inform_work);
  if(port->dev == NULL)
  {
	  printk("[%s-%d] tty dev is NULL\n",__func__,__LINE__);
	  return ;
  }
  sc_pcie_serial_dbg("port%d online\n",port->port_num);
  kobject_uevent(&port->dev->kobj, KOBJ_ONLINE);
  pci_serial_cal_static(port->port_num, NULL, 0, 3);
}

static void pserial_inform_offline_work(struct work_struct *offline_inform_work)
{
  struct pci_port  *port = container_of(offline_inform_work, struct pci_port, offline_inform_work);
  if(port->dev == NULL)
  {
	  sc_pcie_serial_dbg("[%s-%d] tty_dev is NULL\n",__func__,__LINE__);
	  return ;
  }
  
  sc_pcie_serial_dbg("port%d offline\n",port->port_num);
  kobject_uevent(&port->dev->kobj, KOBJ_OFFLINE);
  pci_serial_cal_static(port->port_num, NULL, 0, 7);
}

static int
pci_tty_port_alloc(struct pci_endpoint_serial *ep_serial, unsigned port_num)
{
	struct pci_port	*port;
	int		ret = 0;
	u8		minor = 0;
	port = kzalloc(sizeof(struct pci_port), GFP_KERNEL);
	if (port == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	minor = pci_alloc_minor(port);
	if (minor < 0) {
		port->minor = MAX_PCI_SERIAL_PORTS;
		goto out;
	}

	port->minor = minor;

	tty_port_init(&port->port);
	port->port.ops = &pci_port_ops;
	spin_lock_init(&port->port_lock);
	
	mutex_init(&port->lock);
	init_waitqueue_head(&port->drain_wait);
	init_waitqueue_head(&port->open_wait);

	INIT_WORK(&port->online_inform_work, pserial_inform_online_work);
	INIT_WORK(&port->offline_inform_work, pserial_inform_offline_work);

	port->port_num = port_num;
	ep_serial->ps_config.ps[port_num].port = port;
	port->ctrl_mem = &ep_serial->ps_config.ctrl_mem;
	sc_pcie_serial_dbg("pci_tty_port_alloc, cur port minor:%d\n", minor);
out:
	return ret;
}

/*
static int ps_closed(struct pci_port *port)
{
	int cond;

	spin_lock_irq(&port->port_lock);
	cond = (port->port.count == 0) && !port->openclose;
	spin_unlock_irq(&port->port_lock);
	return cond;
}
*/


static void pserial_free_port(struct pci_port *port)
{
    unsigned long flags;
    //cancel_delayed_work_sync(&port->push);
    /* wait for old opens to finish */
#if 0
    if(!wait_event_timeout(port->close_wait, ps_closed(port),msecs_to_jiffies(3000)))
    {
        //g_offline_timeout_flag = 1 ;
        //printk("[cdnsp:%s-%d]:wait prot close event timeout\n",__func__,__LINE__);
        //usb_sc_debug_info_record("wait port free\n");
        //schedule_work(&port->offline_inform_work);
        //flush_work(&port->offline_inform_work);
        wait_event(port->close_wait, ps_closed(port));
    }
#endif
   // g_offline_timeout_flag = 0 ;
   // WARN_ON(port->used != NULL);
    spin_lock_irqsave(&port->port_lock,flags);
    tty_port_destroy(&port->port);
	if(port->dev)
		tty_unregister_device(pci_tty_driver, port->port_num);
	port->dev = NULL;
    spin_unlock_irqrestore(&port->port_lock,flags);
    kfree(port);
}


int pserial_alloc_port(struct pci_endpoint_serial *ep_serial)
{
	struct device			*tty_dev;
	int				ret;
	int				port_num;
	struct pci_port	*port = NULL;
	char thread_name[64];
	for (port_num = 0; port_num < MAX_PCI_SERIAL_PORTS; port_num++) {
		ret = pci_tty_port_alloc(ep_serial, port_num);
		if (ret)
			goto err;

		/* ... and sysfs class devices, so mdev/udev make /dev/ttyPS* */
		
		tty_dev = tty_port_register_device(&ep_serial->ps_config.ps[port_num].port->port,
				pci_tty_driver, port_num, NULL);
		port = ep_serial->ps_config.ps[port_num].port;
		if (IS_ERR(tty_dev)) {
			pr_err("%s: failed to register tty for port %d, err %ld\n",
					__func__, port_num, PTR_ERR(tty_dev));

			ret = PTR_ERR(tty_dev);
			pserial_free_port(port);
			goto err;
		}
		dev_set_drvdata(tty_dev, &port->port_num);
		device_create_file(tty_dev, &dev_attr_serial_state);
		device_create_file(tty_dev, &dev_attr_dbginfo);
		SET_TTYDEV_DEV(tty_dev, ep_serial->fdev);
		pci_ep_ker_link_open_req(port);
	    
	    mutex_lock(&ep_serial->ps_config.lock);
	    ep_serial->ps_config.ps[port_num].port->dev = tty_dev;
	    mutex_unlock(&ep_serial->ps_config.lock);

		sprintf(thread_name, "pci_serial_rx_th_%d", port_num);
		//ep_serial->ps_config.ps[i].rd_work_thread = kthread_run(pci_epf_rx_work, (void *)(&(ep_serial->ps_config.ps[port_num])), "pci_rx_thread");
		ep_serial->ps_config.ps[port_num].rd_work_thread = kthread_run(pci_epf_rx_work, (void *)(&(ep_serial->ps_config.ps[port_num])), thread_name);
		BUG_ON(IS_ERR(ep_serial->ps_config.ps[port_num].rd_work_thread));
		wake_up_process(ep_serial->ps_config.ps[port_num].rd_work_thread);
	}
err:
	return ret;
}

void recal_pci_serial_wr_pos(void *base, struct pci_serial *ps)
{
	struct pci_serial_ctrl *ctrl_wr = &ps->ctrl_wr;
	struct pci_serial_data *data_wr = &ps->data_wr;
	struct pci_serial_ctrl *mem_ctrl = (struct pci_serial_ctrl *)(base + ctrl_wr->ctrl_offset);
	mem_ctrl->sendPos = 0;
	ctrl_wr->sendPos = 0;
	data_wr->sendPos = 0;
	data_wr->offset	= 0;
	sc_pcie_serial_dbg(" ctrl sendPos:%d, data sendPos:%d, data offset:%d\n", 
		mem_ctrl->sendPos, data_wr->sendPos, data_wr->offset);
}

void recal_pci_serial_rd_pos(void *base, struct pci_serial *ps)
{
	struct pci_serial_ctrl *ctrl_rd = &ps->ctrl_rd;
	struct pci_serial_data *data_rd = &ps->data_rd;
	struct pci_serial_ctrl *mem_ctrl = (struct pci_serial_ctrl *)(base + ctrl_rd->ctrl_offset);
	mem_ctrl->recvPos = 0;
	ctrl_rd->recvPos = 0;
	data_rd->recvPos = 0;
	data_rd->offset	= 0;
	sc_pcie_serial_dbg(" ctrl recvPos:%d, data recvPos:%d, data offset:%d\n", 
		mem_ctrl->recvPos, data_rd->recvPos, data_rd->offset);
}


void update_pci_mem_rd_pos(void *base, struct pci_serial_ctrl * ctrl_rd, int value)
{
	struct pci_serial_ctrl *mem_ctrl = (struct pci_serial_ctrl *)(base + ctrl_rd->ctrl_offset);
	mem_ctrl->recvPos = value;
	sc_pcie_serial_dbg("pci mem ctrl recvPos:%u\n", mem_ctrl->recvPos);
}

void update_pci_mem_wr_pos(void *base, struct pci_serial_ctrl * ctrl_wr, int value)
{
	struct pci_serial_ctrl *mem_ctrl = (struct pci_serial_ctrl *)(base + ctrl_wr->ctrl_offset);
	mem_ctrl->sendPos = value;
	sc_pcie_serial_dbg("pci mem ctrl sendPos:%u\n", mem_ctrl->sendPos);
}

//ep侧的读指针,来更新rc侧的读指针,指向同一块内存，对rc来说就是写buffer,写buffer的写指针,由rc侧软件更新
void update_cpu_mem_rd_pos(void *base, struct pci_serial_ctrl * ctrl_rd, u8 port)
{
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
	//将内存转换为结构体的形式读出来
	struct pci_serial_ctrl *mem_ctrl_rd = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_rd->ctrl_offset);
	ctrl_rd->recvPos = mem_ctrl_rd->recvPos;
	//ep_serial->ps_config.ps[port].data_rd.recvPos =  ctrl_rd->recvPos;
	ep_serial->ps_config.ps[port].data_wr.recvPos =  ctrl_rd->recvPos;
	sc_pcie_serial_dbg(" update ctrl_rd recvPos:%u, sendPos:%u, data:recvPos:%u, sendPos:%u\n", 
		ctrl_rd->recvPos, ctrl_rd->sendPos, ep_serial->ps_config.ps[port].data_wr.recvPos, ep_serial->ps_config.ps[port].data_wr.sendPos);
	
}

//ep侧的写指针,来更新rc侧的写指针,指向同一块内存，对rc来说就是读buffer,读buffer的读指针,由rc侧软件更新
void update_cpu_mem_wr_pos(void *base, struct pci_serial_ctrl * ctrl_wr, u8 port)
{
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
	//将内存转换为结构体的形式读出来
	struct pci_serial_ctrl *mem_ctrl_wr = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_wr->ctrl_offset);
	ctrl_wr->sendPos = mem_ctrl_wr->sendPos;
	ep_serial->ps_config.ps[port].data_rd.sendPos =  ctrl_wr->sendPos;
	sc_pcie_serial_dbg(" update ctrl_rd recvPos:%u, sendPos:%u, data:recvPos:%u, sendPos:%u\n", 
		ctrl_wr->recvPos, ctrl_wr->sendPos, ep_serial->ps_config.ps[port].data_rd.recvPos, ep_serial->ps_config.ps[port].data_rd.sendPos);
}

//读size空间,
static inline bool pci_serial_free_read_space(struct pci_serial_ctrl* ctrl_rd, int size)
{
	if (ctrl_rd->sendPos < ctrl_rd->recvPos){
    	if ((ctrl_rd->recvPos - ctrl_rd->sendPos) > size) //不取等号的原因，不能让下一个即将写与即将读重合
			return 1;
		else
			return 0;
		//if (ctrl_info->recvPos);
		
	}else if (ctrl_rd->sendPos == ctrl_rd->recvPos) { //此时为空
		return 0;
	}else{
		if ((ctrl_rd->size - ctrl_rd->sendPos + ctrl_rd->recvPos ) > size)
			return 1;
		else
			return 0;
	}
}


//当前是否有读size大小的空间
static inline bool pci_serial_free_read_n_space(struct pci_serial_ctrl* ctrl_rd, int size)
{
	unsigned int total_size = ctrl_rd->size;
	if (ctrl_rd->sendPos < ctrl_rd->recvPos){
		if ((total_size - ctrl_rd->recvPos + ctrl_rd->sendPos) >= size)
			return 1;
		else
			return 0;
		
	}else if (ctrl_rd->sendPos == ctrl_rd->recvPos) { //此时为空
		return 0;
	}else{
		if ((ctrl_rd->sendPos - ctrl_rd->recvPos ) >= size)
			return 1;
		else
			return 0;
	}
}

//当前是否有读空间,非空
static inline bool pci_serial_read_space_empty(struct pci_serial_ctrl* ctrl_info, unsigned int port)
{
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
	//将内存转换为结构体的形式读出来
	struct pci_serial_ctrl *ctrl_rd = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_info->ctrl_offset);
	ctrl_info->sendPos = ctrl_rd->sendPos;
	ctrl_info->recvPos = ctrl_rd->recvPos;
	ep_serial->ps_config.ps[port].data_rd.sendPos =  ctrl_info->sendPos;
	ep_serial->ps_config.ps[port].data_rd.recvPos =  ctrl_info->recvPos;
	return ctrl_info->sendPos == ctrl_info->recvPos; //此时为空
	
}

static inline bool pci_serial_write_space_full(struct pci_serial_ctrl* ctrl_wr)
{
	return ctrl_wr->sendPos == ctrl_wr->recvPos; //此时为空,表示对方读完了
	
}


//当前是否有写size大小的空间
static inline bool pci_serial_free_write_space(struct pci_serial_ctrl* ctrl_wr, int size)
{
	if (ctrl_wr->sendPos < ctrl_wr->recvPos){
    	if ((ctrl_wr->recvPos - ctrl_wr->sendPos) > size) //不取等号的原因，不能让下一个即将写与即将读重合,这被解释为空
			return 1;
		else
			return 0;
	}else if (ctrl_wr->sendPos == ctrl_wr->recvPos) { //此时为空
		return 1;
	}else{
		if ((ctrl_wr->size - ctrl_wr->sendPos + ctrl_wr->recvPos ) > size)
			return 1;
		else
			return 0;
	}
}

static void pci_epf_read_ctrl_msg(struct pci_ctrl_buf * ctrl_mem,
				     struct pci_ctrl_msg *msg)
{	
	struct pci_ctrl_msg *ctrl_msg = ctrl_mem->ctrl_rd_msgs;
	u32 idx;

	if (pci_serial_ctrl_rd_empty(ctrl_mem->ctrl_cnt)) {
		sc_pcie_serial_dbg("%s: EP2H ctrl ring is empty\n", __func__);
		return;
	}

	idx = ctrl_mem->ctrl_cnt->h2ep_ctrl_rd_cnt % CTRL_RING_COUNT;
	memcpy(msg, &ctrl_msg[idx], sizeof(*msg));
	sc_pcie_serial_dbg("index:%u, msg:%d, real msg:%d, 0x%08x, 0x%08x, 0x%08x\n", idx, ctrl_msg[idx].msg_id, msg->msg_id, ctrl_msg, &ctrl_msg[idx],ctrl_msg[idx]);
	pci_serial_update_ctrl_rd(ctrl_mem->ctrl_cnt);
}

 static int pci_ep_write_ctrl_msg(struct pci_ctrl_buf * ctrl_mem,
					  struct pci_ctrl_msg *msg)
 {
	struct pci_ctrl_msg *ctrl_msg = ctrl_mem->ctrl_wr_msgs;
	struct pci_endpoint_serial* ep_serial = g_ep_serial;
	u32 idx;

	if (pci_serial_ctrl_wr_full(ctrl_mem->ctrl_cnt)) {
		/* Raise an interrupt to let host process EP2H ring */
		pci_ep_irq_notify_epf(ep_serial->int_base, PCI_EP_NOTIFY_IRQ_OFFSET, PCI_EP_NOTIFY_CTRL);
		sc_pcie_serial_dbg("EP2H ctrl ring full\n");
		return -EAGAIN;
	}

	//idx = ctrl_mem->ctrl_cnt->ep2h_ctrl_wr_cnt % CTRL_RING_COUNT;
	idx = ctrl_mem->ctrl_cnt->h2ep_ctrl_wr_cnt % CTRL_RING_COUNT;
	memcpy(&ctrl_msg[idx], msg, sizeof(*msg));
	sc_pcie_serial_dbg("index:%u, msg:%d, real msg:%d, 0x%08x, 0x%08x, 0x%08x\n", idx, ctrl_msg[idx].msg_id, msg->msg_id, ctrl_msg, &ctrl_msg[idx],ctrl_msg[idx]);
	pci_serial_update_ctrl_wr(ctrl_mem->ctrl_cnt);
	pci_ep_irq_notify_epf(ep_serial->int_base, PCI_EP_NOTIFY_IRQ_OFFSET, PCI_EP_NOTIFY_CTRL);
	return 0;
 }

static void pci_ep_update_link_state(struct pci_port *port)
{
#if 0
	enum dir_link_state rx_state = port->rx_link_state;
	enum dir_link_state tx_state = port->tx_link_state;
	enum os_link_state os_state = port->os_link_state;
	if (os_state == OS_LINK_STATE_CLOSE && tx_state == DIR_LINK_STATE_OPEN) {
		if (port->port.count == 0)
			schedule_work(&port->online_inform_work);
	} else if (os_state == OS_LINK_STATE_DISCONNECT && rx_state == DIR_LINK_STATE_DISCONNECT) {
		if (port->port.count)
			schedule_work(&port->offline_inform_work);
	}
#endif
}

/* OS link state machine */
static void pci_ep_update_link_sm(struct pci_port *port)
{
	//struct net_device *ndev = tvnet->ndev;
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	//enum os_link_state old_state = port->os_link_state;

	if ((port->rx_link_state == DIR_LINK_STATE_OPEN) &&
	    (port->tx_link_state == DIR_LINK_STATE_OPEN))
		port->os_link_state = OS_LINK_STATE_OPEN;
	else if ((port->rx_link_state == DIR_LINK_STATE_DISCONNECT) &&
	    (port->tx_link_state == DIR_LINK_STATE_CLOSE))
		port->os_link_state = OS_LINK_STATE_DISCONNECT;
	else if ((port->rx_link_state == DIR_LINK_STATE_CLOSE) &&
	    (port->tx_link_state == DIR_LINK_STATE_OPEN))
		port->os_link_state = OS_LINK_STATE_DISCONNECT;
	else
		port->os_link_state = OS_LINK_STATE_CLOSE;

	//if (port->os_link_state != old_state)
	//	pci_ep_update_link_state(port);
	sc_pcie_serial_dbg(" ttyPCI%d, update link sm, rx state:%d, tx state:%d, os state:%d\n", 
			port->port_num, port->rx_link_state, port->tx_link_state, port->os_link_state);
}

/* One way link state machine*/
static void pci_ep_user_link_open_req(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	struct pci_ctrl_buf *ctrl_mem = port->ctrl_mem;
	struct pci_serial_config  *ps_config = container_of(ctrl_mem, struct pci_serial_config, ctrl_mem);
	sc_pcie_serial_dbg(" ttyPCI%d, send link open\n", port->port_num);
	recal_pci_serial_wr_pos(ps_config->base, &ps_config->ps[port->port_num]);
	
	msg.msg_id = CTRL_MSG_LINK_OPEN;
	msg.ps_id = port->port_num;
	port->tx_link_state = DIR_LINK_STATE_SENT_OPEN;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	port->rx_link_state = DIR_LINK_STATE_OPEN;
	pci_ep_update_link_sm(port);
}

static void pci_ep_ker_link_open_req(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	struct pci_ctrl_buf *ctrl_mem = port->ctrl_mem;
	struct pci_serial_config  *ps_config = container_of(ctrl_mem, struct pci_serial_config, ctrl_mem);
	sc_pcie_serial_dbg(" ttyPCI%d, kernel send link open\n", port->port_num);
	recal_pci_serial_wr_pos(ps_config->base, &ps_config->ps[port->port_num]);
	//pci_serial_recal_ctrl_pos(port->ctrl_mem->ctrl_cnt);
	msg.msg_id = CTRL_MSG_RESERVED;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
}


static void pci_ep_user_link_already_open_req(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	struct pci_ctrl_buf *ctrl_mem = port->ctrl_mem;
	struct pci_serial_config  *ps_config = container_of(ctrl_mem, struct pci_serial_config, ctrl_mem);
	sc_pcie_serial_dbg(" ttyPCI%d, send link open\n", port->port_num);
	recal_pci_serial_wr_pos(ps_config->base, &ps_config->ps[port->port_num]);
	
	msg.msg_id = CTRL_MSG_LINK_ALREADY_OPEN;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	port->rx_link_state = DIR_LINK_STATE_OPEN;
	//pci_ep_update_link_sm(port);
}


static void pci_ep_user_link_close_req(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	sc_pcie_serial_dbg(" ttyPCI%d, send link close\n", port->port_num);
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	port->rx_link_state = DIR_LINK_STATE_SENT_CLOSE;
	msg.msg_id = CTRL_MSG_LINK_CLOSE;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	pci_ep_update_link_sm(port);
	port->cp_state = false;
}

/**
static void pci_ep_user_link_already_close_req(struct pci_port *port)
{
	sc_pcie_serial_dbg(" ttyPCI%d, send link close\n", port->port_num);
	struct pci_ctrl_msg msg;
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	port->rx_link_state = DIR_LINK_STATE_SENT_CLOSE;
	msg.msg_id = CTRL_MSG_LINK_CLOSE;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	pci_ep_update_link_sm(port);
}
**/

/*
static void pci_ep_user_link_disconnect_req(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	sc_pcie_serial_dbg(" ttyPCI%d, send link disconnect\n", port->port_num);
	port->wait_write = false;
	
	wake_up_interruptible(&port->drain_wait);
	msg.msg_id = CTRL_MSG_LINK_DISCONNECT;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	port->tx_link_state = DIR_LINK_STATE_CLOSE;
	pci_ep_update_link_sm(port);
}
*/

static void pci_ep_rcv_link_open_msg(struct pci_port *port)
{
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	struct pci_ctrl_buf *ctrl_mem = port->ctrl_mem;
	struct pci_serial_config  *ps_config = container_of(ctrl_mem, struct pci_serial_config, ctrl_mem);
	sc_pcie_serial_dbg(" ttyPCI%d, recv link open\n", port->port_num);
	recal_pci_serial_rd_pos(ps_config->base, &ps_config->ps[port->port_num]);
	
	port->tx_link_state = DIR_LINK_STATE_OPEN;
	port->cp_state = true;
	port->icount.rx = 0;
	port->icount.tx = 0;
	g_flg_enter_th = 0;
	g_ps_static_cnt[port->port_num] = 0;
	//rc应用没有关闭串口的情况下
	if(port->port.count /*&& port->os_link_state == OS_LINK_STATE_DISCONNECT*/) {
		pci_ep_user_link_already_open_req(port);
		pci_ep_update_link_sm(port);
		if (!port->used)
			port->ep2host_open = true;
	}else{
		pci_ep_update_link_state(port);
		port->ep2host_open = false;
	}
#if PCI_SERIAL_AP_NOTIFY_CP
	wake_up_interruptible(&port->open_wait);
#endif
}

static void pci_ep_rcv_link_already_open_msg(struct pci_port *port)
{
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	struct pci_ctrl_buf *ctrl_mem = port->ctrl_mem;
	struct pci_serial_config  *ps_config = container_of(ctrl_mem, struct pci_serial_config, ctrl_mem);
	sc_pcie_serial_dbg(" ttyPCI%d, recv link open\n", port->port_num);
	recal_pci_serial_rd_pos(ps_config->base, &ps_config->ps[port->port_num]);
	
	port->tx_link_state = DIR_LINK_STATE_OPEN;
	//port->os_link_state = OS_LINK_STATE_OPEN;
	port->cp_state = true;
	pci_ep_update_link_sm(port);
	//rc应用没有关闭串口的情况下
#if PCI_SERIAL_AP_NOTIFY_CP
	if(port->port.count && !port->used) {
		port->ep2host_open = true;
	}
	else
		port->ep2host_open = false;
	wake_up_interruptible(&port->open_wait);
#endif
}


static void pci_ep_rcv_link_close_msg(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	sc_pcie_serial_dbg(" ttyPCI%d, recv link close\n", port->port_num);
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	/* Stop using empty buffers of remote system */
	//tvnet_host_stop_tx_queue(tvnet);
	port->cp_state = false;
	msg.msg_id = CTRL_MSG_LINK_CLOSE_ACK;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	port->tx_link_state = DIR_LINK_STATE_CLOSE;
	//pci_ep_update_link_sm(port);
}

static void pci_ep_rcv_link_close_ack(struct pci_port *port)
{
	struct pci_ctrl_msg msg;
	sc_pcie_serial_dbg(" ttyPCI%d, recv link close ack\n", port->port_num);
	//struct ps_port *ps_port = epf_serial->ps_config.ps_info[port].port;
	port->wait_write = false;
	port->cp_state = false;
	wake_up_interruptible(&port->drain_wait);
	msg.msg_id = CTRL_MSG_LINK_DISCONNECT;
	msg.ps_id = port->port_num;
	pci_ep_write_ctrl_msg(port->ctrl_mem, &msg);
	port->tx_link_state = DIR_LINK_STATE_CLOSE;
	pci_ep_update_link_sm(port);
}

static void pci_ep_rcv_link_disconnect(struct pci_port *port)
{
	struct pci_endpoint_serial* ep_serial = g_ep_serial;
	sc_pcie_serial_dbg(" ttyPCI%d, recv link disconnect ack\n", port->port_num);
	if(port->rx_link_state == DIR_LINK_STATE_OPEN)
		port->rx_link_state = DIR_LINK_STATE_DISCONNECT;
	if(!pci_serial_read_space_empty(&ep_serial->ps_config.ps[port->port_num].ctrl_rd, port->port_num)) {
		port->wait_read = true;
		atomic_inc(&ep_serial->ps_config.ps[port->port_num].wakeup_flag);
		wake_up(&ep_serial->ps_config.ps[port->port_num].work_wait);
	}else {
		//port->os_link_state = OS_LINK_STATE_DISCONNECT;
		//wake_up_interruptible(&tvnet->link_state_wq);
		pci_ep_update_link_sm(port);
		pci_ep_update_link_state(port);
	}	
}

//head eroor后，如果写buf不是空，需要拉一个中断过去,pci_write函数中 写buf为空，才拉中断
static void pci_ep_handle_full_error(struct pci_port *port)
{
	struct pci_endpoint_serial* ep_serial = g_ep_serial;
	sc_pcie_serial_dbg(" ttyPCI%d, recv ep write full error info\n", port->port_num);
	pci_serial_cal_static(port->port_num, NULL, 0, 63);
	
	if(!pci_serial_read_space_empty(&ep_serial->ps_config.ps[port->port_num].ctrl_rd, port->port_num)) {
		pci_serial_cal_static(port->port_num, NULL, 0, 89);
		atomic_inc(&ep_serial->ps_config.ps[port->port_num].wakeup_flag);
		wake_up(&ep_serial->ps_config.ps[port->port_num].work_wait);
	}
}

static void pci_ep_handle_head_error(struct pci_port *port)
{
    struct pci_serial_ctrl* ctrl_wr;
	struct pci_endpoint_serial* ep_serial = g_ep_serial;
	struct pci_serial_ctrl *mem_ctrl_wr;
	sc_pcie_serial_dbg(" ttyPCI%d, recv head error info\n", port->port_num);
	pci_serial_cal_static(port->port_num, NULL, 0, 67);
	ctrl_wr = &ep_serial->ps_config.ps[port->port_num].ctrl_wr;
	//将内存转换为结构体的形式读出来
	mem_ctrl_wr = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ctrl_wr->ctrl_offset);
	if(mem_ctrl_wr->sendPos != mem_ctrl_wr->recvPos) {
		pci_serial_cal_static(port->port_num, NULL, 0, 68);
		pci_ep_irq_notify_epf(ep_serial->int_base, PCI_EP_NOTIFY_IRQ_OFFSET, PCI_EP_NOTIFY_DATA);
	}
}


//调用此函数时,表明可以读n字节 必须要保证此时ring非空,当前一整包全部读完时，才去真正更新读指针
static inline void pci_serial_read(struct pci_serial_ctrl* ctrl_rd, struct pci_serial_data* data_rd,
									void *base, void *dest, int n)
{
	int direct = data_rd->sendPos - data_rd->recvPos;
	int rd_cnt = 0;
	if (direct == 0) {
		sc_pcie_serial_dbg("4.1, send pos:%d, recv pos:%d, n:%d, rd_cnt:%d, size:%d\n", data_rd->sendPos, data_rd->recvPos, n, rd_cnt, ctrl_rd->size);
		return;
	}
	else if(direct > 0) //send_pos > recv_pos
	{
		sc_pcie_serial_dbg("4.2, send pos:%d, recv pos:%d, n:%d, rd_cnt:%d, size:%d\n", data_rd->sendPos, data_rd->recvPos, n, rd_cnt, ctrl_rd->size);
		//memcpy(dest, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), n);
		//memcpy_fromio();
		memcpy_fromio(dest, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), n);
		CUR_RECV_POS(data_rd) += n;
		data_rd->offset = CUR_SEND_POS(data_rd);
	}
	else
	{
		rd_cnt = ctrl_rd->size - CUR_RECV_POS(data_rd);
		if(n <= rd_cnt) {
			sc_pcie_serial_dbg("4.3, send pos:%d, recv pos:%d, n:%d, rd_cnt:%d, size:%d\n", data_rd->sendPos, data_rd->recvPos, n, rd_cnt, ctrl_rd->size);
			//memcpy(dest, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), n);
			memcpy_fromio(dest, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), n);
			//CUR_RECV_POS(data_rd) += n;
			CUR_RECV_POS(data_rd) = (CUR_RECV_POS(data_rd) + n)%ctrl_rd->size;
			data_rd->offset = CUR_SEND_POS(data_rd);
		}else {
			//memcpy(dest, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), rd_cnt);
			sc_pcie_serial_dbg("4.4, send pos:%d, recv pos:%d, n:%d, rd_cnt:%d, size:%d\n", data_rd->sendPos, data_rd->recvPos, n, rd_cnt, ctrl_rd->size);
			memcpy_fromio(dest, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), rd_cnt);
			CUR_RECV_POS(data_rd) = 0;
			data_rd->offset = CUR_SEND_POS(data_rd);
			//memcpy(dest + rd_cnt, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), n - rd_cnt);
			memcpy_fromio(dest + rd_cnt, (void *)(base + ctrl_rd->data_offset + CUR_RECV_POS(data_rd)), n - rd_cnt);
			CUR_RECV_POS(data_rd) += n - rd_cnt;
			data_rd->offset = CUR_SEND_POS(data_rd);
		}
	}
}

//调用此函数时，必须要保证此时ring非满,当前一整包全部写完时，才去真正更新写指针
static void pci_serial_write(struct pci_serial_ctrl* ctrl_wr, struct pci_serial_data* data_wr,
							void *base, 
							__const void *src, 
							int n)
{
	unsigned int freeCnt = ctrl_wr->size - CUR_SEND_POS(data_wr);

	if (n == 0)
		return;

	if(freeCnt >= n)
	{
		//memcpy((void *)(base + ctrl_wr->data_offset + CUR_SEND_POS(data_wr)), src, n);
		memcpy_toio((void *)(base + ctrl_wr->data_offset + CUR_SEND_POS(data_wr)), src, n);
		CUR_SEND_POS(data_wr) += n;
		data_wr->offset = CUR_SEND_POS(data_wr);

		if(CUR_SEND_POS(data_wr) == ctrl_wr->size)
			CUR_SEND_POS(data_wr) = 0;
	}
	else
	{
		//memcpy((void *)(base + ctrl_wr->data_offset + CUR_SEND_POS(data_wr)), src, freeCnt);
		//memcpy((void *)(base + ctrl_wr->data_offset), src + freeCnt, n - freeCnt);	
		memcpy_toio((void *)(base + ctrl_wr->data_offset + CUR_SEND_POS(data_wr)), src, freeCnt);
		memcpy_toio((void *)(base + ctrl_wr->data_offset), src + freeCnt, n - freeCnt);	
		CUR_SEND_POS(data_wr) = n-freeCnt;
	}
	data_wr->offset = CUR_SEND_POS(data_wr);
}



static int pci_endpoint_serial_write(struct pci_endpoint_serial *ep_serial, void *buf, int count, u8 index)
{
	int ret = 0;
	unsigned int size = 0;
	struct serial_header_fmt tmpMsgHeader;
	bool wr_empty;
	struct pci_ctrl_msg msg;
	//struct pci_ep_serial_ctrl *ep_ctrl = (struct pci_ep_serial_ctrl *)(ep_serial->bar[ep_serial->tty_reg_bar]);
	struct pci_serial * ps = &ep_serial->ps_config.ps[index];
	struct pci_serial_ctrl* ctrl_wr = &ep_serial->ps_config.ps[index].ctrl_wr;
	struct pci_serial_data* data_wr = &ep_serial->ps_config.ps[index].data_wr;

	size = count + sizeof(struct serial_header_fmt);
	size = ALIGN_PCI_SERIAL(size,PCI_SERIAL_ALIGN);

	spin_lock(&ep_serial->ps_config.ps[index].wr_lock);
	update_cpu_mem_rd_pos(ep_serial->ps_config.base, ctrl_wr, ps->port_num);
	wr_empty = (ctrl_wr->recvPos == ctrl_wr->sendPos);
	if (likely(pci_serial_free_write_space(ctrl_wr, size) == 1)){
		tmpMsgHeader.flag = PCI_WRITE_SERIAL_FLAG;
		tmpMsgHeader.len = count;
		pci_serial_write(ctrl_wr, data_wr, 
						ep_serial->ps_config.base, 
						&tmpMsgHeader, 
						sizeof(struct serial_header_fmt));
		pci_serial_write(ctrl_wr, data_wr, 
						ep_serial->ps_config.base, 
						buf, 
						count);
		ps->port->icount.tx += count;
		asm volatile("dmb sy\n");
		ctrl_wr->sendPos = CUR_SEND_POS(data_wr);
		//UPDATE_MEM_SEND_POS(epf_serial->ps_config.base, ctrl_wr, CUR_SEND_POS(data_wr));
		update_pci_mem_wr_pos(ep_serial->ps_config.base, ctrl_wr, CUR_SEND_POS(data_wr)); //写完再更新写指针
		asm volatile("dmb sy\n");
		
	}
	else
	{
		//ret = RPMSG_SPACE_NOT_ENOUGH;
		printk("%s, PCI_SERIAL_SPACE_NOT_ENOUGH\n", __FUNCTION__);
		msg.msg_id = CTRL_MSG_WRITE_FULL_ERROR;
		msg.ps_id = ps->port->port_num;
		pci_ep_write_ctrl_msg(ps->port->ctrl_mem, &msg);
		pci_serial_cal_static(ps->port->port_num, NULL, 0, 14);
		//BUG_ON(1);
		goto exit;
	}
	//这一块需要加一个状态机,表明是否需要触发MSI中断给rc
	//pci_epf_serial_raise_irq(epf_serial, IRQ_TYPE_MSI,
				      // IRQ_MSI_DATA_WRITE);
	if(unlikely(ps->port->wait_write)) {
		ps->port->wait_write = false;
		//ps->port->os_link_state = OS_LINK_STATE_DISCONNECT;
		//pci_ep_update_link_sm(ps->port);
		ret = count;
		goto exit;
	}
	if(wr_empty) {
		//if (port->tx_link_state == DIR_LINK_STATE_OPEN && port->os_link_state == OS_LINK_STATE_CLOSE)
		//	pci_epf_user_link_close_req(port);
		pci_ep_irq_notify_epf(ep_serial->int_base, PCI_EP_NOTIFY_IRQ_OFFSET, PCI_EP_NOTIFY_DATA);
	}

	ret = count;

exit:
	spin_unlock(&ep_serial->ps_config.ps[index].wr_lock);
	
	return ret;
}

static unsigned int total_count = 0;
//u8 g_pcie_tty_raw[100*60*60];
//u32 g_pcie_tty_raw_cnt=0;

//u8 g_pcie_tty[100*60*60];
//u32 g_pcie_tty_cnt=0;

static int pci_endpoint_serial_read(struct pci_serial *ps_info)
{
	int ret = 0;
	struct serial_header_fmt packet_head;
	unsigned int read_len = 0;
	unsigned int count = 0;
	struct pci_ctrl_msg msg;
	//int size = 0x224 + 0x4;
	//int j = 0;
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
    struct pci_serial_ctrl *mem_ctrl_rd;
	//struct pci_ep_serial_ctrl *ep_ctrl = (struct pci_ep_serial_ctrl *)(ep_serial->bar[ep_serial->tty_reg_bar]);
	//disable_irq(g_epf_serial->data_irq);
	//sc_pcie_serial_dbg("1\n");
	update_cpu_mem_wr_pos(ep_serial->ps_config.base, &ps_info->ctrl_rd, ps_info->port_num);
	//sc_pcie_serial_dbg("2\n");
	while(pci_serial_read_space_empty(&ps_info->ctrl_rd, ps_info->port_num) == 0) //非空时进行读操作
	{
		//sc_pcie_serial_dbg("3\n");
		pci_serial_read(&ps_info->ctrl_rd,&ps_info->data_rd, ep_serial->ps_config.base, &packet_head, sizeof(struct serial_header_fmt));
		//memcpy(&g_pcie_tty_raw[g_pcie_tty_raw_cnt], (u8*)(&packet_head), sizeof(struct serial_header_fmt));
		//g_pcie_tty_raw_cnt = g_pcie_tty_raw_cnt + sizeof(struct serial_header_fmt);
		pci_serial_cal_static(ps_info->port_num, (char *)&packet_head, sizeof(struct serial_header_fmt), 36);
		//sc_pcie_serial_dbg("5\n");
		if (unlikely(packet_head.flag != PCI_READ_SERIAL_FLAG))
		{
			sc_pcie_serial_dbg(" head format is error, please check, head:%08x, len:%08x\n", packet_head.flag, packet_head.len);
			sc_pcie_serial_dbg(" one circle read finish, send pos:%d, recv pos:%d size:%d\n", ps_info->data_rd.sendPos, ps_info->data_rd.recvPos, total_count);
			sc_pcie_serial_dbg(" one circle read finish, send pos:%d, recv pos:%d size:%d\n", ps_info->ctrl_rd.sendPos, ps_info->ctrl_rd.recvPos, total_count);
			pci_serial_cal_static(ps_info->port_num, NULL, 0, 12);
			//BUG_ON(1);
			mem_ctrl_rd = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ps_info->ctrl_rd.ctrl_offset);
			ps_info->ctrl_rd.sendPos = mem_ctrl_rd->sendPos;
			ps_info->ctrl_rd.recvPos = ps_info->ctrl_rd.sendPos;
			ep_serial->ps_config.ps[ps_info->port_num].data_rd.sendPos =  ps_info->ctrl_rd.sendPos;
			ep_serial->ps_config.ps[ps_info->port_num].data_rd.recvPos =  ps_info->ctrl_rd.recvPos;
			mem_ctrl_rd->recvPos = ps_info->ctrl_rd.sendPos;  //丢弃剩余的数据
			
			msg.msg_id = CTRL_MSG_HEAD_ERROR;
			msg.ps_id = ps_info->port->port_num;
			pci_ep_write_ctrl_msg(ps_info->port->ctrl_mem, &msg);
			ret = ~PCI_READ_SERIAL_FLAG;
			goto exit;
		}
		read_len = packet_head.len;
		 // read_buffer可能需要使用ring,不然可能会被覆盖,当tty层处理较慢时
		pci_serial_read(&ps_info->ctrl_rd,&ps_info->data_rd, ep_serial->ps_config.base, read_buffer[ps_info->port_num], read_len);
		//memcpy(&g_pcie_tty_raw[g_pcie_tty_raw_cnt], read_buffer[ps_info->port_num], read_len);
		//g_pcie_tty_raw_cnt = g_pcie_tty_raw_cnt + read_len;
		asm volatile("dmb sy\n");
		ps_info->ctrl_rd.recvPos = CUR_RECV_POS(&ps_info->data_rd); //更新读指针
		//UPDATE_MEM_RECV_POS(ep_serial->ps_config.base, &ps_info->ctrl_rd, CUR_SEND_POS(&ps_info->data_rd));
		update_pci_mem_rd_pos(ep_serial->ps_config.base, &ps_info->ctrl_rd, CUR_RECV_POS(&ps_info->data_rd));
		asm volatile("dmb sy\n");
		total_count += read_len;
		ps_info->port->icount.rx += read_len;
		sc_pcie_serial_dbg(" one packet success, head:%08x ,len:%08x, total len:%u\n", packet_head.flag, packet_head.len, total_count);
		count = tty_insert_flip_string(&ps_info->port->port,
						   read_buffer[ps_info->port_num], read_len);
		tty_flip_buffer_push(&ps_info->port->port);
		pci_serial_cal_static(ps_info->port_num, read_buffer[ps_info->port_num], read_len, 13);
		//memcpy(&g_pcie_tty[g_pcie_tty_cnt], read_buffer[ps_info->port_num], read_len);
		//g_pcie_tty_cnt = g_pcie_tty_cnt + read_len;
		if(count != read_len) {
			pci_serial_cal_static(ps_info->port_num, NULL, 0, 64);
			/* stop pushing; TTY layer can't handle more */
			printk("ttyPCI%d: rx block %d / %d\n",
					  ps_info->port->port_num, count, read_len);
				break;
		}
			
	}
exit:
	ps_info->port->reading = false;
	pci_serial_cal_static(ps_info->port_num, NULL, 0, 16); //one circle read finished
	if(unlikely(ps_info->port->wait_read)) {
		ps_info->port->wait_read = false;
		//ps_info->port->os_link_state = OS_LINK_STATE_DISCONNECT;
		pci_ep_update_link_sm(ps_info->port);
		pci_ep_update_link_state(ps_info->port);
	}
	
	sc_pcie_serial_dbg(" one circle read finish, send pos:%d, recv pos:%d size:%d\n", 
			ps_info->ctrl_rd.sendPos, ps_info->ctrl_rd.recvPos, total_count);
	//enable_irq(g_epf_serial->data_irq);
	return ret;
}

//这个中断可以用来报告pcie链路状态,以及控制信息
static irqreturn_t pci_ctrl_isr_serial(int irq, void *data){

	static int eint_ctrl_cnt = 0;
	struct pci_endpoint_serial *epf_serial = (struct pci_endpoint_serial *)data;
	struct pci_ctrl_buf * ctrl_mem = &epf_serial->ps_config.ctrl_mem;
	struct pci_ctrl_msg msg;
	//struct pci_serial_config *  ps_config = epf_serial
	sc_pcie_serial_dbg(" eint_ctrl_cnt get = %d, irq=%d\n", ++eint_ctrl_cnt, irq);

	while (!pci_serial_ctrl_rd_empty(ctrl_mem->ctrl_cnt)) {
		pci_epf_read_ctrl_msg(ctrl_mem, &msg);
		if (msg.msg_id == CTRL_MSG_LINK_OPEN) 	//rc侧先执行关闭操作
			pci_ep_rcv_link_open_msg(epf_serial->ps_config.ps[msg.ps_id].port);
			//pci_epf_rcv_link_open_msg(epf_serial, msg.ps_id);
		else if (msg.msg_id == CTRL_MSG_LINK_CLOSE)	 //rc侧先执行关闭操作
			pci_ep_rcv_link_close_msg(epf_serial->ps_config.ps[msg.ps_id].port);
		//rc 表明需要将剩余待传输的数据都传完之后，发disconnect消息，让对端去读取并push数据
		else if (msg.msg_id == CTRL_MSG_LINK_CLOSE_ACK) //rc侧先执行关闭操作
			pci_ep_rcv_link_close_ack(epf_serial->ps_config.ps[msg.ps_id].port);
		//ep回disconnect,表明他想发的数据都已经发过来了,rc需要将数据push给上层，在通知offline消息
		else if (msg.msg_id == CTRL_MSG_LINK_DISCONNECT) //ep侧先关闭
			pci_ep_rcv_link_disconnect(epf_serial->ps_config.ps[msg.ps_id].port);
		else if (msg.msg_id == CTRL_MSG_LINK_ALREADY_OPEN)
			pci_ep_rcv_link_already_open_msg(epf_serial->ps_config.ps[msg.ps_id].port);
		else if (msg.msg_id == CTRL_MSG_WRITE_FULL_ERROR) {
			struct pci_port * port = epf_serial->ps_config.ps[msg.ps_id].port;
			pci_ep_handle_full_error(port); //这块逻辑需要重新整合下
		}else if (msg.msg_id == CTRL_MSG_HEAD_ERROR) {
			struct pci_port * port = epf_serial->ps_config.ps[msg.ps_id].port;
			pci_ep_handle_head_error(port);
		}
	}
	
	return IRQ_HANDLED;
}

//只负责通知rc去读取数据
static irqreturn_t pci_data_isr_serial(int irq, void *data)
{
	//int i = 0;
	int index = 0;
	struct pci_serial_ctrl *ctrl_rd; 
	struct pci_endpoint_serial * ep_serial = (struct pci_endpoint_serial *)data;
	
	static int eint_data_cnt = 0;
	
	for(index = 0; index < MAX_PCI_SERIAL_PORTS; index++)
	{
		struct pci_serial *ps = &ep_serial->ps_config.ps[index];
		if(!ps->port->used || ps->port->reading)
			continue;
		
		ctrl_rd = (struct pci_serial_ctrl*)(ep_serial->ps_config.base + ps->ctrl_rd.ctrl_offset);
		if(!(ctrl_rd->sendPos == ctrl_rd->recvPos)){
			ps->port->reading = true;
			atomic_inc(&ps->wakeup_flag);
			wake_up(&ps->work_wait);
			g_flg_enter_th++;
		}
	}
	sc_pcie_serial_dbg(" eint_data_cnt get = %d, irq=%d\n", ++eint_data_cnt, irq);
	
	return IRQ_HANDLED;
}


static bool pci_endpoint_serial_request_irq(struct pci_endpoint_serial *ep_serial)
{
	int i = 3;
	int err;
	struct pci_dev *pdev = ep_serial->pdev;
	struct device *dev = &pdev->dev;

	err = devm_request_irq(dev, pci_irq_vector(pdev, i),
				       pci_ctrl_isr_serial,
				       IRQF_SHARED, DRV_MODULE_NAME, ep_serial);
	if (err)
		goto fail;
	i++;
	err = devm_request_irq(dev, pci_irq_vector(pdev, i),
				       pci_data_isr_serial,
				       IRQF_SHARED, DRV_MODULE_NAME, ep_serial);
	if (err)
		goto fail;

	
	return true;
	
fail:
	switch (ep_serial->irq_type) {
	case IRQ_TYPE_LEGACY:
		dev_err(dev, "Failed to request IRQ %d for Legacy\n",
			pci_irq_vector(pdev, i));
		break;
	case IRQ_TYPE_MSI:
		dev_err(dev, "Failed to request IRQ %d for MSI %d\n",
			pci_irq_vector(pdev, i),
			i + 1);
		break;
	case IRQ_TYPE_MSIX:
		dev_err(dev, "Failed to request IRQ %d for MSI-X %d\n",
			pci_irq_vector(pdev, i),
			i + 1);
		break;
	}

	return false;
}



int pci_epf_rx_work(void *ptr)
{
	int wait_event_ret = 0;
	int ret = 0;

	struct pci_serial *ps = (struct pci_serial *)ptr;

	while(!kthread_should_stop())
	{
	
		wait_event_ret = wait_event_interruptible(ps->work_wait, atomic_read(&ps->wakeup_flag) ||
			kthread_should_stop());
		if(atomic_read(&ps->wakeup_flag) > 0){
			atomic_dec(&ps->wakeup_flag);
		}
		if(kthread_should_stop())
		{
			break;
		}
        
		spin_lock(&ps->rd_lock);	
		
		//这里需要考虑是否禁用中断
		//disable_irq(ep_serial->data_irq);
		ret = pci_endpoint_serial_read(ps);
		//enable_irq(ep_serial->data_irq);
		//pci_ep_irq_notify_epf(ep_serial->int_base, PCI_EP_NOTIFY_IRQ_OFFSET, PCI_EP_NOTIFY_DATA);
		spin_unlock(&ps->rd_lock);
	}	
	
	return 0;
}


static void pci_serial_alloc_space(struct pci_dev *pdev)
{
	int i = 0;
	//char thread_name[64];
	//struct pci_endpoint_serial *ep_serial = pci_get_drvdata(pdev);
	struct sc_pci_pub *sc_pci_pub_priv = pci_get_drvdata(pdev);
	struct pci_endpoint_serial *ep_serial = (struct pci_endpoint_serial *)(sc_pci_pub_priv->sc_pci_ep_serial_priv);
	struct pci_serial_config * ps_config = NULL;
	ep_serial->ps_config.base = ep_serial->base;
	ps_config = &ep_serial->ps_config;
	mutex_init(&ep_serial->ps_config.lock);

	ep_serial->ps_config.offset = PCI_SERIAL_RESERVED_BAR_SIZE;
	ps_config->ctrl_mem.ctrl_cnt_offset = ep_serial->ps_config.offset;
	ps_config->ctrl_mem.ctrl_cnt = (struct pci_ctrl_cnt *)(ps_config->base + ps_config->ctrl_mem.ctrl_cnt_offset);
	ep_serial->ps_config.offset += sizeof(struct pci_ctrl_cnt);
	
	ps_config->ctrl_mem.ctrl_rd_offset = ep_serial->ps_config.offset;
	ps_config->ctrl_mem.ctrl_rd_msgs = (struct pci_ctrl_msg *)(ps_config->base + ps_config->ctrl_mem.ctrl_rd_offset);
	ps_config->ctrl_mem.ctrl_rd_size = PCI_SERIAL_CTRL_RD_MEM_SIZE;
	ep_serial->ps_config.offset += ps_config->ctrl_mem.ctrl_rd_size;
	
	ps_config->ctrl_mem.ctrl_wr_offset = ep_serial->ps_config.offset;
	ps_config->ctrl_mem.ctrl_wr_msgs = (struct pci_ctrl_msg *)(ps_config->base + ps_config->ctrl_mem.ctrl_wr_offset);
	ps_config->ctrl_mem.ctrl_wr_size = PCI_SERIAL_CTRL_WR_MEM_SIZE;
	ep_serial->ps_config.offset += ps_config->ctrl_mem.ctrl_wr_size;
	
	for(i = 0; i <  MAX_PCI_SERIAL_PORTS; i++) {
		ep_serial->ps_config.ps[i].ctrl_rd.ctrl_offset = ep_serial->ps_config.offset; //ep 读
		ep_serial->ps_config.offset += sizeof(struct pci_serial_ctrl);
		ep_serial->ps_config.ps[i].ctrl_rd.data_offset = ep_serial->ps_config.offset; //ep读
		ep_serial->ps_config.ps[i].ctrl_rd.size = PCI_SERIAL_RD_MEMROY_SIZE; //ep 读 size
		ep_serial->ps_config.offset += ep_serial->ps_config.ps[i].ctrl_rd.size;
		
		ep_serial->ps_config.ps[i].ctrl_wr.ctrl_offset = ep_serial->ps_config.offset; //ep 写控制
		ep_serial->ps_config.offset += sizeof(struct pci_serial_ctrl);
		ep_serial->ps_config.ps[i].ctrl_wr.data_offset = ep_serial->ps_config.offset; //ep写 数据
		ep_serial->ps_config.ps[i].ctrl_wr.size = PCI_SERIAL_WR_MEMROY_SIZE; //ep 写 size
		ep_serial->ps_config.offset += ep_serial->ps_config.ps[i].ctrl_wr.size;

		ep_serial->ps_config.ps[i].port_num = i;
		spin_lock_init(&ep_serial->ps_config.ps[i].rd_lock);
		spin_lock_init(&ep_serial->ps_config.ps[i].wr_lock);
		init_waitqueue_head(&ep_serial->ps_config.ps[i].work_wait);
		atomic_set(&ep_serial->ps_config.ps[i].wakeup_flag, 0);
#if 0
		sprintf(thread_name, "pci_serial_rx_th_%d", i);
		//ep_serial->ps_config.ps[i].rd_work_thread = kthread_run(pci_epf_rx_work, (void *)(&(ep_serial->ps_config.ps[i])), "pci_rx_thread");
		ep_serial->ps_config.ps[i].rd_work_thread = kthread_run(pci_epf_rx_work, (void *)(&(ep_serial->ps_config.ps[i])), thread_name);
		BUG_ON(IS_ERR(ep_serial->ps_config.ps[i].rd_work_thread));
		wake_up_process(ep_serial->ps_config.ps[i].rd_work_thread);
#endif
		//init_waitqueue_head(&ep_serial->ps_config.ps[i].work_wait);
		//atomic_set(&ep_serial->ps_config.ps[i].wakeup_flag, 0);
	}
    pci_serial_recal_ctrl_pos(ps_config->ctrl_mem.ctrl_cnt);
}

static unsigned int pci_tty_write_room(struct tty_struct *tty)
{
	struct pci_port	*port = tty->driver_data;
	unsigned long	flags;
	unsigned int		room = 0;

	spin_lock_irqsave(&port->port_lock, flags);
	if (port->used)
		room = kfifo_avail(&port->port_write_buf);
	spin_unlock_irqrestore(&port->port_lock, flags);

	//sc_pcie_serial_dbg("gs_write_room: (%d,%p) room=%d\n", port->port_num, tty, room);

	return room;
}

static unsigned int pci_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct pci_port	*port = tty->driver_data;
	unsigned long	flags;
	unsigned int		chars = 0;

	spin_lock_irqsave(&port->port_lock, flags);
	chars = kfifo_len(&port->port_write_buf);
	spin_unlock_irqrestore(&port->port_lock, flags);

	sc_pcie_serial_dbg("pci_tty_chars_in_buffer: (%d,%p) chars=%u\n",
		port->port_num, tty, chars);

	return chars;
}

static int pci_tty_open(struct tty_struct *tty, struct file *file)
{
	int		status;
	int		port_num = tty->index;
	struct pci_port	*port = tty->driver_data;
	
	do {
		if (!port){ //这里可以考虑,port->used为true,该条件由RC侧设置
	        sc_pcie_serial_dbg(" ttyPCI not device,port_num = %d \n", port_num);
			status = -ENODEV;
		}else {
			spin_lock_irq(&port->port_lock);

			/* already open?  Great. */
			if (port->port.count) {
				status = 0;
				port->port.count++;
				sc_pcie_serial_dbg(" already open, port_num = %d \n", port_num);

			/* currently opening/closing? wait ... */
			} else if (port->openclose) {
				status = -EBUSY;

			/* ... else we do the work */
			} else {
				status = -EAGAIN;
				port->openclose = true;
			}
			spin_unlock_irq(&port->port_lock);
		}
		//mutex_unlock(&ports[port_num].lock);

		switch (status) {
		default:
			/* fully handled */
			return status;
		case -EAGAIN:
			/* must do the work */
			break;
		case -EBUSY:
			/* wait for EAGAIN task to finish */
			msleep(1);
			/* REVISIT could have a waitchannel here, if
			 * concurrent open performance is important
			 */
			break;
		}
	} while (status != -EAGAIN);

	/* Do the "real open" */
	spin_lock_irq(&port->port_lock);

	/* allocate circular buffer on first open */
	if (!kfifo_initialized(&port->port_write_buf)) {

		spin_unlock_irq(&port->port_lock);
		status = kfifo_alloc(&port->port_write_buf,
				     PCI_SERIAL_WRITE_BUF_SIZE, GFP_KERNEL);
		spin_lock_irq(&port->port_lock);

		if (status) {
			sc_pcie_serial_dbg(" ttyPS%d (%p,%p) no buffer\n",
				port->port_num, tty, file);
			port->openclose = false;
			goto exit_unlock_port;
		}
	}

	if (!kfifo_initialized(&port->port_read_buf)) {

		spin_unlock_irq(&port->port_lock);
		status = kfifo_alloc(&port->port_read_buf,
				     PCI_SERIAL_READ_BUF_SIZE, GFP_KERNEL);
		spin_lock_irq(&port->port_lock);

		if (status) {
			sc_pcie_serial_dbg(" ttyPCI%d (%p,%p) no buffer\n",
				port->port_num, tty, file);
			port->openclose = false;
			goto exit_unlock_port;
		}
	}

	/* REVISIT if REMOVED (ports[].port NULL), abort the open
	 * to let rmmod work faster (but this way isn't wrong).
	 */

	/* REVISIT maybe wait for "carrier detect" */
	port->port.tty = tty;
	port->port.count = 1;
	port->openclose = false;
	 //实际并没有应用处理offline消息，这里将其注释
	//if (port->rx_link_state == DIR_LINK_STATE_CLOSE)
	pci_ep_user_link_open_req(port);
#if PCI_SERIAL_AP_NOTIFY_CP
	if(port->tx_link_state == DIR_LINK_STATE_SENT_OPEN)
		wait_event_interruptible_timeout(port->open_wait,
					port->ep2host_open,
					PS_CLOSE_TIMEOUT * HZ);
	port->ep2host_open = false;
#endif
	tty_wakeup(port->port.tty);
	port->used = true;
	port->icount.rx = 0;
	port->icount.tx = 0;
	g_flg_enter_th = 0;
	g_ps_static_cnt[port->port_num] = 0;
	sc_pcie_serial_dbg(" ttyPCI%d (%p,%p)\n", port->port_num, tty, file);

	pci_serial_cal_static(port->port_num, NULL, 0, 1);
	
	status = 0;

exit_unlock_port:
	spin_unlock_irq(&port->port_lock);
	return status;
}

//这块或者判断对方是否读完,即是否为空
static int pci_tty_writes_finished(struct pci_port *p)
{
	int cond;
	struct pci_endpoint_serial *ep_serial = g_ep_serial;
	struct pci_serial_ctrl *ctrl_wr = &ep_serial->ps_config.ps[p->port_num].ctrl_wr;
	ctrl_wr = (struct pci_serial_ctrl *)(ep_serial->ps_config.base + ctrl_wr->ctrl_offset);
	/* return true on disconnect or empty buffer */
	spin_lock_irq(&p->port_lock);
	//kfifo_reset(&p->port_write_buf);//直接清空fifo
	cond = pci_serial_write_space_full(ctrl_wr);
	//cond = !kfifo_len(&p->port_write_buf); //disconnect condition not do
	spin_unlock_irq(&p->port_lock);

	return cond;
}


static void pci_tty_close(struct tty_struct *tty, struct file *file)
{
	struct pci_port *port = tty->driver_data;    
	int		port_num = tty->index;
    struct pci_endpoint_serial *ep_serial = NULL;
	struct pci_serial_ctrl *ctrl_wr = NULL;
	//maybe the port is null,do we need check?
   if(port == NULL)
   {
       sc_pcie_serial_dbg("--->[%s-%d]:port is NULL,port_num = %d\n",__func__,__LINE__ ,port_num);   
       return ;
   }
	sc_pcie_serial_dbg(" ttyPCI%d ,port = 0x%llx\n", port_num, port);   
   
	spin_lock_irq(&port->port_lock);
	if (port->port.count != 1) {
		if (port->port.count == 0)
			WARN_ON(1);
		else
			--port->port.count;
		sc_pcie_serial_dbg("app still holding ttyPCI%d handle\n", port_num);
		pci_serial_cal_static(port->port_num, NULL, 0, 19);
		goto exit;
	}

	//pr_debug("pci_close: ttyPCI%d (%p,%p) ...\n", port->port_num, tty, file);

	/* mark port as closing but in use; we can drop port lock
	 * and sleep if necessary
	 */
	port->openclose = true;
	port->port.count = 0;
	
#if 0
	if(port->tx_link_state == DIR_LINK_STATE_SENT_OPEN)
		wait_event_interruptible_timeout(port->open_wait,
					port->ep2host_open,
					PS_CLOSE_TIMEOUT * HZ);
	port->ep2host_open = false;
#endif
	port->ep2host_open = false;
	//if (port->rx_link_state == DIR_LINK_STATE_OPEN && port->os_link_state == OS_LINK_STATE_OPEN) { //说明对方没有先发起关动作
	

	//wake_up_interruptible(&port->drain_wait);
	if (port->port.tty)
		tty_hangup(port->port.tty);

	/* wait for circular write buffer to drain, disconnect, or at
	 * most PS_CLOSE_TIMEOUT seconds; then discard the rest
	 */
	ep_serial = g_ep_serial;
	ctrl_wr = &ep_serial->ps_config.ps[port->port_num].ctrl_wr;
	ctrl_wr = (struct pci_serial_ctrl *)(ep_serial->ps_config.base + ctrl_wr->ctrl_offset);

	if (port->rx_link_state == DIR_LINK_STATE_OPEN) { //说明对方没有先发起关动作
		port->wait_write = true;
		pci_ep_user_link_close_req(port);

		if (!pci_serial_write_space_full(ctrl_wr)) {
			spin_unlock_irq(&port->port_lock);
			pci_serial_cal_static(port->port_num, NULL, 0, 17);
		
			wait_event_interruptible_timeout(port->drain_wait,
						pci_tty_writes_finished(port),
						PS_CLOSE_TIMEOUT * HZ);
			
			spin_lock_irq(&port->port_lock);
		}
	}else{	//对方已经关闭
		port->rx_link_state = DIR_LINK_STATE_CLOSE;
		pci_ep_update_link_sm(port);
		//wake_up_interruptible(&port->drain_wait);
	}

	/* Iff we're disconnected, there can be no I/O in flight so it's
	 * ok to free the circular buffer; else just scrub it.  And don't
	 * let the push tasklet fire again until we're re-opened.
	 */
	kfifo_reset(&port->port_write_buf);
	kfifo_reset(&port->port_read_buf);

	port->port.tty = NULL;

	port->openclose = false;
	port->used = false;
	//wake_up(&port->close_wait);
	sc_pcie_serial_dbg(" ttyPCI%d ,port = 0x%llx\n", port_num, port);

	pci_serial_cal_static(port->port_num, NULL, 0, 18);
exit:
	spin_unlock_irq(&port->port_lock);
}


static int pci_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct pci_port	*port = tty->driver_data;
	struct device *fdev = tty->dev->parent;
	struct sc_pci_pub *sc_pci_pub_priv = dev_get_drvdata(fdev);
	//struct pci_endpoint_serial *ep_serial = dev_get_drvdata(fdev);
	struct pci_endpoint_serial *ep_serial = (struct pci_endpoint_serial *)sc_pci_pub_priv->sc_pci_ep_serial_priv;
	unsigned long	flags;
	
	int fifo_in_cnt = -1;
	u8 index = port->port_num;

	spin_lock_irqsave(&port->port_lock, flags);
#if 0
	if (count){
		fifo_in_cnt = kfifo_in(&port->port_write_buf, buf, count);
		
	}
#endif
	/* treat count == 0 as flush_chars()  from kfifo dequeue*/
	
	if (port->used && port->tx_link_state == DIR_LINK_STATE_OPEN)
		fifo_in_cnt = pci_endpoint_serial_write(ep_serial, (void *)buf, count, index);
	else
	{
		sc_pcie_serial_dbg("link state is not ok\n");
		kfifo_reset(&port->port_write_buf);
	}
	spin_unlock_irqrestore(&port->port_lock, flags);
	
	sc_pcie_serial_dbg("ttyPCI%d (%p) writing %d bytes, success tx %d bytes\n", port->port_num, tty, count, fifo_in_cnt);
	
	return fifo_in_cnt;
}


static const struct tty_operations pci_tty_ops = {
	.install =		pci_tty_install,
	.open =			pci_tty_open,
	.close =		pci_tty_close,
	//.cleanup =		pci_tty_cleanup,
	//.hangup =		pci_tty_hangup,
	.write =		pci_tty_write,
	.write_room =	pci_tty_write_room,
	//.ioctl =		pci_tty_ioctl,
	.throttle =		pci_tty_throttle,
	.unthrottle =	pci_tty_unthrottle,
	.chars_in_buffer =	pci_tty_chars_in_buffer,
	//.break_ctl =	pci_tty_break_ctl,
	//.set_termios =	pci_tty_set_termios,
	//.tiocmget =		pci_tty_tiocmget,
	//.tiocmset =		pci_tty_tiocmset,
	//.get_serial =	pci_get_serial_info,
	//.set_serial =	pci_set_serial_info,
	//.get_icount =	pci_tty_get_icount,
};

void sc_pci_endpoint_serial_remove(struct pci_dev *pdev)
{
	int i = 0;
	//struct pci_endpoint_serial *ep_serial = pci_get_drvdata(pdev);
	struct sc_pci_pub *sc_pci_pub_priv = pci_get_drvdata(pdev);
	struct pci_endpoint_serial *ep_serial = (struct pci_endpoint_serial *)(sc_pci_pub_priv->sc_pci_ep_serial_priv);

	for(i = 0; i < MAX_PCI_SERIAL_PORTS; i++)
		kthread_stop(ep_serial->ps_config.ps[i].rd_work_thread);
	
	pci_iounmap(pdev, ep_serial->base);

	pci_endpoint_serial_release_irq(ep_serial);
	pci_endpoint_serial_free_irq_vectors(ep_serial);

}
EXPORT_SYMBOL_GPL(sc_pci_endpoint_serial_remove);


static int pci_serial_init(void)
{
	int status = 0;
	sc_pcie_serial_dbg("pci_serial_init start\n");
	pci_tty_driver = tty_alloc_driver(MAX_PCI_SERIAL_PORTS, 0);
	if (!pci_tty_driver) {
		sc_pcie_serial_dbg("pci_tty_driver is null\n");
		return -ENOMEM;
	}

	pci_tty_driver->driver_name = "pci_serial";
	pci_tty_driver->name = "ttyPCI";
	/* uses dynamically assigned dev_t values */

	pci_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	pci_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	pci_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	pci_tty_driver->init_termios = tty_std_termios;

	/* 9600-8-N-1 ... matches defaults expected by "usbser.sys" on
	 * MS-Windows.  Otherwise, most of these flags shouldn't affect
	 * anything unless we were to actually hook up to a serial line.
	 */
	pci_tty_driver->init_termios.c_cflag =
			B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	pci_tty_driver->init_termios.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ISIG);
	pci_tty_driver->init_termios.c_iflag &= ~(ICRNL | IXON);
	pci_tty_driver->init_termios.c_oflag &= ~OPOST;
	pci_tty_driver->init_termios.c_ispeed = 115200;
	pci_tty_driver->init_termios.c_ospeed = 115200;

	tty_set_operations(pci_tty_driver, &pci_tty_ops);

	/* export the driver ... */
	status = tty_register_driver(pci_tty_driver);
	if (status) {
		pr_err("%s: cannot register, err %d\n",
				__func__, status);
		goto fail;
	}

	sc_pcie_serial_dbg("%s: registered %d ttyPCI* device%s\n", __func__,
			MAX_PCI_SERIAL_PORTS,
			(MAX_PCI_SERIAL_PORTS == 1) ? "" : "s");
	return 0;
fail:
	tty_driver_kref_put(pci_tty_driver);
	pci_tty_driver = NULL;
	return status;
}

int sc_pci_endpoint_serial_probe(struct pci_dev *pdev)
{
   int err = -1;
   enum pci_barno bar;
   struct device *dev = &pdev->dev;
   struct pci_endpoint_serial *ep_serial;
   enum pci_barno tty_reg_bar = BAR_0;
   struct sc_pci_pub *sc_pci_pub_priv = pci_get_drvdata(pdev);

   sc_pcie_serial_dbg("%s, %d, pci_endpoint_serial_probe start\n", __FUNCTION__,__LINE__);
   ep_serial = devm_kzalloc(dev, sizeof(*ep_serial), GFP_KERNEL);
   if (!ep_serial)
	   return -ENOMEM;
   
   sc_pci_pub_priv->sc_pci_ep_serial_priv = ep_serial;

   ep_serial->tty_reg_bar = BAR_2;
   ep_serial->alignment = SZ_64K;
   ep_serial->pdev = pdev;
   ep_serial->irq_type = IRQ_TYPE_MSI;
   ep_serial->num_irqs = 8;
   tty_reg_bar = ep_serial->tty_reg_bar;

   mutex_init(&ep_serial->mutex);

   if (!pci_endpoint_serial_request_irq(ep_serial))
	   goto final;
   err = pci_serial_init();
   if(err)
   	   goto final;

   ep_serial->int_base = sc_pci_pub_priv->mmio_bar5;
   ep_serial->base = pci_ioremap_bar(pdev, ep_serial->tty_reg_bar);
   if (!ep_serial->base) {
	   err = -ENOMEM;
	   dev_err(dev, "Cannot perform PCI test without BAR%d\n",
		   tty_reg_bar);
	   goto err_iounmap;
   }
   bar = ep_serial->tty_reg_bar;
   sc_pcie_serial_dbg("resource start:0x%08x, end:0x%08x len:0x%08x, base:0x%llx\n", pci_resource_start(pdev, bar), 
					   pci_resource_end(pdev, bar),pci_resource_len(pdev, bar), ep_serial->base);
   g_ep_serial = ep_serial;

   ep_serial->fdev = &pdev->dev;
   
   pci_serial_alloc_space(pdev);

   err = pserial_alloc_port(ep_serial);
   if(err)
	   goto err_port;  //需要进行错误处理

   sc_pcie_serial_dbg("end\n");

   return 0;
   
err_port:
err_iounmap:
   pci_iounmap(pdev, ep_serial->base);
final:
   devm_kfree(dev, ep_serial);
   sc_pci_pub_priv->sc_pci_ep_serial_priv = NULL;
   return err;
}
EXPORT_SYMBOL_GPL(sc_pci_endpoint_serial_probe);

