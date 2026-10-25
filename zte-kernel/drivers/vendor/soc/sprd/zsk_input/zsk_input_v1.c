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
static int keyboard_ztp_enable = 1;
static int keyboard_update_status = 0;
static int last_zsk_report_buffer[14];
static int last_mouse_btn = 0;
static int last_finger0_valid = 0;
static int last_finger1_valid = 0;
static int last_finger2_valid = 0;
static int last_finger3_valid = 0;

//bool zte_case_type = 0;
//#define ZTE_ZSK_INPUT_DEBUG

/*zte keyboard touch panel start*/
static struct input_dev *ztp_input;
/*zte keyboard touch panel end*/

struct zgpioctl_info {
	int keyboard_id;	/* gpio60 keyboard_id*/
	int keyboard_en;	/* gpio62 keyboard_en low active*/
	struct delayed_work keyboard_id_work;
	struct mutex keyboard_id_lock;
};

u32 *lkeycode = NULL;
u32 *hwcode = NULL;
static int hwcode_num = 0;

static int zsk_input_register_flag = 0;

#if IS_ENABLED(CONFIG_ZTE_HALL_CTL_KEYBOARD_EN)
void keyboard_en_control(bool enable)
{
	unsigned int keyboard_id_irq_gpio_value;

	keyboard_id_irq_gpio_value = gpio_get_value(info->keyboard_id);
	log_info("%s: keyboard_id is %d, enable is %d.\n", __func__, keyboard_id_irq_gpio_value, enable);
	if (keyboard_id_irq_gpio_value == 0) {
		gpio_set_value(info->keyboard_en, enable);
	}
}
EXPORT_SYMBOL(keyboard_en_control);
#endif

static void keyboard_id_work_func(struct work_struct *work)
{
	unsigned int keyboard_id_irq_gpio_value;
	int i;
	int ret = 0;

	mutex_lock(&info->keyboard_id_lock);

	keyboard_id_irq_gpio_value = gpio_get_value(info->keyboard_id);
	log_info("%s: keyboard_id is %d, zsk_flag is %d.\n", __func__, keyboard_id_irq_gpio_value, zsk_input_register_flag);
	if (keyboard_id_irq_gpio_value == 1) {
		#ifdef ZTE_ZSK_INPUT_DEBUG
		log_info("%s: disable keyboard power, zsk_flag is %d.\n", __func__, zsk_input_register_flag);
		#endif
		gpio_set_value(info->keyboard_en, 0);
		/*unregister zkb_input && ztp_input*/
		if (zsk_input_register_flag == 1) {
			zsk_input_register_flag = 0;
			input_unregister_device(zkb_input);
			input_unregister_device(ztp_input);	//zte keyboard touch panel
		}
		// if (zte_case_type == 1) {
		// 	zte_case_type = 0;
		// 	log_info("zsk_input zte_case_type is reset!\n", __func__);
		// }
	} else {
		#ifdef ZTE_ZSK_INPUT_DEBUG
		log_info("%s: enable keyboard power, zsk_flag is %d.\n", __func__, zsk_input_register_flag);
		#endif
		gpio_set_value(info->keyboard_en, 1);
		/*register zkb_input*/
		if (zsk_input_register_flag == 0) {
			keyboard_ztp_enable = 1;	//enable ztp when keyboard connected
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
			zkb_input->id.version = 0x0001;
			zkb_input->keycode = lkeycode;

			if (input_register_device(zkb_input) != 0) {
				input_free_device(zkb_input);
				log_info("%s:zkb_input register device fail!!\n", __func__);
			} else
				zsk_input_register_flag = 1;

			/*zte keyboard touch panel start*/
			ztp_input = input_allocate_device();

			set_bit(EV_KEY, ztp_input->evbit);
			//set_bit(EV_REP, zkb_input->evbit);
			set_bit(EV_ABS, ztp_input->evbit);

			set_bit(BTN_MOUSE, ztp_input->keybit);
			set_bit(BTN_TOOL_FINGER, ztp_input->keybit);
			//set_bit(BTN_TOOL_QUINTTAP, ztp_input->keybit);
			set_bit(BTN_TOUCH, ztp_input->keybit);
			set_bit(BTN_TOOL_DOUBLETAP, ztp_input->keybit);
			set_bit(BTN_TOOL_TRIPLETAP, ztp_input->keybit);
			set_bit(BTN_TOOL_QUADTAP, ztp_input->keybit);

			set_bit(INPUT_PROP_POINTER, ztp_input->propbit);
			set_bit(INPUT_PROP_BUTTONPAD, ztp_input->propbit);

			ztp_input->name = "ztp_input";
			ztp_input->phys = "ztp_input/input0";
			ztp_input->id.bustype = BUS_HOST;
			ztp_input->id.vendor = 0x19d2;
			ztp_input->id.product = 0x2023;
			ztp_input->id.version = 0x0001;

			input_set_capability(ztp_input, EV_MSC, MSC_TIMESTAMP);

			input_set_abs_params(ztp_input, ABS_X, 0, res_x, 0, 0);
			input_abs_set_res(ztp_input, ABS_X, res_x / width);
			input_set_abs_params(ztp_input, ABS_Y, 0, res_y, 0, 0);
			input_abs_set_res(ztp_input, ABS_Y, res_y / height);
			input_set_abs_params(ztp_input, ABS_MT_POSITION_X, 0, res_x, 0, 0);
			input_set_abs_params(ztp_input, ABS_MT_POSITION_Y, 0, res_y, 0, 0);
			ret = input_mt_init_slots(ztp_input, slot_num, INPUT_MT_POINTER);
 			if (ret)
				log_err("%s:ztp_input init slot failed!!\n", __func__);


			if (input_register_device(ztp_input) != 0) {
				input_free_device(ztp_input);
				log_info("%s:zsk-ztp_input register device fail!!\n", __func__);
			} else
				zsk_input_register_flag = 1;
			/*zte keyboard touch panel end*/
		}
	}

	mutex_unlock(&info->keyboard_id_lock);
}
static irqreturn_t keyboard_id_irq_thread(int irq, void *handle)
{
	struct zgpioctl_info *info = (struct zgpioctl_info *)handle;

	schedule_delayed_work(&info->keyboard_id_work, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static int zsk_dts_parse_init(struct zgpioctl_info *info)
{
	int retval;
	struct device_node *node;
	unsigned int irq_num;

	node = of_find_compatible_node(NULL, NULL, "zte,zsk_keyboard");
	if (node) {
		/*keyboard_id*/
		retval = of_get_named_gpio(node, "zte,keyboard_id", 0);
		if (retval < 0) {
			log_err("%s: error invalid keyboard_id gpio err: %d\n", __func__, retval);
		}
		info->keyboard_id = retval;
		/*keyboard_en*/
		retval = of_get_named_gpio(node, "zte,keyboard_en", 0);
		if (retval < 0) {
			log_err("%s: error invalid keyboard_en gpio err: %d\n", __func__, retval);
		}
		info->keyboard_en = retval;
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

	/*keyboard_id irq*/
	irq_num = gpio_to_irq(info->keyboard_id);
	if (irq_num < 0) {
		log_err("%s: error gpio_to_irq returned %d\n", __func__, irq_num);
		return 1;
	}

	retval = request_threaded_irq(irq_num, NULL, keyboard_id_irq_thread, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT, "keyboard_id_irq", info);
	if (retval != 0) {
		log_err("%s: request_keyboard_id fail, ret %d, irqnum %d!!!\n", __func__, retval, info->keyboard_id);
		return 1;
	}
	enable_irq_wake(gpio_to_irq(info->keyboard_id));
	return 0;
}

static int zsk_gpio_and_irq_init(void)
{
	int rc = 0;

	log_info("%s: enter\n", __func__);
	if (!zsk_dts_parse_init(info)) {
		/*request keyboard_id*/
		rc = gpio_request(info->keyboard_id, NULL);
		if (rc) {
			log_err("request keyboard_id gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_input(info->keyboard_id);
		if (rc) {
			log_err("keyboard_id gpio direction set failed, rc=%d\n", rc);
		}
		/*request keyboard_en*/
		rc = gpio_request(info->keyboard_en, NULL);
		if (rc) {
			log_err("request keyboard_en gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(info->keyboard_en, 0);
		if (rc) {
			log_err("keyboard_en gpio direction set failed, rc=%d\n", rc);
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

	caps_lock_led = (int) zkb_input->led[0];

	if (copy_to_user(buf, &caps_lock_led, sizeof(caps_lock_led))) {
		ret = -ENODEV;
		goto out;
	}
	ret = sizeof(caps_lock_led);
	#ifdef ZTE_ZSK_INPUT_DEBUG
	log_err("%s: caps_lock_led =%d\n", __func__, caps_lock_led);
	#endif

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

	int finger0_x = 0;
	int finger0_y = 0;
	int finger0_valid = 0;
	int finger1_x = 0;
	int finger1_y = 0;
	int finger1_valid = 0;
	int finger2_x = 0;
	int finger2_y = 0;
	int finger2_valid = 0;
	int finger3_x = 0;
	int finger3_y = 0;
	int finger3_valid = 0;
	int time_stamp = 0;
	int fingers_num = 0;
	int mouse_btn = 0;

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
	
	// if(((int) buffer[1] == 0xAA) && ((int) buffer[2] == 0x80) && ((int) buffer[3] == 0x55)) {
	// 	zte_case_type = 1;
	// 	log_info("zsk_input zte_case_type is BIG case!\n", __func__);
	// 	goto out;
	// }
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

		input_sync(zkb_input);

		/*save key as last*/
		for(i = 0; i < 14; i++) {
			last_zsk_report_buffer[i] = zsk_report_buffer[i];
		}
		#ifdef ZTE_ZSK_INPUT_DEBUG
			log_err("%s: caps_lock_led is 0x%x\n", __func__, (int) zkb_input->led[0]);
		#endif
	}else if((int) buffer[0] == 0x07){
		//log_info("%s: ztp ztp ztp 2 !!!\n", __func__);

		time_stamp = ((int) buffer[18] << 8)|((int) buffer[17]);
		fingers_num = (int) buffer[19];
		mouse_btn = (int) buffer[20];
		finger0_valid = (int) buffer[1] & 0x02;
		finger1_valid = (int) buffer[5] & 0x02;
		finger2_valid = (int) buffer[9] & 0x02;
		finger3_valid = (int) buffer[13] & 0x02;

		/*finger1 start*/
		if(finger0_valid) {
			finger0_x = ((((int) buffer[3]) & 0x0f) << 8)|((int) buffer[2]);
			finger0_y = ((int) buffer[4] << 4)|((((int) buffer[3]) & 0xf0) >> 4);

			input_mt_slot(ztp_input, 0);
			input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 1);
			input_report_key(ztp_input, BTN_TOUCH, 1);
			input_report_abs(ztp_input, ABS_MT_POSITION_X, finger0_x);
			input_report_abs(ztp_input, ABS_MT_POSITION_Y, finger0_y);
			input_report_abs(ztp_input, ABS_X, finger0_x);
			input_report_abs(ztp_input, ABS_Y, finger0_y);

			last_finger0_valid = finger0_valid;
		} else {
			if(last_finger0_valid) {
				last_finger0_valid = 0;
				input_mt_slot(ztp_input, 0);
				input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 0);
				//log_info("%s ztp_abs: finger0 released\n", __func__);
				//input_report_key(ztp_input, BTN_TOUCH, 0);
			}
		}
		/*finger2 start*/
		if(finger1_valid) {
			finger1_x = ((((int) buffer[7]) & 0x0f) << 8)|((int) buffer[6]);
			finger1_y = ((int) buffer[8] << 4)|((((int) buffer[7]) & 0xf0) >> 4);

			input_mt_slot(ztp_input, 1);
			input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 1);
			input_report_key(ztp_input, BTN_TOUCH, 1);
			input_report_abs(ztp_input, ABS_MT_POSITION_X, finger1_x);
			input_report_abs(ztp_input, ABS_MT_POSITION_Y, finger1_y);
			input_report_abs(ztp_input, ABS_X, finger1_x);
			input_report_abs(ztp_input, ABS_Y, finger1_y);

			last_finger1_valid = finger1_valid;
		} else {
			if(last_finger1_valid) {
				last_finger1_valid = 0;
				input_mt_slot(ztp_input, 1);
				input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 0);
				//log_info("%s ztp_abs: finger1 released\n", __func__);
				//input_report_key(ztp_input, BTN_TOUCH, 0);
			}
		}
		/*finger3 start*/
		if(finger2_valid) {
			finger2_x = ((((int) buffer[11]) & 0x0f) << 8)|((int) buffer[10]);
			finger2_y = ((int) buffer[12] << 4)|((((int) buffer[11]) & 0xf0) >> 4);

			input_mt_slot(ztp_input, 2);
			input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 1);
			input_report_key(ztp_input, BTN_TOUCH, 1);
			input_report_abs(ztp_input, ABS_MT_POSITION_X, finger2_x);
			input_report_abs(ztp_input, ABS_MT_POSITION_Y, finger2_y);
			input_report_abs(ztp_input, ABS_X, finger2_x);
			input_report_abs(ztp_input, ABS_Y, finger2_y);

			last_finger2_valid = finger2_valid;
		} else {
			if(last_finger2_valid) {
				last_finger2_valid = 0;
				input_mt_slot(ztp_input, 2);
				input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 0);
				//log_info("%s ztp_abs: finger2 released\n", __func__);
				//input_report_key(ztp_input, BTN_TOUCH, 0);
			}
		}
		/*finger4 start*/
		if(finger3_valid) {
			finger3_x = ((((int) buffer[15]) & 0x0f) << 8)|((int) buffer[14]);
			finger3_y = ((int) buffer[16] << 4)|((((int) buffer[15]) & 0xf0) >> 4);

			input_mt_slot(ztp_input, 3);
			input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 1);
			input_report_key(ztp_input, BTN_TOUCH, 1);
			input_report_abs(ztp_input, ABS_MT_POSITION_X, finger3_x);
			input_report_abs(ztp_input, ABS_MT_POSITION_Y, finger3_y);
			input_report_abs(ztp_input, ABS_X, finger3_x);
			input_report_abs(ztp_input, ABS_Y, finger3_y);

			last_finger3_valid = finger3_valid;
		} else {
			//release finger3
			if(last_finger3_valid) {
				last_finger3_valid = 0;
				input_mt_slot(ztp_input, 3);
				input_mt_report_slot_state(ztp_input, MT_TOOL_FINGER, 0);
				//log_info("%s ztp_abs: finger3 released\n", __func__);
				//input_report_key(ztp_input, BTN_TOUCH, 0);
			}
		}

		if(mouse_btn){
			input_report_key(ztp_input, BTN_MOUSE, 1);
			last_mouse_btn = mouse_btn;
			input_sync(ztp_input);
		}else {
			if(last_mouse_btn) {
				last_mouse_btn = 0;
				input_report_key(ztp_input, BTN_MOUSE, 0);
				input_sync(ztp_input);
			}
		}

		input_event(ztp_input, EV_MSC, MSC_TIMESTAMP, time_stamp);
		if((finger0_valid == 0) && (finger1_valid == 0) && (finger2_valid == 0) && (finger3_valid == 0))
			input_report_key(ztp_input, BTN_TOUCH, 0);
		input_sync(ztp_input);

		//log_info("%s ztp_abs:(%d,%d),(%d,%d),(%d,%d),(%d,%d)\n", __func__, finger0_x, finger0_y, finger1_x, finger1_y, finger2_x, finger2_y, finger3_x, finger3_y);
		//input_mt_sync_frame(ztp_input);
		//input_mt_report_finger_count(ztp_input, fingers_num);
		//input_event(ztp_input, EV_MSC, MSC_TIMESTAMP, time_stamp);
		//input_sync(ztp_input);
	}
	ret = count;

out:
	mutex_unlock(&info->keyboard_id_lock);
	return ret;
}

/*add sysfs node for upgrade/version/ztp_ctl start*/
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

static ssize_t ztp_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 32, "%d\n", keyboard_ztp_enable);
}
static ssize_t ztp_enable_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret;
	char status[16] = {0,};
	char *ztp_status[2] = { status, NULL };

	ret = kstrtou32(buf, 32, &keyboard_ztp_enable);
	if (ret) {
		log_err("%s: failed ret=%d\n", __func__, ret);
	}

	snprintf(status, 16, "ZTP_STATUS=%d", keyboard_ztp_enable);
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, ztp_status);
	log_info("ztp_enable_store:%d\n", keyboard_ztp_enable);

	return count;
}
static DEVICE_ATTR(ztp_enable, 0664, ztp_enable_show, ztp_enable_store);

static struct attribute *zsk_input_attributes[] = {
	&dev_attr_zsk_update_status.attr,
	&dev_attr_ztp_enable.attr,
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
/*add sysfs node for upgrade/version/ztp_ctl start*/

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
	ret = zsk_gpio_and_irq_init();
	if (ret){
		log_err("%s: zsk_gpio_and_irq_init failed!\n", __func__);
		return ret;
	}
	schedule_delayed_work(&info->keyboard_id_work, msecs_to_jiffies(3000));

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
	//	input_unregister_device(ztp_input);
	//}
	//misc_deregister(&zsk_miscdev);

	log_info("%s: finished!\n", __func__);
}
static int zsk_input_remove(struct platform_device *keyboard_dev)
{
	//if (zsk_input_register_flag == 1) {
	//	input_unregister_device(zkb_input);
	//	input_unregister_device(ztp_input);
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
MODULE_DESCRIPTION("ZTE ZSK && ZTP Driver");
