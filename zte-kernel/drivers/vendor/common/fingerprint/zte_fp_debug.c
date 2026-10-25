/****************************

       zte_fp_debug.c

****************************/

#include "zte_fp_entry.h"
/*log lovel*/
int zte_fp_log_level = INFO_LOG;
char* zte_fp_log_level_str[] = {"ERR_LOG", "WARN_LOG", "INFO_LOG", "DEBUG_LOG", "ALL_LOG"};
/*proc node*/
struct proc_dir_entry *zte_fp_debug_proc_dir = NULL;
#define PROC_ZTE_FP_DEBUG_DIR    "zte_fp_debug"
#define PROC_ZTE_FP_LOG_LEVEL    "log_level"
#define PROC_ZTE_FP_HW_RESET     "hw_reset"
#define PROC_ZTE_FP_HW_POWER     "hw_power"

/*gpio*/
int fp_pwr_gpio, fp_rst_gpio, fp_int_gpio;
#define ZTE_FP_RST_DELAY_TIME       20 //ms

/*******************************************log_level start******************************************/
static ssize_t log_level_get(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char *data_buf = NULL;

	if (*offset != 0) {
		return 0;
	}

	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		zte_fp_log(ERR_LOG, "alloc data_buf failed");
		return -ENOMEM;
	}

	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n\n*************************usage*************************\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "           ERR_LOG       echo 0 > log_level\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "           WARN_LOG      echo 1 > log_level\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "           INFO_LOG      echo 2 > log_level\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "           DEBUG_LOG     echo 3 > log_level\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "           ALL_LOG       echo 4 > log_level\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len,  "\n*******************************************************\n\n");

	len += snprintf(data_buf + len, PAGE_SIZE - len, "Now log level : %s\n\n", zte_fp_log_level_str[zte_fp_log_level]);

	zte_fp_log(INFO_LOG, "Now log level : %s", zte_fp_log_level_str[zte_fp_log_level]);

	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	return len;
}
static ssize_t log_level_set(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;

	zte_fp_log(INFO_LOG, "Old log_level = %s", zte_fp_log_level_str[zte_fp_log_level]);

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret) {
		return -EINVAL;
	}

	zte_fp_log(INFO_LOG, "input = %d", input);
	switch (input) {
		case ERR_LOG :
			zte_fp_log_level = ERR_LOG;
		break;

		case WARN_LOG :
			zte_fp_log_level = WARN_LOG;
		break;

		case INFO_LOG :
			zte_fp_log_level = INFO_LOG;
		break;

		case DEBUG_LOG :
			zte_fp_log_level = DEBUG_LOG;
		break;

		case ALL_LOG :
			zte_fp_log_level = ALL_LOG;
		break;

		default  :
			zte_fp_log_level = INFO_LOG;
		break;
	}

	zte_fp_log(INFO_LOG, "New log_level = %s", zte_fp_log_level_str[zte_fp_log_level]);

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops log_level = {
	.proc_read = log_level_get,
	.proc_write = log_level_set,
};
#else
static const struct file_operations log_level = {
	.owner = THIS_MODULE,
	.read = log_level_get,
	.write = log_level_set,
};
#endif

/*****************************************log_level end***********************************************/

/*****************************************hw_reset start**********************************************/
static ssize_t hw_reset_get(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char *data_buf = NULL;

	if (*offset != 0) {
		return 0;
	}

	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		zte_fp_log(ERR_LOG, "alloc data_buf failed");
		return -ENOMEM;
	}

	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n\n*************************usage*************************\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "     Set reset low                   echo 0 > hw_reset\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "     Set reset high                  echo 1 > hw_reset\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "     Set reset low->high             echo 2 > hw_reset\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "     Set reset high->low             echo 3 > hw_reset\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "     Set reset low->high->low        echo 4 > hw_reset\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "     Set reset high->low->high       echo 5 > hw_reset\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n*******************************************************\n\n");

	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	return len;
}
static ssize_t hw_reset_set(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret) {
		return -EINVAL;
	}

	if(!gpio_is_valid(fp_rst_gpio)) {
		return -EINVAL;
	}

	zte_fp_log(INFO_LOG, "input = %d", input);
	switch (input) {
		case 0 :
			zte_fp_log(INFO_LOG, "Set reset low");
			gpio_set_value(fp_rst_gpio, 0);
		break;

		case 1 :
			zte_fp_log(INFO_LOG, "Set reset high");
			gpio_set_value(fp_rst_gpio, 1);
		break;

		case 2 :
			zte_fp_log(INFO_LOG, "Set reset low->high");
			gpio_set_value(fp_rst_gpio, 0);
			msleep(ZTE_FP_RST_DELAY_TIME);
			gpio_set_value(fp_rst_gpio, 1);
		break;

		case 3 :
			zte_fp_log(INFO_LOG, "Set reset high->low");
			gpio_set_value(fp_rst_gpio, 1);
			msleep(ZTE_FP_RST_DELAY_TIME);
			gpio_set_value(fp_rst_gpio, 0);
		break;

		case 4 :
			zte_fp_log(INFO_LOG, "Set reset low->high->low");
			gpio_set_value(fp_rst_gpio, 0);
			msleep(ZTE_FP_RST_DELAY_TIME);
			gpio_set_value(fp_rst_gpio, 1);
			msleep(ZTE_FP_RST_DELAY_TIME);
			gpio_set_value(fp_rst_gpio, 0);
		break;

		case 5 :
			zte_fp_log(INFO_LOG, "Set reset high->low->high");
			gpio_set_value(fp_rst_gpio, 1);
			msleep(ZTE_FP_RST_DELAY_TIME);
			gpio_set_value(fp_rst_gpio, 0);
			msleep(ZTE_FP_RST_DELAY_TIME);
			gpio_set_value(fp_rst_gpio, 1);
		break;

		default :
			zte_fp_log(INFO_LOG, "Do nothing");
		break;
	}

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops hw_reset = {
	.proc_read = hw_reset_get,
	.proc_write = hw_reset_set,
};
#else
static const struct file_operations hw_reset = {
	.owner = THIS_MODULE,
	.read = hw_reset_get,
	.write = hw_reset_set,
};
#endif

/*****************************************hw_reset end***********************************************/

/*****************************************hw_power start**********************************************/
static ssize_t hw_power_get(struct file *file,
					 char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char *data_buf = NULL;

	if (*offset != 0) {
		return 0;
	}

	data_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		zte_fp_log(ERR_LOG, "alloc data_buf failed");
		return -ENOMEM;
	}

	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n\n*************************usage************************\n\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "       Set power low              echo 0 > hw_power\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "       Set power high             echo 1 > hw_power\n");
	len += snprintf(data_buf + len, PAGE_SIZE - len, "\n*******************************************************\n\n");

	simple_read_from_buffer(buffer, count, offset, data_buf, len);
	kfree(data_buf);
	return len;
}
static ssize_t hw_power_set(struct file *file,
				const char __user *buffer, size_t len, loff_t *off)
{
	int ret = 0;
	unsigned int input = 0;

	ret = kstrtouint_from_user(buffer, len, 10, &input);
	if (ret) {
		return -EINVAL;
	}

	if(!gpio_is_valid(fp_pwr_gpio)) {
		return -EINVAL;
	}

	zte_fp_log(INFO_LOG, "input = %d", input);
	switch (input) {
		case 0 :
			zte_fp_log(INFO_LOG, "Set power low");
			gpio_set_value(fp_pwr_gpio, 0);
		break;

		case 1 :
			zte_fp_log(INFO_LOG, "Set power high");
			gpio_set_value(fp_pwr_gpio, 1);
		break;

		default :
			zte_fp_log(INFO_LOG, "Do nothing");
		break;
	}

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops hw_power = {
	.proc_read = hw_power_get,
	.proc_write = hw_power_set,
};
#else
static const struct file_operations hw_power = {
	.owner = THIS_MODULE,
	.read = hw_power_get,
	.write = hw_power_set,
};
#endif
/*****************************************hw_power end***********************************************/

int zte_fp_debug_proc_init(void)
{
	int ret = 0;
	struct proc_dir_entry *zte_fp_debug = NULL;

	zte_fp_log(INFO_LOG, "enter");

	zte_fp_debug_proc_dir = proc_mkdir(PROC_ZTE_FP_DEBUG_DIR, NULL);
	if (zte_fp_debug_proc_dir == NULL) {
		zte_fp_log(ERR_LOG, "create proc/zte_fp_debug failed");
		return -EPERM;
	} else {
		zte_fp_log(INFO_LOG, "create proc/zte_fp_debug success");
	}

	zte_fp_debug = proc_create(PROC_ZTE_FP_LOG_LEVEL, 0664, zte_fp_debug_proc_dir, &log_level);
	if (zte_fp_debug == NULL) {
		zte_fp_log(ERR_LOG, "create proc/zte_fp_debug/log_level failed");
		return -EPERM;
	} else {
		zte_fp_log(INFO_LOG, "create proc/zte_fp_debug/log_level success");
	}

	zte_fp_debug = proc_create(PROC_ZTE_FP_HW_RESET, 0664, zte_fp_debug_proc_dir, &hw_reset);
	if (zte_fp_debug == NULL) {
		zte_fp_log(ERR_LOG, "create proc/zte_fp_debug/hw_reset failed");
		return -EPERM;
	} else {
		zte_fp_log(INFO_LOG, "create proc/zte_fp_debug/hw_reset success");
	}

	zte_fp_debug = proc_create(PROC_ZTE_FP_HW_POWER, 0664, zte_fp_debug_proc_dir, &hw_power);
	if (zte_fp_debug == NULL) {
		zte_fp_log(ERR_LOG, "create proc/zte_fp_debug/hw_power failed");
		return -EPERM;
	} else {
		zte_fp_log(INFO_LOG, "create proc/zte_fp_debug/hw_power success");
	}

	zte_fp_log(INFO_LOG, "exit");
	return ret;
}

int zte_fp_debug_proc_deinit(void)
{
	int ret = 0;
	zte_fp_log(INFO_LOG, "enter");

	if (zte_fp_debug_proc_dir == NULL) {
		zte_fp_log(ERR_LOG, "proc/zte_fp_debug is NULL");
		return -EPERM;
	}
	remove_proc_entry(PROC_ZTE_FP_LOG_LEVEL, zte_fp_debug_proc_dir);
	remove_proc_entry(PROC_ZTE_FP_HW_RESET, zte_fp_debug_proc_dir);
	remove_proc_entry(PROC_ZTE_FP_HW_POWER, zte_fp_debug_proc_dir);
	remove_proc_entry(PROC_ZTE_FP_DEBUG_DIR, NULL);

	zte_fp_log(INFO_LOG, "exit");
	return ret;
}

int zte_fp_debug_gpio_init(void)
{
	int ret = 0;
	struct device_node *dev_node = NULL;
#if 0
	struct device_node *dev_node1 = NULL;
#endif
	zte_fp_log(INFO_LOG, "enter");

	/* Find device tree node. */
	dev_node = of_find_compatible_node(NULL, NULL, ZTE_FP_COMPATIBLE);
	if (!dev_node) {
		zte_fp_log(ERR_LOG, "of_find_compatible_node(%s) failed", ZTE_FP_COMPATIBLE);
		return (-ENODEV);
	} else {
		zte_fp_log(INFO_LOG, "of_find_compatible_node(%s) success", ZTE_FP_COMPATIBLE);
	}

#if 0
	dev_node1 = of_find_node_by_name(NULL, ZTE_FP_MODULE_NAME);
	if (!dev_node1) {
		zte_fp_log(ERR_LOG, "of_find_node_by_name(%s) failed", ZTE_FP_MODULE_NAME);
		//return (-ENODEV);
	} else {
		zte_fp_log(INFO_LOG, "of_find_node_by_name(%s) success", ZTE_FP_MODULE_NAME);
	}
#endif

    /*------------------------reset-------------------------*/
	fp_rst_gpio = of_get_named_gpio(dev_node, "zte_fp_rst_gpio", 0);
	zte_fp_log(INFO_LOG, "fp_rst_gpio = %d", fp_rst_gpio);
	if (gpio_is_valid(fp_rst_gpio)) {
		zte_fp_log(INFO_LOG, "of_get_named_gpio fp_rst_gpio success");
	} else {
		zte_fp_log(ERR_LOG, "of_get_named_gpio fp_rst_gpio failed");
		return (-EBUSY);
	}

    /*------------------------int-------------------------*/
	fp_int_gpio = of_get_named_gpio(dev_node, "zte_fp_int_gpio", 0);
	zte_fp_log(INFO_LOG, "fp_int_gpio = %d", fp_int_gpio);
	if (gpio_is_valid(fp_int_gpio)) {
		zte_fp_log(INFO_LOG, "of_get_named_gpio fp_int_gpio success");
	} else {
		zte_fp_log(ERR_LOG, "of_get_named_gpio fp_int_gpio failed");
		return (-EBUSY);
	}

    /*------------------------power-------------------------*/
	fp_pwr_gpio = of_get_named_gpio(dev_node, "zte_fp_pwr_gpio", 0);
	zte_fp_log(INFO_LOG, "fp_pwr_gpio = %d", fp_pwr_gpio);
	if (gpio_is_valid(fp_pwr_gpio)) {
		zte_fp_log(INFO_LOG, "of_get_named_gpio fp_pwr_gpio success");
	} else {
		zte_fp_log(ERR_LOG, "of_get_named_gpio fp_pwr_gpio failed");
		return (-EBUSY);
	}

	zte_fp_log(INFO_LOG, "exit");
	return ret;
}

int zte_fp_debug_gpio_deinit(void)
{
	zte_fp_log(INFO_LOG, "enter");

	if(gpio_is_valid(fp_rst_gpio)) {
		fp_rst_gpio = 0;
	}

	if(gpio_is_valid(fp_int_gpio)) {
		fp_int_gpio = 0;
	}

	if(gpio_is_valid(fp_pwr_gpio)) {
		fp_pwr_gpio = 0;
	}

	zte_fp_log(INFO_LOG, "exit");
	return 0;
}
