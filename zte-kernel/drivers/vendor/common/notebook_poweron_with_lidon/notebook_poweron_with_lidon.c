#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/err.h>
#include <linux/printk.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm_wakeup.h>

#define ZTE_BSP_LOG_PREFIX "[ZTE_LDD_LID_ON]"
#define log_err(fmt, ...) pr_err(ZTE_BSP_LOG_PREFIX "[ERR]" fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) pr_warn(ZTE_BSP_LOG_PREFIX "[WARN]" fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) pr_info(ZTE_BSP_LOG_PREFIX "[INFO]" fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) pr_debug(ZTE_BSP_LOG_PREFIX "[DEBUG]" fmt, ##__VA_ARGS__)

#define NB_POWERON_LIBON_EN "poweron_with_lidon_en"
#define NB_POWERON_LIBON_STATUS "poweron_with_lidon_en_status"

#define DRIVER_NAME "poweron_with_lidon"


struct poweron_with_lidon_data {
	int poweron_with_lidon_en;
	int poweron_with_lidon_en_status;
	struct mutex lidon_lock;
	int nb_gpio_open_lid_d;
	int nb_gpio_open_lid_clk;
	int nb_gpio_d_status;

};
struct poweron_with_lidon_data *lidon_data;

static ssize_t poweron_with_lidon_en_show(struct device *dev,
                        struct device_attribute *attr, char *buf)
{
	struct poweron_with_lidon_data *lidon_data = dev_get_drvdata(dev);
	int val;

	mutex_lock(&lidon_data->lidon_lock);
	val = lidon_data->poweron_with_lidon_en;
	mutex_unlock(&lidon_data->lidon_lock);

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t poweron_with_lidon_en_store(struct device *dev,
                         struct device_attribute *attr,
                         const char *buf, size_t count)
{
	struct poweron_with_lidon_data *lidon_data = dev_get_drvdata(dev);
	int ret, value;

	ret = kstrtoint(buf, 10, &value);
	if (ret < 0)
		return ret;

	mutex_lock(&lidon_data->lidon_lock);
	lidon_data->poweron_with_lidon_en = value;
	if (value == 0) {
		gpio_set_value(lidon_data->nb_gpio_open_lid_clk, 0);
		msleep(5);
		gpio_set_value(lidon_data->nb_gpio_open_lid_d, 0);
		msleep(5);
		gpio_set_value(lidon_data->nb_gpio_open_lid_clk, 1);
	} else {
		gpio_set_value(lidon_data->nb_gpio_open_lid_clk, 0);
		msleep(5);
		gpio_set_value(lidon_data->nb_gpio_open_lid_d, 1);
		msleep(5);
		gpio_set_value(lidon_data->nb_gpio_open_lid_clk, 1);
	}
	mutex_unlock(&lidon_data->lidon_lock);

	return count;
}

static ssize_t poweron_with_lidon_en_status_show(struct device *dev,
                        struct device_attribute *attr, char *buf)
{
	struct poweron_with_lidon_data *lidon_data = dev_get_drvdata(dev);
	int val;

	mutex_lock(&lidon_data->lidon_lock);
	lidon_data->poweron_with_lidon_en_status = !gpio_get_value(lidon_data->nb_gpio_d_status);
	val = lidon_data->poweron_with_lidon_en_status;
	//log_err("%s: lidon_data->nb_gpio_d_status: %d\n", __FUNCTION__, lidon_data->poweron_with_lidon_en_status);
	mutex_unlock(&lidon_data->lidon_lock);

	return sysfs_emit(buf, "%d\n", val);
}

static DEVICE_ATTR(poweron_with_lidon_en, 0664, poweron_with_lidon_en_show, poweron_with_lidon_en_store);
static DEVICE_ATTR(poweron_with_lidon_en_status, 0444, poweron_with_lidon_en_status_show, NULL);

static int lidon_gpio_init() {
	int ret = 0;
	struct device_node *node;

	node = of_find_node_with_property(NULL, "zte,nb_gpio_open_lid_d");
	if(node) {
		lidon_data->nb_gpio_open_lid_d = of_get_named_gpio(node, "zte,nb_gpio_open_lid_d", 0);
	}
	ret = gpio_request(lidon_data->nb_gpio_open_lid_d, "nb_gpio_open_lid_d");
	if (ret < 0) {
		log_err("nb_gpio_open_lid_d request failed!\n");
		return ret;
	}
	ret = gpio_direction_output(lidon_data->nb_gpio_open_lid_d,0);
	if (ret < 0) {
		log_err("nb_gpio_open_lid_d set dir failed!\n");
		return ret;
	}

	msleep(1);
	node = of_find_node_with_property(NULL, "zte,nb_gpio_open_lid_clk");
	if(node) {
		lidon_data->nb_gpio_open_lid_clk = of_get_named_gpio(node, "zte,nb_gpio_open_lid_clk", 0);
	}
	ret = gpio_request(lidon_data->nb_gpio_open_lid_clk, "nb_gpio_open_lid_clk");
	if (ret < 0) {
		log_err("nb_gpio_open_lid_clk request failed!\n");
		return ret;
	}
	ret = gpio_direction_output(lidon_data->nb_gpio_open_lid_clk,1);
	if (ret < 0) {
		log_err("nb_gpio_open_lid_clk set dir failed!\n");
		return ret;
	}

	msleep(1);
	node = of_find_node_with_property(NULL, "zte,nb_gpio_d_status");
	if(node) {
		lidon_data->nb_gpio_d_status = of_get_named_gpio(node, "zte,nb_gpio_d_status", 0);
	}
	ret = gpio_request(lidon_data->nb_gpio_d_status, "nb_gpio_d_status");
	if (ret < 0) {
		log_err("nb_gpio_d_status request failed!\n");
		return ret;
	}
	ret = gpio_direction_input(lidon_data->nb_gpio_d_status);
	if (ret < 0) {
		log_err("nb_gpio_d_status set dir failed!\n");
		return ret;
	}

/* enable lidon  func when boot start */
	gpio_set_value(lidon_data->nb_gpio_open_lid_clk, 0);
	msleep(5);
	gpio_set_value(lidon_data->nb_gpio_open_lid_d, 1);
	msleep(5);
	gpio_set_value(lidon_data->nb_gpio_open_lid_clk, 1);
	lidon_data->poweron_with_lidon_en = 1;
/* enable lidon  func when boot end */

	return 0;
}

static int poweron_with_lidon_probe(struct platform_device *pdev)
{
	int ret;

	lidon_data = devm_kzalloc(&pdev->dev, sizeof(*lidon_data), GFP_KERNEL);
	if (!lidon_data)
		return -ENOMEM;

	mutex_init(&lidon_data->lidon_lock);
	dev_set_drvdata(&pdev->dev, lidon_data);

	ret = device_create_file(&pdev->dev, &dev_attr_poweron_with_lidon_en);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create poweron_with_lidon_en attribute\n");
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_poweron_with_lidon_en_status);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create poweron_with_lidon_en_status attribute\n");
		device_remove_file(&pdev->dev, &dev_attr_poweron_with_lidon_en);
		return ret;
	}

	ret = lidon_gpio_init();
	if (ret) {
		dev_err(&pdev->dev, "Failed to create poweron_with_lidon_en_status attribute\n");
		return ret;
	}
	dev_info(&pdev->dev, "poweron_with_lidon probed\n");
	return 0;
}

static int poweron_with_lidon_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_poweron_with_lidon_en);
	device_remove_file(&pdev->dev, &dev_attr_poweron_with_lidon_en_status);

	dev_info(&pdev->dev, "poweron_with_lidon removed\n");
	return 0;
}

static const struct of_device_id poweron_with_lidon_of_match[] = {
	{ .compatible = "zte,poweron-with-lidon" },
	{}
};
MODULE_DEVICE_TABLE(of, poweron_with_lidon_of_match);

static struct platform_driver poweron_with_lidon_driver = {
	.probe = poweron_with_lidon_probe,
	.remove = poweron_with_lidon_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = poweron_with_lidon_of_match,
	},
};
module_platform_driver(poweron_with_lidon_driver);

MODULE_AUTHOR("zte light <dou.mingming@zte.com.cn>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("zte notebook poweron when lid on");