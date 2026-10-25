 /*
 * Copyright (C) 2012-2022 Microarray Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "madev.h"
#include <linux/wait.h>
#include <linux/freezer.h>
/*spdev use for recording the data for other use*/
static unsigned int is_enable_irq;
static unsigned int u1_flag;
static unsigned int u2_flag;
static int irq_flag;
static unsigned int ma_drv_reg;
static unsigned int is_screen_on;
static unsigned int ma_speed;
static unsigned int int_pin_state;
static struct notifier_block notifier;
static struct spi_device *ma_spi_t;
static DECLARE_WAIT_QUEUE_HEAD(gWaitq);
static DECLARE_WAIT_QUEUE_HEAD(U1_Waitq);
static DECLARE_WAIT_QUEUE_HEAD(U2_Waitq);
struct wakeup_source *gProcessWakeLock;
struct work_struct gWork;
struct workqueue_struct *gWorkq;
static struct fprint_dev *sdev;
static struct input_dev *ma_input_t;

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_lock);
static DEFINE_MUTEX(drv_lock);
static DEFINE_MUTEX(ioctl_lock);
static DECLARE_WAIT_QUEUE_HEAD(drv_waitq);

static bool irq_enabled;
static unsigned int g_factory_flag;

extern struct fprint_gpio mafp_gpio;

static void mas_work(struct work_struct *pws)
{
	irq_flag = 1;
	__pm_wakeup_event(gProcessWakeLock, 3000);
	wake_up(&gWaitq);
}

static irqreturn_t mas_interrupt(int irq, void *dev_id)
{
#ifdef DOUBLE_EDGE_IRQ
	if (mas_get_int_value(0) == 1) {
	/*TODO IRQF_TRIGGER_RISING*/
	} else {
	/*TODO IRQF_TRIGGER_FALLING*/
	}
#else
	queue_work(gWorkq, &gWork);
#endif
	return IRQ_HANDLED;
}

static void mas_set_input(void)
{
	struct input_dev *input = NULL;
	int ret = 0;

	MALOG(INFO_LOG, "start");

	input = input_allocate_device();
	if (!input) {
		MALOG(ERR_LOG, "input_allocate_device failed.");
		return;
	} else {
		MALOG(INFO_LOG, "input_allocate_device success");
	}

	set_bit(EV_KEY, input->evbit);
	set_bit(FINGERPRINT_SWIPE_UP, input->keybit); /*单触*/
	set_bit(FINGERPRINT_SWIPE_DOWN, input->keybit);
	set_bit(FINGERPRINT_SWIPE_LEFT, input->keybit);
	set_bit(FINGERPRINT_SWIPE_RIGHT, input->keybit);
	set_bit(FINGERPRINT_TAP, input->keybit);
	set_bit(FINGERPRINT_DTAP, input->keybit);
	set_bit(FINGERPRINT_LONGPRESS, input->keybit);

	set_bit(KEY_POWER, input->keybit);

	input->name = MA_CHR_DEV_NAME;
	input->id.bustype = BUS_SPI;
	ret = input_register_device(input);
	if (ret != 0) {
		input_free_device(input);
		MALOG(ERR_LOG, "failed to register input device.");
		return;
	} else {
		MALOG(INFO_LOG, "input_register_device success");
	}
	ma_input_t  = input;

	MALOG(INFO_LOG, "normal end");
}

/* static int mas_ioctl (struct inode *node, struct file *filp, unsigned int cmd, uns igned long arg)
** this function only supported while the linux kernel version under v2.6.36,while the kernel version under v2.6.36, use
* this line
*/
static long mas_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ioctl_ret = 0, ret = 0;
	int cp_ret;
	unsigned int version = MA_DRV_VERSION;

	MALOG(DEBUG_LOG, "start");

	switch (cmd) {
	case MA_IOC_DELK:
		MALOG(INFO_LOG, "MA_IOC_DELK");
		__pm_wakeup_event(gProcessWakeLock, jiffies_to_msecs(5*HZ));
		MALOG(DEBUG_LOG, "__pm_wakeup_event");
		break;
	case MA_IOC_SLEP:
		MALOG(INFO_LOG, "MA_IOC_SLEP");
		irq_flag = 0;
		ioctl_ret = wait_event_freezekillable_unsafe(gWaitq,
				irq_flag != 0);
		MALOG(DEBUG_LOG, "wait_event_freezekillable_unsafe ioctl_ret=%d", ioctl_ret);
		return ioctl_ret;
	case MA_IOC_WKUP:
		MALOG(INFO_LOG, "MA_IOC_WKUP");
		/*wake up, schedule the process into the runqueue*/
		irq_flag = 1;
		wake_up(&gWaitq);
		MALOG(DEBUG_LOG, "wake_up gWaitq");
		break;
	case MA_IOC_ENCK:
		MALOG(INFO_LOG, "MA_IOC_ENCK");
		/*if the spi clock is not opening always, do this methods*/
		mas_enable_spi_clock(ma_spi_t);
		MALOG(DEBUG_LOG, "mas_enable_spi_clock");
		break;
	case MA_IOC_DICK:
		MALOG(INFO_LOG, "MA_IOC_DICK");
		/*disable the spi clock*/
		mas_disable_spi_clock(ma_spi_t);
		MALOG(DEBUG_LOG, "mas_disable_spi_clock");
		break;
	case MA_IOC_EINT:
		MALOG(INFO_LOG, "MA_IOC_EINT");
		if (irq_enabled) {
			/*irq_enabled = true;*/
			if (mafp_gpio.int_num > 0) {
				enable_irq(mafp_gpio.int_num);
				MALOG(DEBUG_LOG, "enable_irq %d", mafp_gpio.int_num);
			}
		}
		break;
	case MA_IOC_DINT:
		MALOG(INFO_LOG, "MA_IOC_DINT");
		if (irq_enabled) {
			if (mafp_gpio.int_num > 0) {
				disable_irq_nosync(mafp_gpio.int_num);
				/*irq_enabled = false;*/
				MALOG(DEBUG_LOG, "disable_irq_nosync %d", mafp_gpio.int_num);
			}
		}
		break;
	case MA_IOC_TPDW:
		MALOG(INFO_LOG, "MA_IOC_TPDW ignored");
		//input_report_key(ma_input_t, FINGERPRINT_TAP, 1);
		//input_sync(ma_input_t); /*tap down*/
		break;
	case MA_IOC_TPUP:
		MALOG(INFO_LOG, "MA_IOC_TPUP ignored");
		//input_report_key(ma_input_t, FINGERPRINT_TAP, 0);
		//input_sync(ma_input_t);  /*tap up*/
		break;
	case MA_IOC_SGTP:
		MALOG(INFO_LOG, "MA_IOC_SGTP ignored");
		//input_report_key(ma_input_t, FINGERPRINT_TAP, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_TAP, 0);
		//input_sync(ma_input_t);  /*single tap*/
		break;
	case MA_IOC_DBTP:
		MALOG(INFO_LOG, "MA_IOC_DBTP ignored");
		//input_report_key(ma_input_t, FINGERPRINT_DTAP, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_DTAP, 0);
		//input_sync(ma_input_t);	/*double tap*/
		break;
	case MA_IOC_LGTP:
		MALOG(INFO_LOG, "MA_IOC_LGTP ignored");
		//input_report_key(ma_input_t, FINGERPRINT_LONGPRESS, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_LONGPRESS, 0);
		//input_sync(ma_input_t);	 /*long tap*/
		break;
	case MA_IOC_EIRQ:
		MALOG(INFO_LOG, "MA_IOC_EIRQ");
		if (irq_enabled != true) {
			irq_enabled = true;
#ifdef IRQ_TRIGGER_HIGH
			ioctl_ret = request_irq(mafp_gpio.int_num, mas_interrupt,
			IRQF_TRIGGER_HIGH|IRQF_NO_SUSPEND, MA_EINT_NAME,
			NULL);
			MALOG(INFO_LOG, "IRQF_TRIGGER_HIGH|IRQF_NO_SUSPEND");
#else
			ioctl_ret = request_irq(mafp_gpio.int_num, mas_interrupt,
			IRQF_TRIGGER_RISING|IRQF_NO_SUSPEND, MA_EINT_NAME,
			NULL);
			MALOG(INFO_LOG, "IRQF_TRIGGER_RISING|IRQF_NO_SUSPEND");
#endif
			if (ioctl_ret < 0) {
				MALOG(ERR_LOG, "request_irq failed");
				break;
			} else {
				MALOG(INFO_LOG, "request_irq success");
			}
			enable_irq_wake(mafp_gpio.int_num);
			MALOG(INFO_LOG, "enable_irq_wake int_num=%d ioctl_ret=%d", mafp_gpio.int_num, ioctl_ret);
			is_enable_irq = ioctl_ret;
		}
		break;
	case MA_IOC_DIRQ:
		MALOG(INFO_LOG, "MA_IOC_DIRQ");
		if (is_enable_irq > 0) {
			if (mafp_gpio.int_num > 0) {
				free_irq(mafp_gpio.int_num, NULL);
				MALOG(INFO_LOG, "free_irq %d", mafp_gpio.int_num);
				mafp_gpio.int_num = 0;
			}
		}
		break;
	case MA_IOC_NAVW:
		MALOG(INFO_LOG, "MA_IOC_NAVW ignored");
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_UP, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_UP, 0);
		//input_sync(ma_input_t);
		break;
	case MA_IOC_NAVA:
		MALOG(INFO_LOG, "MA_IOC_NAVA ignored");
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_LEFT, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_LEFT, 0);
		//input_sync(ma_input_t);
		break;
	case MA_IOC_NAVS:
		MALOG(INFO_LOG, "MA_IOC_NAVS ignored");
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_DOWN, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_DOWN, 0);
		//input_sync(ma_input_t);
		break;
	case MA_IOC_NAVD:
		MALOG(INFO_LOG, "MA_IOC_NAVD ignored");
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_RIGHT, 1);
		//input_sync(ma_input_t);
		//input_report_key(ma_input_t, FINGERPRINT_SWIPE_RIGHT, 0);
		input_sync(ma_input_t);
		break;
	case MA_IOC_SPAR:
		MALOG(INFO_LOG, "MA_IOC_SPAR");
		mutex_lock(&ioctl_lock);
		ret = copy_from_user(&ma_drv_reg, (unsigned int *)arg, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_GPAR:
		MALOG(INFO_LOG, "MA_IOC_GPAR");
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &ma_drv_reg, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_PWOF:
		MALOG(INFO_LOG, "MA_IOC_PWOF");
		mas_enable_power(false);
		break;
	case MA_IOC_PWON:
		MALOG(INFO_LOG, "MA_IOC_PWON");
		mas_enable_power(true);
		mas_reset(2000);
		MALOG(INFO_LOG, "mas_reset 2ms success");
		break;
	case MA_IOC_GVER:
		MALOG(INFO_LOG, "MA_IOC_GVER");
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &version, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_STSP:
		MALOG(INFO_LOG, "MA_IOC_STSP");
		cp_ret = copy_from_user(&ma_speed, (unsigned int *)arg,
				sizeof(unsigned int));
		ma_spi_change(ma_spi_t, ma_speed, 0);
		MALOG(DEBUG_LOG, "ma_spi_change %d", ma_speed);
		break;
	case MA_IOC_FD_WAIT_CMD:
		MALOG(INFO_LOG, "MA_IOC_FD_WAIT_CMD");
		u2_flag = 0;
		ioctl_ret = wait_event_interruptible(U2_Waitq, u2_flag != 0);
		MALOG(DEBUG_LOG, "wait_event_interruptible U2_Waitq ioctl_ret=%d", ioctl_ret);
		break;
	case MA_IOC_TEST_WAKE_FD:
		MALOG(INFO_LOG, "MA_IOC_TEST_WAKE_FD");
		u2_flag = 1;
		wake_up_interruptible(&U2_Waitq);
		MALOG(DEBUG_LOG, "wake_up_interruptible U2_Waitq");
		break;
	case MA_IOC_TEST_WAIT_FD_RET:
		MALOG(INFO_LOG, "MA_IOC_TEST_WAIT_FD_RET");
		u1_flag = 0;
		ioctl_ret = wait_event_interruptible(U1_Waitq,  u1_flag != 0);
		MALOG(DEBUG_LOG, "wait_event_interruptible U1_Waitq ioctl_ret=%d", ioctl_ret);
		mutex_lock(&ioctl_lock);
		cp_ret = copy_to_user((unsigned int *)arg, &ma_drv_reg,
				sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_FD_WAKE_TEST_RET:
		MALOG(INFO_LOG, "MA_IOC_FD_WAKE_TEST_RET");
		mutex_lock(&ioctl_lock);
		cp_ret = copy_from_user(&ma_drv_reg, (unsigned int *)arg,
				sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		msleep(20);
		u1_flag = 1;
		wake_up_interruptible(&U1_Waitq);
		MALOG(DEBUG_LOG, "wake_up_interruptible U1_Waitq");
		break;
	case MA_IOC_GET_SCREEN:
		MALOG(INFO_LOG, "MA_IOC_GET_SCREEN");
		mutex_lock(&ioctl_lock);
		cp_ret = copy_to_user((unsigned int *)arg, &is_screen_on,
				sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_GET_INT_STATE:
		MALOG(INFO_LOG, "MA_IOC_GET_INT_STATE");
		int_pin_state = mas_get_int_value(0);
		MALOG(DEBUG_LOG, "mas_get_int_value int_pin_state=%d", int_pin_state);
		if (int_pin_state == 0 || int_pin_state == 1) {
			mutex_lock(&ioctl_lock);
			cp_ret = copy_to_user((unsigned int *)arg,
				&int_pin_state, sizeof(unsigned int));
			mutex_unlock(&ioctl_lock);
		}
		break;
	case MA_IOC_GET_FACTORY_FLAG:
		MALOG(INFO_LOG, "MA_IOC_GET_FACTORY_FLAG");
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &g_factory_flag,
				sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_SET_FACTORY_FLAG:
		MALOG(INFO_LOG, "MA_IOC_SET_FACTORY_FLAG");
		mutex_lock(&ioctl_lock);
		ret = copy_from_user(&g_factory_flag, (unsigned int *)arg,
				sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	default:
		ioctl_ret = -EINVAL;
		MALOG(ERR_LOG, "mas_ioctl no such cmd");
	}

	MALOG(DEBUG_LOG, "end");
	return ioctl_ret;
}


#ifdef CONFIG_COMPAT
static long mas_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	retval = filp->f_op->unlocked_ioctl(filp, cmd, arg);
	return retval;
}
#endif

void *kernel_memaddr;
unsigned long kernel_memesize;

int mas_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long page;

	if (!kernel_memaddr) {
		kernel_memaddr = kmalloc(128*1024, GFP_KERNEL);
		if (!kernel_memaddr)
			return -1;
	}
	page = virt_to_phys((void *)kernel_memaddr) >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma, vma->vm_start, page,
		(vma->vm_end - vma->vm_start), vma->vm_page_prot))
		return -1;
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);
	return 0;
}

static int mas_open(struct inode *inode, struct file *filp)
{
	int ret = 0;

	MALOG(INFO_LOG, "start");

	ret = mas_get_gpio(mafp_gpio.dev);
	if (ret) {
		MALOG(ERR_LOG, "mas_get_gpio failed, ret = %d", ret);
	} else {
		MALOG(INFO_LOG, "mas_get_gpio success");
	}

	MALOG(INFO_LOG, "end");
	return ret;
}

static int mas_release(struct inode *inode, struct file *filp)
{
	int ret = 0;

	MALOG(INFO_LOG, "start");

	ret = mas_free_gpio();

	MALOG(INFO_LOG, "end");
	return ret;
}

/*---------------------------------- fops start------------------------------------*/
static const struct file_operations sfops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mas_ioctl,
	.mmap = mas_mmap,
	/*using the previous line replacing the unlock_ioctl while the linux kernel under version2.6.36*/
	/*.ioctl = mas_ioctl,*/
#ifdef CONFIG_COMPAT
	.compat_ioctl = mas_compat_ioctl,
#endif
	.open = mas_open,
	.release = mas_release,

};
/*---------------------------------- fops end ---------------------------------*/

static int init_file_node(void)
{
	int ret = 0;

	MALOG(INFO_LOG, "start");

	ret = alloc_chrdev_region(&sdev->idd, 0, 1, MA_CHR_DEV_NAME);
	if (ret < 0) {
		MALOG(ERR_LOG, "alloc_chrdev_region error, ret=%d", ret);
		return -1;
	} else {
		MALOG(INFO_LOG, "alloc_chrdev_region success");
	}

	sdev->chd = cdev_alloc();
	if (!sdev->chd) {
		MALOG(ERR_LOG, "cdev_alloc error!");
		return -1;
	} else {
		MALOG(INFO_LOG, "cdev_alloc success");
	}

	sdev->chd->owner = THIS_MODULE;
	sdev->chd->ops = &sfops;
	ret = cdev_add(sdev->chd, sdev->idd, 1);
	if (ret) {
		MALOG(ERR_LOG, "cdev_add error, ret=%d", ret);
		return -1;
	} else {
		MALOG(INFO_LOG, "cdev_add success");
	}

	sdev->cls = class_create(THIS_MODULE, MA_CHR_DEV_NAME);
	if (IS_ERR(sdev->cls)) {
		MALOG(ERR_LOG, "class_create error!");
		return -1;
	} else {
		MALOG(INFO_LOG, "class_create success");
	}

	sdev->dev = device_create(sdev->cls, NULL, sdev->idd, NULL, MA_CHR_FILE_NAME);
	ret = IS_ERR(sdev->dev) ? PTR_ERR(sdev->dev) : 0;
	if (ret) {
		MALOG(ERR_LOG, "device_create error, ret=%d", ret);
		return -1;
	} else {
		MALOG(INFO_LOG, "device_create success");
	}

	MALOG(INFO_LOG, "normal end");
	return 0;
}

static int deinit_file_node(void)
{
	MALOG(INFO_LOG, "start");

	unregister_chrdev_region(sdev->idd, 1);
	cdev_del(sdev->chd);

	MALOG(INFO_LOG, "end");
	return 0;
}

static int deinit_interrupt(void)
{
	MALOG(INFO_LOG, "start");
	return 0;
}


static int init_vars(void)
{
	MALOG(INFO_LOG, "start");

	sdev = kmalloc(sizeof(struct fprint_dev), GFP_KERNEL);
	if (sdev == NULL) {
		MALOG(ERR_LOG, "malloc sdev space failed");
	} else {
		MALOG(ERR_LOG, "malloc sdev space success");
	}
	ma_drv_reg = 0;
//	wakeup_source_init(&gProcessWakeLock, "microarray_process_wakelock");
	gProcessWakeLock = wakeup_source_register(NULL, "microarray_process_wakelock");
	if (!gProcessWakeLock) {
		return -ENOMEM;
	}

	INIT_WORK(&gWork, mas_work);
	gWorkq = create_singlethread_workqueue("mas_workqueue");
	if (!gWorkq) {
		MALOG(ERR_LOG, "create_single_workqueue error!");
		return -ENOMEM;
	}

	MALOG(INFO_LOG, "normal end");
	return 0;
}
static int deinit_vars(void)
{
	MALOG(INFO_LOG, "start");

	if (gWorkq) {
		destroy_workqueue(gWorkq);
		gWorkq = NULL;
	}
//	wakeup_source_trash(&gProcessWakeLock);
	if (gProcessWakeLock) {
		wakeup_source_unregister(gProcessWakeLock);
		gProcessWakeLock = NULL;
	}

	MALOG(INFO_LOG, "end");
	return 0;
}

static int init_spi(struct spi_device *spi)
{
	MALOG(INFO_LOG, "start");

	ma_spi_t = spi;

	MALOG(INFO_LOG, "end");
	return 0;
}

static int deinit_spi(struct spi_device *spi)
{
	MALOG(INFO_LOG, "start");

	return 0;
}

static int mas_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	unsigned int blank;

	MALOG(INFO_LOG, "start");

	if (event != FB_EVENT_BLANK)  {
	  return 0;
	}
	blank = *(int *)evdata->data;
	switch (blank) {
	case FB_BLANK_UNBLANK:
		is_screen_on = 1;
		break;
	case FB_BLANK_POWERDOWN:
		is_screen_on = 0;
		break;
	default:
		break;
	}

	MALOG(INFO_LOG, "end");
	return 0;
}

static int init_notifier_call(void);
static int deinit_notifier_call(void);
static int init_notifier_call(void)
{
	MALOG(INFO_LOG, "start");

	notifier.notifier_call = mas_fb_notifier_callback;
	fb_register_client(&notifier);
	is_screen_on = 1;

	MALOG(INFO_LOG, "end");
	return 0;
}

static int deinit_notifier_call(void)
{
	fb_unregister_client(&notifier);
	return 0;
}

int mas_plat_probe(struct platform_device *pdev)
{
	int ret = 0;

	MALOG(INFO_LOG, "start");

	mafp_gpio.dev = pdev;

	mafp_gpio.power_type = 0;
	mafp_gpio.fp_reg = NULL;
	mafp_gpio.power_voltage = 2800000;
	mafp_gpio.int_gpio = 0;
	mafp_gpio.int_num = 0;
	mafp_gpio.reset_gpio = 0;
	mafp_gpio.en_gpio = 0;

	MALOG(INFO_LOG, "poing 1");
	ret = init_vars();
	if (ret) {
		MALOG(ERR_LOG, "init_vars failed, ret = %d", ret);
		goto err1;
	} else {
		MALOG(INFO_LOG, "init_vars success");
	}

	MALOG(INFO_LOG, "poing 2");
	ret = init_file_node();
	if (ret) {
		MALOG(ERR_LOG, "init_file_node failed, ret = %d", ret);
		goto err2;
	} else {
		MALOG(INFO_LOG, "init_file_node success");
	}

	MALOG(INFO_LOG, "poing 3");
	ret = init_spi(NULL);
	if (ret) {
		MALOG(ERR_LOG, "init_spi failed, ret = %d", ret);
		goto err3;
	} else {
		MALOG(INFO_LOG, "init_spi success");
	}

	MALOG(INFO_LOG, "poing 4");
	mas_set_input();

	ret = init_notifier_call();
	if (ret != 0) {
		MALOG(ERR_LOG, "init_notifier_call failed, ret = %d", ret);
		goto err4;
	} else {
		MALOG(INFO_LOG, "init_notifier_call success");
	}

	MALOG(INFO_LOG, "normal end");
	return ret;

err4:
	deinit_notifier_call();

err3:
	deinit_spi(NULL);

err2:
	deinit_file_node();

err1:
	deinit_vars();

	MALOG(INFO_LOG, "abnormal end");
	return ret;
}

int mas_plat_remove(struct platform_device *pdev)
{
	deinit_file_node();
	deinit_interrupt();
	deinit_vars();
	return 0;
}

int mas_init(void)
{
	int ret = 0;
	irq_enabled = false;

	MALOG(INFO_LOG, "start");

#ifdef IRQ_TRIGGER_HIGH
	MALOG(INFO_LOG, "__init mas_init IRQ_TRIGGER_HIGH!");
#endif

	ret = mas_get_platform();
	if (ret) {
		MALOG(ERR_LOG, "mas_get_platform failed, ret = %d", ret);
	} else {
		MALOG(INFO_LOG, "mas_get_platform success");
	}

	MALOG(INFO_LOG, "end");
	return ret;
}

void mas_exit(void)
{
	MALOG(INFO_LOG, "start");

	mas_remove_platform();

	MALOG(INFO_LOG, "end");
}

//module_init(mas_init);
//module_exit(mas_exit);

MODULE_AUTHOR("Microarray");
MODULE_DESCRIPTION("Driver for microarray fingerprint sensor");
MODULE_LICENSE("GPL");
