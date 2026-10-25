#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/pcie-rc-sprd.h>

#include "aicwf_txrxif.h"

struct marlin_dev {
    int dcdc_en;
    int power_key_en;
    int ldo_en;
    int wf_wake_host_en;
};

struct marlin_dev marlin_dev;

// 函数原型声明

void marlin_gpio_free_all(void)
{
    if (gpio_is_valid(marlin_dev.wf_wake_host_en)) {
        gpio_free(marlin_dev.wf_wake_host_en);
    }

    if (gpio_is_valid(marlin_dev.dcdc_en)) {
        gpio_direction_output(marlin_dev.dcdc_en, 0); // 拉低
        gpio_free(marlin_dev.dcdc_en);
    }

    if (gpio_is_valid(marlin_dev.ldo_en)) {
        gpio_direction_output(marlin_dev.ldo_en, 0); // 拉低
        gpio_free(marlin_dev.ldo_en);
    }

    if (gpio_is_valid(marlin_dev.power_key_en)) {
        gpio_direction_output(marlin_dev.power_key_en, 0); // 拉低
        gpio_free(marlin_dev.power_key_en);
    }

    AICWFDBG(LOGINFO,"Marlin GPIOs released and powered down\n");
}

static struct platform_device *to_pdev_from_ep_node(struct device_node *ep_node)
{
    struct device_node *pdev_node;

    AICWFDBG(LOGINFO,"%s\n", __func__);
    pdev_node = of_parse_phandle(ep_node, "sprd,rc-ctrl", 0);
    if (!pdev_node) {
        AICWFDBG(LOGINFO,"%s: pcie ep lacks property sprd,rc-ctrl\n", __func__);
        return NULL;
    }

    return of_find_device_by_node(pdev_node);
}

// 修复：函数定义需加 void，并使用 static
static int sprd_pcie_scan_card(void)
{
    struct platform_device *pdev;
    struct device *dev;
    struct device_node *np = of_find_compatible_node(NULL, NULL, "unisoc,marlin3_pcie");
    AICWFDBG(LOGERROR,"sprd_pcie_scan_card enter\n");

    if (!np) {
        AICWFDBG(LOGERROR,"device node not found\n");
        return -ENODEV;
    }

    AICWFDBG(LOGERROR,"device node name: %s\n", np->name);

    pdev = to_pdev_from_ep_node(np);
    if (!pdev) {
        AICWFDBG(LOGINFO,"can't get pcie rc node\n");
        return -ENODEV;
    }

    dev = &pdev->dev;
    AICWFDBG(LOGINFO,"%s: rc node name: %s\n", __func__, dev->of_node->name);

    sprd_pcie_configure_device(pdev);

    AICWFDBG(LOGINFO,"scan end\n");

    return 0;
}

static int sprd_pcie_remove_card(void)
{
    struct platform_device *pdev;
    struct device *dev;
    struct device_node *np = of_find_compatible_node(NULL, NULL, "unisoc,marlin3_pcie");

    if (!np) {
        AICWFDBG(LOGERROR,"device node not found\n");
        return -ENODEV;
    }

    AICWFDBG(LOGINFO,"device node name: %s\n", np->name);

    pdev = to_pdev_from_ep_node(np);
    if (!pdev) {
        AICWFDBG(LOGINFO,"can't get pcie rc node\n");
        return -ENODEV;
    }

	dev = &pdev->dev;
	AICWFDBG(LOGINFO,"%s: rc node name: %s\n",
			__func__, dev->of_node->name);

	sprd_pcie_unconfigure_device(pdev);
	/*priv->dev = NULL;
	priv->rc_pd = NULL;
	if (wait_for_completion_timeout(&priv->remove_done,
					msecs_to_jiffies(5000)) == 0)
		WCN_ERR("remove card time out\n");
	else
		WCN_INFO("remove card end\n");*/
    AICWFDBG(LOGINFO,"remove_card end\n");
    return 0;
}



int marlin_gpio_init(void)
{
    struct device_node *np = of_find_compatible_node(NULL, NULL, "unisoc,marlin3_pcie");
    int ret=0;

    AICWFDBG(LOGINFO,"marlin_gpio_init aic dbg \n");

    if (!np) {
        AICWFDBG(LOGERROR,"Failed to find device node\n");
        return -ENODEV;
    }

    marlin_dev.dcdc_en = of_get_named_gpio(np, "aicdcdcenabled-gpios", 0);
    marlin_dev.power_key_en = of_get_named_gpio(np, "aicpowerkeyenabled-gpios", 0);
    marlin_dev.ldo_en = of_get_named_gpio(np, "aicldoenabled-gpios", 0);
    marlin_dev.wf_wake_host_en = of_get_named_gpio(np, "aicwf_wake_host-gpios", 0);
    of_node_put(np);

    if (!gpio_is_valid(marlin_dev.dcdc_en) ||
        !gpio_is_valid(marlin_dev.power_key_en) ||
        !gpio_is_valid(marlin_dev.ldo_en) ||
        !gpio_is_valid(marlin_dev.wf_wake_host_en)) {
        AICWFDBG(LOGERROR,"Invalid GPIOs detected\n");
        return -EINVAL;
    }

    ret = gpio_request(marlin_dev.dcdc_en, "marlin_dcdc_en");
    if (ret) goto err_out;
    ret = gpio_request(marlin_dev.power_key_en, "marlin_power_key_en");
    if (ret) goto err_out;
    ret = gpio_request(marlin_dev.ldo_en, "marlin_ldo_en");
    if (ret) goto err_out;
    ret = gpio_request(marlin_dev.wf_wake_host_en, "marlin_wf_wake_host_en");
    if (ret) goto err_out;

    mdelay(5);
    AICWFDBG(LOGINFO,"marlin wf_wake_host_en pulled up\n");

    gpio_direction_output(marlin_dev.dcdc_en, 0);
    gpio_direction_output(marlin_dev.ldo_en, 0);
    gpio_direction_output(marlin_dev.power_key_en, 0);
    mdelay(1);

    //gpio_direction_output(marlin_dev.dcdc_en, 0);
    //mdelay(1);
    gpio_direction_output(marlin_dev.dcdc_en, 1);
    mdelay(1);
    AICWFDBG(LOGINFO,"marlin dcdc_en pulled up\n");

    //gpio_direction_output(marlin_dev.ldo_en, 0);
    //mdelay(1);
    gpio_direction_output(marlin_dev.ldo_en, 1);
    mdelay(1);
    AICWFDBG(LOGINFO,"marlin ldo_en pulled up\n");

    //gpio_direction_output(marlin_dev.power_key_en, 0);
    //mdelay(1);
    gpio_direction_output(marlin_dev.power_key_en, 1);
    mdelay(1);
    AICWFDBG(LOGINFO,"marlin power_key_en pulled up\n");

    ret = sprd_pcie_scan_card();
    if (ret)
        AICWFDBG(LOGINFO,"scan ret %d", ret);

    return 0;

err_out:
    AICWFDBG(LOGERROR,"GPIO request or setup failed\n");
    marlin_gpio_free_all();
    return ret;
}

void marlin_gpio_exit(void)
{
    int ret=0;

    ret = sprd_pcie_remove_card();
    if (ret)
        AICWFDBG(LOGINFO,"gpio exit ret %d", ret);

    marlin_gpio_free_all();
    AICWFDBG(LOGINFO,"Marlin GPIO module unloaded\n");
}
