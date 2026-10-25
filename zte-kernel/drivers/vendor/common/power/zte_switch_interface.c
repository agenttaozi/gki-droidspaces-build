/***********************************
*****zte switch interface****************
************************************
************************************/

#define pr_fmt(fmt)	"[ZTE_LDD_SWITCH][SWITCH_INTERFACE]: %s: " fmt, __func__

#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0))
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <vendor/common/zte_misc.h>

static int zte_switch_status = 1;
module_param(zte_switch_status, int, 0644);

struct zte_switch_chip {
	/*struct mutex lock;*/
	struct mutex update_lock;

	int dp_hdmi_switch_status;
	struct gpio_desc *dp_switch_en;
	struct gpio_desc *dp_switch_sel1;
	struct gpio_desc *dp_switch_sel2;

	int qc_switch_status;
	struct gpio_desc *qc_switch_en;
	struct platform_device *pdev;

	int mux64_switch_status;
	struct regulator *mux64_pwr;
	struct gpio_desc *mux64_switch_en;
	struct gpio_desc *mux64_switch_pol;
};

struct zte_switch_chip  *zte_switch_chip_data;

static int dp_switch_hdmi_gpio_contorl(struct zte_switch_chip *pdata, int state)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(pdata->dp_switch_en) || IS_ERR_OR_NULL(pdata->dp_switch_sel1) || IS_ERR_OR_NULL(pdata->dp_switch_sel2)) {
		pr_info(">>>%s error:pdata gpio is NULL\n", __func__);
		return -EINVAL;
	}

	switch (state) {
	case 0:
		gpiod_set_value(pdata->dp_switch_sel1, 0);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->dp_switch_sel2, 0);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->dp_switch_en, 0);
		usleep_range(5000, 5500);
		pr_info(">>>%s:set dp switch to Hi-Z state\n", __func__);
		break;
	case 1:
		gpiod_set_value(pdata->dp_switch_sel1, 1);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->dp_switch_sel2, 0);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->dp_switch_en, 1);
		usleep_range(5000, 5500);
		pr_info(">>>%s:set dp switch to USB connect state\n", __func__);
		break;
	case 2:
		gpiod_set_value(pdata->dp_switch_sel1, 1);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->dp_switch_sel2, 1);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->dp_switch_en, 1);
		usleep_range(5000, 5500);
		pr_info(">>>%s:set dp switch to HDMI connect state\n", __func__);
		break;
	default:
		pr_err("unsupported control mode.\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
TRUTH TABLE
State      EN     SEL1     SEL2     Description
0:         0       x       x       Switch disabled, All channels are Hi-Z
1:         1       0       0       Not used Now
2:         1       0       1       Not used Now
3:         1       1       0       All A channels are enabled, All B channels are Hi-Z   ---->>>> USB
4:         1       1       1       All B channels are enabled, All A channels are Hi-Z   ---->>>> HDMI
*/
static int dp_switch_hdmi_enable_set(const char *val, const void *arg)
{
	struct zte_switch_chip *pdata = (struct zte_switch_chip *) arg;
	int dp_switch_flag = 0;
	int ret = 0;

	if (!pdata) {
		pr_info("pdata is null\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(pdata->dp_switch_en) || IS_ERR_OR_NULL(pdata->dp_switch_sel1) || IS_ERR_OR_NULL(pdata->dp_switch_sel2)) {
		pr_info(">>>%s error:pdata gpio is NULL\n", __func__);
		return -EINVAL;
	}

	sscanf(val, "%d", &dp_switch_flag);
	pr_info("%s dp_switch_flag = %d, 0:Disable, 1 For USB, 2 For HDMI\n", __func__, dp_switch_flag);
	ret = dp_switch_hdmi_gpio_contorl(pdata, dp_switch_flag);
	if (ret < 0) {
		pr_err("Failed to dp_switch_hdmi_gpio_contorl\n");
	}
	pdata->dp_hdmi_switch_status = dp_switch_flag;
	return ret;
}

static int dp_switch_hdmi_enable_get(char *val, const void *arg)
{
	struct zte_switch_chip *pdata = (struct zte_switch_chip *) arg;

	if (!pdata) {
		pr_info("pdata is null\n");
		return snprintf(val, PAGE_SIZE, "arg is null");
	}

	return snprintf(val, PAGE_SIZE, "%u", pdata->dp_hdmi_switch_status);
}

struct zte_misc_ops switch_dp_enable_node = {
	.node_name = "dp_hdmi_switch",
	.set = dp_switch_hdmi_enable_set,
	.get = dp_switch_hdmi_enable_get,
	.free = NULL,
	.arg = NULL,
};

/*
TRUTH TABLE
State      EN     Description
0:         0       Switch to QC Chip
1:         1       Switch to USB
*/
static int qc_switch_enable_set(const char *val, const void *arg)
{
	struct zte_switch_chip *pdata = (struct zte_switch_chip *) arg;
	int qc_switch_flag = 0;
	int ret = 0;

	if (!pdata) {
		pr_info("pdata is null\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(pdata->qc_switch_en)) {
		pr_info(">>>%s error:pdata gpio is NULL\n", __func__);
		return -EINVAL;
	}

	sscanf(val, "%d", &qc_switch_flag);
	pr_info("%s qc_switch_en = %d, 0 For QC chip, 1 For USB\n", __func__, qc_switch_flag);
	if (qc_switch_flag == 1) {
		gpiod_set_value(pdata->qc_switch_en, 1);
		usleep_range(5000, 5500);
	} else {
		gpiod_set_value(pdata->qc_switch_en, 0);
		usleep_range(5000, 5500);
	}
	pdata->qc_switch_status = qc_switch_flag;
	return ret;
}

static int qc_switch_enable_get(char *val, const void *arg)
{
	struct zte_switch_chip *pdata = (struct zte_switch_chip *) arg;

	if (!pdata) {
		pr_info("pdata is null\n");
		return snprintf(val, PAGE_SIZE, "arg is null");
	}

	return snprintf(val, PAGE_SIZE, "%u", pdata->qc_switch_status);
}

struct zte_misc_ops switch_qc_enable_node = {
	.node_name = "qc_usb_switch",
	.set = qc_switch_enable_set,
	.get = qc_switch_enable_get,
	.free = NULL,
	.arg = NULL,
};

/*
TRUTH TABLE
State            EN     POL    Description
FLIP:            1       0      Switch to Flip
NO FLIP:         1       1      Switch to No Flip
Disable          0       0      Power-off
*/
static int mux64_switch_enable_set(const char *val, const void *arg)
{
	struct zte_switch_chip *pdata = (struct zte_switch_chip *) arg;
	int mux64_switch_flag = 0;
	int ret = 0;

	if (!pdata) {
		pr_info("pdata is null\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(pdata->mux64_pwr)  || IS_ERR_OR_NULL(pdata->mux64_switch_en) || IS_ERR_OR_NULL(pdata->mux64_switch_pol)) {
		pr_info(">>>%s error:pdata parameter is error\n", __func__);
		return -EINVAL;
	}

	sscanf(val, "%d", &mux64_switch_flag);
	pr_info("%s mux64_switch_en = %d, 0 For Disable, 1 For No Flip DP, 2 For Flip DP\n", __func__, mux64_switch_flag);

	if (mux64_switch_flag == 1) {
		ret = regulator_enable(pdata->mux64_pwr);
		if (ret) {
			pr_err(">>>%s:regulator_enable failed, ret=%d\n", __func__, ret);
			return -EINVAL;
		} else {
			pr_info(">>>%s:regulator_enable success, power_voltage 3300000\n", __func__);
		}

		gpiod_set_value(pdata->mux64_switch_pol, 0);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->mux64_switch_en, 1);
		usleep_range(5000, 5500);
	} else if (mux64_switch_flag == 2)  {
		ret = regulator_enable(pdata->mux64_pwr);
		if (ret) {
			pr_err(">>>%s:regulator_enable failed, ret=%d\n", __func__, ret);
			return -EINVAL;
		} else {
			pr_info(">>>%s:regulator_enable success, power_voltage 3300000\n", __func__);
		}

		gpiod_set_value(pdata->mux64_switch_pol, 1);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->mux64_switch_en, 1);
		usleep_range(5000, 5500);
	} else {
		gpiod_set_value(pdata->mux64_switch_pol, 0);
		usleep_range(5000, 5500);
		gpiod_set_value(pdata->mux64_switch_en, 0);
		usleep_range(5000, 5500);
		ret = regulator_disable(pdata->mux64_pwr);
		if (ret) {
			pr_err(">>>%s:regulator_disable failed, ret=%d\n", __func__, ret);
		} else {
			pr_info(">>>%s:regulator_disable success\n", __func__);
		}
	}
	pdata->mux64_switch_status = mux64_switch_flag;
	return ret;
}

static int mux64_switch_enable_get(char *val, const void *arg)
{
	struct zte_switch_chip *pdata = (struct zte_switch_chip *) arg;

	if (!pdata) {
		pr_info("pdata is null\n");
		return snprintf(val, PAGE_SIZE, "arg is null");
	}

	return snprintf(val, PAGE_SIZE, "%u", pdata->mux64_switch_status);
}

struct zte_misc_ops switch_mux64_enable_node = {
	.node_name = "mux64_switch",
	.set = mux64_switch_enable_set,
	.get = mux64_switch_enable_get,
	.free = NULL,
	.arg = NULL,
};

static int zte_switch_parse_dt(struct zte_switch_chip *pdata)
{
	int ret = 0;
	struct platform_device *pdev = pdata->pdev;

	pr_info("Finding dp-switch-en pin");
	pdata->dp_switch_en = devm_gpiod_get(&pdev->dev, "dpen", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->dp_switch_en)) {
		pr_err("Failed to find dp-switch-en pin, no defined\n");
	}

	pr_info("Finding dp-switch-sel1 pin");
	pdata->dp_switch_sel1 = devm_gpiod_get(&pdev->dev, "dpsel1", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->dp_switch_sel1)) {
		pr_err("Failed to find dp-switch-sel1 pin, no defined\n");
	}

	pr_info("Finding dp-switch-sel2 pin");
	pdata->dp_switch_sel2 = devm_gpiod_get(&pdev->dev, "dpsel2", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->dp_switch_sel2)) {
		pr_err("Failed to find dp-switch-sel2 pin, no defined\n");
	}

	pr_info("Finding qc-switch-en pin");
	pdata->qc_switch_en = devm_gpiod_get(&pdev->dev, "qcen", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->qc_switch_en)) {
		pr_err("Failed to find qc-switch-en pin, no defined\n");
	}

	pr_info("Finding mux64_pwr regulator");
	pdata->mux64_pwr = devm_regulator_get(&pdev->dev, "muxvdd");
	if (IS_ERR_OR_NULL(pdata->mux64_pwr)) {
		pr_err(">>>%s:failed to get muxvdd, no defined\n", __func__);
	} else {
		pr_info(">>>%s:get vdd regulator success\n", __func__);
	}
	ret = regulator_set_voltage(pdata->mux64_pwr, 3300000, 3300000);
	if (ret) {
		pr_err(">>>%s:regulator_set_voltage failed, ret=%d\n", __func__, ret);
	} else {
		pr_info(">>>%s:regulator_set_voltage success, power_voltage 3300000\n", __func__);
	}

	pr_info("Finding mux64_switch_en pin");
	pdata->mux64_switch_en = devm_gpiod_get(&pdev->dev, "muxen", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->mux64_switch_en)) {
		pr_err("Failed to find mux64_switch_en pin, no defined\n");
	}
	pr_info("Finding mux64_switch_pol pin");
	pdata->mux64_switch_pol = devm_gpiod_get(&pdev->dev, "muxpol", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->mux64_switch_pol)) {
		pr_err("Failed to find mux64_switch_pol pin, no defined\n");
	}

	return 0;
}

static int  zte_switch_probe(struct platform_device *pdev)
{
	int error = 0;

	pr_info("zte_switch_probe\n");
	if (pdev->dev.of_node == NULL) {
		pr_info("can not find device tree node\n");
		error = -ENODEV;
		goto exit;
	}

	zte_switch_chip_data = kzalloc(sizeof(struct zte_switch_chip), GFP_KERNEL);
	if (IS_ERR_OR_NULL(zte_switch_chip_data)) {
		error = -ENOMEM;
		goto exit;
	}

	zte_switch_chip_data->pdev = pdev;

	error = zte_switch_parse_dt(zte_switch_chip_data);
	if (error < 0) {
		pr_err("Failed to parse device tree\n");
		goto fail1;
	}

	/* Set DP Switch Default to  USB connect */
	dp_switch_hdmi_gpio_contorl(zte_switch_chip_data, 1);
	/* Set QC Switch  Default to QC chip connect */
	gpiod_set_value(zte_switch_chip_data->qc_switch_en, 0);

	zte_misc_register_callback(&switch_dp_enable_node, zte_switch_chip_data);
	zte_misc_register_callback(&switch_qc_enable_node, zte_switch_chip_data);
	zte_misc_register_callback(&switch_mux64_enable_node, zte_switch_chip_data);

	pr_info("%s:zte_switch Init success!\n", __func__);
	return 0;

fail1:
	platform_set_drvdata(pdev, NULL);
	kfree(zte_switch_chip_data);
exit:
	return error;
}

static int zte_switch_remove(struct platform_device *pdev)
{
	kfree(zte_switch_chip_data);
	return 0;
}

static struct of_device_id zte_switch_of_match[] = {
	{.compatible = "zte,zte_switch_ic", },
	{},
};

static struct platform_driver zte_switch_driver = {
	.probe = zte_switch_probe,
	.remove = zte_switch_remove,
	.driver = {
		.name = "zte_switch_ic",
		.owner = THIS_MODULE,
		.of_match_table = zte_switch_of_match,
	},
};

static int  zte_switch_init(void)
{
	pr_info("zte_switch_init\n");
	return platform_driver_register(&zte_switch_driver);
}

static void  zte_switch_exit(void)
{
	platform_driver_unregister(&zte_switch_driver);
	pr_info("zte_switch_exit\n");
}

module_init(zte_switch_init);
module_exit(zte_switch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.charger <zte.charger@zte.com>");
MODULE_DESCRIPTION("Switch interface Driver");
MODULE_ALIAS("platform:switch-interface");
