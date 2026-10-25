/*
 * Driver for the WHS3844 USB3.0 Redriver
 *
 * Copyright (C) 2024 Willsemi Ltd.
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

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/usb/typec.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/usb/typec.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <vendor/common/zte_misc.h>
#include <linux/delay.h>

int get_c_to_c(void);

/* General Registers */
#define REG_REVISION			0x08
#define REG_CONFIG_CTRL			0x0A
#define REG_CHANNEL_SWAP_SEL	0x0B
#define REG_VOD_DCGAIN			0x0C
#define REG_DP_AUX				0x13

#define REG_UFP2_EQ				0X10
#define REG_UFP1_EQ				0X11
#define REG_DFP2_EQ				0X20
#define REG_DFP1_EQ				0X21

/* register  value */
#define USB3_ON_EQ				0x11 /* 0x0a=0x11*/
#define VOD_CFG1				0x5C /* 0x0c=0x5C*/
#define UF2_EQ_WHS3488				0x44 /* 0x10=0x44*/
#define UF1_EQ_WHS3488				0x55 /* 0x11=0x55*/

#define UF2_EQ_TI1044				0x77 /* 0x10=0x44*/
#define UF1_EQ_TI1044				0x77 /* 0x11=0x55*/

#define DF2_EQ				0x55 /* 0x20=0x55*/
#define DF1_EQ				0x55 /* 0x21=0x55*/


/* register CONFIG_CTRL value */
#define DISABLE_TX_RX	0x00
#define USB3_ON_CC1		0x1B
#define	USB3_ON_CC2		0x1F
#define DP_ON_CC1		0x1B
#define DP_ON_CC2		0x1F

/* register DP_AUX value */
#define ENABLE_AUX_SNOOP	0x00
#define DISABLE_AUX_SNOOP	0x80

enum cc_state {
	CC_STATE_OPEN = 0,
	CC_STATE_USB3,
	CC_STATE_DP,
};

/* must align with smblib's integer representation of cc orientation. */
enum cc_orientation {
	CC_ORIENTATION_NONE = 0,
	CC_ORIENTATION_CC1 = 1,
	CC_ORIENTATION_CC2 = 2,
};

static const char * const cc_state_name[] = {
	[CC_STATE_USB3]		= "USB3",
	[CC_STATE_DP]		= "Display Port",
	[CC_STATE_OPEN]		= "Open",
};

static const char * const cc_orientation_name[] = {
	[CC_ORIENTATION_NONE]	= "None",
	[CC_ORIENTATION_CC1]	= "CC1",
	[CC_ORIENTATION_CC2]	= "CC2",
};

enum vendor_id_data {
	VENDORID_NONE = 0,
	VENDORID_WH3844 = 1,
	VENDORID_TI1044 = 2,
};

struct whs3844_chip {
	struct device		*dev;
	struct i2c_client	*i2c_client;
	struct gpio_desc *dp_status;
	struct gpio_desc *rx_detect;
	struct workqueue_struct *wq;
	struct delayed_work notify_work;
	struct extcon_dev *edev;
	struct extcon_dev *host_edev;
	struct notifier_block usb3_nb;
	struct notifier_block	psy_nb;
	struct notifier_block dp_nb;
	struct notifier_block hdmi_nb;
	enum cc_state		cc_state;
	enum cc_orientation	cc_orientation;
	int cc_status_last;
	int usb3_status_last;
	int vendorid;
};

static struct whs3844_chip *chip = NULL;

extern int zte_misc_set_node_val(const char *node_name, int value);
extern int zte_misc_get_node_val(const char *node_name);

static int whs3844_soft_reset(struct whs3844_chip *chip)
{
	int ret = 0;

	pr_err("WHS3844 %s:i2c_smbus_write_byte_data reg[0x1B]=0xC0, SOFT RESET!!!\n", __func__);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, 0x1B, 0xC0);
	if (ret < 0) {
		dev_err(chip->dev, "WHS3844 %s - cannot soft reset, ret=%d\n", __func__, ret);
		return ret;
	}

	dev_info(chip->dev, "WHS3844 %s\n", __func__);

	return ret;
}

static int whs3844_aux_snoop(struct whs3844_chip *chip, u8 val)
{
	int ret = 0;

	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_DP_AUX, val);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_DP_AUX, val);
	if (ret < 0) {
		dev_err(chip->dev,
			"WHS3844 %s - cannot 0x%02X aux snoop, ret=%d\n",
			__func__, val, ret);
		return ret;
	}

	return ret;
}

static int whs3844_set_config_ctrl(struct whs3844_chip *chip, u8 config_ctrl_value)
{
	int ret = 0;

	/*0A reg*/
	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_CONFIG_CTRL, config_ctrl_value);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_CONFIG_CTRL,
					config_ctrl_value);
	if (ret < 0) {
		pr_err("WHS3844 %s:i2c_smbus_write_byte_data reg[0x0A] error\n", __func__);
		return ret;
	}

	return ret;
}

static inline void print_mem(const unsigned char *buffer, unsigned int len)
{
	unsigned int i = 0;
	char buf[256] = {0,};

	memset(buf, 0, sizeof(buf));
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "WHS3844-0x00: ");

	for (i = 0; i < len; i++) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "0x%02X ", buffer[i]);

		if ((i != 0) && ((i + 1) % 8 == 0)) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "    ");
		}

		if ((i != 0) && ((i + 1) % 16 == 0)) {
			dev_err(chip->dev, "%s\n", buf);
			memset(buf, 0, sizeof(buf));
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "WHS3844-0x%02X: ", (i + 1));
		}
	}

	dev_err(chip->dev, "%s\n", buf);

}

static int whs3844_dump_reg(struct whs3844_chip *chip)
{
	unsigned char reg[0x25] = {0,};
	int i = 0, ret = 0;

	for (i = 0; i <= 0x23; i++) {
		if (i == 0x1B)
			continue;

		ret = i2c_smbus_read_i2c_block_data(chip->i2c_client, i, 1, reg + i);
		if (ret < 0) {
			dev_err(chip->dev,
				"WHS3844 %s - cannot read reg[%d], ret=%d\n",
				__func__, i, ret);
			return ret;
		}
	}

	print_mem(reg, sizeof(reg));

	return ret;
}

static int whs3844_check_vendor_id(struct whs3844_chip *chip)
{
	unsigned char reg[3] = {0,};
	const unsigned char wh3844_data[3] = {0x00, 0x40, 0x09};
	const unsigned char ti1044_data[3] = {0x54, 0x55, 0x53};
	int i = 0, ret = 0;

	for (i = 0; i < 3; i++) {
		ret = i2c_smbus_read_i2c_block_data(chip->i2c_client, i, 1, reg + i);
		if (ret < 0) {
			dev_err(chip->dev,
				"WHS3844 %s - cannot read reg[%d], ret=%d\n",
				__func__, i, ret);
			return ret;
		}
	}

	dev_err(chip->dev, "WHS3844 %s - reg[0-3]: 0x%02X, 0x%02X, 0x%02X\n", __func__, reg[0], reg[1], reg[2]);

	if (!memcmp(reg, wh3844_data, sizeof(reg))) {
		chip->vendorid = VENDORID_WH3844;
		dev_err(chip->dev, "WHS3844 %s VENDORID_WH3844\n", __func__);
	} else if (!memcmp(reg, ti1044_data, sizeof(reg))) {
		chip->vendorid = VENDORID_TI1044;
		dev_err(chip->dev, "WHS3844 %s VENDORID_TI1044\n", __func__);
	} else {
		chip->vendorid = VENDORID_WH3844;
		dev_err(chip->dev, "WHS3844 %s VENDORID_NONE, setting VENDORID_WH3844\n", __func__);
	}

	return ret;
}

static int whs3844_usb_eq_config(struct whs3844_chip *chip)
{
	int ret = 0;
	int vendor_data = 0;

//#if 0
/*0C reg=usb=0x50*/
	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_VOD_DCGAIN, VOD_CFG1);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_VOD_DCGAIN, VOD_CFG1);
	if (ret < 0) {
		pr_err("WHS3844 %s:i2c_smbus_write_byte_data REG_VOD_DCGAIN=0x0C error\n", __func__);
		return ret;
	}

/*10 reg*/
	vendor_data = (chip->vendorid == VENDORID_TI1044) ? UF2_EQ_TI1044 : UF2_EQ_WHS3488;
	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_UFP2_EQ, vendor_data);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_UFP2_EQ, vendor_data);
	if (ret < 0) {
		pr_err("WHS3844 %s:i2c_smbus_write_byte_data REG_UFP2_EQ=0x10 error\n", __func__);
		return ret;
	}

/*11 reg=usb=0x33*/
	vendor_data = (chip->vendorid == VENDORID_TI1044) ? UF1_EQ_TI1044 : UF1_EQ_WHS3488;
	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_UFP1_EQ, vendor_data);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_UFP1_EQ, vendor_data);
	if (ret < 0) {
		pr_err("WHS3844 %s:i2c_smbus_write_byte_data REG_UFP1_EQ=0x11 error\n", __func__);
		return ret;
	}

/*20 reg=usb=0x55*/
	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_DFP2_EQ, DF2_EQ);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_DFP2_EQ, DF2_EQ);
	if (ret < 0) {
		pr_err("WHS3844 %s:i2c_smbus_write_byte_data REG_DFP2_EQ=0x20 error\n", __func__);
		return ret;
	}

/*21 reg=usb=0x55*/
	pr_err("WHS3844 %s: write reg[0x%02X]=0x%02X\n", __func__, REG_DFP1_EQ, DF1_EQ);
	ret = i2c_smbus_write_byte_data(chip->i2c_client, REG_DFP1_EQ, DF1_EQ);
	if (ret < 0) {
		pr_err("WHS3844 %s:i2c_smbus_write_byte_data REG_DFP1_EQ=0x21 error\n", __func__);
		return ret;
	}
//#endif

	return ret;
}

static int whs3844_usb3_update_state(enum cc_state cc_state,
				      enum cc_orientation cc_orientation)
{
	int ret = 0;
	u8 aux_snoop_value = 0x80;
	u8 config_ctrl_value = 0;

	if (chip->vendorid == VENDORID_NONE)
		whs3844_check_vendor_id(chip);

	pr_err("WHS3844 %s: start cc_state=%d, cc_orientation=%d, cc_status_last=%d\n",
			__func__, cc_state, cc_orientation, chip->cc_status_last);

	if ((cc_state != CC_STATE_OPEN) && (chip->cc_status_last == false)) {
		if (cc_orientation == CC_ORIENTATION_CC1){
			config_ctrl_value = USB3_ON_CC1;
		} else if (cc_orientation == CC_ORIENTATION_CC2) {
			config_ctrl_value = USB3_ON_CC2;
		}

		/*eq reg*/
		whs3844_usb_eq_config(chip);

		/*0x13 reg*/
		whs3844_aux_snoop(chip, aux_snoop_value);

		/*0x0A reg*/
		whs3844_set_config_ctrl(chip, config_ctrl_value);

		whs3844_dump_reg(chip);

		chip->cc_status_last = true;
	} else if ((cc_state == CC_STATE_OPEN) && (chip->cc_status_last == true)){
		whs3844_soft_reset(chip);

		chip->cc_status_last = false;
	}

	return ret;
}

struct psy_changed_event {
	struct work_struct work;
	struct whs3844_chip *chip;
};

int whs3844_enable_hdmi_path(int enable)
{
	int ret = 0;

	if (enable) {
		/*0:Disable, 1: enable*/
		dev_info(chip->dev, "%s: set hdmi_power_en to 1\n", __func__);
		ret = zte_misc_set_node_val("hdmi_power_en", 1);
		if (ret) {
			dev_info(chip->dev, "%s: Failed to set dp_hdmi_switch mode.\n", __func__);
		}

		/*0:Disable, 1 For USB, 2 For HDMI*/
		dev_info(chip->dev, "%s: set dp_hdmi_switch to HDMI mode\n", __func__);
		ret = zte_misc_set_node_val("dp_hdmi_switch", 2);
		if (ret) {
			dev_info(chip->dev, "%s: Failed to set dp_hdmi_switch mode.\n", __func__);
		}

		/*wait hw switch finish*/
		msleep(300);
	} else {
		/*0:Disable, 1: enable*/
		dev_info(chip->dev, "%s: set hdmi_power_en to 0\n", __func__);
		ret = zte_misc_set_node_val("hdmi_power_en", 0);
		if (ret) {
			dev_info(chip->dev, "%s: Failed to set dp_hdmi_switch mode.\n", __func__);
		}
	}

	return ret;
}
EXPORT_SYMBOL(whs3844_enable_hdmi_path);

int whs3844_enable_dp_path(int enable)
{
	int ret = 0;
	int cc_status_now = get_c_to_c();

	/*0 For Disable, 1 For No Flip DP, 2 For Flip DP*/
	dev_info(chip->dev, "%s: set mux64_switch to %d \n", __func__, cc_status_now);
	ret = zte_misc_set_node_val("mux64_switch", cc_status_now);
	if (ret) {
		dev_info(chip->dev, "%s: Failed to mux64_switch mode: %d\n", __func__, cc_status_now);
	}

	dev_info(chip->dev, "%s: whs3844_usb3_update_state %d\n", __func__, cc_status_now);
	ret = whs3844_usb3_update_state(CC_STATE_DP, cc_status_now);
	if (ret < 0) {
		dev_info(chip->dev, "WHS3844 %s:whs3844_usb3_update_state error\n", __func__);
	}

	/*0:Disable, 1 For USB, 2 For HDMI*/
	dev_info(chip->dev, "%s: set dp_hdmi_switch to usb mode\n", __func__);
	ret = zte_misc_set_node_val("dp_hdmi_switch", 1);
	if (ret) {
		dev_info(chip->dev, "%s: Failed to set dp_hdmi_switch mode USB: %d\n", __func__);
	}

	dev_info(chip->dev, "%s: set dp_status gpio to 1\n", __func__);
	gpiod_set_value(chip->dp_status, 1);

	/*wait hw switch finish*/
	msleep(100);

	return ret;
}
EXPORT_SYMBOL(whs3844_enable_dp_path);

__maybe_unused static int whs3844_extcon_usb3_notifier_call(struct notifier_block *nb,
		unsigned long event, void *data)
{
	int ret = 0;
	int usb3_status_now = 0;
	int cc_status_now = get_c_to_c();

	if (IS_ERR_OR_NULL(chip->host_edev)) {
		dev_err(chip->dev, "%s chip->host_edev is NULL\n", __func__);
		return NOTIFY_OK;
	}

	usb3_status_now = extcon_get_state(chip->host_edev, EXTCON_USB_HOST);

	dev_info(chip->dev, "%s: ##########  usb3: %d, LAST: %d, CC: %d\n",
		__func__, usb3_status_now, chip->usb3_status_last, cc_status_now);

	if (!chip->usb3_status_last && usb3_status_now) {
		dev_info(chip->dev, "%s: ########## usb3 PLUG IN\n", __func__);

		/*0 For Disable, 1 For No Flip DP, 2 For Flip DP*/
		dev_info(chip->dev, "%s: set mux64_switch to %d \n", __func__, cc_status_now);
		ret = zte_misc_set_node_val("mux64_switch", cc_status_now);
		if (ret) {
			dev_info(chip->dev, "%s: Failed to mux64_switch mode: %d\n", __func__, cc_status_now);
		}
		msleep(20);

		dev_info(chip->dev, "%s: set rx_detect gpio to 1\n", __func__);
		gpiod_set_value(chip->rx_detect, 1);

		dev_info(chip->dev, "%s: will call whs3844_usb3_update_state %d\n", __func__, cc_status_now);
		ret = whs3844_usb3_update_state(CC_STATE_USB3, cc_status_now);
		if (ret < 0) {
			dev_info(chip->dev, "WHS3844 %s:whs3844_usb3_update_state error\n", __func__);
		}

		chip->usb3_status_last = usb3_status_now;
	} else if ((chip->usb3_status_last && !usb3_status_now) || !cc_status_now) {
		dev_info(chip->dev, "%s: ########## usb3 PLUG OUT CC_STATE_USB3=%d  cc_status_now=%d\n", __func__,CC_STATE_USB3, cc_status_now);
		dev_info(chip->dev, "%s: set rx_detect gpio to 0\n", __func__);
		gpiod_set_value(chip->rx_detect, 0);
		ret = whs3844_usb3_update_state(CC_STATE_OPEN, CC_ORIENTATION_NONE);
		if (ret < 0) {
			dev_info(chip->dev, "WHS3844 %s:whs3844_usb3_update_state plug out error\n", __func__);
		}

		/*0 For Disable, 1 For No Flip DP, 2 For Flip DP*/
		dev_info(chip->dev, "%s: set mux64_switch to %d \n", __func__, cc_status_now);
		ret = zte_misc_set_node_val("mux64_switch", 0);
		if (ret) {
			dev_info(chip->dev, "%s: Failed to mux64_switch mode: %d\n", __func__, cc_status_now);
		}

		chip->usb3_status_last = 0;
	}

	return NOTIFY_OK;
}

static int whs3844_register_usb3_notifier(void)
{
	int ret = 0;

	chip->usb3_nb.notifier_call = whs3844_extcon_usb3_notifier_call;
	ret = devm_extcon_register_notifier(chip->dev, chip->host_edev, EXTCON_USB_HOST, &chip->usb3_nb);
	if (ret) {
		dev_err(chip->dev, "Failed to register extcon notifier for EXTCON_USB_HOST: %d\n", ret);
		return ret;
	}

    dev_info(chip->dev, "Success to register extcon notifier for EXTCON_USB_HOST\n");

	return 0;
}

static void whs3844_extcon_notify_work(struct work_struct *work)
{
	work = work ? work : 0;

    if (IS_ERR_OR_NULL(chip)) {
        pr_err("%s, failed to find pd extcon dev, retrying\n", __func__);
        return;
    }

	dev_err(chip->dev, "WHS3844 %s - check usb dp hdmi is prsent?\n", __func__);

	whs3844_extcon_usb3_notifier_call(NULL, 0, NULL);
}

static int whs3844_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	int ret = 0;

	pr_err("WHS3844 %s:start\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE)) {
		dev_err(&client->dev, "WHS3844 %s - i2c_check_functionality error\n", __func__);
		return -EIO;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "WHS3844 %s - devm_kzalloc error\n", __func__);
		return -ENOMEM;
	}

	dev_err(&client->dev,"WHS3844 %s - devm_kzalloc success\n", __func__);

	chip->dev = &client->dev;
	chip->i2c_client = client;

	chip->edev = extcon_get_edev_by_phandle(chip->dev, 0);
	if (IS_ERR_OR_NULL(chip->edev)) {
		dev_err(chip->dev, "failed to find pd extcon dev, retrying\n");
		devm_kfree(chip->dev, chip);
		chip = NULL;
		return -EPROBE_DEFER;
	}

	chip->host_edev = extcon_get_edev_by_phandle(chip->dev, 1);
	if (IS_ERR_OR_NULL(chip->host_edev)) {
		dev_err(chip->dev, "failed to find host_edev, retrying\n");
		devm_kfree(chip->dev, chip);
		chip = NULL;
		return -EPROBE_DEFER;
	}

	i2c_set_clientdata(client, chip);

	chip->dp_status = devm_gpiod_get(chip->dev, "dp_status", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(chip->dp_status)) {
		dev_err(chip->dev, "Failed to find dp_status pin, no defined\n");
	}

	dev_err(&client->dev, "Find dp_status pin success\n");

	chip->rx_detect = devm_gpiod_get(chip->dev, "rx_detect", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(chip->rx_detect)) {
		dev_err(chip->dev, "Failed to find rx_detect pin, no defined\n");
	}

	dev_err(&client->dev, "Find rx_detect pin success\n");

	ret = whs3844_usb3_update_state(CC_STATE_OPEN, CC_ORIENTATION_NONE);
	if (ret < 0) {
		dev_err(&client->dev, "WHS3844 %s - Error: REG_CONFIG_CTRL := DISABLE_TX_RX failed, ret=%d\n", __func__, ret);

	}

	chip->cc_orientation = CC_ORIENTATION_CC1;//CC_ORIENTATION_NONE;
	chip->cc_state = CC_STATE_USB3;//CC_STATE_OPEN;

	ret = whs3844_register_usb3_notifier();
	if (ret) {
		dev_err(chip->dev, "WHS3844 %s - Error: cannot register whs3844_register_usb3_notifier\n", __func__);
	}

	INIT_DELAYED_WORK(&chip->notify_work, whs3844_extcon_notify_work);

	schedule_delayed_work(&chip->notify_work, msecs_to_jiffies(500));

	return ret;
}

static int whs3844_i2c_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "WHS3844 %s - remove WHS3844 i2c driver\n",
		 __func__);
	return 0;
}

static const struct of_device_id whs3844_i2c_match_table[] = {
	{.compatible = "willsemi,whs3844_i2c" },
	{},
};

static const struct i2c_device_id whs3844_i2c_id[] = {
	{ "whs3844_i2c", 0 },
	{ }
};

static struct i2c_driver whs3844_i2c_driver = {
	.driver = {
		.name		= "whs3844_i2c",
		.owner		= THIS_MODULE,
		.of_match_table = whs3844_i2c_match_table,
	},
	.probe		= whs3844_i2c_probe,
	.remove		= whs3844_i2c_remove,
	.id_table	= whs3844_i2c_id,
};
module_i2c_driver(whs3844_i2c_driver);

MODULE_DESCRIPTION("usb30 phy whs3844 redrive driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: zte_switch_interface");