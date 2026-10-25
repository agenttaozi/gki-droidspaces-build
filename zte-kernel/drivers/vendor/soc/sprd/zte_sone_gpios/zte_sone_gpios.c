/* Started by AICoder, pid:7a9fda1787t73be1434c0a61247374276a8024a7 */
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/pcie-rc-sprd.h>
#include <linux/delay.h>

extern int dw_pcie_read(void __iomem *addr, int size, u32 *val);
#define BOOT_MODE_MAX_LEN 20
static char boot_mode[BOOT_MODE_MAX_LEN] = "NONE";

struct zte_gpios_data {
    int usbswitch_det_gpio;
    int pwn_det_gpio;
    int reset_det_gpio;
    int upwn_det_gpio;
    int pwnok_det_gpio;
    u32 usbswitch_active_low;
    u32 pwn_active_low;
    u32 reset_active_low;
    u32 upwn_active_low;
    u32 pwnok_active_low;
    struct device *dev;
};

static int usbswitch_status = 0;
static int pwn_status = 0;
static int reset_status = 0;
static int upwn_status = 0;
static int pwnok_status = 0;
static int pciescan_status = 0;
//static char pciereg_status[1000] = "NONE";

static int rpc_get_bootmode(void)
{
	struct device_node *np = NULL;
	const char *cmd_line = NULL, *s = NULL;
	int ret = 0;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		pr_err("%s: find chosen failed\n", __func__);
		return 0;
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		pr_err("%s: read bootargs failed\n", __func__);
		return 0;
	}

	s = strstr(cmd_line, "sprdboot.mode=");
	if (!s) {
		pr_err("%s: find sprdboot.mode failed\n", __func__);
		return 0;
	}

	sscanf(s, "sprdboot.mode=%s", boot_mode);
	pr_info("%s: rpc sprdboot.mode is %s\n", __func__, boot_mode);
	return 0;
}

static struct platform_device *to_pdev_from_ep_node(struct device_node *ep_node)
{
    struct device_node *pdev_node;

    pr_info("%s\n", __func__);
    pdev_node = of_parse_phandle(ep_node, "sprd,rc-ctrl", 0);
    if (!pdev_node) {
        pr_err("%s: pcie ep lacks property sprd,rc-ctrl\n", __func__);
        return NULL;
    }
    return of_find_device_by_node(pdev_node);
}

static ssize_t usbswitch_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    return snprintf(buf, 32, "%d\n", usbswitch_status);
}

static ssize_t usbswitch_status_store(struct device *dev,
                struct device_attribute *attr, const char *buf,
                size_t count)
{
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    struct zte_gpios_data *data = platform_get_drvdata(pdev);
    int val = -1;

    if (sscanf(buf, "%d", &val) == -1) {
        pr_err("%s: parse usbswitch_status wrong", __func__);
    }

    if (1 == val) {
        if (gpio_is_valid(data->usbswitch_det_gpio))
            gpio_direction_output(data->usbswitch_det_gpio, 1);
    }

    if (0 == val) {
        if (gpio_is_valid(data->usbswitch_det_gpio))
            gpio_direction_output(data->usbswitch_det_gpio, 0);
    }
    usbswitch_status = val;
    return count;
}

static ssize_t pwn_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    return snprintf(buf, 32, "%d\n", pwn_status);
}

static ssize_t pwn_status_store(struct device *dev,
                struct device_attribute *attr, const char *buf,
                size_t count)
{
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    struct zte_gpios_data *data = platform_get_drvdata(pdev);
    int val = -1;

    if (sscanf(buf, "%d", &val) == -1) {
        pr_err("%s: parse pwn_status wrong", __func__);
    }

    if (1 == val) {
        if (gpio_is_valid(data->pwn_det_gpio))
            gpio_direction_output(data->pwn_det_gpio, 1);
    }

    if (0 == val) {
        if (gpio_is_valid(data->pwn_det_gpio))
            gpio_direction_output(data->pwn_det_gpio, 0);
    }

    pwn_status = val;
    return count;
}

static ssize_t reset_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    return snprintf(buf, 32, "%d\n", reset_status);
}

static ssize_t reset_status_store(struct device *dev,
                struct device_attribute *attr, const char *buf,
                size_t count)
{
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    struct zte_gpios_data *data = platform_get_drvdata(pdev);
    int val = -1;

    if (sscanf(buf, "%d", &val) == -1) {
        pr_err("%s: parse reset_status wrong", __func__);
    }

    if (1 == val) {
        if (gpio_is_valid(data->reset_det_gpio))
            gpio_direction_output(data->reset_det_gpio, 1);
    }

    if (0 == val) {
        if (gpio_is_valid(data->reset_det_gpio))
            gpio_direction_output(data->reset_det_gpio, 0);
    }

    reset_status = val;
    return count;
}

static ssize_t upwn_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    return snprintf(buf, 32, "%d\n", upwn_status);
}

static ssize_t upwn_status_store(struct device *dev,
                struct device_attribute *attr, const char *buf,
                size_t count)
{
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    struct zte_gpios_data *data = platform_get_drvdata(pdev);
    int val = -1;

    if (sscanf(buf, "%d", &val) == -1) {
        pr_err("%s: parse upwn_status wrong", __func__);
    }

    if (1 == val) {
        if (gpio_is_valid(data->upwn_det_gpio))
            gpio_direction_output(data->upwn_det_gpio, 1);
    }

    if (0 == val) {
        if (gpio_is_valid(data->upwn_det_gpio))
            gpio_direction_output(data->upwn_det_gpio, 0);
    }

    upwn_status = val;
    return count;
}

static ssize_t pwnok_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    return snprintf(buf, 32, "%d\n", pwnok_status);
}

static ssize_t pciescan_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    return snprintf(buf, 32, "%d\n", pciescan_status);
}

static ssize_t pciescan_status_store(struct device *dev,
                struct device_attribute *attr, const char *buf,
                size_t count)
{
    struct platform_device *pdev;
    struct device_node *np = dev->of_node;
    int val = -1;
    int ret = -1;

    pdev = to_pdev_from_ep_node(np);
 	if (!pdev) {
	    pr_err("can't get pcie rc node\n");
        return count;
    }

    if (sscanf(buf, "%d", &val) == -1) {
        pr_err("%s: parse pciescan_status wrong", __func__);
    }

    if (1 == val) {
        ret = sprd_pcie_configure_device(pdev);
        if (ret) {
            pr_err("%s: sprd_pcie_configure_device wrong", __func__);
            pciescan_status = 0;
        } else {
            pciescan_status = 1;
        }
    }

    return count;
}

static ssize_t pciereg_status_show(struct device *dev, struct device_attribute *attr,
               char *buf)
{
    void __iomem *vir_addr;
    u32 val;
    int ret = -1;
    int i = 0;
    char *ptr = buf;
    for (i = 0;i < 40; i++) {
        vir_addr = ioremap(0x887ffe000 + 4 * i, 4);
        if (!vir_addr) {
            pr_err("%s: failed to ioremap pcie address 0x%x", __func__, 0x887ffe000 + 4 * i);
            return -1;
        }
	    dw_pcie_read(vir_addr, 4, &val);
        ret = snprintf(ptr, 2000, "address:0x%x, value:0x%x\n", 0x887ffe000 + 4 * i, val);
        if (ret < 0) {
            pr_err("%s: failed to get pcie address 0x%x", __func__, 0x887ffe000 + 4 * i);
            return -1;
        }
        ptr += ret;
    }
    return ptr - buf;
}

static DEVICE_ATTR_RW(usbswitch_status);
static DEVICE_ATTR_RW(pwn_status);
static DEVICE_ATTR_RW(reset_status);
static DEVICE_ATTR_RW(upwn_status);
static DEVICE_ATTR_RO(pwnok_status);
static DEVICE_ATTR_RW(pciescan_status);
static DEVICE_ATTR_RO(pciereg_status);

static struct attribute *zte_gpios_attributes[] = {
    &dev_attr_usbswitch_status.attr,
    &dev_attr_pwn_status.attr,
    &dev_attr_reset_status.attr,
    &dev_attr_upwn_status.attr,
    &dev_attr_pwnok_status.attr,
    &dev_attr_pciescan_status.attr,
    &dev_attr_pciereg_status.attr,
    NULL
};

static const struct attribute_group zte_gpios_group = {
    .attrs = zte_gpios_attributes,
};

static int zte_gpios_probe(struct platform_device *pdev)
{
    struct zte_gpios_data *drvdata;
    int ret = 0;
    pr_info("%s enter\n", __func__);

    drvdata =
        devm_kzalloc(&pdev->dev, sizeof(struct zte_gpios_data), GFP_KERNEL);
    if (!drvdata)
        return -ENOMEM;

    if (pdev->dev.of_node) {
        drvdata->usbswitch_det_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "sone_usbswitch_det_gpio", 0, &drvdata->usbswitch_active_low);
        if (drvdata->usbswitch_det_gpio < 0) {
            pr_err("%s: get usbswitch_det_gpio failed\n", __func__);
            return -1;
        }

        ret = gpio_request(drvdata->usbswitch_det_gpio, "sone_usbswitch_det_gpio");
        if (ret) {
            pr_err("%s: gpio request usbswitch_det_gpio failed\n", __func__);
            return -1;
        }

        drvdata->pwn_det_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "sone_pwn_det_gpio", 0, &drvdata->pwn_active_low);
        if (drvdata->pwn_det_gpio < 0) {
            pr_err("%s: get pwn_det_gpio failed\n", __func__);
            return -1;
        }

        ret = gpio_request(drvdata->pwn_det_gpio, "sone_pwn_det_gpio");
        if (ret) {
            pr_err("%s: gpio request pwn_det_gpio failed\n", __func__);
            return -1;
        }

        drvdata->reset_det_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "sone_reset_det_gpio", 0, &drvdata->reset_active_low);
        if (drvdata->reset_det_gpio < 0) {
            pr_err("%s: get reset_det_gpio failed\n", __func__);
            return -1;
        }

        ret = gpio_request(drvdata->reset_det_gpio, "sone_reset_det_gpio");
        if (ret) {
            pr_err("%s: gpio request reset_det_gpio failed\n", __func__);
            return -1;
        }

        drvdata->upwn_det_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "sone_upwn_det_gpio", 0, &drvdata->upwn_active_low);
        if (drvdata->upwn_det_gpio < 0) {
            pr_err("%s: get upwn_det_gpio failed\n", __func__);
            return -1;
        }

        ret = gpio_request(drvdata->upwn_det_gpio, "sone_upwn_det_gpio");
        if (ret) {
            pr_err("%s: gpio request upwn_det_gpio failed\n", __func__);
            return -1;
        }

        drvdata->pwnok_det_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "sone_pwnok_det_gpio", 0, &drvdata->pwnok_active_low);
        if (drvdata->pwnok_det_gpio < 0) {
            pr_err("%s: get pwnok_det_gpio failed\n", __func__);
            return -1;
        }

        ret = gpio_request(drvdata->pwnok_det_gpio, "sone_pwnok_det_gpio");
        if (ret) {
            pr_err("%s: gpio request pwnok_det_gpio failed\n", __func__);
            return -1;
        }

        drvdata->dev = &pdev->dev;

        ret = sysfs_create_group(&pdev->dev.kobj, &zte_gpios_group);
        if (ret) {
            pr_err("%s: sysfs create group failed\n", __func__);
            return -1;
        }

        platform_set_drvdata(pdev, drvdata);

        usbswitch_status = gpio_get_value(drvdata->usbswitch_det_gpio) ^ drvdata->usbswitch_active_low;
        pwn_status = gpio_get_value(drvdata->pwn_det_gpio) ^ drvdata->pwn_active_low;
        reset_status = gpio_get_value(drvdata->reset_det_gpio) ^ drvdata->reset_active_low;
        upwn_status = gpio_get_value(drvdata->upwn_det_gpio) ^ drvdata->upwn_active_low;
        pwnok_status = gpio_get_value(drvdata->pwnok_det_gpio) ^ drvdata->pwnok_active_low;

        rpc_get_bootmode();
    }

    pr_info("%s exit\n", __func__);
    return 0;
}

#ifdef CONFIG_PM
static int zte_gpios_suspend(struct platform_device *pdev, pm_message_t state)
{
    return 0;
}

static int zte_gpios_resume(struct platform_device *pdev)
{
    return 0;
}
#else
#define zte_gpios_suspend NULL
#define zte_gpios_resume NULL
#endif

static void zte_gpios_shutdown(struct platform_device *pdev)
{
    struct zte_gpios_data *data = platform_get_drvdata(pdev);
	pr_err("zte_gpios_shutdown start\n");

	if (strcmp(boot_mode, "charger") == 0) {
		pr_info("%s: in charger mode, no need power S1!!\n", __func__);
		return;
    }

    if (gpio_is_valid(data->pwn_det_gpio)) {
	    pr_err("zte_gpios_shutdown shutdown S1\n");
        gpio_direction_output(data->pwn_det_gpio, 1);
        mdelay(3000);
        gpio_direction_output(data->pwn_det_gpio, 0);
        mdelay(5000);
    }
    return;
}

static const struct of_device_id of_zte_gpios_match[] = {
    {.compatible = "zte-sone-gpios",},
    {},
};

static struct platform_driver zte_gpios_driver = {
    .probe = zte_gpios_probe,
    .suspend = zte_gpios_suspend,
    .resume = zte_gpios_resume,
    .shutdown = zte_gpios_shutdown,
    .driver = {
           .name = "zte-gpios",
           .of_match_table = of_zte_gpios_match,
           },
};

module_platform_driver(zte_gpios_driver);

MODULE_DESCRIPTION("ZTE gpios driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:zte-gpios");
/* Ended by AICoder, pid:7a9fda1787t73be1434c0a61247374276a8024a7 */
