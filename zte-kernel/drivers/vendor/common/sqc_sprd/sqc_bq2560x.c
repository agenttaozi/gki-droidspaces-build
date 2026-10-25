/*
 * Driver for the TI bq2560x charger.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/usb/phy.h>
#include <uapi/linux/usb/charger.h>
#include <linux/timer.h>
#include "sqc_bq2560x.h"

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include <vendor/comdef/zlog_common_base.h>
#endif

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>
int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

enum {
	BQ25601_MASTER,
	BQ25601_SLAVE,
};

#define SGM41513_I2C_ADDR			0x1a
#define BQ2560X_BATTERY_NAME			"sc27xx-fgu"
#define BIT_DP_DM_BC_ENB			BIT(0)
#define BQ2560X_OTG_ALARM_TIMER_S		15

#define	BQ2560X_REG_IINLIM_BASE			100

#define BQ2560X_REG_ICHG_LSB			60

#define BQ2560X_REG_ICHG_MASK			GENMASK(5, 0)

#define BQ2560X_REG_CHG_MASK			GENMASK(4, 4)


#define BQ2560X_REG_RESET_MASK			GENMASK(6, 6)

#define BQ2560X_REG_OTG_MASK			GENMASK(5, 5)

#define BQ2560X_REG_WATCHDOG_MASK		GENMASK(6, 6)

#define BQ2560X_REG_TERMINAL_VOLTAGE_MASK	GENMASK(7, 3)
#define BQ2560X_REG_TERMINAL_VOLTAGE_SHIFT	3

#define BQ2560X_REG_TERMINAL_CUR_MASK		GENMASK(3, 0)
#define BQ2560X_REG_VINDPM_VOLTAGE_MASK		GENMASK(3, 0)
#define BQ2560X_REG_OVP_MASK			GENMASK(7, 6)
#define BQ2560X_REG_OVP_SHIFT			6
#define BQ2560X_REG_EN_HIZ_MASK			GENMASK(7, 7)
#define BQ2560X_REG_EN_HIZ_SHIFT		7
#define BQ2560X_REG_LIMIT_CURRENT_MASK		GENMASK(4, 0)
#define BQ2560X_DISABLE_PIN_MASK_2730		BIT(0)
#define BQ2560X_DISABLE_PIN_MASK_2721		BIT(15)
#define BQ2560X_DISABLE_PIN_MASK_2720		BIT(0)
#define BQ2560X_DISABLE_PIN_MASK		BIT(0)

#define BQ2560X_OTG_VALID_MS			200
#define BQ2560X_FEED_WATCHDOG_VALID_MS		50
#define BQ2560X_OTG_RETRY_TIMES			10
#define BQ2560X_LIMIT_CURRENT_MAX		3200000

#define BQ2560X_ROLE_MASTER_DEFAULT		1
#define BQ2560X_ROLE_SLAVE			2

#define BQ2560X_FCHG_OVP_6V			6000
#define BQ2560X_FCHG_OVP_9V			9000
#define BQ2560X_FAST_CHARGER_VOLTAGE_MAX	10500000
#define BQ2560X_NORMAL_CHARGER_VOLTAGE_MAX	6500000

#define VCHG_CTRL_THRESHOLD_MV_072 4370
#define DEFAULT_VINDPM_MV			4500

#define BQ2560X_REG_EN_BATFET_MASK			GENMASK(2, 2)
#define BQ2560X_REG_EN_BATFET_SHIFT		2

struct bq2560x_charger_info {
	struct i2c_client *client;
	struct device *dev;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	struct power_supply *psy_usb;
	struct zte_power_supply *zte_psy_usb;
	struct power_supply_charge_current cur;
	struct mutex lock;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	struct zlog_client *zlog_client;
#endif
	bool charging;
	bool is_charger_online;
	struct regulator_dev *reg;
	struct delayed_work otg_work;
	struct delayed_work wdt_work;
	struct delayed_work vindpm_work;
	struct delayed_work force_recharge_work;
	struct delayed_work update_work;
	struct regmap *pmic;
	struct alarm otg_timer;
	struct alarm icl_vote_timer;
	u32 charger_detect;
	u32 charger_pd;
	u32 charger_pd_mask;
	int voltage_max_microvolt;
	bool otg_enable;
	bool is_charging_enabled;
	struct gpio_desc *gpiod;
	struct extcon_dev *edev;
	int vindpm_value;
	int boost_limit;
	int chip_main_id;
	int chip_sub_id;
	bool suspended;
	bool use_typec_extcon;
	bool batfet_rst_en;
	bool host_status_check;
	bool support_single_buck_9V;
	bool disable_ship_mode;
	bool enable_driver_icl_vote;
	int init_finished;
	int hw_mode;
	int chg_status;
	char *name;
};

static enum power_supply_usb_type bq2560x_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};

static enum power_supply_property bq2560x_usb_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PRESENT,
};

static enum zte_power_supply_property zte_bq2560x_usb_props[] = {
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_TUNING_VINDPM,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER,
};

static enum zte_power_supply_property zte_bq2560x_slave_usb_props[] = {
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
};

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
static struct zlog_mod_info zlog_bc_dev1 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "buck_charger1",
	.device_name = "buckcharger",
	.ic_name = "bq2560x",
	.module_name = "TI",
	.fops = NULL,
};

static struct zlog_mod_info zlog_bc_dev2 = {
	.module_no = ZLOG_MODULE_CHG,
	.name = "buck_charger2",
	.device_name = "buckcharger",
	.ic_name = "bq2560x",
	.module_name = "TI",
	.fops = NULL,
};
#endif
static struct bq2560x_charger_info *bq2560x_master_data = NULL;
static struct bq2560x_charger_info *bq2560x_slave_data = NULL;

static int slave_chg_id = 0;
static int slave_chg_retry_counter = 0;

static int bq2560x_charger_tuning_vindpm_insert(struct bq2560x_charger_info *info);
static void bq2560x_charger_enable_hiz(struct bq2560x_charger_info *info, bool enable);
static int bq2560x_charger_dumper_reg(struct bq2560x_charger_info *info);

static int
bq2560x_charger_set_limit_current(struct bq2560x_charger_info *info,
				  u32 limit_cur);

static int power_supply_get_prop_by_name(const char *name, enum power_supply_property psp, int *data)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_err("psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = power_supply_get_by_name(name);
	if (!psy) {
		pr_err("get %s psy failed!!\n", name);
		goto failed_loop;
	}

	rc = power_supply_get_property(psy,
				psp, &val);
	power_supply_put(psy);

	if (rc < 0) {
		pr_err("Failed to get %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	*data = val.intval;

	return 0;

failed_loop:
	return -EINVAL;
}

#define bq_err(fmt, ...)								\
do {											\
	if (info->hw_mode == BQ25601_MASTER) {					\
		pr_err("[bq2560x-MASTER]:" fmt, ##__VA_ARGS__);	\
	} else { \
		pr_err("[bq2560x-SLAVE]:" fmt, ##__VA_ARGS__);\
	} \
} while (0);

#define bq_info(fmt, ...)								\
do {											\
	if (info->hw_mode == BQ25601_MASTER) {					\
		pr_info("[bq2560x-MASTER]:" fmt,  ##__VA_ARGS__);	\
	} else {		\
		pr_info("[bq2560x-SLAVE]:" fmt,  ##__VA_ARGS__); \
	} \
} while (0);


#define bq_dbg(fmt, ...)								\
do {											\
	if (info->hw_mode == BQ25601_MASTER)	{					\
		pr_debug("[bq2560x-MASTER]:" fmt, ##__VA_ARGS__);	\
	} else {			\
		pr_debug("[bq2560x-SLAVE]:" fmt, ##__VA_ARGS__); \
	} \
} while (0);


static const struct i2c_device_id bq2560x_i2c_id[] = {
	{"ti,bq2560x_chg", BQ25601_MASTER},
	{"ti,bq2560x_chg2", BQ25601_SLAVE},
	{ }
};

static const struct of_device_id bq2560x_charger_of_match[] = {
	{
		.compatible = "ti,bq2560x_chg",
		.data = (void *)BQ25601_MASTER,
	},
	{
		.compatible = "ti,bq2560x_chg2",
		.data = (void *)BQ25601_SLAVE,
	},
	{},
};

MODULE_DEVICE_TABLE(of, bq2560x_charger_of_match);


#if IS_ENABLED(CONFIG_BQ2560X_BQ25890_SUPPORT)
static int bq2560x_read_no_retry(struct bq2560x_charger_info *info, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(info->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}
#endif

static int bq2560x_read_no_record(struct bq2560x_charger_info *info, u8 reg, u8 *data)
{
	int ret = 0, retry_cnt = 3;

	if (info->suspended) {
		bq_err("bq2560x not iic read after system suspend!\n");
		return 0;
	}

	do {
		ret = i2c_smbus_read_byte_data(info->client, reg);
		if (ret < 0) {
			bq_err("bq2560x_read failed, ret=%d, retry_cnt=%d\n", ret, retry_cnt);
			usleep_range(5000, 5500);
		}
	} while ((ret < 0) && (retry_cnt-- > 0));

	if (ret < 0) {
		*data = 0;
		return ret;
	} else {
		*data = ret;
	}

	return 0;
}

static int bq2560x_read(struct bq2560x_charger_info *info, u8 reg, u8 *data)
{
	int ret = 0, retry_cnt = 3;

	if (info->suspended) {
		bq_err("bq2560x not iic read after system suspend!\n");
		return 0;
	}

	do {
		ret = i2c_smbus_read_byte_data(info->client, reg);
		if (ret < 0) {
			bq_err("bq2560x_read failed, ret=%d, retry_cnt=%d\n", ret, retry_cnt);
			usleep_range(5000, 5500);
		}
	} while ((ret < 0) && (retry_cnt-- > 0));

	if (ret < 0) {
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		zlog_client_record(info->zlog_client, "chg_name:%s, hwmode:%d i2c read err:%d\n", info->name, info->hw_mode, ret);
		zlog_client_notify(info->zlog_client, ZLOG_CHG_I2C_R_ERROR_NO);
#endif
		*data = 0;
		return ret;
	} else {
		*data = ret;
	}

	return 0;
}

static int bq2560x_write(struct bq2560x_charger_info *info, u8 reg, u8 data)
{
	int ret = 0, retry_cnt = 3;

	if (info->suspended) {
		bq_err("bq2560x not iic write after system suspend!\n");
		return 0;
	}

	do {
		ret = i2c_smbus_write_byte_data(info->client, reg, data);
		if (ret < 0) {
			bq_err("bq2560x_write failed, ret=%d, retry_cnt=%d\n", ret, retry_cnt);
			usleep_range(5000, 5500);
		}
	} while ((ret < 0) && (retry_cnt-- > 0));

	if (ret < 0) {
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		zlog_client_record(info->zlog_client, "chg_name:%s, hwmode:%d i2c write err:%d\n", info->name, info->hw_mode, ret);
		zlog_client_notify(info->zlog_client, ZLOG_CHG_I2C_W_ERROR_NO);
#endif
		return ret;
	}

	return 0;
}

static int bq2560x_update_bits(struct bq2560x_charger_info *info, u8 reg,
			       u8 mask, u8 data)
{
	u8 v;
	int ret;

	ret = bq2560x_read(info, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= (data & mask);

	return bq2560x_write(info, reg, v);
}

static int
bq2560x_charger_set_termina_enable(struct bq2560x_charger_info *info, bool enable)
{
	return bq2560x_update_bits(info, BQ2560X_REG_05,
				   BQ2560X_REG05_EN_TERM_MASK,
				   enable << BQ2560X_REG05_EN_TERM_SHIFT);
}

static int
bq2560x_charger_set_vindpm(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val = 0;

	if (vol < BQ2560X_REG06_VINDPM_BASE)
		reg_val = 0x0;
	else if (vol > BQ2560X_REG06_VINDPM_MAX)
		reg_val = 0x0f;
	else
		reg_val = (vol - BQ2560X_REG06_VINDPM_BASE) / BQ2560X_REG06_VINDPM_LSB;

	bq_err("set reg_val=0x%02x\n", reg_val);

	return bq2560x_update_bits(info, BQ2560X_REG_06,
				   BQ2560X_REG06_VINDPM_MASK, reg_val << BQ2560X_REG06_VINDPM_SHIFT);
}

static int bq2560x_charger_set_ovp(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < 5500)
		reg_val = 0x0;
	else if (vol > 5500 && vol <= 6500)
		reg_val = 0x01;
	else if (vol > 6500 && vol <= 10500)
		reg_val = 0x02;
	else
		reg_val = 0x03;
	
	if (info->chip_main_id == SGM41516S_VENDOR_ID)
		return bq2560x_update_bits(info, SGM41516S_REG_10,
				   BQ2560X_REG_OVP_MASK,
				   reg_val << BQ2560X_REG_OVP_SHIFT);

	return bq2560x_update_bits(info, BQ2560X_REG_06,
				   BQ2560X_REG_OVP_MASK,
				   reg_val << BQ2560X_REG_OVP_SHIFT);
}

static int bq2560x_charger_get_ovp(struct bq2560x_charger_info *info, u32 *vol)
{
	u8 reg_val = 0;
	int ret = 0;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		ret = bq2560x_read(info, SGM41516S_REG_10, &reg_val);
		if (ret < 0) {
			bq_err("Failed to get ovp val, %d\n", ret);
			return ret;
		}
	} else {
		ret = bq2560x_read(info, BQ2560X_REG_06, &reg_val);
		if (ret < 0) {
			bq_err("Failed to get ovp val, %d\n", ret);
			return ret;
		}
	}

	reg_val = (reg_val & BQ2560X_REG_OVP_MASK) >> BQ2560X_REG_OVP_SHIFT;

	if (reg_val == 0x00) {
		*vol = 5500;
	} else if (reg_val == 0x01) {
		*vol = 6500;
	} else if (reg_val == 0x02) {
		*vol = 10500;
	} else if (reg_val == 0x03) {
		*vol = 14000;
	} else {
		*vol = 0;
	}

	return 0;
}

static int
bq2560x_charger_set_termina_vol(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val = 0;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (vol < SGM41516S_VREG_BASE)
			vol = SGM41516S_VREG_BASE;
		else if (vol >= SGM41516S_VREG_MAX)
			vol = SGM41516S_VREG_MAX;

		reg_val = (vol - SGM41516S_VREG_BASE) / SGM41516S_VREG_LSB;

		return bq2560x_update_bits(info, BQ2560X_REG_04,
						SGM41516S_VREG_MASK,
						reg_val << SGM41516S_VREG_SHIFT);
	} else {
		if (vol < BQ2560X_REG04_VREG_BASE)
			reg_val = 0x0;
		else if (vol >= BQ2560X_REG04_VREG_MAX)
			reg_val = 0x18;
		else
			reg_val = (vol - BQ2560X_REG04_VREG_BASE) / BQ2560X_REG04_VREG_LSB;

		return bq2560x_update_bits(info, BQ2560X_REG_04,
				   BQ2560X_REG04_VREG_MASK,
				   reg_val << BQ2560X_REG04_VREG_SHIFT);
	}
}

static int
bq2560x_charger_get_termina_vol(struct bq2560x_charger_info *info, u32 *vol)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_04, &reg_val);
	if (ret < 0)
		return ret;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		reg_val &= SGM41516S_VREG_MASK;
		reg_val = reg_val >> SGM41516S_VREG_SHIFT;

		*vol = (reg_val * SGM41516S_VREG_LSB + SGM41516S_VREG_BASE) * 1000;
	} else {
		reg_val &= BQ2560X_REG04_VREG_MASK;
		reg_val = reg_val >> BQ2560X_REG04_VREG_SHIFT;

		if (reg_val == BQ2560X_REG04_VREG_CODE)
			*vol = BQ2560X_REG04_VREG_EXCE_VOLTAGE * 1000;
		else
			*vol = (reg_val * BQ2560X_REG04_VREG_LSB +
						BQ2560X_REG04_VREG_BASE) * 1000;
	}

	return 0;
}

static int
sgm41513_charger_set_otgf_itremr_enable(struct bq2560x_charger_info* info, bool enable)
{

	return bq2560x_update_bits(info, SGM41513_REG_0D,
				SGM41513_REG_0D_OTGF_ITREMR_MASK,
				enable << SGM41513_REG_0D_OTGF_ITREMR_SHIFT);
}

static int
sgm41513_charger_get_otgf_itremr_enable(struct bq2560x_charger_info* info)
{
	int ret = 0;
	u8 reg_val;

	ret = bq2560x_read(info, SGM41513_REG_0D, &reg_val);
	if (ret < 0)
		return ret;

	reg_val = (reg_val & SGM41513_REG_0D_OTGF_ITREMR_MASK) >> SGM41513_REG_0D_OTGF_ITREMR_SHIFT;

	return !reg_val;
}


static int
bq2560x_charger_set_termina_cur(struct bq2560x_charger_info *info, u32 cur)
{
	u8 reg_val = 0, offset = 0;

	if (info->client->addr == SGM41513_I2C_ADDR) {
		if (cur > 240) {
			sgm41513_charger_set_otgf_itremr_enable(info, SGM41513_REG_0D_OTGF1500KHz_ITREMR_ENABLE);
			if (cur <= 360) {
				offset = 5;
				reg_val = (cur - 240) / 60;
			} else if (cur < 480) {
				reg_val = 0x07;
			} else if (cur <= 1200) {
				offset = 0x8;
				reg_val = (cur - 480) / 120;
			} else {
				reg_val = 0xF;
			}
		} else {
			sgm41513_charger_set_otgf_itremr_enable(info, SGM41513_REG_0D_OTGF500KHz_ITREMR_DISABLE);
			if (cur < 5) {
				reg_val = 0;
			} else if (cur <= 20) {
				offset = 0x0;
				reg_val = (cur - 5) / 5;
			} else if (cur <= 60) {
				offset = 0x3;
				reg_val = (cur - 20) / 10;
			} else if (cur < 80) {
				reg_val = 0x7;
			} else if (cur <= 240) {
				offset = 0x8;
				reg_val = (cur - 80) / 20;
			} else {
				reg_val = 0xF;
			}
		}
		reg_val += offset;
		if (reg_val > 0xF)
			reg_val = 0xF;
	} else {
		if (cur <= BQ2560X_REG03_ITERM_BASE)
			reg_val = 0x0;
		else if (cur >= 960)
			reg_val = 0xF;
		else
			reg_val = (cur - BQ2560X_REG03_ITERM_BASE) / BQ2560X_REG03_ITERM_LSB;
	}
	return bq2560x_update_bits(info, BQ2560X_REG_03,
				   BQ2560X_REG03_ITERM_MASK,
				   reg_val << BQ2560X_REG03_ITERM_SHIFT);
}

static int
bq2560x_charger_get_termia_cur(struct bq2560x_charger_info *info, u32 *cur)
{
	u8 reg_val = 0, sign1 = 0, sign2 = 0, val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_03, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG03_ITERM_MASK;
	sign1 = reg_val & BIT(3);
	sign2 = reg_val & BIT(2);
	val = reg_val & GENMASK(2, 0);
	reg_val = reg_val >> BQ2560X_REG03_ITERM_SHIFT;

	if (info->client->addr == SGM41513_I2C_ADDR) {
		if (sgm41513_charger_get_otgf_itremr_enable(info)) {
			if (sign1) {
				if(val == 0x7)
					*cur = 240 * 6 * 1000;
				else
					*cur = (val * 20 * 6 + 80 *6) * 1000;
			} else if(sign2) {
				val = val - 0x3;
				*cur = (val * 10 * 6 + 20 * 6) * 1000;
			} else
				*cur = (val * 5 * 6 + 5 * 6) * 1000;
		} else {
			if (sign1) {
				if (val == 0x7)
					*cur = 240 * 1000;
				else
					*cur = (val * 20 + 80) * 1000;
			} else if (sign2) {
				val = val - 0x3;
				*cur = (val * 10 + 20) * 1000;
			} else {
				*cur = (val * 5 + 5) * 1000;
			}
		}
	}
	else
		*cur = (reg_val * BQ2560X_REG03_ITERM_LSB +
						BQ2560X_REG03_ITERM_BASE) * 1000;
	return 0;
}

static int
bq2560x_charger_set_recharge_voltage(struct bq2560x_charger_info *info,
						u32 recharge_voltage_uv)
{
	u8 reg_val = 0;
	int ret = 0;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (recharge_voltage_uv <= 100000)
			reg_val = SGM41516S_VRECHG_100mV;
		else if (recharge_voltage_uv <= 200000)
			reg_val = SGM41516S_VRECHG_200mV;
		else if (recharge_voltage_uv <= 300000)
			reg_val = SGM41516S_VRECHG_300mV;
		else
			reg_val = SGM41516S_VRECHG_600mV;
		ret = bq2560x_update_bits(info, SGM415156S_REG_11,
				   SGM41516S_VRECHG_MASK,
				   reg_val << SGM41516S_VRECHG_SHIFT);
	} else {
		reg_val = (recharge_voltage_uv > 100000) ? BQ2560X_REG04_VRECHG_200MV : BQ2560X_REG04_VRECHG_100MV;
		ret = bq2560x_update_bits(info, BQ2560X_REG_04,
				   BQ2560X_REG04_VRECHG_MASK,
				   reg_val << BQ2560X_REG04_VRECHG_SHIFT);
		if (ret) {
			bq_err("set bq25601 recharge_voltage failed\n");
			return ret;
		}
	}

	return 0;
}

static int
bq2560x_charger_get_recharge_voltage(struct bq2560x_charger_info *info,
						u32 *recharge_voltage_uv)
{
	u8 reg_val = 0;
	int ret = 0;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		ret = bq2560x_read(info, SGM415156S_REG_11, &reg_val);
		if (ret < 0)
			return ret;
		reg_val &= SGM41516S_VRECHG_MASK;
		reg_val = reg_val >> SGM41516S_VRECHG_SHIFT;

		if (reg_val == SGM41516S_VRECHG_100mV)
			*recharge_voltage_uv = 100000;
		else if (reg_val == SGM41516S_VRECHG_200mV)
			*recharge_voltage_uv = 200000;
		else if (reg_val == SGM41516S_VRECHG_300mV)
			*recharge_voltage_uv = 300000;
		else
			*recharge_voltage_uv = 600000;
	} else {
		ret = bq2560x_read(info, BQ2560X_REG_04, &reg_val);
		if (ret < 0)
			return ret;

		reg_val &= BQ2560X_REG04_VRECHG_MASK;
		reg_val = reg_val >> BQ2560X_REG04_VRECHG_SHIFT;

		*recharge_voltage_uv = reg_val ? 200000 : 100000;
	}
	return 0;
}

static int bq2560x_charger_set_safety_timer(struct bq2560x_charger_info *info,
					u8 enable)
{
	return bq2560x_update_bits(info, BQ2560X_REG_05,
					BQ2560X_REG05_EN_TIMER_MASK,
					enable << BQ2560X_REG05_WDT_SHIFT);
}

static int bq2560x_charger_set_watchdog_timer(struct bq2560x_charger_info *info,
					u32 timer)
{
	u8 reg_val = 0;

	if (timer >= 160)
		reg_val = BQ2560X_REG05_WDT_160S;
	else if (timer <= 0)
		reg_val = BQ2560X_REG05_WDT_DISABLE;
	else
		reg_val = (timer - BQ2560X_REG05_WDT_BASE) / BQ2560X_REG05_WDT_LSB;
	return bq2560x_update_bits(info, BQ2560X_REG_05,
					BQ2560X_REG05_WDT_MASK,
					reg_val << BQ2560X_REG05_WDT_SHIFT);
}

static int bq2560x_charger_set_boost_limit(struct bq2560x_charger_info *info,
					u32 limit)
{
	u8 reg_val = 0;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (limit <= 500)
			reg_val = SGM41516_BOOST_LIM_0P5A;
		else if (limit <= 1000)
			reg_val = SGM41516_BOOST_LIM_1P0A;
		else if (limit <= 1200)
			reg_val = SGM41516_BOOST_LIM_1P2A;
		else if (limit <= 1500)
			reg_val = SGM41516_BOOST_LIM_1P5A;
		else if (limit <= 2000)
			reg_val = SGM41516_BOOST_LIM_2P0A;
		else if (limit <= 2500)
			reg_val = SGM41516_BOOST_LIM_2P5A;
		else if (limit <= 3000)
			reg_val = SGM41516_BOOST_LIM_3P0A;
		else if (limit <= 3200)
			reg_val = SGM41516_BOOST_LIM_3P2A;

		return bq2560x_update_bits(info, SGM41516S_REG_10,
						SGM41516_BOOST_LIMIT_MASK,
						reg_val << SGM41516_BOOST_LIMIT_SHIFT);	
	}else {
		if (limit < 1200)
			reg_val = BQ2560X_REG02_BOOST_LIM_0P5A;
		else
			reg_val = BQ2560X_REG02_BOOST_LIM_1P2A;

		return bq2560x_update_bits(info, BQ2560X_REG_02,
						BQ2560X_REG02_BOOST_LIM_MASK,
						reg_val << BQ2560X_REG02_BOOST_LIM_SHIFT);
	}
}

static int bq2560x_charger_set_boostv(struct bq2560x_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (vol <= 4850)
			reg_val = 0x0;
		else if (vol <= 5000)
			reg_val = 0x01;
		else if (vol <= 5150)
			reg_val = 0x02;
		else if (vol <= 5300)
			reg_val = 0x03;
		else if (vol <= 5800)
			reg_val = 0x04;
		else if (vol <= 6400)
			reg_val = 0x05;
		else if (vol <= 6900)
			reg_val = 0x06;
		else
			reg_val = 0x07;

		return bq2560x_update_bits(info, BQ2560X_REG_06, SGM41516S_BOOSTV_MASK, reg_val << BQ2560X_REG06_BOOSTV_SHIFT);
	} else {
		if (vol <= 4850)
			reg_val = BQ2560X_REG06_BOOSTV_4P85V;
		else if (vol <= 5000)
			reg_val = BQ2560X_REG06_BOOSTV_5V;
		else if (vol <= 5150)
			reg_val = BQ2560X_REG06_BOOSTV_5P15V;
		else
			reg_val = BQ2560X_REG06_BOOSTV_5P3V;

		return bq2560x_update_bits(info, BQ2560X_REG_06, BQ2560X_REG06_BOOSTV_MASK, reg_val << BQ2560X_REG06_BOOSTV_SHIFT);
	}
}



static int bq2560x_charger_hw_init(struct bq2560x_charger_info *info)
{
	struct sprd_battery_info bat_info = { };
	int termination_cur;
	int ret = 0;

	bat_info.batt_id_cha = NULL;

	if (info->psy_usb)
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		ret = sprd_battery_get_battery_info_sqc(info->psy_usb, &bat_info, 0);
#else
		ret = sprd_battery_get_battery_info(info->psy_usb, &bat_info);
#endif
	if (ret || !info->psy_usb) {
		bq_info("no battery information is supplied\n");

		/*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 100 mA, and default
		 * charge termination voltage to 4.2V.
		 */
		info->cur.sdp_limit = 500000;
		info->cur.sdp_cur = 500000;
		info->cur.dcp_limit = 5000000;
		info->cur.dcp_cur = 500000;
		info->cur.cdp_limit = 5000000;
		info->cur.cdp_cur = 1500000;
		info->cur.unknown_limit = 5000000;
		info->cur.unknown_cur = 500000;
		info->voltage_max_microvolt = 4440;
	} else {
		info->cur.sdp_limit = bat_info.cur.sdp_limit;
		info->cur.sdp_cur = bat_info.cur.sdp_cur;
		info->cur.dcp_limit = bat_info.cur.dcp_limit;
		info->cur.dcp_cur = bat_info.cur.dcp_cur;
		info->cur.cdp_limit = bat_info.cur.cdp_limit;
		info->cur.cdp_cur = bat_info.cur.cdp_cur;
		info->cur.unknown_limit = bat_info.cur.unknown_limit;
		info->cur.unknown_cur = bat_info.cur.unknown_cur;
		info->cur.fchg_limit = bat_info.cur.fchg_limit;
		info->cur.fchg_cur = bat_info.cur.fchg_cur;

		info->voltage_max_microvolt = bat_info.constant_charge_voltage_max_uv / 1000;
		termination_cur = bat_info.charge_term_current_ua / 1000;
		sprd_battery_put_battery_info(info->psy_usb, &bat_info);
	}

	bq2560x_charger_dumper_reg(info);
	ret = bq2560x_update_bits(info, BQ2560X_REG_0B,
			  BQ2560X_REG0B_RESET_MASK,
			  BQ2560X_REG0B_RESET << BQ2560X_REG0B_RESET_SHIFT);
	if (ret) {
		bq_err("reset bq2560x failed\n");
		return ret;
	}
	ret = bq2560x_charger_set_ovp(info, BQ2560X_FCHG_OVP_6V);
	if (ret) {
		bq_err("set bq2560x ovp failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_vindpm(info, DEFAULT_VINDPM_MV);
	if (ret) {
		bq_err("set bq2560x vindpm vol failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_termina_vol(info,
					      info->voltage_max_microvolt);
	if (ret) {
		bq_err("set bq2560x terminal vol failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_termina_cur(info, termination_cur);
	if (ret) {
		bq_err("set bq2560x terminal cur failed\n");
		return ret;
	}

	ret = bq2560x_charger_set_termina_enable(info,
		info->hw_mode == BQ25601_SLAVE ? BQ2560X_REG05_TERM_DISABLE : BQ2560X_REG05_TERM_ENABLE);
	if (ret) {
		bq_err("set bq2560x terminal cur failed\n");
		return ret;
	}

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		ret = bq2560x_charger_set_boostv(info, 5300);
		if (ret) {
			bq_err("set bq2560x boost vol failed\n");
			return ret;
		}
	}

	ret = bq2560x_charger_set_limit_current(info,
						info->cur.unknown_cur);
	if (ret)
		bq_err("set bq2560x limit current failed\n");

	ret = bq2560x_charger_set_safety_timer(info, BQ2560X_REG05_CHG_TIMER_DISABLE);
	if (ret)
		bq_err("set safety timer failed\n");
	ret = bq2560x_charger_set_watchdog_timer(info, 0);
	if (ret)
		bq_err("set watchdog timer failed\n");

	if (info->boost_limit !=0) {
		ret = bq2560x_charger_set_boost_limit(info, info->boost_limit);
		if (ret)
			bq_err("set boost limit failed\n");
	}

	bq2560x_update_bits(info, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_DISABLE << BQ2560X_REG01_PFM_DIS_SHIFT);

	if (info->batfet_rst_en) {
		ret = bq2560x_update_bits(info, BQ2560X_REG_07,
			BQ2560X_REG_EN_BATFET_MASK, BQ2560X_REG07_BATFET_RST_ENABLE << BQ2560X_REG_EN_BATFET_SHIFT);	
		bq_info("enable batfet_rst!\n");
		if (ret)
			bq_err("enable batfet_rst failed!\n");
	} else {
		ret = bq2560x_update_bits(info, BQ2560X_REG_07,
					BQ2560X_REG_EN_BATFET_MASK, 0);
		bq_info("disable batfet_rst!\n");
		if (ret)
			bq_err("disable batfet_rst failed!\n");
	}

	ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DLY_MASK,
			BQ2560X_REG07_BATFET_DLY_0S << BQ2560X_REG07_BATFET_DLY_SHIFT);
	if (ret)
		bq_err("set battfet delay 0s failed\n");

	if (info->hw_mode == BQ25601_SLAVE) {
		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
					BQ2560X_REG01_CHG_CONFIG_MASK,
					0 << BQ2560X_REG01_CHG_CONFIG_SHIFT);
		bq_info("disable slave charger!\n");
		bq2560x_charger_enable_hiz(info, true);
	}

	return ret;
}

static int bq2560x_charger_start_charge(struct bq2560x_charger_info *info)
{
	int ret = 0;
	u8 reg_val = 0x01;

	bq_info("bq25601: start charge #####\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				BQ2560X_REG01_CHG_CONFIG_MASK,
				reg_val << BQ2560X_REG01_CHG_CONFIG_SHIFT);

	if (ret)
		bq_err("enable bq2560x charge failed\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_OTG_CONFIG_MASK,
				  BQ2560X_REG01_OTG_DISABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
	if (ret) {
		bq_err("disable bq2560x otg failed\n");
	}

	info->is_charging_enabled = true;

	return ret;
}

static void bq2560x_charger_stop_charge(struct bq2560x_charger_info *info)
{
	int ret = 0;
	u8 reg_val = 0x00;

	bq_info("bq25601: stop charge #####\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				BQ2560X_REG01_CHG_CONFIG_MASK,
				reg_val << BQ2560X_REG01_CHG_CONFIG_SHIFT);

	if (ret)
		bq_err("disable bq2560x charge failed\n");
	info->is_charging_enabled = false;
}

static void bq2560x_charger_enable_hiz(struct bq2560x_charger_info *info, bool enable)
{
	int ret = 0;
	u8 reg_val = BQ2560X_REG00_HIZ_DISABLE;

	if (enable) {
		bq_info("bq25601: enable hiz #####\n");
		reg_val |= BQ2560X_REG00_HIZ_ENABLE;
	} else {
		bq_info("bq25601: disable hiz #####\n");
	}

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		 bq2560x_update_bits(info, SGM41516S_REG_12,
				    SGM41516S_ENHIZ_MASK,
				    reg_val << SGM41516S_ENHIZ_SHIFT);
	} else {
		ret = bq2560x_update_bits(info, BQ2560X_REG_00,
						BQ2560X_REG00_ENHIZ_MASK,
						reg_val << BQ2560X_REG00_ENHIZ_SHIFT);
		if (ret)
			bq_err("set HIZ mode  failed\n");
	}
}

static int bq2560x_charger_get_hiz_status(struct bq2560x_charger_info *info, unsigned int *enable)
{
	int ret = 0;
	u8 reg_val = 0x00;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		ret = bq2560x_read(info, SGM41516S_REG_12, &reg_val);
		if (ret) {
			*enable = 0;
			bq_err("get bq2560x hiz status failed\n");
			return 0;
		}

		*enable = (reg_val & SGM41516S_ENHIZ_MASK) ? 1 : 0;

	} else {
		ret = bq2560x_read(info, BQ2560X_REG_00, &reg_val);
		if (ret) {
			*enable = 0;
			bq_err("get bq2560x hiz status failed\n");
			return 0;
		}

		*enable = (reg_val & BQ2560X_REG00_ENHIZ_MASK) ? 1 : 0;
	}

	return 0;
}

static int bq2560x_charger_get_charge_status(struct bq2560x_charger_info *info, unsigned int *en)
{
	int ret = 0;
	u8 reg_val = 0x00;

	ret = bq2560x_read(info, BQ2560X_REG_01, &reg_val);
	if (ret) {
		*en = 0;
		bq_err("get bq2560x charge status failed\n");
		return 0;
	}

	*en = (reg_val & BQ2560X_REG01_CHG_CONFIG_MASK) ? 1 : 0;

	return 0;
}

static int bq2560x_charger_set_current(struct bq2560x_charger_info *info,
				       u32 cur)
{
	u8 reg_val;
	u8 step = 0, mask = 0;

	cur = cur / 1000;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (cur > SGM41516S_ICHG_MAX)
			cur = SGM41516S_ICHG_MAX;

		reg_val = cur / BQ2560X_REG02_ICHG_LSB;
		reg_val &= SGM41516S_ICHG_MASK;

		return bq2560x_update_bits(info, BQ2560X_REG_02,
						SGM41516S_ICHG_MASK,
						reg_val);
	}

	if (info->client->addr == SGM41513_I2C_ADDR) {
		if (cur <= 35) {
			mask = 0;
			step = cur / 5;
		} else if (cur <= 120) {
			mask = 1;
			if (cur >= 40) {
				step = (cur - 40) / 10;
			} else {
				step = 0;
			}
		} else if (cur <= 270) {
			mask = 2;
			if (cur >= 130) {
				step = (cur - 130) / 20;
			} else {
				step = 0;
			}
		} else if (cur <= 510) {
			mask = 3;
			if (cur >= 300) {
				step = (cur - 300) / 30;
			} else {
				step = 0;
			}
		} else if (cur <= 960) {
			mask = 4;
			if (cur >= 540) {
				step = (cur - 540) / 60;
			} else {
				step = 0;
			}
		} else if (cur <= 1440) {
			mask = 5;
			if (cur >= 1020) {
				step = (cur - 1020) / 60;
			} else {
				step = 0;
			}
		} else if (cur <= 2340) {
			mask = 6;
			if (cur >= 1500) {
				step = (cur - 1500) / 120;
			} else {
				step = 0;
			}
		} else if (cur <= 3000) {
			mask = 7;
			if (cur >= 2460) {
				step = (cur - 2460) / 120;
			} else {
				step = 0;
			}
		} else {
			mask = 7;
			step = 0x07;
		}

		step = (step > 0x07) ? 0x07 : step;
		if (step == 0 && mask != 0) {
			/*if current jump to next new mask,we use last step*/
			reg_val = (mask << 3) - 1;
		} else {
			reg_val = mask << 3 | step;
		}
	} else if (info->chip_main_id == SGM41516D_VENDOR_ID) {
		if (cur > 3780) {
			reg_val = 0x3f;
		} else {
			reg_val = cur / BQ2560X_REG02_ICHG_LSB;
			reg_val &= BQ2560X_REG02_ICHG_MASK;
		}
	} else {
		if (cur > 3000) {
			reg_val = 0x32;
		} else {
			reg_val = cur / BQ2560X_REG02_ICHG_LSB;
			reg_val &= BQ2560X_REG02_ICHG_MASK;
		}
	}

	return bq2560x_update_bits(info, BQ2560X_REG_02,
				   BQ2560X_REG02_ICHG_MASK,
				   reg_val);
}

static int bq2560x_charger_get_current(struct bq2560x_charger_info *info,
				       u32 *cur)
{
	u8 reg_val = 0, mask = 0, step = 0;
	int ret;

	ret = bq2560x_read(info, BQ2560X_REG_02, &reg_val);
	if (ret < 0)
		return ret;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		reg_val &= SGM41516S_ICHG_MASK;

		*cur = (reg_val * BQ2560X_REG02_ICHG_LSB + BQ2560X_REG02_ICHG_BASE) * 1000;
	} else {
		reg_val &= BQ2560X_REG02_ICHG_MASK;

		mask = reg_val >> 3;
		step = reg_val & GENMASK(2, 0);

		if (info->client->addr == SGM41513_I2C_ADDR) {
			if (mask == 0) {
				*cur = (step * 5 + 0) * 1000;
			} else if (mask == 1) {
				*cur = (step * 10 + 40) * 1000;
			} else if (mask == 2) {
				*cur = (step * 20 + 130) * 1000;
			} else if (mask == 3) {
				*cur = (step * 30 + 300) * 1000;
			} else if (mask == 4) {
				*cur = (step * 60 + 540) * 1000;
			} else if (mask == 5) {
				*cur = (step * 60 + 1020) * 1000;
			} else if (mask == 6) {
				*cur = (step * 120 + 1500) * 1000;
			} else {
				*cur = (step * 120 + 2460) * 1000;
				*cur = (*cur  > 3000000) ? 3000000 : *cur;
			}
		} else {
			*cur = (reg_val * BQ2560X_REG02_ICHG_LSB + BQ2560X_REG02_ICHG_BASE) * 1000;
		}
	}

	return 0;
}

static int
bq2560x_charger_set_limit_current(struct bq2560x_charger_info *info,
				  u32 limit_cur)
{
	u8 reg_val = 0;
	int ret = 0;

	limit_cur = limit_cur / 1000;

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (limit_cur <= 100)
			reg_val = 0x0;
		else if (limit_cur >= 3300)
			reg_val = 0x20;
		else
			reg_val = (limit_cur - BQ2560X_REG00_IINLIM_BASE) / BQ2560X_REG00_IINLIM_LSB;

		ret = bq2560x_update_bits(info, BQ2560X_REG_00, SGM41516S_IINLIM_MASK, reg_val);
		if (ret)
			bq_err("set bq2560x limit cur failed\n");
	} else {
		if (limit_cur <= BQ2560X_REG00_IINLIM_BASE)
			reg_val = 0x0;
		else if (limit_cur >= BQ2560X_REG00_IINLIM_MAX)
			reg_val = 0x1F;
		else
			reg_val = (limit_cur - BQ2560X_REG00_IINLIM_BASE) / BQ2560X_REG00_IINLIM_LSB;

		ret = bq2560x_write(info, BQ2560X_REG_00, reg_val);
		if (ret)
			bq_err("set bq2560x limit cur failed\n");
	}

#ifdef CONFIG_TYPEC_PD_CTS_TEST
	if (limit_cur <= BQ2560X_REG00_IINLIM_BASE) {
		ret = bq2560x_charger_set_vindpm(info, 5400);
		if (ret)
			bq_err("set bq2560x vindpm vol 5400 failed\n");
	} else {
		ret = bq2560x_charger_set_vindpm(info, DEFAULT_VINDPM_MV);
		if (ret)
			bq_err("set bq2560x vindpm vol failed\n");
	}
#else
	if (limit_cur <= BQ2560X_REG00_IINLIM_BASE) {
		ret = bq2560x_charger_set_vindpm(info, DEFAULT_VINDPM_MV);
		if (ret)
			bq_err("set bq2560x vindpm vol DEFAULT_VINDPM_MV failed\n");
	} else {
		ret = bq2560x_charger_set_vindpm(info, DEFAULT_VINDPM_MV);
		if (ret)
			bq_err("set bq2560x vindpm vol failed\n");
	}
#endif

	return ret;
}

static int
bq2560x_charger_get_limit_current(struct bq2560x_charger_info *info,
				  u32 *limit_cur)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_00, &reg_val);
	if (ret < 0)
		return ret;
	if (info->chip_main_id == SGM41516S_VENDOR_ID)
		reg_val &= SGM41516S_IINLIM_MASK;
	else
		reg_val &= BQ2560X_REG00_IINLIM_MASK;

	reg_val = reg_val >> BQ2560X_REG00_IINLIM_SHIFT;
	*limit_cur = (reg_val * BQ2560X_REG00_IINLIM_LSB + BQ2560X_REG00_IINLIM_BASE) * 1000;
	if (*limit_cur >= BQ2560X_REG00_IINLIM_MAX * 1000)
		*limit_cur = BQ2560X_REG00_IINLIM_MAX * 1000;

	return 0;
}

static int bq2560x_charger_get_health(struct bq2560x_charger_info *info,
				      u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int bq2560x_charger_get_online(struct bq2560x_charger_info *info,
				      u32 *online)
{
	if (info->is_charger_online)
		*online = true;
	else
		*online = false;

	return 0;
}

static int bq2560x_charger_feed_watchdog(struct bq2560x_charger_info *info,
					 u32 val)
{
	int ret;

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_WDT_RESET_MASK,
				  BQ2560X_REG01_WDT_RESET_MASK);
	if (ret)
		bq_err("reset bq2560x failed\n");
	bq_info("\n");

	return ret;
}

static int bq2560x_charger_set_shipmode(struct bq2560x_charger_info *info,
					  u32 val)
{
	int ret = 0;

	if (info->disable_ship_mode) {
		ret = -1;
		bq_err("need disable shipmode ret=%d\n", ret);
		return ret;
	}

	ret = bq2560x_update_bits(info, BQ2560X_REG_05, BQ2560X_REG05_WDT_MASK,
			BQ2560X_REG05_WDT_DISABLE << BQ2560X_REG05_WDT_SHIFT);
	if (ret)
		bq_err("disable bq2560x wdt failed\n");

	ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DIS_MASK,
			BQ2560X_REG07_BATFET_OFF << BQ2560X_REG07_BATFET_DIS_SHIFT);

	if (ret)
		bq_err("set_shipmode failed\n");
	bq_info("bq2560x: set shipmode #####\n");

	return ret;
}

static int bq2560x_charger_get_shipmode(struct bq2560x_charger_info *info, u32 *val)
{
	u8 data = 0;
	int ret = 0;

	if (info->disable_ship_mode) {
		ret = -1;
		*val = 0;
		bq_err("need disable shipmode ret=%d\n", ret);
		return ret;
	}

	ret = bq2560x_read(info, BQ2560X_REG_07, &data);
	if (ret < 0)
		return ret;
	data = (data & BQ2560X_REG07_BATFET_DIS_MASK) >> BQ2560X_REG07_BATFET_DIS_SHIFT;
	*val = !data;

	return 0;
}

static int bq2560x_charger_set_powerpath(struct bq2560x_charger_info *info,
					  unsigned int val)
{
	int ret = 0;
#if 0
	if (val) {
		ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DIS_MASK,
				BQ2560X_REG07_BATFET_OFF << BQ2560X_REG07_BATFET_DIS_SHIFT);
		if (ret)
			bq_err("enable bq2560x powerpath failed\n");
	} else {
		ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DIS_MASK,
				BQ2560X_REG07_BATFET_ON << BQ2560X_REG07_BATFET_DIS_SHIFT);
		if (ret)
			bq_err("disable bq2560x powerpath failed\n");
	}

	ret = bq2560x_update_bits(info, BQ2560X_REG_07, BQ2560X_REG07_BATFET_DLY_MASK,
			BQ2560X_REG07_BATFET_DLY_0S << BQ2560X_REG07_BATFET_DLY_SHIFT);
	if (ret)
		bq_err("set powerpath delay 0s failed\n");
#endif
	bq_info("bq2560x: set bq2560x powerpath val:%d  #####\n", val);

	return ret;
}

static int bq2560x_charger_get_powerpath(struct bq2560x_charger_info *info, u32 *val)
{
	u8 data = 0;
	int ret = 0;

	ret = bq2560x_read(info, BQ2560X_REG_07, &data);
	if (ret < 0)
		return ret;
	data = (data & BQ2560X_REG07_BATFET_DIS_MASK) >> BQ2560X_REG07_BATFET_DIS_SHIFT;
	*val = !!data;

	return 0;
}


static int bq2560x_charger_get_vindpm(struct bq2560x_charger_info *info,
					u32 *vol)
{
	u8 reg_val;
	int ret;

	ret = bq2560x_read(info, BQ2560X_REG_06, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG06_VINDPM_MASK;
	reg_val = reg_val >> BQ2560X_REG06_VINDPM_SHIFT;

	*vol = reg_val * BQ2560X_REG06_VINDPM_LSB + BQ2560X_REG06_VINDPM_BASE;
	return 0;
}

static int bq2560x_charger_get_reg_status(struct bq2560x_charger_info *info,
					u32 *status)
{
	u8 reg_val = 0;
	int ret = 0;

	if (!info || !status) {
		bq_err("%s: Null ptr\n", __func__);
		return -EFAULT;
	}

	ret = bq2560x_read(info, BQ2560X_REG_08, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= BQ2560X_REG08_CHRG_STAT_MASK;
	reg_val = reg_val >> BQ2560X_REG08_CHRG_STAT_SHIFT;
	*status = reg_val;

	return 0;
}

static int bq2560x_charger_get_status(struct bq2560x_charger_info *info)
{
	u32 charger_status = 0;
	int ret = 0;

	ret = bq2560x_charger_get_reg_status(info, &charger_status);
	if (ret == 0) {
		if (charger_status == BQ2560X_REG08_CHRG_STAT_IDLE) {
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else if ((charger_status == BQ2560X_REG08_CHRG_STAT_PRECHG) ||
					(charger_status == BQ2560X_REG08_CHRG_STAT_FASTCHG)) {
			return POWER_SUPPLY_STATUS_CHARGING;
		} else if (charger_status == BQ2560X_REG08_CHRG_STAT_CHGDONE) {
			return POWER_SUPPLY_STATUS_FULL;
		} else {
			return POWER_SUPPLY_STATUS_CHARGING;
		}
	}
	bq_info("bq2560x: read status failed, ret = %d\n", ret);
	return POWER_SUPPLY_STATUS_UNKNOWN;
}

static int bq2560x_charger_set_status(struct bq2560x_charger_info *info,
				      int val)
{
	int ret = 0;

	if (!val) {
		bq2560x_charger_stop_charge(info);
		info->charging = false;
	} else if (val && !info->charging) {
		ret = bq2560x_charger_start_charge(info);
		if (ret) {
			bq_err("start charge failed\n");
		} else {
			info->charging = true;
		}
	}

	return ret;
}

static void bq2560x_print_regs(struct bq2560x_charger_info *info, char *buffer, unsigned int len)
{
	int i = 0;
	u8 value[12] = {0};
	char temp = 0;
	int read_reg_cnt = 0;

	for (i = 0; i < ARRAY_SIZE(value); i++) {
retry_read_reg:
		bq2560x_read(info, i, &(value[i]));
		if (i == ARRAY_SIZE(value) - 1) {
			bq_info("0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
				value[0], value[1], value[2], value[3], value[4], value[5],
				value[6], value[7], value[8], value[9], value[10], value[11]);
		}
		/*
		 * 8.3.9.3 Interrupt to Host:
		 * To read the current fault status, the host has to read
		 * REG09 two times consecutively.
		 */
		if (i == BQ2560X_REG_09 && !(read_reg_cnt++))
			goto retry_read_reg;
	}

	temp = (value[8] & GENMASK(4, 3)) >> 3;

	switch (temp) {
	case 0:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: NOT CHARGING, ");
		break;
	case 1:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: PRE CHARGING, ");
		break;
	case 2:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: FAST CHARGING, ");
		break;
	case 3:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "CHG_STAT: TERM CHARGING, ");
		break;
	default:
		break;
	}

	if (value[9] & BIT(7)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "WT_FAULT: 1, ");
	}

	if (value[9] & BIT(6)) {
		snprintf(buffer + strlen(buffer), len- strlen(buffer), "BOOST_FAULT: 1, ");
	}

	temp = (value[9] & GENMASK(5, 4)) >> 4;

	switch (temp) {
	case 1:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "VAC OVP or VBUS UVP: 1, ");
		break;
	case 2:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "THERMAL SHUTDOWN: 1, ");
		break;
	case 3:
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "SAFETY TIMEOUT: 1, ");
		break;
	default:
		break;
	}

	if (value[9] & BIT(3)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "BATOVP: 1, ");
	}

	if (value[10] & BIT(6)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "VINDPM: 1, ");
		info->chg_status |= BIT(SQC_CHG_STATUS_AICL);
	} else {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "VINDPM: 0, ");
		info->chg_status &= ~ BIT(SQC_CHG_STATUS_AICL);
	}

	if (value[10] & BIT(5)) {
		snprintf(buffer + strlen(buffer), len - strlen(buffer), "IINDPM: 1, ");
	}
	
}

static int bq2560x_charger_dumper_reg(struct bq2560x_charger_info *info)
{
	int usb_icl = 0, fcc = 0, fcv = 0, topoff = 0, recharge_voltage = 0;
	char buffer[512] = {0, };

	bq2560x_charger_get_termina_vol(info, &fcv);

	bq2560x_charger_get_limit_current(info, &usb_icl);

	bq2560x_charger_get_current(info, &fcc);

	bq2560x_charger_get_termia_cur(info, &topoff);

	bq2560x_charger_get_recharge_voltage(info, &recharge_voltage);
	
	bq2560x_print_regs(info, buffer, sizeof(buffer));

	bq_info("bq2560x: charging[%d], fcv[%d], usb_icl[%d], fcc[%d], topoff[%d], rechg_volt[%d], %s",
				info->charging, fcv / 1000, usb_icl / 1000, fcc / 1000,
				topoff / 1000, recharge_voltage / 1000, buffer);
	return 0;
}

enum alarmtimer_restart icl_vote_timer_func(struct alarm *alarm, ktime_t now)
{
	union power_supply_propval icl_val;
	icl_val.intval = 1800000;

	sqc_set_property(POWER_SUPPLY_PROP_INPUT_POWER_LIMIT, &icl_val);

	return ALARMTIMER_NORESTART;
}

static int bq2560x_charger_usb_change(struct notifier_block *nb,
				      unsigned long limit, void *data)
{
	struct bq2560x_charger_info *info =
		container_of(nb, struct bq2560x_charger_info, usb_notify);
	union power_supply_propval icl_val = {.intval = 0};

	bq_info("bq25601: bq25601_charger_usb_change: %d\n", limit);

	if (info->enable_driver_icl_vote) {
		if (info->hw_mode == BQ25601_MASTER) {
			if (limit) {
				alarm_start_relative(&info->icl_vote_timer, ms_to_ktime(15 * 1000));
			} else {
				alarm_cancel(&info->icl_vote_timer);
				icl_val.intval = -1;
				sqc_set_property(POWER_SUPPLY_PROP_INPUT_POWER_LIMIT, &icl_val);
			}
		}
	}

	return NOTIFY_OK;
}

#define VINDPM_LOW_THRESHOLD_UV 4150000
static int bq2560x_charger_tuning_vindpm_insert(struct bq2560x_charger_info *info)
{
	union power_supply_propval val;
	union power_supply_propval batt_vol_uv;

	struct power_supply *fuel_gauge;
	int ret1 = 0, ret2 = 0,  vchg = 0, vindpm = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

	ret2 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n", __func__);
		return ret1;
	}

	if (ret2) {
		bq_err("%s: get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n", __func__);
		return ret2;
	}

	vchg = val.intval / 1000;

	bq2560x_charger_get_vindpm(info, &vindpm);
	bq_info("%s: get CHARGE_VOLTAGE %d, vindpm %d\n", __func__, vchg, vindpm);

	if (vchg > info->vindpm_value) {
		bq2560x_charger_set_vindpm(info, vchg > info->vindpm_value);
		bq_info(" vchg %d, batt_vol_uv=%d, now vindpm %d, set vindpm to %d\n",
			 vchg, batt_vol_uv.intval, vindpm, vchg > info->vindpm_value);
		bq2560x_charger_dumper_reg(info);
	} else {
		if (vchg > batt_vol_uv.intval / 1000 + 200) {
			bq2560x_charger_set_vindpm(info, batt_vol_uv.intval / 1000 + 200);
			bq_info("%s: vchg %d, batt_vol_uv=%d,  now vindpm %d, set vindpm1 to %d\n",
				__func__, vchg, batt_vol_uv.intval, vindpm, batt_vol_uv.intval / 1000 + 200);
		} else {
			bq2560x_charger_set_vindpm(info, vchg + 100);
			bq_info("%s: vchg %d, batt_vol_uv=%d,  now vindpm %d, set vindpm2 to %d\n",
				__func__, vchg, batt_vol_uv.intval, vindpm, vchg + 100);
		}
	}

	return 0;
}

static int bq2560x_charger_tuning_vindpm(struct bq2560x_charger_info *info)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret = 0, vchg = 0, vindpm = 0;
	union power_supply_propval batt_vol_uv;
	int diff_vbat_vindpm = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	if (!info->suspended) {
		ret = power_supply_get_property(fuel_gauge,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

		ret = power_supply_get_property(fuel_gauge,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);
		power_supply_put(fuel_gauge);
		if (ret)
			return ret;

		vchg = val.intval / 1000;

		bq2560x_charger_get_vindpm(info, &vindpm);

		diff_vbat_vindpm = vindpm - batt_vol_uv.intval / 1000;
		bq_info("%s: get CHARGE_VOLTAGE %d, vindpm %d batt_vol_uv %d diff_vbat_vindpm=%d\n",
			__func__, vchg, vindpm,  batt_vol_uv.intval, diff_vbat_vindpm);

		if (diff_vbat_vindpm < 200) {
			vindpm = vindpm + 100;
			if (vindpm > BQ2560X_REG06_VINDPM_NORMAL)
				vindpm = BQ2560X_REG06_VINDPM_NORMAL;
			bq2560x_charger_set_vindpm(info, vindpm);
			bq_info("%s: vchg %d, now vindpm %d\n", __func__, vchg, vindpm);
		}
	} else {
			pr_info("%s: system suspend,not adjust vindpm\n", __func__);
	}

	return 0;
}

static void tuning_vindpm_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
							  struct bq2560x_charger_info,
							  vindpm_work);
	pm_stay_awake(info->dev);
	bq2560x_charger_tuning_vindpm(info);
	pm_relax(info->dev);
}

static void force_recharge_workfunc(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
							  struct bq2560x_charger_info,
							  force_recharge_work);
	pm_stay_awake(info->dev);
	if (info->is_charging_enabled) {
		bq_info("restart charger\n");
		bq2560x_charger_stop_charge(info);
		msleep(50);
		bq2560x_charger_start_charge(info);
	}
	pm_relax(info->dev);
}

static void update_workfunc(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
							  struct bq2560x_charger_info,
							  update_work);
	pm_stay_awake(info->dev);
	bq_info("is_charger_online=%d\n", info->is_charger_online);
	if (!info->is_charger_online) {
		bq2560x_charger_set_watchdog_timer(info, 0);
	} else {
		bq2560x_charger_set_watchdog_timer(info, 80);
	}
	pm_relax(info->dev);
}

static int bq2560x_charger_config_is_enabled(struct bq2560x_charger_info *info)
{
	int ret;
	u8 val;

	ret = bq2560x_read(info, BQ2560X_REG_01, &val);
	if (ret) {
		bq_err("failed to get bq2560x otg status\n");
		return ret;
	}

	val &= BQ2560X_REG01_CHG_CONFIG_MASK;

	return val;
}

static int bq2560x_charger_usb_get_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = power_supply_get_drvdata(psy);
	u32 cur, online, health;
	enum usb_charger_type type;
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (info->is_charger_online)
			val->intval = bq2560x_charger_get_status(info);
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = bq2560x_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = bq2560x_charger_get_limit_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq2560x_charger_get_online(info, &online);
		if (ret)
			goto out;

		val->intval = online;

		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
			val->intval = 0;
		} else {
			ret = bq2560x_charger_get_health(info, &health);
			if (ret)
				goto out;

			val->intval = health;
		}
		break;

	case POWER_SUPPLY_PROP_USB_TYPE:
		type = info->usb_phy->chg_type;

		switch (type) {
		case SDP_TYPE:
			val->intval = POWER_SUPPLY_USB_TYPE_SDP;
			break;

		case DCP_TYPE:
			val->intval = POWER_SUPPLY_USB_TYPE_DCP;
			break;

		case CDP_TYPE:
			val->intval = POWER_SUPPLY_USB_TYPE_CDP;
			break;

		default:
			val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		}

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		bq2560x_charger_get_termina_vol(info, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = bq2560x_charger_get_termia_cur(info, &val->intval);
		if (ret < 0)
			bq_err("get topoff failed\n");
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int bq2560x_charger_usb_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;
	u32 cur = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		bq_info("bq2560x[REG]: set fcc %d", val->intval);
		ret = bq2560x_charger_set_current(info, val->intval);
		if (ret < 0)
			bq_err("set charge current failed\n");
		bq2560x_charger_get_current(info, &cur);
		bq_info("bq2560x[REG]: get fcc %d", cur);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		bq_info("bq2560x[REG]: set topoff %d", val->intval);
		ret = bq2560x_charger_set_termina_cur(info, val->intval);
		if (ret < 0)
			bq_err("set charge voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		bq_info("bq2560x[REG]: set usb icl %d", val->intval);
		ret = bq2560x_charger_set_limit_current(info, val->intval);
		if (ret < 0)
			bq_err("set input current limit failed\n");
		bq2560x_charger_get_limit_current(info, &cur);
		bq_info("bq2560x[REG]: get usb icl %d", cur);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		bq_info("bq2560x[REG]: set enable %d", val->intval);

		ret = bq2560x_charger_set_status(info, val->intval);
		if (ret < 0)
			bq_err("set charge status failed\n");
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = bq2560x_charger_set_termina_vol(info, val->intval / 1000);
		if (ret < 0)
			bq_err("failed to set terminate voltage\n");
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		info->is_charger_online = !!val->intval;
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int bq2560x_charger_property_is_writeable(struct power_supply *psy,
						 enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_PRESENT:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct power_supply_desc bq2560x_charger_desc = {
	.name			= "charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= bq2560x_usb_props,
	.num_properties		= ARRAY_SIZE(bq2560x_usb_props),
	.get_property		= bq2560x_charger_usb_get_property,
	.set_property		= bq2560x_charger_usb_set_property,
	.property_is_writeable	= bq2560x_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static int zte_bq2560x_charger_usb_get_property(struct zte_power_supply *psy,
					    enum zte_power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	u32 enabled = 0;
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		enabled = bq2560x_charger_config_is_enabled(info);
		val->intval = !!enabled;
		break;
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
		ret = bq2560x_charger_get_vindpm(info, &val->intval);
		if (ret < 0)
			bq_err("get vindpm failed\n");
		bq_info("bq2560x_1#[REG]: get vindpm: %d!", val->intval);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		ret = bq2560x_charger_get_recharge_voltage(info, &val->intval);
		if (ret < 0)
			bq_err("get charge recharge_voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = bq2560x_charger_get_shipmode(info, &val->intval);
		if (ret < 0)
			bq_err("get shipmode failed\n");
		bq_info("bq2560x[REG]: get shipmode: %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq2560x_charger_usb_set_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	if (psp != POWER_SUPPLY_PROP_SET_SHIP_MODE)
		return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		bq_info("bq2560x[REG]: set recharge_voltage %d", val->intval);

		ret = bq2560x_charger_set_recharge_voltage(info, val->intval);
		if (ret < 0)
			bq_err("set charge recharge_voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_FEED_WATCHDOG:
		bq_info("bq2560x[REG]: feed watchdog\n");
		ret = bq2560x_charger_feed_watchdog(info, val->intval);
		if (ret < 0)
			bq_err("feed charger watchdog failed\n");
		schedule_delayed_work(&info->vindpm_work, HZ * 3);
		bq2560x_charger_dumper_reg(info);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		if (val->intval == 0) {
			bq_info("bq2560x[REG]: set shipmode %d", val->intval);
			ret = bq2560x_charger_set_shipmode(info, val->intval);
			if (ret < 0)
				bq_err("set shipmode failed\n");
		} else
			bq_info("bq2560x[REG]: set shipmode invalid val %d!", val->intval);
		break;
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
		bq_info("bq2560x_2[REG]: tuning vindpm %d", val->intval);
		ret = bq2560x_charger_tuning_vindpm_insert(info);
		if (ret < 0)
			bq_err("failed to set tuning vindpm\n");
		break;
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		if (val->intval == true) {
			ret = bq2560x_charger_start_charge(info);
			if (ret)
				bq_err("start charge failed\n");
		} else if (val->intval == false) {
			bq2560x_charger_stop_charge(info);
		}
		break;
	case POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER:
		bq_info("bq2560x[REG]: set dog %d", val->intval);
		ret = bq2560x_charger_set_watchdog_timer(info, val->intval);
		if (ret < 0)
			bq_err("failed to set watchdog timer\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int zte_bq2560x_charger_property_is_writeable(struct zte_power_supply *psy,
						 enum zte_power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
	case POWER_SUPPLY_PROP_FEED_WATCHDOG:
	case POWER_SUPPLY_PROP_SET_WATCHDOG_TIMER:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct zte_power_supply_desc zte_bq2560x_charger_desc = {
	.name			= "zte_charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_bq2560x_usb_props,
	.num_properties		= ARRAY_SIZE(zte_bq2560x_usb_props),
	.get_property		= zte_bq2560x_charger_usb_get_property,
	.set_property		= zte_bq2560x_charger_usb_set_property,
	.property_is_writeable	= zte_bq2560x_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static int zte_bq2560x_slave_charger_usb_get_property(struct zte_power_supply *psy,
					    enum zte_power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = bq2560x_charger_get_shipmode(info, &val->intval);
		if (ret < 0)
			bq_err("get shipmode failed\n");
		bq_info("bq2560x[REG]: get shipmode: %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq2560x_slave_charger_usb_set_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2560x_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	if (psp != POWER_SUPPLY_PROP_SET_SHIP_MODE)
		return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		if (val->intval == 0) {
			bq_info("bq2560x[REG]: set shipmode %d", val->intval);
			ret = bq2560x_charger_set_shipmode(info, val->intval);
			if (ret < 0)
				bq_err("set shipmode failed\n");
		} else
			bq_info("bq2560x[REG]: set shipmode invalid val %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int zte_bq2560x_slave_charger_property_is_writeable(struct zte_power_supply *psy,
						 enum zte_power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct zte_power_supply_desc bq2560x_slave_charger_desc = {
	.name			= "zte_slave_charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_bq2560x_slave_usb_props,
	.num_properties		= ARRAY_SIZE(zte_bq2560x_slave_usb_props),
	.get_property		= zte_bq2560x_slave_charger_usb_get_property,
	.set_property		= zte_bq2560x_slave_charger_usb_set_property,
	.property_is_writeable	= zte_bq2560x_slave_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static void
bq2560x_charger_feed_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
							 struct bq2560x_charger_info,
							 wdt_work);
	int ret;

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_WDT_RESET_MASK,
				  BQ2560X_REG01_WDT_RESET_MASK);
	if (ret) {
		bq_err("reset bq2560x failed\n");
		return;
	}
	schedule_delayed_work(&info->wdt_work, HZ * 15);
}

#ifdef CONFIG_REGULATOR

static int bq2560x_charger_get_otg_currnent(struct bq2560x_charger_info *info)
{
	int ret;
	u8 value = 0;
	int otg_current = 0;

	ret = bq2560x_read(info, SGM41516S_REG_10, &value);
	if (ret) {
		bq_err("get sgm41516S otg current failed\n");
		return 0;
	}

	value = value & SGM41516S_REG_10_BOOST_LIM_MASK;

	switch (value) {
	case 0x00:
		otg_current = 500;
		break;
	case 0x01:
		otg_current = 1000;
		break;
	case 0x02:
		otg_current = 1200;
		break;
	case 0x03:
		otg_current = 1500;
		break;
	case 0x04:
		otg_current = 2000;
		break;
	case 0x05:
		otg_current = 2500;
		break;
	case 0x06:
		otg_current = 3000;
		break;
	case 0x07:
		otg_current = 3200;
		break;
	default:
		otg_current = 0;
	}

	bq_err("sgm41516S get otg_curr %d, reg0x%02X\n", otg_current, value);

	return otg_current;
}

static bool bq2560x_charger_set_otg_currnent(struct bq2560x_charger_info *info, int otg_current_ma)
{
	int ret = 0;
	u8 otg_current_reg = 0;

	if (otg_current_ma <= 500) {
		otg_current_reg = 0x00;
	} else if (otg_current_ma <= 1000) {
		otg_current_reg = 0x01;
	} else if (otg_current_ma <= 1200) {
		otg_current_reg = 0x02;
	} else if (otg_current_ma <= 1500) {
		otg_current_reg = 0x03;
	} else if (otg_current_ma <= 2000) {
		otg_current_reg = 0x04;
	} else if (otg_current_ma <= 2500) {
		otg_current_reg = 0x05;
	} else if (otg_current_ma <= 3000) {
		otg_current_reg = 0x06;
	} else if (otg_current_ma <= 3200) {
		otg_current_reg = 0x07;
	} else {
		otg_current_reg = 0x02;
	}

	ret = bq2560x_update_bits(info, SGM41516S_REG_10,
				SGM41516S_REG_10_BOOST_LIM_MASK,
				otg_current_reg << SGM41516S_REG_10_BOOST_LIM_SHIFT);
	if (ret) {
		bq_err("set sgm41516S otg current failed\n");
	}

	bq_err("sgm41516S set otg_curr %d, reg0x%02X\n", otg_current_ma, otg_current_reg);

	return 0;
}

#define SGM41516S_OTG_2000MA_CAPACITY 70
#define SGM41516S_OTG_1000MA_CAPACITY 20

#define SGM41516S_OTG_2000MA 2000
#define SGM41516S_OTG_1000MA 1200

static int bq2560x_charger_update_otg_currnent(struct bq2560x_charger_info *info)
{
	int ret = 0;
	int batt_cap = 0;

	ret = power_supply_get_prop_by_name("battery", POWER_SUPPLY_PROP_CAPACITY, &batt_cap);
	if (ret < 0) {
		bq_err("batt_cap %d%%, current %d\n", batt_cap, SGM41516S_OTG_2000MA_CAPACITY);
		return 0;
	}

	if (batt_cap > SGM41516S_OTG_2000MA_CAPACITY) {
		bq_err("batt_cap %d%%, current %d\n", batt_cap, SGM41516S_OTG_2000MA);
		if (bq2560x_charger_get_otg_currnent(info) != SGM41516S_OTG_2000MA) {
			bq2560x_charger_set_otg_currnent(info, SGM41516S_OTG_2000MA);
		}
	} else if (batt_cap > SGM41516S_OTG_1000MA_CAPACITY) {
		bq_err("batt_cap %d%%, current %d\n", batt_cap, SGM41516S_OTG_1000MA);
		if (bq2560x_charger_get_otg_currnent(info) != SGM41516S_OTG_1000MA) {
			bq2560x_charger_set_otg_currnent(info, SGM41516S_OTG_1000MA);
		}
	} else {
		bq_err("batt_cap %d%%, disable bq2560x otg\n", batt_cap);
		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				BQ2560X_REG01_PFM_DIS_MASK,
				BQ2560X_REG01_PFM_DISABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
		if (ret) {
			bq_err("disable bq2560x otg PFM mode failed\n");
		}

		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_OTG_CONFIG_MASK,
				  BQ2560X_REG01_OTG_DISABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
		if (ret) {
			bq_err("disable bq2560x otg failed\n");
		}

		return -EINVAL;
	}

	return 0;
}

static bool bq2560x_charger_check_otg_valid(struct bq2560x_charger_info *info)
{
	int ret;
	u8 value = 0;
	bool status = false;

	ret = bq2560x_read(info, BQ2560X_REG_01, &value);
	if (ret) {
		pr_err("get bq2560x charger otg valid status failed\n");
		return status;
	}

	if (value & BQ2560X_REG_OTG_MASK)
		status = true;
	else
		pr_err("otg is not valid, REG_1 = 0x%x\n", value);

	return status;
}

static bool bq2560x_charger_check_otg_fault(struct bq2560x_charger_info *info)
{
	int ret;
	u8 value = 0;
	bool status = true;

	ret = bq2560x_read(info, BQ2560X_REG_09, &value);
	if (ret) {
		pr_err("get bq2560x charger otg fault status failed\n");
		return status;
	}

	if (!(value & BQ2560X_REG09_FAULT_BOOST_MASK))
		status = false;
	else
		pr_err("boost fault occurs, REG_9 = 0x%x\n", value);

	return status;
}

static int bq2560x_host_status_check(struct bq2560x_charger_info *info)
{
	int ret;

	if (!IS_ERR_OR_NULL(info->edev)
			&& !extcon_get_state(info->edev, EXTCON_USB_HOST)) {
		bq_err("disable bq2560x otg, extcon otg state: %d\n", extcon_get_state(info->edev, EXTCON_USB_HOST));

		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				BQ2560X_REG01_OTG_CONFIG_MASK,
				BQ2560X_REG01_OTG_DISABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
		if (ret) {
			bq_err("disable bq2560x otg failed\n");
		}

		/* Enable charger detection function to identify the charger type */
		if (!info->use_typec_extcon) {
			ret = regmap_update_bits(info->pmic, info->charger_detect, BIT_DP_DM_BC_ENB, 0);
			if (ret)
				dev_err(info->dev, "enable BC1.2 failed\n");
		}

		return 0;
	}

	return 1;
}

static void bq2560x_charger_otg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq2560x_charger_info *info = container_of(dwork,
			struct bq2560x_charger_info, otg_work);
	bool otg_valid = bq2560x_charger_check_otg_valid(info);
	bool otg_fault = 0;
	int ret, retry = 0;
	int hiz_status = 0;

	if (info->suspended) {
		bq_info("Device suspend, no need to check.\n");
		goto out;
	}

	if (bq2560x_slave_data) {
		bq2560x_charger_get_hiz_status(bq2560x_slave_data, &hiz_status);
		bq_info("Slave bq2560x_charger_get_hiz_status:%d\n", hiz_status);
		if (hiz_status == 0) {
			bq2560x_charger_enable_hiz(bq2560x_slave_data, true);
		}
	}

	if (info->chip_main_id == SGM41516S_VENDOR_ID) {
		if (bq2560x_charger_update_otg_currnent(info) < 0) {
			bq_info("otg is disabled, goto out\n");
			goto out;
		}
	}

	/* Enable PFM mode when in OTG mode to reduce the power */
	bq2560x_update_bits(info, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_ENABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
	if (bq2560x_slave_data) {
		bq2560x_update_bits(bq2560x_slave_data, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_ENABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
	}

	bq_info("%s otg_valid is %d", __func__, otg_valid);

	/*bq_err("otg host_status_check: %d, extcon otg state: %d\n",
			info->host_status_check, extcon_get_state(info->edev, EXTCON_USB_HOST));*/

	if (info->host_status_check && !bq2560x_host_status_check(info)) {
		return;
	}

	if (otg_valid)
		goto out;

	do {
		otg_fault = bq2560x_charger_check_otg_fault(info);
		bq_info("otg_fault:%d, otg_valid:%d, retry:%d", otg_fault, otg_valid, retry);
		if (!otg_fault) {
		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
					  BQ2560X_REG01_OTG_CONFIG_MASK,
					  BQ2560X_REG01_OTG_ENABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
		if (ret)
			bq_err("restart bq2560x charger otg failed\n");
		}

		otg_valid = bq2560x_charger_check_otg_valid(info);
	} while (!otg_valid && retry++ < BQ2560X_OTG_RETRY_TIMES);

	if (retry >= BQ2560X_OTG_RETRY_TIMES) {
		bq_err("Restart OTG failed\n");
		return;
	}

out:
	schedule_delayed_work(&info->otg_work, msecs_to_jiffies(1500));
}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
static int zte_bq2560x_set_prop_by_name(const char *name, enum zte_power_supply_property psp, int data)
{
	struct zte_power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_err("%s:psy name is NULL!!\n", __func__);
		goto failed_loop;
	}

	psy = zte_power_supply_get_by_name(name);
	if (!psy) {
		pr_err("%s:get %s psy failed!!\n", __func__, name);
		goto failed_loop;
	}

	val.intval = data;

	rc = zte_power_supply_set_property(psy,
				psp, &val);
	zte_power_supply_put(psy);

	if (rc < 0) {
		pr_err("%s:Failed to set %s property:%d rc=%d\n", __func__, name, psp, rc);
		return rc;
	}

	return 0;

failed_loop:
	return -EINVAL;
}
#endif

static int bq2560x_charger_enable_otg(struct regulator_dev *dev)
{
	struct bq2560x_charger_info *info = rdev_get_drvdata(dev);
	int ret;
	int hiz_status = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	bq_info("%s into", __func__);
	info->otg_enable = true;

	pm_wakeup_event(info->dev, MSEC_PER_SEC);
	/*
	 * Disable charger detection function in case
	 * affecting the OTG timing sequence.
	 */
	if (!info->use_typec_extcon) {
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					BIT_DP_DM_BC_ENB, BIT_DP_DM_BC_ENB);
		if (ret) {
			bq_err("failed to disable bc1.2 detect function.\n");
			return ret;
		}
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	zte_bq2560x_set_prop_by_name("zte_cp_charger_psy", POWER_SUPPLY_PROP_USB_OTG, 1);
#endif

	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_OTG_CONFIG_MASK,
				  BQ2560X_REG01_OTG_ENABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
	if (ret) {
		bq_err("enable bq2560x otg failed\n");
		regmap_update_bits(info->pmic, info->charger_detect,
				   BIT_DP_DM_BC_ENB, 0);
		return ret;
	}

	if (bq2560x_slave_data) {
		bq2560x_charger_get_hiz_status(bq2560x_slave_data, &hiz_status);
		bq_info("Slave bq2560x_charger_get_hiz_status:%d\n", hiz_status);
		if (hiz_status == 0) {
			bq2560x_charger_enable_hiz(bq2560x_slave_data, true);
		}
	}

	/* Enable PFM mode when in OTG mode, reduce the power */
	bq2560x_update_bits(info, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_ENABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
	if (bq2560x_slave_data) {
		bq2560x_update_bits(bq2560x_slave_data, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_ENABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
	}
	bq2560x_charger_dumper_reg(info);

	schedule_delayed_work(&info->wdt_work,
			      msecs_to_jiffies(BQ2560X_FEED_WATCHDOG_VALID_MS));
	schedule_delayed_work(&info->otg_work,
			      msecs_to_jiffies(BQ2560X_OTG_VALID_MS));

	return 0;
}

static int bq2560x_charger_disable_otg(struct regulator_dev *dev)
{
	struct bq2560x_charger_info *info = rdev_get_drvdata(dev);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	bq_info("%s into", __func__);
	info->otg_enable = false;
	pm_wakeup_event(info->dev, MSEC_PER_SEC);

	if (info->suspended) {
		bq_info("Cannot disable otg because the I2C is in suspend mode.\n");
		goto out;
	}

	/* Disable PFM mode when exit OTG mode */
	bq2560x_update_bits(info, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_DISABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
	if (bq2560x_slave_data) {
		bq2560x_update_bits(bq2560x_slave_data, BQ2560X_REG_01,
					BQ2560X_REG01_PFM_DIS_MASK,
					BQ2560X_REG01_PFM_DISABLE << BQ2560X_REG01_PFM_DIS_SHIFT);
	}

	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);
	ret = bq2560x_update_bits(info, BQ2560X_REG_01,
				  BQ2560X_REG01_OTG_CONFIG_MASK,
				  BQ2560X_REG01_OTG_DISABLE << BQ2560X_REG01_OTG_CONFIG_SHIFT);
	if (ret) {
		bq_err("disable bq2560x otg failed\n");
		return ret;
	}

	/* Enable charger detection function to identify the charger type */
	if (!info->use_typec_extcon) {
		ret = regmap_update_bits(info->pmic, info->charger_detect, BIT_DP_DM_BC_ENB, 0);
		if (ret)
			dev_err(info->dev, "enable BC1.2 failed\n");
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	zte_bq2560x_set_prop_by_name("zte_cp_charger_psy", POWER_SUPPLY_PROP_USB_OTG, 0);
#endif

out:
	return ret;
}

static int bq2560x_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct bq2560x_charger_info *info = rdev_get_drvdata(dev);

	if (!info->init_finished) {
		bq_info("%s bq2560x is not inited", __func__);
		return 0;
	}

	bq_info("%s vbus_is_enabled %d\n", __func__, info->otg_enable);

	return info->otg_enable;
}

static const struct regulator_ops bq2560x_charger_vbus_ops = {
	.enable = bq2560x_charger_enable_otg,
	.disable = bq2560x_charger_disable_otg,
	.is_enabled = bq2560x_charger_vbus_is_enabled,
};

static const struct regulator_desc bq2560x_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &bq2560x_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int
bq2560x_charger_register_vbus_regulator(struct bq2560x_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = info->dev;
	cfg.driver_data = info;
	reg = devm_regulator_register(info->dev,
				      &bq2560x_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		bq_err("Can't register regulator:%d\n", ret);
	}

	info->reg = reg;

	return ret;
}

#else
static int
bq2560x_charger_register_vbus_regulator(struct bq2560x_charger_info *info)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
#if !(IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX))
static int zte_sqc_set_prop_by_name(const char *name, enum zte_power_supply_property psp, int data)
{
	struct zte_power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_info("bq2560x:psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = zte_power_supply_get_by_name(name);
	if (!psy) {
		pr_info("bq2560x:get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = zte_power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("bq2560x:Failed to set %s property:%d rc=%d\n", name, psp, rc);
		return rc;
	}

	zte_power_supply_put(psy);

	return 0;

failed_loop:
	return -EINVAL;
}
#endif

static int sqc_mp_get_chg_type(void *arg, unsigned int *chg_type)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;

	arg = arg ? arg : NULL;
	if (info && info->support_single_buck_9V)
		*chg_type = SQC_PMIC_TYPE_BUCK_5A;
	else
		*chg_type = SQC_PMIC_TYPE_BUCK;

	return 0;
}

static int sqc_mp_get_chg_status(void *arg, unsigned int *charing_status)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;

	*charing_status = info->chg_status;

	return 0;
}

static int sqc_mp_set_enable_chging(void *arg, unsigned int en)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;

	if (en) {
#ifndef ZTE_FEATURE_PV_AR
		bq2560x_charger_set_watchdog_timer(info, 80);
		bq2560x_charger_feed_watchdog(info, 1);
#endif
		if (info->hw_mode == BQ25601_SLAVE) {
			if (info->gpiod)
				gpiod_set_value_cansleep(info->gpiod, 1);
			bq_info("bq2560x[REG]: set battery hiz false\n");
			bq2560x_charger_enable_hiz(info, false);
		}

		bq2560x_charger_start_charge(info);

		info->charging = true;

		bq2560x_charger_dumper_reg(info);
	} else {
		bq2560x_charger_stop_charge(info);
#ifndef ZTE_FEATURE_PV_AR
		bq2560x_charger_set_watchdog_timer(info, 0);
#endif
		bq2560x_charger_dumper_reg(info);
		if (info->hw_mode == BQ25601_SLAVE) {
			bq_info("bq2560x[REG]: set battery hiz true\n");
			bq2560x_charger_enable_hiz(info, true);
			if (info->gpiod)
				gpiod_set_value_cansleep(info->gpiod, 0);
		}

		info->charging = false;
	}

	bq_info("bq2560x %s %d\n", __func__, en);

	return 0;
}

static int sqc_mp_get_enable_chging(void *arg, unsigned int *en)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_charge_status(info, en);

	bq_info("bq2560x %s %d\n", __func__, *en);

	return ret;
}

static int sqc_mp_get_ichg(void *arg, u32 *ichg_ma)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_current(info, ichg_ma);

	*ichg_ma = *ichg_ma / 1000;

	bq_info("bq2560x %s %d\n", __func__, *ichg_ma);

	return ret;
}

static int sqc_mp_set_ichg(void *arg, u32 mA)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_current(info, mA * 1000);

	bq_info("bq2560x %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_set_ieoc(void *arg, u32 mA)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_termina_cur(info, mA);

	bq_info("bq2560x %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_get_ieoc(void *arg, u32 *ieoc)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_termia_cur(info, ieoc);

	*ieoc = *ieoc / 1000;

	bq_info("bq2560x %s %d\n", __func__, *ieoc);

	return ret;
}

static int sqc_mp_get_aicr(void *arg, u32 *aicr_ma)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_limit_current(info, aicr_ma);

	bq_info("bq2560x %s %d\n", __func__, *aicr_ma);

	return ret;
}

static int sqc_mp_set_aicr(void *arg, u32 aicr_ma)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_limit_current(info, aicr_ma * 1000);

	bq_info("bq2560x %s %d\n", __func__, aicr_ma);

	return ret;
}

#ifdef CONFIG_TYPEC_PD_CTS_TEST
void temp_set_limit_current(u32 limit_cur)
{
	if (bq2560x_master_data)
		bq2560x_charger_set_limit_current(bq2560x_master_data, limit_cur);
	pr_info("bq2560x %s %d\n", __func__, limit_cur);
}
EXPORT_SYMBOL(temp_set_limit_current);

void temp_set_ichg(u32 mA)
{
	if (bq2560x_master_data)
		bq2560x_charger_set_current(bq2560x_master_data, mA);
	pr_info("bq2560x %s %d\n", __func__, mA);
}
EXPORT_SYMBOL(temp_set_ichg);
#endif

static int sqc_mp_get_cv(void *arg, u32 *cv_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_termina_vol(info, cv_mv);

	bq_info("bq2560x %s %d\n", __func__, *cv_mv);

	return ret;
}

static int sqc_mp_set_cv(void *arg, u32 cv_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_termina_vol(info, cv_mv);

	bq_info("bq2560x %s %d\n", __func__, cv_mv);

	return ret;
}

static int sqc_mp_get_rechg_voltage(void *arg, u32 *rechg_volt_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_recharge_voltage(info, rechg_volt_mv);

	*rechg_volt_mv = *rechg_volt_mv / 1000;

	bq_info("bq2560x %s %d\n", __func__, *rechg_volt_mv);

	return ret;
}

static int sqc_mp_set_rechg_voltage(void *arg, u32 rechg_volt_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_recharge_voltage(info, rechg_volt_mv * 1000);

	bq_info("bq2560x %s %d\n", __func__, rechg_volt_mv);

	return ret;
}

static int sqc_mp_ovp_volt_get(void *arg, u32 *ac_ovp_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_ovp(info, ac_ovp_mv);

	bq_info("bq2560x %s %d\n", __func__, *ac_ovp_mv);

	return ret;
}

static int sqc_mp_ovp_volt_set(void *arg, u32 ac_ovp_mv)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_ovp(info, ac_ovp_mv);

	bq_info("bq2560x %s %d\n", __func__, ac_ovp_mv);

	return ret;
}

static int sqc_mp_get_vbat(void *arg, u32 *mV)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	union power_supply_propval batt_vol_uv;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n", __func__);
		return ret1;
	}

	*mV = batt_vol_uv.intval / 1000;

	return 0;
}

static int sqc_mp_get_ibat(void *arg, u32 *mA)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_CURRENT_NOW failed!\n", __func__);
		return ret1;
	}

	*mA = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_vbus(void *arg, u32 *mV)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ2560X_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		bq_err("%s: get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n", __func__);
		return ret1;
	}

	*mV = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_ibus(void *arg, u32 *mA)
{
	arg = arg ? arg : NULL;

	*mA = 0;

	return 0;
}

static int sqc_enable_powerpath_set(void *arg, unsigned int enabled)
{
	struct bq2560x_charger_info *chg_dev = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_set_powerpath(chg_dev, enabled);

	return ret;
}

static int sqc_enable_powerpath_get(void *arg, unsigned int *enabled)
{
	struct bq2560x_charger_info *chg_dev = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_powerpath(chg_dev, enabled);

	return ret;
}

static int sqc_enable_hiz_set(void *arg, unsigned int enable)
{
	struct bq2560x_charger_info *chg_dev = (struct bq2560x_charger_info *)arg;

	bq2560x_charger_enable_hiz(chg_dev, !!enable);

	return 0;
}

static int sqc_enable_hiz_get(void *arg, unsigned int *enable)
{
	struct bq2560x_charger_info *chg_dev = (struct bq2560x_charger_info *)arg;
	int ret = 0;

	ret = bq2560x_charger_get_hiz_status(chg_dev, enable);

	return ret;
}

static struct sqc_pmic_chg_ops bq2560x_sqc_chg_ops = {

	.init_pmic_charger = NULL,

	.get_chg_type = sqc_mp_get_chg_type,
	.get_chg_status = sqc_mp_get_chg_status,


	.chg_enable = sqc_mp_set_enable_chging,
	.chg_enable_get = sqc_mp_get_enable_chging,

	.set_chging_fcv = sqc_mp_set_cv,
	.get_chging_fcv = sqc_mp_get_cv,
	.set_chging_fcc = sqc_mp_set_ichg,
	.get_chging_fcc = sqc_mp_get_ichg,

	.set_chging_icl = sqc_mp_set_aicr,
	.get_chging_icl = sqc_mp_get_aicr,

	.set_chging_topoff = sqc_mp_set_ieoc,
	.get_chging_topoff = sqc_mp_get_ieoc,

	.set_rechg_volt = sqc_mp_set_rechg_voltage,
	.get_rechg_volt = sqc_mp_get_rechg_voltage,

	.ac_ovp_volt_set = sqc_mp_ovp_volt_set,
	.ac_ovp_volt_get = sqc_mp_ovp_volt_get,

	.batt_ibat_get = sqc_mp_get_ibat,
	.batt_vbat_get = sqc_mp_get_vbat,

	.usb_ibus_get = sqc_mp_get_ibus,
	.usb_vbus_get = sqc_mp_get_vbus,

	.enable_path_set = sqc_enable_powerpath_set,
	.enable_path_get = sqc_enable_powerpath_get,

	.enable_hiz_set = sqc_enable_hiz_set,
	.enable_hiz_get = sqc_enable_hiz_get,
};
#if !(IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX))
extern struct sqc_bc1d2_proto_ops sqc_bc1d2_proto_node;
static int sqc_chg_type = SQC_NONE_TYPE;

static int sqc_bc1d2_get_charger_type(int *chg_type)
{
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)sqc_bc1d2_proto_node.arg;
	int usb_present = 0;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		*chg_type = SQC_NONE_TYPE;
		return 0;
	}

	if (!info->is_charger_online && !info->usb_phy->chg_type) {
		power_supply_get_prop_by_name("usb", POWER_SUPPLY_PROP_PRESENT, &usb_present);
		if (usb_present) {
			*chg_type = SQC_FLOAT_TYPE;
		} else {
			*chg_type = SQC_NONE_TYPE;
			goto out_loop;
		}
	}

	switch (info->usb_phy->chg_type) {
	case SDP_TYPE:
		*chg_type = SQC_SDP_TYPE;
		break;
	case DCP_TYPE:
		*chg_type = SQC_DCP_TYPE;
		break;
	case CDP_TYPE:
		*chg_type = SQC_CDP_TYPE;
		break;
	case ACA_TYPE:
	default:
		*chg_type = SQC_FLOAT_TYPE;
	}

	bq_info("sqc_chg_type: %d, chg_type: %d\n", sqc_chg_type, *chg_type);

	if ((sqc_chg_type == SQC_SLEEP_MODE_TYPE)
			&& (*chg_type == SQC_NONE_TYPE)) {
		sqc_chg_type = *chg_type;
		zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 1);
	} else if (sqc_chg_type == SQC_SLEEP_MODE_TYPE) {
		*chg_type = sqc_chg_type;
	}

out_loop:
	pr_info("[SQC-HW]: [%s] is_charger_online: %d, sprd_type: %d, chg_type: %d\n",
		__func__, info->is_charger_online, info->usb_phy->chg_type, *chg_type);

	return 0;
}


struct sqc_bc1d2_proto_ops sqc_bc1d2_proto_node = {
	.status_init = NULL,
	.status_remove = NULL,
	.get_charger_type = sqc_bc1d2_get_charger_type,
	.set_charger_type = NULL,
	.get_protocol_status = NULL,
	.get_chip_vendor_id = NULL,
	.set_qc3d0_dp = NULL,
	.set_qc3d0_dm = NULL,
	.set_qc3d0_plus_dp = NULL,
	.set_qc3d0_plus_dm = NULL,
};

int sqc_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0, ret = 0;
	struct bq2560x_charger_info *info = (struct bq2560x_charger_info *)sqc_bc1d2_proto_node.arg;

	sscanf(val, "%d", &sleep_mode_enable);

	bq_info("sleep_mode_enable = %d\n", sleep_mode_enable);

	if (sleep_mode_enable) {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			bq_info("sleep on status");

			/*disabel sqc-daemon*/
			sqc_chg_type = SQC_SLEEP_MODE_TYPE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);

			ret = bq2560x_charger_set_watchdog_timer(info, 0);
			if (ret < 0)
				bq_err("failed to set watchdog timer\n");

			/*mtk enter sleep mode*/
			zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);

		}
	} else {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			bq_info("sleep off status");
			sqc_chg_type = SQC_NONE_TYPE;
		}
	}

	return 0;
}

int sqc_sleep_node_get(char *val, const void *arg)
{
	int sleep_mode = 0;

	if (sqc_chg_type == SQC_SLEEP_MODE_TYPE)
		sleep_mode = 1;

	return snprintf(val, PAGE_SIZE, "%u", sleep_mode);
}

static struct zte_misc_ops qc3dp_sleep_mode_node = {
	.node_name = "qc3dp_sleep_mode",
	.set = sqc_sleep_node_set,
	.get = sqc_sleep_node_get,
	.free = NULL,
	.arg = NULL,
};
#endif

#endif

static ssize_t bq2560x_show_registers(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[512];
	int len;
	int idx = 0;
	int ret ;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "Master Reg");
	if (!bq2560x_master_data) {
		pr_err("bq2560x_master_data null\n");
		return idx;
	}

	for (addr = 0x0; addr <= 0x0B; addr++) {
		ret = bq2560x_read(bq2560x_master_data, addr, &val);
		if (ret >= 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	if (bq2560x_slave_data) {
		len = snprintf(tmpbuf, PAGE_SIZE - idx, "%s:\n", "SLAVE Reg");
		memcpy(&buf[idx], tmpbuf, len);
		idx += len;
		for (addr = 0x0; addr <= 0x0B; addr++) {
			ret = bq2560x_read(bq2560x_slave_data, addr, &val);
			if (ret >= 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2x] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t bq2560x_store_registers(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int no = 0;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%d %x %x", &no, &reg, &val);
	if (ret != 3) {
		pr_err("invalid parameters, echo charger_no reg val > register\n");
		goto end;
	}

	pr_info("%s write charger no:%d, reg:%x, val:%x\n", __func__, no, reg, val);
	if (no == 0 && reg < 0x0B) {
		if (bq2560x_master_data)
			bq2560x_write(bq2560x_master_data, (unsigned char)reg, (unsigned char)val);
	} else if (no == 1 && reg < 0x0B) {
		if (bq2560x_slave_data)
			bq2560x_write(bq2560x_slave_data, (unsigned char)reg, (unsigned char)val);
		else
			pr_err("no slave charger\n");
	} else {
		pr_err("no or reg parameters invalid\n");
	}

end:
	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, bq2560x_show_registers, bq2560x_store_registers);

static struct attribute *bq2560x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group bq2560x_attr_group = {
	.attrs = bq2560x_attributes,
};

static int bq2560x_chip_name_show(struct seq_file *m, void *v)
{
	struct bq2560x_charger_info *pinfo = m->private;

	if (pinfo) {
		seq_printf(m, "%s\n", pinfo->name);
	} else {
		seq_printf(m, "%s\n", "unknown");
	}
	return 0;
}

static int bq2560x_chip_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, bq2560x_chip_name_show, PDE_DATA(inode));
}

static const struct proc_ops bq2560x_chip_name_node = {
	.proc_open = bq2560x_chip_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};

static int bq2560x_check_is_upm6910h(struct bq2560x_charger_info *info)
{
	u8 reg_value = 0;
	int ret = -1;

	ret = bq2560x_read_no_record(info, UPM6910H_REG_A7, &reg_value);
	if (ret < 0) {
		bq_info("%s read UPM6910H_REG_A7 no ack \n", __func__);
	} else {
		bq_info("%s Reg[A7] reg_value = %d \n", __func__, reg_value);
	}

	if (reg_value == 5) {
		ret = 1;
	} else {
		ret = -1;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_BQ2560X_BQ25890_SUPPORT)
static int bq2560x_check_is_bq25890(struct bq2560x_charger_info *info)
{
	int ret = 0, part_id = -EINVAL;
	u8 reg_val;

	ret = bq2560x_read_no_retry(info, BQ25890_REG_14, &reg_val);
	if (ret < 0) {
		bq_err("failed to get REG_14 value, ret = %d\n", ret);
		return 0;
	}

	part_id = (reg_val & REG14_PN_MASK) >> REG14_PN_SHIFT;
	if (part_id != BQ25890_DEV_ID && part_id != BQ25892_DEV_ID) {
		bq_err("REG_14 PN id is 0x%x\n", part_id);
		return 0;
	}

	return 1;
}
#endif


static int bq2560x_get_chip_vendor_id(struct bq2560x_charger_info *info)
{
	u8 reg_value = 0;
	u8 pn = 0, sub_id = 0, addr_change = 0;
	int ret = -1;

#if IS_ENABLED(CONFIG_BQ2560X_BQ25890_SUPPORT)
	int is_bq25890 = bq2560x_check_is_bq25890(info);
	if (is_bq25890) {
		bq_err("PN id is bq25890, return\n");
		return -EINVAL;
	}
#endif

	ret = bq2560x_read_no_record(info, BQ2560X_REG_0B, &reg_value);

	pn = (reg_value & BQ2560X_REG0B_PN_MASK) >> BQ2560X_REG0B_PN_SHIFT;
	sub_id = (reg_value & BQ2560X_REG0B_VENDOR_ID_MASK) >> BQ2560X_REG0B_VENDOR_ID_SHIFT;

	if (ret < 0) {
		bq_err("%s: failed to get 0x6B vendor id ret=%d\n", __func__, ret);
		info->chip_main_id = -1;
		info->chip_sub_id = -1;
		info->client->addr = SGM41513_I2C_ADDR;
		addr_change = 1;
	} else {
		info->chip_main_id = pn;
		info->chip_sub_id = sub_id;
	}

	if(addr_change) {
		ret = bq2560x_read(info, BQ2560X_REG_0B, &reg_value);

		pn = (reg_value & BQ2560X_REG0B_PN_MASK) >> BQ2560X_REG0B_PN_SHIFT;
		sub_id = (reg_value & BQ2560X_REG0B_VENDOR_ID_MASK) >> BQ2560X_REG0B_VENDOR_ID_SHIFT;

		if (ret < 0) {
			bq_err("%s: failed to get 0x1A vendor id ret=%d\n", __func__, ret);
			info->chip_main_id = -1;
			info->chip_sub_id = -1;
		} else {
			info->chip_main_id = pn;
			info->chip_sub_id = sub_id;
		}
		addr_change = 0;

		if (info->chip_main_id == SGM41513_VENDOR_ID) {
			info->name = "sgm41513";
		}  else if (info->chip_main_id == SGM41513A_VENDOR_ID) {
			info->name = "sgm41513A";
		} else {
			info->name = "unknown";
		}
	} else {
		if (info->chip_main_id == SGM41511_UPM6910_VENDOR_ID) {
			if (info->chip_sub_id == 1 || info->chip_sub_id == 0) {
				if (bq2560x_check_is_upm6910h(info) < 0 ) {
					info->name = "sgm41511";
				} else {
					info->name = "upm6910h";
				}
			}
		} else if (info->chip_main_id == SY6974_VENDOR_ID) {
			info->name = "sy6974";
		} else if (info->chip_main_id == SGM41516S_VENDOR_ID) {
			info->name = "sgm41516S";
		} else if (info->chip_main_id == SGM41516D_VENDOR_ID) {
			info->name = "sgm41516D";
		} else {
			info->name = "unknown";
		}
	}

	bq_info("%s reg=0x%x, value=0x%x pn=0x%x sub_id=0x%x chg_IC_name=%s\n",
		__func__, BQ2560X_REG_0B, reg_value, pn, sub_id, info->name);

#if IS_ENABLED(CONFIG_BQ2560X_BQ25890_SUPPORT)
	if (!strncmp(info->name, "unknown", strlen("unknown")))
		return -EINVAL;
#endif
	return ret;
}

static int bq2560x_slave_chg_id_get(char *val, const void *arg)
{
	pr_info("%s,slave_chg_id: %d\n", __func__, slave_chg_id);

	return snprintf(val, PAGE_SIZE, "%u", slave_chg_id);
}

static struct zte_misc_ops slave_chg_id_node = {
	.node_name = "slave_chg_id",
	.set = NULL,
	.get = bq2560x_slave_chg_id_get,
	.free = NULL,
	.arg = NULL,
};

static int bq2560x_charger_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct bq2560x_charger_info *info = NULL;
	struct device_node *regmap_np = NULL;
	struct platform_device *regmap_pdev = NULL;
	struct sqc_pmic_chg_ops *sqc_ops = NULL;
	struct power_supply_config charger_cfg= { };
	int ret = 0;

	pr_info("bq2560x:%s enter\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_err("No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -EPROBE_DEFER;
	info->name = "unkown";
	info->client = client;
	info->dev = dev;
	info->init_finished = 0;
	info->is_charging_enabled = false;
	info->batfet_rst_en = false;
	mutex_init(&info->lock);

	pr_err("%s:client addr is %x\n", __func__, info->client->addr);
#if IS_ENABLED(CONFIG_BQ2560X_BQ25890_SUPPORT)
	ret = bq2560x_get_chip_vendor_id(info);
	if (ret) {
		dev_err(info->dev, "%s, failed to get device id, ret = %d\n", __func__, ret);
		mutex_destroy(&info->lock);
		return ret;
	}
#else
	bq2560x_get_chip_vendor_id(info);
#endif

	info->use_typec_extcon = device_property_read_bool(dev, "use-typec-extcon");
	info->enable_driver_icl_vote = device_property_read_bool(dev, "driver-icl-vote");
	info->batfet_rst_en = device_property_read_bool(dev, "batfet-rst-en");

	info->host_status_check = device_property_read_bool(dev, "host-status-check");
	info->support_single_buck_9V = device_property_read_bool(dev, "support-single-buck-9V");
	bq_info("support_single_buck_9V=%d\n", info->support_single_buck_9V);
	info->disable_ship_mode = device_property_read_bool(dev, "disable-ship-mode");
	bq_info("disable_ship_mode=%d\n", info->disable_ship_mode);
	ret = device_property_read_bool(dev, "role-slave");
	if (ret)
		info->hw_mode = BQ25601_SLAVE;
	else
		info->hw_mode = BQ25601_MASTER;

	bq_info("dtsmode=%d, addr=0x%02X\n", info->hw_mode, client->addr);


	i2c_set_clientdata(client, info);

	info->usb_phy = devm_usb_get_phy_by_phandle(dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		bq_err("failed to find USB phy\n");
		goto GET_USB_PHY_FAILED;
	}

	info->edev = extcon_get_edev_by_phandle(info->dev, 0);
	if (IS_ERR(info->edev)) {
		bq_err("failed to find vbus extcon device.\n");
		return -EPROBE_DEFER;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		ret = bq2560x_charger_register_vbus_regulator(info);
		if (ret) {
			bq_err("failed to register vbus regulator.\n");
			return -EPROBE_DEFER;
		}
	}

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,sc27xx-syscon");
	if (!regmap_np)
		regmap_np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");

	if (regmap_np) {
		if (of_device_is_compatible(regmap_np->parent, "sprd,sc2721"))
			info->charger_pd_mask = BQ2560X_DISABLE_PIN_MASK_2721;
		else
			info->charger_pd_mask = BQ2560X_DISABLE_PIN_MASK;
	} else {
		bq_err("unable to get syscon node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 1,
					 &info->charger_detect);
	if (ret) {
		bq_err("failed to get charger_detect\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 2,
					 &info->charger_pd);
	if (ret) {
		bq_err("failed to get charger_pd reg\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dev->of_node, "vindpm-value", &info->vindpm_value) >= 0) {
		bq_info("vindpm-value use value is %d\n", info->vindpm_value);
	} else {
		bq_err("failed to get vindpm-value use default value %d\n", VCHG_CTRL_THRESHOLD_MV_072);
		info->vindpm_value = VCHG_CTRL_THRESHOLD_MV_072;
	}

	if (of_property_read_u32(dev->of_node, "boost-limit-value", &info->boost_limit) >= 0) {
		bq_info("boost-limit-value use value is %d\n", info->boost_limit);
	}

	info->gpiod = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(info->gpiod)) {
		bq_err("failed to get enable gpiod\n");
		info->gpiod = NULL;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if (!regmap_pdev) {
		of_node_put(regmap_np);
		bq_err("unable to get syscon device\n");
		return -ENODEV;
	}

	of_node_put(regmap_np);
	info->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!info->pmic) {
		bq_err("unable to get pmic regmap device\n");
		return -ENODEV;
	}

	INIT_DELAYED_WORK(&info->update_work, update_workfunc);

	info->usb_notify.notifier_call = bq2560x_charger_usb_change;
	ret = usb_register_notifier(info->usb_phy, &info->usb_notify);
	if (ret) {
		bq_err("failed to register notifier:%d\n", ret);
		goto GET_USB_PHY_FAILED;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		charger_cfg.drv_data = info;
		charger_cfg.of_node = dev->of_node;
		info->psy_usb = devm_power_supply_register(dev,
							   &bq2560x_charger_desc,
							   &charger_cfg);
		if (IS_ERR(info->psy_usb)) {
			bq_err("failed to register power supply\n");
			goto USB_REGISTER_NOTIFIER_FAILED;
		}

		info->zte_psy_usb = zte_devm_power_supply_register(dev,
						   &zte_bq2560x_charger_desc,
						   &charger_cfg);
		if (IS_ERR(info->zte_psy_usb)) {
			dev_err(dev, "failed to register zte power supply\n");
		}
	}

	if (info->hw_mode == BQ25601_SLAVE) {
		slave_chg_retry_counter ++;
		pr_err("slave_chg_retry_counter=%d\n", slave_chg_retry_counter);
	}

	ret = bq2560x_charger_hw_init(info);
	if (ret) {
		pr_err("bq2560x_charger_hw_init failed ret=%d\n", ret);
		goto USB_REGISTER_NOTIFIER_FAILED;
	}

	if (info->hw_mode == BQ25601_SLAVE) {
		charger_cfg.drv_data = info;
		charger_cfg.of_node = dev->of_node;

		info->zte_psy_usb = zte_devm_power_supply_register(dev,
						   &bq2560x_slave_charger_desc,
						   &charger_cfg);
		if (IS_ERR(info->zte_psy_usb)) {
			dev_err(dev, "failed to register zte power supply\n");
		}
	}


	if (info->hw_mode == BQ25601_MASTER) {
		proc_create_data("driver/chg_name", 0664, NULL,
			&bq2560x_chip_name_node, info);
	}

	alarm_init(&info->otg_timer, ALARM_BOOTTIME, NULL);
	if (info->enable_driver_icl_vote)
		alarm_init(&info->icl_vote_timer, ALARM_BOOTTIME, icl_vote_timer_func);
	INIT_DELAYED_WORK(&info->otg_work, bq2560x_charger_otg_work);
	INIT_DELAYED_WORK(&info->wdt_work,
			  bq2560x_charger_feed_watchdog_work);
	INIT_DELAYED_WORK(&info->vindpm_work, tuning_vindpm_work);
	INIT_DELAYED_WORK(&info->force_recharge_work, force_recharge_workfunc);

	ret = sysfs_create_group(&info->dev->kobj, &bq2560x_attr_group);
	if (ret) {
		bq_err("failed to register sysfs. err\n");
	}

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	if (info->hw_mode == BQ25601_SLAVE) {
		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq2560x_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)info;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PARALLEL1);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto USB_REGISTER_NOTIFIER_FAILED;
		}
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		info->zlog_client = zlog_register_client(&zlog_bc_dev2);
		if (!info->zlog_client)
			pr_err("%s zlog register client zlog_bc_dev2 fail\n", __func__);
		else
			pr_err("%s zlog register client zlog_bc_dev2 success\n", __func__);
#endif
	} else {

		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq2560x_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)info;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PRIMARY);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto USB_REGISTER_NOTIFIER_FAILED;
		}
#if !(IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX))
		sqc_bc1d2_proto_node.arg = (void *)info;
		sqc_hal_bc1d2_register(&sqc_bc1d2_proto_node);

		zte_misc_register_callback(&qc3dp_sleep_mode_node, info);
#endif

	}
#endif

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		info->zlog_client = zlog_register_client(&zlog_bc_dev1);
		if (!info->zlog_client)
			pr_err("%s zlog register client zlog_bc_dev1 fail\n", __func__);
		else
			pr_err("%s zlog register client zlog_bc_dev1 success\n", __func__);
#endif

	if (info->hw_mode == BQ25601_SLAVE) {
		slave_chg_id = 1;
		bq2560x_slave_data = info;
		bq_info("regist slave_id node!\n");
		zte_misc_register_callback(&slave_chg_id_node, info);
	} else {
		bq2560x_master_data = info;
	}

	info->suspended = false;
	device_init_wakeup(info->dev, true);

	if (info->hw_mode == BQ25601_MASTER && info->usb_phy->chg_state == USB_CHARGER_PRESENT) {
		schedule_delayed_work(&info->force_recharge_work, msecs_to_jiffies(13000));
	}

	bq_err("%s ok, use_typec_extcon = %d\n", __func__, info->use_typec_extcon);

	info->init_finished = 1;

	return 0;
USB_REGISTER_NOTIFIER_FAILED:
	usb_unregister_notifier(info->usb_phy, &info->usb_notify);
GET_USB_PHY_FAILED:
	mutex_destroy(&info->lock);
	if (info)
		devm_kfree(info->dev, info);

	if (slave_chg_retry_counter == 10) {
		pr_err("slave chg probe failed retry over\n");
		return -ENODEV;
	}

	return -EPROBE_DEFER;
}

static void bq2560x_charger_shutdown(struct i2c_client *client)
{
	struct bq2560x_charger_info *info = i2c_get_clientdata(client);
	int ret = 0;

	dev_info(info->dev, "%s enter!\n", __func__);

	bq2560x_charger_enable_hiz(info, false);
	bq2560x_charger_set_vindpm(info, DEFAULT_VINDPM_MV);

	cancel_delayed_work_sync(&info->wdt_work);

	if (info->otg_enable) {
		cancel_delayed_work_sync(&info->otg_work);
		ret = bq2560x_update_bits(info, BQ2560X_REG_01,
					  BQ2560X_REG_OTG_MASK,
					  0);
		if (ret)
			dev_err(info->dev, "disable bq2560x otg failed ret = %d\n", ret);

		/* Enable charger detection function to identify the charger type */
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);

		if (ret)
			dev_err(info->dev,
				"enable charger detection function failed ret = %d\n", ret);
	}
}

static int bq2560x_charger_remove(struct i2c_client *client)
{
	struct bq2560x_charger_info *info = i2c_get_clientdata(client);

	info->init_finished = 0;

	usb_unregister_notifier(info->usb_phy, &info->usb_notify);

	device_init_wakeup(info->dev, false);

	return 0;
}


#if IS_ENABLED(CONFIG_PM_SLEEP)
static int bq2560x_charger_alarm_prepare(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);
	ktime_t now, add;

	if (!info) {
		pr_err("%s: info is null!\n", __func__);
		return 0;
	}

	if (!info->otg_enable)
		return 0;

	now = ktime_get_boottime();
	add = ktime_set(BQ2560X_OTG_ALARM_TIMER_S, 0);
	alarm_start(&info->otg_timer, ktime_add(now, add));
	return 0;
}

static void bq2560x_charger_alarm_complete(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!info->otg_enable)
		return;

	alarm_cancel(&info->otg_timer);
}

static int bq2560x_charger_suspend(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (info->otg_enable)
		bq2560x_charger_feed_watchdog(info, 1);

	info->suspended = true;

	if (!info->otg_enable)
		return 0;

	cancel_delayed_work_sync(&info->wdt_work);
	return 0;
}

static int bq2560x_charger_resume(struct device *dev)
{
	struct bq2560x_charger_info *info = dev_get_drvdata(dev);
	bool otg_valid = false;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (info->otg_enable)
		bq2560x_charger_feed_watchdog(info, 1);

	info->suspended = false;

	/* The OTG is not really disabled, when it was removed in sleep mode */
	if ((!info->otg_enable) && (info->reg)){
		otg_valid = bq2560x_charger_check_otg_valid(info);
		if (otg_valid) {
			bq2560x_charger_disable_otg(info->reg);
		}
	}

	if (!info->otg_enable)
		return 0;

	schedule_delayed_work(&info->wdt_work, HZ * 15);

	return 0;
}
#endif

static const struct dev_pm_ops bq2560x_charger_pm_ops = {
	.prepare = bq2560x_charger_alarm_prepare,
	.complete = bq2560x_charger_alarm_complete,
	SET_SYSTEM_SLEEP_PM_OPS(bq2560x_charger_suspend,
				bq2560x_charger_resume)
};
static struct i2c_driver bq2560x_charger_driver = {
	.driver = {
		.name = "bq2560x_chg",
		.of_match_table = bq2560x_charger_of_match,
		.pm = &bq2560x_charger_pm_ops,
	},
	.probe = bq2560x_charger_probe,
	.shutdown = bq2560x_charger_shutdown,
	.remove = bq2560x_charger_remove,
	.id_table = bq2560x_i2c_id,
};

module_i2c_driver(bq2560x_charger_driver);
MODULE_DESCRIPTION("BQ2560X Charger Driver");
MODULE_LICENSE("GPL v2");
