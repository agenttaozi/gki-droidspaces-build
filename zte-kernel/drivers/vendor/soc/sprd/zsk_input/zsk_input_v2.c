/*
 * linux/drivers/char/zsk_input.c
 * zsk is zte serial2keyboard
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/debugfs.h>
#include <linux/extcon.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/regulator/consumer.h>
#include <linux/sched/clock.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/usb/typec.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/pd.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <linux/input/mt.h>

#define ZTE_BSP_LOG_PREFIX "[ZTE_LDD_KEYBOARD]"
#define log_err(fmt, ...) pr_err(ZTE_BSP_LOG_PREFIX "[ERR]" fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) pr_warn(ZTE_BSP_LOG_PREFIX "[WARN]" fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) pr_info(ZTE_BSP_LOG_PREFIX "[INFO]" fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) pr_debug(ZTE_BSP_LOG_PREFIX "[DEBUG]" fmt, ##__VA_ARGS__)

//#define DEVICE_NAME	"zsk-input"
//modifier key
#define ZSK_KEY_LEFTCTRL 0x01
#define ZSK_KEY_LEFTSHIFT 0x02
#define ZSK_KEY_LEFTALT 0x04
#define ZSK_KEY_LEFTWIN 0x08
#define ZSK_KEY_RIGHTCTRL 0x10
#define ZSK_KEY_RIGHTSHIFT 0x20
#define ZSK_KEY_RIGHTALT 0x40
#define ZSK_KEY_RIGHTWIN 0x80

/* resolution in pixels */
static int res_x = 1900;
static int res_y = 900;
/* size in mm */
static int width = 95;
static int height = 47;
/* nums for slots */
static int slot_num = 4;

static struct input_dev *zkb_input;
static struct zgpioctl_info *info;
static struct miscdevice zsk_miscdev;
static int keyboard_update_status = 0;
static int last_zsk_report_buffer[14];
static int zsk_muteled_value = 0;
static int zsk_capsled_flag = 0;

//#define ZTE_ZSK_INPUT_DEBUG

struct zgpioctl_info {
	int keyboard_en;	/* gpio62 keyboard_en low active*/
	int keyboard_rst;	/* gpio81 keyboard_rst*/
	int keyboard_irq;	/* gpio59 keyboard_irq*/
	int capslock_gpio;	/* gpio46 capslock_gpio*/
	int micmute_gpio;	/* gpio183 micmute_gpio*/
	struct delayed_work keyboard_id_work;
	struct delayed_work led_sync_work;
	struct mutex keyboard_id_lock;
};

u32 *lkeycode = NULL;
u32 *hwcode = NULL;
static int hwcode_num = 0;

static int zsk_input_register_flag = 0;

#if IS_ENABLED(CONFIG_ZTE_HALL_CTL_KEYBOARD_EN)
void keyboard_en_control(bool enable)
{
	log_info("%s: enable is %d.\n", __func__, enable);
	//gpio_set_value(info->keyboard_en, enable);
}
EXPORT_SYMBOL(keyboard_en_control);
#endif

static void keyboard_id_work_func(struct work_struct *work)
{
	int i;

	mutex_lock(&info->keyboard_id_lock);

	#ifdef ZTE_ZSK_INPUT_DEBUG
	log_info("%s: enable keyboard power, zsk_flag is %d.\n", __func__, zsk_input_register_flag);
	#endif
	gpio_set_value(info->keyboard_en, 1);
	gpio_set_value(info->keyboard_rst, 1);
	gpio_set_value(info->keyboard_irq, 1);
	/*register zkb_input*/
	if (zsk_input_register_flag == 0) {
		keyboard_update_status = 0;		//disable keyboard update
		zkb_input = input_allocate_device();

		set_bit(EV_KEY, zkb_input->evbit);
		set_bit(EV_REP, zkb_input->evbit);
		set_bit(EV_LED, zkb_input->evbit);
		set_bit(LED_CAPSL, zkb_input->ledbit);
		for (i = 0; i < hwcode_num; i++)
			set_bit(lkeycode[i], zkb_input->keybit);
		set_bit(KEY_LEFTCTRL, zkb_input->keybit);
		set_bit(KEY_LEFTSHIFT, zkb_input->keybit);
		set_bit(KEY_LEFTALT, zkb_input->keybit);
		set_bit(KEY_LEFTMETA, zkb_input->keybit);
		set_bit(KEY_RIGHTCTRL, zkb_input->keybit);
		set_bit(KEY_RIGHTSHIFT, zkb_input->keybit);
		set_bit(KEY_RIGHTALT, zkb_input->keybit);
		set_bit(KEY_RIGHTMETA, zkb_input->keybit);
		zkb_input->name = "zkb_input";
		zkb_input->phys = "zkb_input/input0";
		zkb_input->id.bustype = BUS_HOST;
		zkb_input->id.vendor = 0x19d2;
		zkb_input->id.product = 0x1987;
		zkb_input->id.version = 0x0002;
		zkb_input->keycode = lkeycode;

		if (input_register_device(zkb_input) != 0) {
			input_free_device(zkb_input);
			log_info("%s:zkb_input register device fail!!\n", __func__);
		} else
			zsk_input_register_flag = 1;
	}

	mutex_unlock(&info->keyboard_id_lock);
}

/*control keyboard capslock && micmute led*/
static void led_sync_work_func(struct work_struct *work)
{
	if (zsk_input_register_flag == 1) {
		zsk_capsled_flag = !!test_bit(LED_CAPSL, zkb_input->led);
		//log_info("%s:caps_lock_led: %d \n", __func__, zsk_capsled_flag);
		gpio_direction_output(info->capslock_gpio, zsk_capsled_flag);
	}
}

static int zsk_dts_parse_init(struct zgpioctl_info *info)
{
	int retval;
	struct device_node *node;
	//unsigned int irq_num;

	node = of_find_compatible_node(NULL, NULL, "zte,zsk_keyboard");
	if (node) {
		/*keyboard_en*/
		retval = of_get_named_gpio(node, "zte,keyboard_en", 0);
		if (retval < 0) {
			log_err("%s: error invalid keyboard_en gpio err: %d\n", __func__, retval);
		}
		info->keyboard_en = retval;
		/*keyboard_rst*/
		retval = of_get_named_gpio(node, "zte,keyboard_rst", 0);
		if (retval < 0) {
			log_err("%s: error invalid keyboard_rst gpio err: %d\n", __func__, retval);
		}
		info->keyboard_rst = retval;
		/*keyboard_irq*/
		retval = of_get_named_gpio(node, "zte,keyboard_irq", 0);
		if (retval < 0) {
			log_err("%s: error invalid keyboard_irq gpio err: %d\n", __func__, retval);
		}
		info->keyboard_irq = retval;
		/*capslock_gpio*/
		retval = of_get_named_gpio(node, "zte,capslock_gpio", 0);
		if (retval < 0) {
			log_err("%s: error invalid capslock_gpio gpio err: %d\n", __func__, retval);
		}
		info->capslock_gpio = retval;
		/*micmute_gpio*/
		retval = of_get_named_gpio(node, "zte,micmute_gpio", 0);
		if (retval < 0) {
			log_err("%s: error invalid micmute_gpio gpio err: %d\n", __func__, retval);
		}
		info->micmute_gpio = retval;
		/*get keyboard para*/
		retval = of_property_read_u32(node, "zsk,res_x", &res_x);
		if (retval)
			log_err("%s: cannot get zsk,res_x from dts!\n", __func__);
		retval = of_property_read_u32(node, "zsk,res_y", &res_y);
		if (retval)
			log_err("%s: cannot get zsk,res_y from dts!\n", __func__);
		retval = of_property_read_u32(node, "zsk,width", &width);
		if (retval)
			log_err("%s: cannot get zsk,width from dts!\n", __func__);
		retval = of_property_read_u32(node, "zsk,height", &height);
		if (retval)
			log_err("%s: cannot get zsk,height from dts!\n", __func__);
		retval = of_property_read_u32(node, "zsk,slot_num", &slot_num);
		if (retval)
			log_err("%s: cannot get zsk,slot_num from dts!\n", __func__);
		retval = of_property_read_u32(node, "zsk,hwcode_num", &hwcode_num);
		if (retval)
			log_err("%s: cannot get zsk,hwcode_num from dts!\n", __func__);
		/*get keycode and hwcode*/
		lkeycode = kzalloc(sizeof(u32) * hwcode_num, GFP_KERNEL);
		hwcode = kzalloc(sizeof(u32) * hwcode_num, GFP_KERNEL);
		retval = of_property_read_u32_array(node, "zsk,lkeycode", lkeycode, hwcode_num);
		if (retval) {
			log_err("%s: cannot get zsk,lkeycode from dts!\n", __func__);
			return 1;
		}
		retval = of_property_read_u32_array(node, "zsk,hwcode", hwcode, hwcode_num);
		if (retval) {
			log_err("%s: cannot get zsk,hwcode from dts!\n", __func__);
			return 1;
		}
	} else {
		log_err("%s: cannot get zsk_keyboard node\n", __func__);
		return 1;
	}

	return 0;
}

static int zsk_gpio_and_irq_init(void)
{
	int rc = 0;

	log_info("%s: enter\n", __func__);
	if (!zsk_dts_parse_init(info)) {
		/*request keyboard_en*/
		rc = gpio_request(info->keyboard_en, NULL);
		if (rc) {
			log_err("request keyboard_en gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(info->keyboard_en, 1);
		if (rc) {
			log_err("keyboard_en gpio direction set failed, rc=%d\n", rc);
		}
		/*request keyboard_rst*/
		rc = gpio_request(info->keyboard_rst, NULL);
		if (rc) {
			log_err("request keyboard_rst gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(info->keyboard_rst, 1);
		if (rc) {
			log_err("keyboard_rst gpio direction set failed, rc=%d\n", rc);
		}
		/*request keyboard_irq*/
		rc = gpio_request(info->keyboard_irq, NULL);
		if (rc) {
			log_err("request keyboard_irq gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(info->keyboard_irq, 1);
		if (rc) {
			log_err("keyboard_irq gpio direction set failed, rc=%d\n", rc);
		}
		/*request capslock_gpio*/
		rc = gpio_request(info->capslock_gpio, NULL);
		if (rc) {
			log_err("request capslock_gpio gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(info->capslock_gpio, 0);
		if (rc) {
			log_err("capslock_gpio gpio direction set failed, rc=%d\n", rc);
		}
		/*request micmute_gpio*/
		rc = gpio_request(info->micmute_gpio, NULL);
		if (rc) {
			log_err("request micmute_gpio gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(info->micmute_gpio, 0);
		if (rc) {
			log_err("micmute_gpio gpio direction set failed, rc=%d\n", rc);
		}
	} else {
		log_err("%s: zsk_dts_parse_init failed!\n", __func__);
		return 1;
	}
	log_info("%s: success\n", __func__);

	return 0;
}

ssize_t zsk_input_read(struct file *filp, char __user *buf, size_t size, loff_t *offset)
{
	unsigned int caps_lock_led = 0;
	int ret = 0;

	mutex_lock(&info->keyboard_id_lock);
	if (zsk_input_register_flag == 0) {
		if (copy_to_user(buf, &caps_lock_led, sizeof(caps_lock_led)))
			ret = -ENODEV;
		ret = -ENODEV;
		goto out;
	}

	caps_lock_led = zsk_capsled_flag;

	if (copy_to_user(buf, &caps_lock_led, sizeof(caps_lock_led))) {
		ret = -ENODEV;
		goto out;
	}
	ret = sizeof(caps_lock_led);

out:
	mutex_unlock(&info->keyboard_id_lock);
	return ret;
}

static ssize_t zsk_input_write (struct file *filp, const char __user *user_buffer,size_t count, loff_t *offset)
{
	int i = -1, k = 0, last_zsk_report_buffer_flag = 0;
	char buffer[21];
	int zsk_report_buffer[21];
	int ret = 0;

	#ifdef ZTE_ZSK_INPUT_DEBUG
	int j = 0;
	#endif

	mutex_lock(&info->keyboard_id_lock);

	/*initial buffer*/
	for (i = 0; i < 21; i++) {
		zsk_report_buffer[i] = 0;
	}
	for (i = 0; i < 21; i++) {
		buffer[i] = 0;
	}
	if (copy_from_user(buffer, user_buffer, sizeof(buffer))) {
		ret = -EFAULT;
		goto out;
	}
	if (zsk_input_register_flag == 0) {
		ret = -ENODEV;
		goto out;
	}

#ifdef ZTE_ZSK_INPUT_DEBUG
	for (j = 0; j < sizeof(buffer); j++)
		log_info("%s:dump buffer[%d] is 0x%x\n", __func__, j, (int) buffer[j]);
#endif
	
	if((int) buffer[0] == 1){
		/*obtain function key*/
		if(((int) buffer[1] & ZSK_KEY_LEFTCTRL) == ZSK_KEY_LEFTCTRL) {
			zsk_report_buffer[0] = KEY_LEFTCTRL;
		}
		if(((int) buffer[1] & ZSK_KEY_LEFTSHIFT) == ZSK_KEY_LEFTSHIFT) {
			zsk_report_buffer[1] = KEY_LEFTSHIFT;
		}
		if(((int) buffer[1] & ZSK_KEY_LEFTALT) == ZSK_KEY_LEFTALT) {
			zsk_report_buffer[2] = KEY_LEFTALT;
		}
		if(((int) buffer[1] & ZSK_KEY_LEFTWIN) == ZSK_KEY_LEFTWIN) {
			zsk_report_buffer[3] = KEY_LEFTMETA;
		}
		if(((int) buffer[1] & ZSK_KEY_RIGHTCTRL) == ZSK_KEY_RIGHTCTRL) {
			zsk_report_buffer[4] = KEY_RIGHTCTRL;
		}
		if(((int) buffer[1] & ZSK_KEY_RIGHTSHIFT) == ZSK_KEY_RIGHTSHIFT) {
			zsk_report_buffer[5] = KEY_RIGHTSHIFT;
		}
		if(((int) buffer[1] & ZSK_KEY_RIGHTALT) == ZSK_KEY_RIGHTALT) {
			zsk_report_buffer[6] = KEY_RIGHTALT;
		}
		if(((int) buffer[1] & ZSK_KEY_RIGHTWIN) == ZSK_KEY_RIGHTWIN) {
			zsk_report_buffer[7] = KEY_RIGHTMETA;
		}
		#ifdef ZTE_ZSK_INPUT_DEBUG
		for (j = 0; j < 8; j++) {
			log_info("%s:dump function key[%d] is 0x%x\n", __func__, j, (int) zsk_report_buffer[j]);
			log_info("%s:dump last function key[%d] is 0x%x\n", __func__, j, (int) last_zsk_report_buffer[j]);
		}
		#endif
		/*report function key pressed this time*/
		for (i = 0; i < 8; i++) {
			if (zsk_report_buffer[i] != 0) {
				input_report_key(zkb_input, zsk_report_buffer[i], 1);
			}
		}
		/*report function key released this time*/
		for (i = 0; i < 8; i++) {
			if ((last_zsk_report_buffer[i] != 0) && (zsk_report_buffer[i] == 0)) {
				input_report_key(zkb_input, last_zsk_report_buffer[i], 0);
			}
		}

		/*obtain ordinary key*/
		for (i = 0; i < hwcode_num; i++) {
			if (buffer[2] == hwcode[i]) {
				zsk_report_buffer[8] = lkeycode[i];
			}
			if (buffer[3] == hwcode[i]) {
				zsk_report_buffer[9] = lkeycode[i];
			}
			if (buffer[4] == hwcode[i]) {
				zsk_report_buffer[10] = lkeycode[i];
			}
			if (buffer[5] == hwcode[i]) {
				zsk_report_buffer[11] = lkeycode[i];
			}
			if (buffer[6] == hwcode[i]) {
				zsk_report_buffer[12] = lkeycode[i];
			}
			if (buffer[7] == hwcode[i]) {
				zsk_report_buffer[13] = lkeycode[i];
			}
		}
		#ifdef ZTE_ZSK_INPUT_DEBUG
		for (j = 8; j < 14; j++) {
			log_info("%s:dump ordinary key[%d] is 0x%x\n", __func__, j, (int) zsk_report_buffer[j]);
			log_info("%s:dump last ordinary key[%d] is 0x%x\n", __func__, j, (int) last_zsk_report_buffer[j]);
		}
		#endif
		/*report ordinary key pressed this time*/
		for (i = 8; i < 14; i++) {
			if (zsk_report_buffer[i] != 0) {
				input_report_key(zkb_input, zsk_report_buffer[i], 1);
			}
		}
		/*report ordinary key released this time*/
		for (i = 8; i < 14; i++) {
			if (last_zsk_report_buffer[i] != 0) {
				for (k = 8; k < 14; k++) {
					if (zsk_report_buffer[k] == last_zsk_report_buffer[i]) {
						last_zsk_report_buffer_flag = 1;
						break;
					}
				}
				if (last_zsk_report_buffer_flag == 0)
					input_report_key(zkb_input, last_zsk_report_buffer[i], 0);
				else
					last_zsk_report_buffer_flag = 0;
			}
		}

		/*control keyboard capslock && micmute led*/
		//for (i = 8; i < 14; i++) {
		//	if (zsk_report_buffer[i] == KEY_CAPSLOCK) {
		//		zsk_capsled_flag = !test_bit(LED_CAPSL, zkb_input->led);  //update led status before sync
		//		gpio_direction_output(info->capslock_gpio, zsk_capsled_flag);
		//		log_info("%s: caps_lock_led = %d \n", __func__,  zsk_capsled_flag);
		//		break;
		//		//schedule_delayed_work(&info->led_sync_work, msecs_to_jiffies(200));
		//	}
		//}

		input_sync(zkb_input);

		/*save key as last*/
		for(i = 0; i < 14; i++) {
			last_zsk_report_buffer[i] = zsk_report_buffer[i];
		}
		#ifdef ZTE_ZSK_INPUT_DEBUG
			log_err("%s: caps_lock_led is 0x%x\n", __func__, (int) zkb_input->led[0]);
		#endif
	}
	ret = count;

	/*control keyboard capslock && micmute led*/
	schedule_delayed_work(&info->led_sync_work, msecs_to_jiffies(5));

out:
	mutex_unlock(&info->keyboard_id_lock);
	return ret;
}

/*add sysfs node for upgrade/version start*/
static ssize_t zsk_update_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	//log_info("keyboard_update_status:%d\n", keyboard_update_status);
	return snprintf(buf, 32, "%d\n", keyboard_update_status);
}
static ssize_t zsk_update_status_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret;
	unsigned long temp_status = 0;

	ret = kstrtoul(buf, 32, &temp_status);
	if (ret) {
		log_err("%s: failed\n", __func__);
		return ret;
	}
	keyboard_update_status = (unsigned int) temp_status;
	log_info("keyboard_update_status:%d\n", keyboard_update_status);
	return count;
}
static DEVICE_ATTR(zsk_update_status, 0664, zsk_update_status_show, zsk_update_status_store);

static ssize_t zsk_muteled_value_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	log_info("zsk_muteled_value:%d\n", zsk_muteled_value);
	return snprintf(buf, 32, "%d\n", zsk_muteled_value);
}
static ssize_t zsk_muteled_value_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret;
	unsigned long temp_status = 0;

	ret = kstrtoul(buf, 32, &temp_status);
	if (ret) {
		log_err("%s: failed\n", __func__);
		return ret;
	}
	zsk_muteled_value = (unsigned int) temp_status;
	gpio_direction_output(info->micmute_gpio, zsk_muteled_value);
	log_info("zsk_muteled_value:%d\n", zsk_muteled_value);
	return count;
}
static DEVICE_ATTR(zsk_muteled_value, 0664, zsk_muteled_value_show, zsk_muteled_value_store);

static struct attribute *zsk_input_attributes[] = {
	&dev_attr_zsk_update_status.attr,
	&dev_attr_zsk_muteled_value.attr,
	NULL
};


static const struct attribute_group zsk_input_attr_groups = {
	//.name = ,
	.attrs = zsk_input_attributes,
};

static int init_zsk_sys_node()
{
	int ret = 0;

	ret = sysfs_create_group(&zsk_miscdev.this_device->kobj, &zsk_input_attr_groups);
	if(ret)
		log_err("%s: sysfs_create_group zsk_input failed!!\n", __func__);

	return ret;
}
/*add sysfs node for upgrade/version start*/

static int zsk_input_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int zsk_input_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static struct file_operations zsk_dev_fops = {
	.owner	= THIS_MODULE,
	.open	= zsk_input_open,
	.write	= zsk_input_write,
	.read   = zsk_input_read,
	.release	=zsk_input_release,
};

static struct miscdevice zsk_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "zsk-input",
	.fops	= &zsk_dev_fops,
};

static int zsk_input_probe(struct platform_device *keyboard_dev)
{
	int ret = 0;

	log_info("%s: start!\n", __func__);
	info = kzalloc(sizeof(struct zgpioctl_info), GFP_KERNEL);
	if (!info) {
		log_err("%s: error kzalloc\n", __func__);
		return -ENOMEM;
	}
	
	mutex_init(&info->keyboard_id_lock);
	INIT_DELAYED_WORK(&info->keyboard_id_work, keyboard_id_work_func);
	INIT_DELAYED_WORK(&info->led_sync_work, led_sync_work_func);

	ret = zsk_gpio_and_irq_init();
	if (ret){
		log_err("%s: zsk_gpio_and_irq_init failed!\n", __func__);
		return ret;
	}
	schedule_delayed_work(&info->keyboard_id_work, msecs_to_jiffies(3000));
	schedule_delayed_work(&info->led_sync_work, msecs_to_jiffies(4000));

	ret = misc_register(&zsk_miscdev);
	
	ret = init_zsk_sys_node();
	if(ret)
		log_err("%s:init_zsk_sys_node failed!\n", __func__);

	log_info("%s: finished!\n", __func__);
	return ret;
}

static void zsk_input_shutdown(struct platform_device *keyboard_dev)
{
	//if (zsk_input_register_flag == 1) {
	//	input_unregister_device(zkb_input);
	//}
	//misc_deregister(&zsk_miscdev);

	log_info("%s: finished!\n", __func__);
}
static int zsk_input_remove(struct platform_device *keyboard_dev)
{
	//if (zsk_input_register_flag == 1) {
	//	input_unregister_device(zkb_input);
	//}
	//misc_deregister(&zsk_miscdev);

	log_info("%s: finished!\n", __func__);
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "zte,zsk_keyboard", },
	{ },
};

static struct platform_driver zsk_input_driver = {
	.driver		= {
		.name		= "zte,zsk_keyboard",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
	},
	.probe		= zsk_input_probe,
	.shutdown	= zsk_input_shutdown,
	.remove		= zsk_input_remove,
};

module_platform_driver(zsk_input_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DouMingming <dou.mingming@zte.com.cn>");
MODULE_DESCRIPTION("ZTE ZSK Driver");
