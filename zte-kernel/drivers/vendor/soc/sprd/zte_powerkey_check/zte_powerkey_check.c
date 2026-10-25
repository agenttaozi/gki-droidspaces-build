#include <linux/delay.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/time.h>
#include <linux/ctype.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/timer.h>

#define ZTE_BSP_LOG_PREFIX "[ZTE_LDD_BOOT][POWERKEY]"
#define log_err(fmt, ...) pr_err(ZTE_BSP_LOG_PREFIX "[ERR]" fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) pr_warn(ZTE_BSP_LOG_PREFIX "[WARN]" fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) pr_info(ZTE_BSP_LOG_PREFIX "[INFO]" fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) pr_debug(ZTE_BSP_LOG_PREFIX "[DEBUG]" fmt, ##__VA_ARGS__)

static int calibrate_mode = 0;
static struct timer_list cali_powerkey_timer;
static struct work_struct powerkey_reboot_work;

struct zte_powerkey_check_data_type {
	struct input_handle *handle;
	int value;
	int in_use;
};

#define ZTE_POWERKEY_CHECK_DATA_MAX_SIZE 10
static struct zte_powerkey_check_data_type zte_powerkey_check_data[ZTE_POWERKEY_CHECK_DATA_MAX_SIZE];
static int zte_powerkey_check_data_write_index = 0;
static int zte_powerkey_check_data_read_index = 0;
static DEFINE_SPINLOCK(zte_powerkey_data_lock);
static struct delayed_work powerkey_check_dwork;

#define STATUS_SIZE 16

static void zte_powerkey_check_work(struct work_struct *work)
{
	int index = -1;
	do {
		unsigned long flags;

		spin_lock_irqsave(&zte_powerkey_data_lock, flags);
		index = zte_powerkey_check_data_read_index;
		if (zte_powerkey_check_data[index].in_use == 0) {
			spin_unlock_irqrestore(&zte_powerkey_data_lock, flags);
			break;
		}
		zte_powerkey_check_data_read_index = (zte_powerkey_check_data_read_index + 1) % ZTE_POWERKEY_CHECK_DATA_MAX_SIZE;
		spin_unlock_irqrestore(&zte_powerkey_data_lock, flags);

		if (zte_powerkey_check_data[index].in_use) {
			if (zte_powerkey_check_data[index].value) {
				char pressed_status[STATUS_SIZE] = {0};
				char *key_code = "KEYCODE=116";
				char *envp[3] = {key_code, pressed_status, NULL};
				// ZTE send powerkey uevent to fingerprint
				snprintf(pressed_status, STATUS_SIZE, "PRESSED=%d", zte_powerkey_check_data[index].value);
				kobject_uevent_env(&(zte_powerkey_check_data[index].handle->dev->dev.kobj), KOBJ_CHANGE, envp);
				log_info("powerkey pressed\n");
			} else {
				log_info("powerkey released\n");
			}
		}

		spin_lock_irqsave(&zte_powerkey_data_lock, flags);
		zte_powerkey_check_data[index].in_use = 0;
		spin_unlock_irqrestore(&zte_powerkey_data_lock, flags);
	} while(1);
}

static void powerkey_reboot_work_func(struct work_struct *work)
{
	kernel_restart("LONGPRESS");
}

static void cali_powerkey_timer_func(struct timer_list *t)
{
	schedule_work(&powerkey_reboot_work);
}

static void zte_powerkey_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	if (type == EV_KEY && code == KEY_POWER) {
		int index = zte_powerkey_check_data_write_index;
		if (calibrate_mode) {
			if (value)
				mod_timer(&cali_powerkey_timer, jiffies + 3 * HZ);
			else
				del_timer(&cali_powerkey_timer);
		}
		spin_lock(&zte_powerkey_data_lock);
		if (zte_powerkey_check_data[index].in_use == 0) {
			zte_powerkey_check_data[index].handle = handle;
			zte_powerkey_check_data[index].value = value;
			zte_powerkey_check_data[index].in_use = 1;
			zte_powerkey_check_data_write_index = (zte_powerkey_check_data_write_index + 1) % ZTE_POWERKEY_CHECK_DATA_MAX_SIZE;
		}
		spin_unlock(&zte_powerkey_data_lock);
		schedule_delayed_work(&powerkey_check_dwork, 0);
	}
}

static const struct input_device_id zte_powerkey_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { [BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER) },
	},
	{},
};
static int zte_powerkey_connect(struct input_handler *handler,
							   struct input_dev *dev,
							   const struct input_device_id *id)
{
	struct input_handle *zte_powerkey_handle;
	int error;
	zte_powerkey_handle = (struct input_handle *)kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!zte_powerkey_handle)
		return -ENOMEM;

	zte_powerkey_handle->dev = dev;
	zte_powerkey_handle->handler = handler;
	zte_powerkey_handle->name = "calireboot";

	error = input_register_handle(zte_powerkey_handle);
	if (error) {
		log_err("Failed to register input zte_powerkey handler, error %d\n", error);
		goto err_free;
	}

	error = input_open_device(zte_powerkey_handle);
	if (error) {
		log_err("Failed to open input device, error %d\n", error);
		goto err_unregister;
	}

	return 0;

err_unregister:
	input_unregister_handle(zte_powerkey_handle);
err_free:
	kfree(zte_powerkey_handle);
	return error;
}

static void zte_powerkey_disconnect(struct input_handle *handle)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&zte_powerkey_data_lock, flags);
	for (i = 0; i < ZTE_POWERKEY_CHECK_DATA_MAX_SIZE; i++)
		zte_powerkey_check_data[i].in_use = 0;
	spin_unlock_irqrestore(&zte_powerkey_data_lock, flags);

	cancel_delayed_work_sync(&powerkey_check_dwork);
	del_timer(&cali_powerkey_timer);
	cancel_work_sync(&powerkey_reboot_work);

	input_close_device(handle);
	input_unregister_handle(handle);
}

static struct input_handler zte_powerkey_handler = {
	.event = zte_powerkey_event,
	.connect = zte_powerkey_connect,
	.disconnect = zte_powerkey_disconnect,
	.name = "zte_powerkey",
	.id_table = zte_powerkey_ids,
};

static int __init zte_powerkey_init(void)
{
	struct device_node *cmdline_node;
	const char *cmdline;

	log_info("%s: called\n", __func__);

	cmdline_node = of_find_node_by_path("/chosen");
	if (!cmdline_node || of_property_read_string(cmdline_node, "bootargs", &cmdline)) {
		log_err("%s: Cannot parse bootargs\n", __func__);
		return -EINVAL;
	}

	if (strstr(cmdline, "sprdboot.mode=cali")) {
		calibrate_mode = 1;
		log_info("%s: in calibrate mode\n", __func__);
	}

	INIT_DELAYED_WORK(&powerkey_check_dwork, zte_powerkey_check_work);
	INIT_WORK(&powerkey_reboot_work, powerkey_reboot_work_func);
	timer_setup(&cali_powerkey_timer, cali_powerkey_timer_func, 0);

	if (input_register_handler(&zte_powerkey_handler)) {
		log_err("Failed to register zte_powerkey_handler\n");
		return -EINVAL;
	}

	return 0;
}

static void __exit zte_powerkey_exit(void)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&zte_powerkey_data_lock, flags);
	for (i = 0; i < ZTE_POWERKEY_CHECK_DATA_MAX_SIZE; i++)
		zte_powerkey_check_data[i].in_use = 0;
	spin_unlock_irqrestore(&zte_powerkey_data_lock, flags);

	input_unregister_handler(&zte_powerkey_handler);
	cancel_delayed_work_sync(&powerkey_check_dwork);
	del_timer(&cali_powerkey_timer);
	cancel_work_sync(&powerkey_reboot_work);
}

module_init(zte_powerkey_init);
module_exit(zte_powerkey_exit);
MODULE_LICENSE("GPL");
