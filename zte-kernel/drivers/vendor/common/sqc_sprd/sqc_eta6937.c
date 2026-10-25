/*
 * Driver for the ETA Solutions eta6937 charger.
 * Author: Jinfeng.Lin1 <jinfeng.lin1@unisoc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/alarmtimer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/power/sprd_battery_info.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pm_wakeup.h>
#include <linux/proc_fs.h>

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>
int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

#define ETA6937_REG_0					0x0
#define ETA6937_REG_1					0x1
#define ETA6937_REG_2					0x2
#define ETA6937_REG_3					0x3
#define ETA6937_REG_4					0x4
#define ETA6937_REG_5					0x5
#define ETA6937_REG_6					0x6
#define ETA6937_REG_7					0x7
/* Register bits */
/* ETA6937_REG_0 (0x00) status and control reg*/
#define ETA6937_REG_TMR_RST_OTG_STAT_MASK		GENMASK(7, 7)
#define ETA6937_REG_TMR_RST_OTG_STAT_SHIFT		(7)
#define ETA6937_REG_EN_STAT_MASK			GENMASK(6, 6)
#define ETA6937_REG_EN_STAT_SHIFT			(6)
#define ETA6937_REG_STAT1_STAT2_MASK			GENMASK(5, 4)
#define ETA6937_REG_STAT1_STAT2_SHIFT			(4)
#define ETA6937_REG_BOOST_MASK				GENMASK(3, 3)
#define ETA6937_REG_BOOST_SHIFT				(3)
#define ETA6937_REG_FAULT_MASK				GENMASK(2, 0)
#define ETA6937_REG_FAULT_SHIFT				(0)

/* FAN5405_REG_1 (0x01) control reg */
#define ETA6937_REG_IIN_LIMIT_1_MASK			GENMASK(7, 6)
#define ETA6937_REG_IIN_LIMIT_1_SHIFT			(6)
#define ETA6937_REG_VLOWV_MASK				GENMASK(5, 4)
#define ETA6937_REG_VLOWV_SHIFT				(4)
#define ETA6937_REG_TE_MASK				GENMASK(3, 3)
#define ETA6937_REG_TE_SHIFT				(3)
#define ETA6937_REG_NCE_MASK				GENMASK(2, 2)
#define ETA6937_REG_NCE_SHIFT				(2)
#define ETA6937_REG_HZ_MODE_MASK			GENMASK(1, 1)
#define ETA6937_REG_HZ_MODE_SHIFT			(1)
#define ETA6937_EN_HIZ				(1)
#define ETA6937_EXIT_HIZ			(0)
#define ETA6937_REG_OPA_MODE_MASK			GENMASK(0, 0)
#define ETA6937_REG_OPA_MODE_SHIFT			(0)
#define ETA6937_REG_OPA_MODE_DISABLE			0
#define ETA6937_REG_OPA_MODE_ENABLE			1

/* ETA6937_REG_2 (0x02) control and battery voltage reg*/
#define ETA6937_REG_VOREG_MASK				GENMASK(7, 2)
#define ETA6937_REG_VOREG_SHIFT				(2)
#define ETA6937_REG_OTG_PL_MASK				GENMASK(1, 1)
#define ETA6937_REG_OTG_PL_SHIFT			(1)
#define ETA6937_REG_OTG_EN_MASK				GENMASK(0, 0)
#define ETA6937_REG_OTG_EN_SHIFT			(0)

/* ETA6937_REG_3 (0x03) Vendor/Part/Revision reg */
#define ETA6937_REG_VENDOR_CODE_MASK			GENMASK(7, 5)
#define ETA6937_REG_VENDOR_CODE_SHIFT			(5)
#define ETA6937_REG_PN_CODE_MASK			GENMASK(4, 3)
#define ETA6937_REG_PN_CODE_SHIFT			(3)
#define ETA6937_REG_REV_CODE_MASK			GENMASK(2, 0)
#define ETA6937_REG_REV_CODE_SHIFT			(0)
#define ETA6947_DEV_ID					6
#define ETA6937_DEV_ID					0xA

/* ETA6937_REG_4 (0x04) battery termination and fast charge current reg*/
#define ETA6937_REG_RESET_MASK				GENMASK(7, 7)
#define ETA6937_REG_RESET_SHIFT				(7)
#define ETA6937_REG_ICHG_MASK				GENMASK(6, 4)
#define ETA6937_REG_ICHG_SHIFT				(4)
#define ETA6937_REG_ICHG_OFFSET_MASK			GENMASK(3, 3)
#define ETA6937_REG_ICHG_OFFSET_SHIFT			(3)
#define ETA6937_REG_ITERM_MASK				GENMASK(2, 0)
#define ETA6937_REG_ITERM_SHIFT				(0)
#define ETA6937_REG_ITERM_MIN				50
#define ETA6937_REG_ITERM_MAX				400
#define ETA6937_REG_ITERM_BASE				50
#define ETA6937_REG_ITERM_LSB				50


/* ETA6937_REG_5 (0x05) special charger voltage and enable pin status reg*/
#define ETA6937_REG_ICHG_4_MASK				GENMASK(7, 7)
#define ETA6937_REG_ICHG_4_SHIFT			(7)
#define ETA6937_REG_ICHG_3_MASK				GENMASK(6, 6)
#define ETA6937_REG_ICHG_3_SHIFT			(6)
#define ETA6937_REG_LOW_CHG_MASK			GENMASK(5, 5)
#define ETA6937_REG_LOW_CHG_SHIFT			(5)
#define ETA6937_REG_DPM_STATUS_MASK		        GENMASK(4, 4)
#define ETA6937_REG_DPM_STATUS_SHIFT			(4)
#define ETA6937_REG_CD_STATUS_MASK			GENMASK(3, 3)
#define ETA6937_REG_CD_STATUS_SHIFT			(3)
#define ETA6937_REG_VINDPM_MASK				GENMASK(2, 0)
#define ETA6937_REG_VINDPM_SHIFT			(0)
#define ETA6937_VINDPM_MAX 14360
#define ETA6937_VINDPM_BASE 4200
#define ETA6937_VINDPM_STEP 80

/* ETA6937_REG_6 (0x06) savety limit reg*/
#define ETA6937_REG_IMCHRG_MASK				GENMASK(7, 4)
#define ETA6937_REG_IMCHRG_SHIFT			(4)
#define ETA6937_REG_VMREG_MASK				GENMASK(3, 0)
#define ETA6937_REG_VMREG_SHIFT				(0)

/* ETA6937_REG_7 (0x07) etra current limit and DPM Level Setting reg */
#define ETA6937_REG_VINDPM_ADD_MASK                     GENMASK(7, 4)
#define ETA6937_REG_VINDPM_ADD_SHIFT			(4)
#define ETA6937_REG_EN_ILIM_2_MASK			GENMASK(3, 3)
#define ETA6937_REG_EN_ILIM_2_SHIFT			(3)
#define ETA6937_REG_IIN_LIMIT_2_MASK			GENMASK(2, 0)
#define ETA6937_REG_IIN_LIMIT_2_SHIFT			(0)

#define ETA6937_BATTERY_NAME				"sc27xx-fgu"
#define BIT_DP_DM_BC_ENB				BIT(0)
#define ETA6937_OTG_VALID_MS				(500)
#define ETA6937_FEED_WATCHDOG_VALID_MS			(50)
#define ETA6937_WDG_TIMER_S				(15)

#define ETA6937_OTG_TIMER_FAULT				(0x6)

#define ETA6937_DISABLE_PIN_MASK_2730			BIT(0)
#define ETA6937_DISABLE_PIN_MASK_2721			BIT(15)
#define ETA6937_DISABLE_PIN_MASK_2720			BIT(0)

#define ETA6937_CHG_IMMIN		(550000)
#define ETA6937_CHG_IMSTEP		(200000)
#define ETA6937_CHG_IMMAX		(3050000)
#define ETA6937_CHG_IMIN		(550000)
#define ETA6937_CHG_ISTEP		(100000)
#define ETA6937_CHG_IMAX		(3050000)
#define ETA6937_CHG_IMAX30500		(0x19)

#define ETA6937_CHG_VMREG_MIN		(4200)
#define ETA6937_CHG_VMREG_STEP		(20)
#define ETA6937_CHG_VMREG_MAX		(4440)
#define ETA6937_CHG_VOREG_MIN		(3500)
#define ETA6937_CHG_VOREG_STEP		(20)
#define ETA6937_CHG_VOREG_MAX		(4440)

#define ETA6937_REG_VOREG		(BIT(5) | BIT(3))
#define ETA6937_IIN_LIM_SEL		(1)
#define ETA6937_REG_ICHG_OFFSET		(0)
#define ETA6937_REG_IIN_LIMIT1_MAX	(3000000)

#define ETA6937_MAX_CHG_CUR_MA			3050
#define ETA6937_RECHARGE_VOL_MV				120
#define ETA6937_BATTERY_NAME				"sc27xx-fgu"

#define ENABLE_CHARGE 0
#define DISABLE_CHARGE 1

#define VINDPM_DEFAULT_MV		4600
#define SC27XX_FGU_NAME		"sc27xx-fgu"

static int eta6937_max_chg_cur[] = {
	550000,
	750000,
	950000,
	1150000,
	1350000,
	1550000,
	1750000,
	1950000,
	2150000,
	2350000,
	2550000,
	2750000,
	2950000,
	3050000,
};

static int eta6937_iin_lim1_tbl[] = {
	100000,
	500000,
	800000,
};

static int eta6937_iin_lim2_tbl[] = {
	300000,
	500000,
	800000,
	1200000,
	1500000,
	2000000,
	3000000,
	5000000,
};

struct eta6937_charge_current {
	int sdp_limit;
	int sdp_cur;
	int dcp_limit;
	int dcp_cur;
	int cdp_limit;
	int cdp_cur;
	int unknown_limit;
	int unknown_cur;
	int fchg_limit;
	int fchg_cur;
};

struct eta6937_charger_info {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *psy_usb;
	struct eta6937_charge_current cur;
	struct mutex lock;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	bool charging;
	bool is_charger_online;
	struct delayed_work otg_work;
	struct delayed_work wdt_work;
	struct regmap *pmic;
	u32 charger_detect;
	u32 charger_pd;
	u32 charger_pd_mask;
	//struct gpio_desc *gpiod;
	struct extcon_dev *edev;
	bool otg_enable;
	struct alarm wdg_timer;
	bool need_reinit;
	int termination_cur;

	int chg_status;
	int ac_ovp_mv;
	int hiz_enable;
	int enable_powerpath;
	int vindpm_value;
	bool use_typec_extcon;
	bool enable_dynamic_mivr;
	int vendor_id;
};

static int eta6937_charger_set_limit_current(struct eta6937_charger_info *info, u32 limit_cur);
static int eta6937_charger_dump_register(struct eta6937_charger_info *info);

#if !(IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX))
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
#endif

static int eta6937_read(struct eta6937_charger_info *info, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(info->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}

static int eta6937_write(struct eta6937_charger_info *info, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(info->client, reg, data);
}

static int eta6937_update_bits(struct eta6937_charger_info *info, u8 reg, u8 mask, u8 data)
{
	u8 v;
	int ret;

	ret = eta6937_read(info, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= (data & mask);

	return eta6937_write(info, reg, v);
}

static int eta6937_charger_set_hiz(struct eta6937_charger_info *info, u32 en_hiz)
{
    u8 reg_val = 0;

    if (en_hiz)
        reg_val = ETA6937_EN_HIZ;
    else
        reg_val = ETA6937_EXIT_HIZ;

    return eta6937_update_bits(info, ETA6937_REG_1, ETA6937_REG_HZ_MODE_MASK, reg_val << ETA6937_REG_HZ_MODE_SHIFT);
}

static int eta6937_charger_set_max_batt_reg_vol(struct eta6937_charger_info *info, u32 vol)
{
	u8 reg_val;
	int ret;

	if (vol < ETA6937_CHG_VMREG_MIN)
		vol = ETA6937_CHG_VMREG_MIN;
	else if (vol > ETA6937_CHG_VMREG_MAX)
		vol = ETA6937_CHG_VMREG_MAX;

	reg_val = (vol - ETA6937_CHG_VMREG_MIN) / ETA6937_CHG_VMREG_STEP;

	ret = eta6937_update_bits(info, ETA6937_REG_6, ETA6937_REG_VMREG_MASK,
			   (reg_val << ETA6937_REG_VMREG_SHIFT));

	ret = eta6937_update_bits(info, ETA6937_REG_0, ETA6937_REG_TMR_RST_OTG_STAT_MASK,
			   ETA6937_REG_TMR_RST_OTG_STAT_MASK);

	return eta6937_update_bits(info, ETA6937_REG_6, ETA6937_REG_VMREG_MASK,
			   (reg_val << ETA6937_REG_VMREG_SHIFT));
}

static int eta6937_charger_set_termina_vol(struct eta6937_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < ETA6937_CHG_VOREG_MIN)
		vol = ETA6937_CHG_VOREG_MIN;
	else if (vol > ETA6937_CHG_VOREG_MAX)
		vol =  ETA6937_CHG_VOREG_MAX;

	reg_val = (vol - ETA6937_CHG_VOREG_MIN) / ETA6937_CHG_VOREG_STEP;

	return eta6937_update_bits(info, ETA6937_REG_2, ETA6937_REG_VOREG_MASK,
				   reg_val << ETA6937_REG_VOREG_SHIFT);
}

static int eta6937_charger_get_termina_vol(struct eta6937_charger_info *info, u32* vol)
{
    int ret = 0;
    u8 reg_val = 0;

    ret = eta6937_read(info, ETA6937_REG_2, &reg_val);
    if (ret) {
        pr_err("Failed to read termina_vol\n");
        return ret;
    }

    reg_val = (reg_val & ETA6937_REG_VOREG_MASK) >> ETA6937_REG_VOREG_SHIFT;

    *vol = reg_val * ETA6937_CHG_VOREG_STEP + ETA6937_CHG_VOREG_MIN;

    return 0;
}

static int eta6937_charger_set_max_chg_cur(struct eta6937_charger_info *info, u32 cur)
{
	u8 reg_val;
	int i, ret;

	/*if ICHG_OFFSET = 1, chg cur + 100mA*/
	cur -= 100 * ETA6937_REG_ICHG_OFFSET;
	for (i = 1; i < ARRAY_SIZE(eta6937_max_chg_cur); ++i)
		if (cur < eta6937_max_chg_cur[i])
			break;

	reg_val = i - 1;

	ret = eta6937_update_bits(info, ETA6937_REG_6, ETA6937_REG_IMCHRG_MASK,
			    reg_val << ETA6937_REG_IMCHRG_SHIFT);

	ret = eta6937_update_bits(info, ETA6937_REG_0, ETA6937_REG_TMR_RST_OTG_STAT_MASK,
			   ETA6937_REG_TMR_RST_OTG_STAT_MASK);

	return eta6937_update_bits(info, ETA6937_REG_6, ETA6937_REG_IMCHRG_MASK,
				    reg_val << ETA6937_REG_IMCHRG_SHIFT);
}

static int eta6937_charger_set_current(struct eta6937_charger_info *info, u32 cur)
{
	u8 reg_val;
	int ret;

	/*if ICHG_OFFSET = 1, chg cur + 100mA*/
	cur -= 100 * ETA6937_REG_ICHG_OFFSET;

	if (cur < ETA6937_CHG_IMIN)
		cur = ETA6937_CHG_IMIN;
	else if (cur > ETA6937_CHG_IMAX)
		cur = ETA6937_CHG_IMAX;

	reg_val = (cur - ETA6937_CHG_IMIN) / ETA6937_CHG_ISTEP;
	/*set reg 4 ichg[2:0]*/
	ret = eta6937_update_bits(info, ETA6937_REG_4, ETA6937_REG_ICHG_MASK,
				    (reg_val & 0x7) << ETA6937_REG_ICHG_SHIFT);
	if (ret) {
		dev_err(info->dev, "set eta6937 ichg[2:0] cur failed ret = %d\n", ret);
		return ret;
	}
	/*set reg5 ichg[4:3]*/
	ret = eta6937_update_bits(info, ETA6937_REG_5, ETA6937_REG_ICHG_3_MASK | ETA6937_REG_ICHG_4_MASK,
				    ((reg_val & 0x18) >> 0x3) << ETA6937_REG_ICHG_3_SHIFT);
	if (ret)
		dev_err(info->dev, "set eta6937 ichg[4:3] cur failed ret = %d\n", ret);

	return ret;
}

static int eta6937_charger_get_current(struct eta6937_charger_info *info, u32 *cur)
{
	u8 reg_val = 0, val;
	int ret;

	ret = eta6937_read(info, ETA6937_REG_4, &val);
	if (ret < 0) {
		dev_err(info->dev, "set eta6937 ichg[2:0] cur failed ret = %d\n", ret);
		return ret;
	}

	val &= ETA6937_REG_ICHG_MASK;
	val = val >> ETA6937_REG_ICHG_SHIFT;
	reg_val |= val;

	ret = eta6937_read(info, ETA6937_REG_5, &val);
	if (ret < 0) {
		dev_err(info->dev, "set eta6937 ichg[3:4] cur failed ret = %d\n", ret);
		return ret;
	}

	val &= (ETA6937_REG_ICHG_3_MASK | ETA6937_REG_ICHG_4_MASK);
	val = (val >> ETA6937_REG_ICHG_3_SHIFT << 0x3);
	reg_val |= val;

	if (reg_val > ETA6937_CHG_IMAX30500)
		reg_val = ETA6937_CHG_IMAX30500;

	*cur = reg_val * ETA6937_CHG_ISTEP + ETA6937_CHG_IMIN + 100 * ETA6937_REG_ICHG_OFFSET;

	return 0;
}

/* Started by AICoder, pid:y8efeq77dcm6d8014e6c099b902a6d254e45635b */
/* Started by AICoder, pid:e8036y763dtee6c146e0087aa0b4d641da70c691 */
static int eta6937_charger_start_charge(struct eta6937_charger_info *info)
{
    int ret = 0;
    u8 reg_val = 0x00;

    pr_info("eta6937: start charge #####\n");

    ret = eta6937_update_bits(info, ETA6937_REG_1,
                ETA6937_REG_NCE_MASK,
                reg_val << ETA6937_REG_NCE_SHIFT);
    if (ret) {
        pr_err("enable eta6937 charge failed\n");
        return ret;
    }

    ret = eta6937_update_bits(info, ETA6937_REG_1,
                  ETA6937_REG_OPA_MODE_MASK,
                  ETA6937_REG_OPA_MODE_DISABLE << ETA6937_REG_OPA_MODE_SHIFT);
    if (ret) {
        pr_err("disable eta6937 otg failed\n");
        return ret;
    }

    return ret;
}

static void eta6937_charger_stop_charge(struct eta6937_charger_info *info)
{
    int ret = 0;
    u8 reg_val = 0x01;

    pr_info("eta6937: stop charge #####\n");

    ret = eta6937_update_bits(info, ETA6937_REG_1,
                ETA6937_REG_NCE_MASK,
                reg_val << ETA6937_REG_NCE_SHIFT);
    if (ret) {
        pr_err("disable bq2560x charge failed\n");
    }
}
/* Ended by AICoder, pid:e8036y763dtee6c146e0087aa0b4d641da70c691 */
/* Ended by AICoder, pid:y8efeq77dcm6d8014e6c099b902a6d254e45635b */

static int eta6937_charger_set_termina_cur(struct eta6937_charger_info *info, u32 term_cur) {
    u8 reg_val = 0;

    if (term_cur <= ETA6937_REG_ITERM_MIN)
        term_cur = ETA6937_REG_ITERM_MIN;
    else if (term_cur >= ETA6937_REG_ITERM_MAX)
        term_cur = ETA6937_REG_ITERM_MAX;

    reg_val = (term_cur - ETA6937_REG_ITERM_BASE) / ETA6937_REG_ITERM_LSB;

	eta6937_update_bits(info, ETA6937_REG_1, ETA6937_REG_TE_MASK, 1);
    return eta6937_update_bits(info, ETA6937_REG_4, ETA6937_REG_ITERM_MASK, reg_val << ETA6937_REG_ITERM_SHIFT);
}

static int eta6937_charger_get_termia_cur(struct eta6937_charger_info *info, u32 *cur)
{
    int ret = 0;
    u8 reg_val = 0;

    ret = eta6937_read(info, ETA6937_REG_4, &reg_val);
    if (ret) {
        pr_err("Failed to read termina_cur\n");
        return ret;
    }

    reg_val = (reg_val & ETA6937_REG_ITERM_MASK) >> ETA6937_REG_ITERM_SHIFT;

    *cur = reg_val * ETA6937_REG_ITERM_LSB + ETA6937_REG_ITERM_BASE;

    return 0;
}

/* Started by AICoder, pid:rfaf9266d1v94fb149de08c5a016dc31e1e0632f */
static int eta6937_charger_set_vindpm(struct eta6937_charger_info *info, u32 vol)
{
	int ret = 0;
	u8 reg_val = 0;
	u8 reg1_val = 0;
	u8 reg2_val = 0;

	if (vol < ETA6937_VINDPM_BASE) 
		reg_val = 0x00;
	else if (vol > ETA6937_VINDPM_MAX)
		reg_val = 0x7f;
	else 
		reg_val = (vol - ETA6937_VINDPM_BASE) / ETA6937_VINDPM_STEP;

	reg1_val = reg_val & 0x07;
	reg2_val = ((reg_val & 0x78) >> 3);

	ret = eta6937_update_bits(info, ETA6937_REG_5, ETA6937_REG_VINDPM_MASK,
				   reg1_val << ETA6937_REG_VINDPM_SHIFT);

	ret = eta6937_update_bits(info, ETA6937_REG_7, ETA6937_REG_VINDPM_ADD_MASK,
				   reg2_val << ETA6937_REG_VINDPM_ADD_SHIFT);

	return ret;
}
/* Ended by AICoder, pid:rfaf9266d1v94fb149de08c5a016dc31e1e0632f */

static int eta6937_charger_tuning_vindpm(struct eta6937_charger_info *info)
{
	union power_supply_propval val;
	union power_supply_propval batt_vol_uv;

	struct power_supply *fuel_gauge;
	int ret1 = 0, ret2 = 0, vchg = 0;
	int batt_vol_mv = 0;

	fuel_gauge = power_supply_get_by_name(SC27XX_FGU_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n");
		return ret1;
	}

	ret2 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);
	if (ret2) {
		pr_err("get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n");
		return ret2;
	}

	vchg = val.intval / 1000;
	batt_vol_mv = batt_vol_uv.intval / 1000;

	pr_info("vbus %d, batt_vol_mv %d\n", vchg, batt_vol_mv);
	if (batt_vol_mv < 4300) {
		ret1 = eta6937_charger_set_vindpm(info, 4500);
	} else {
		ret1 = eta6937_charger_set_vindpm(info, info->vindpm_value);
	}

	return ret1;
}

static int eta6937_charger_set_limit1_current(struct eta6937_charger_info *info, u32 limit_cur)
{
	u8 reg_val;
	int i;

	if (limit_cur > eta6937_iin_lim1_tbl[ARRAY_SIZE(eta6937_iin_lim1_tbl) - 1]) {
		reg_val = ARRAY_SIZE(eta6937_iin_lim1_tbl);
	} else {
		for (i = 1; i < ARRAY_SIZE(eta6937_iin_lim1_tbl); ++i)
			if (limit_cur < eta6937_iin_lim1_tbl[i])
				break;

		reg_val = i - 1;
	}

	return eta6937_update_bits(info, ETA6937_REG_1, ETA6937_REG_IIN_LIMIT_1_MASK,
				   reg_val << ETA6937_REG_IIN_LIMIT_1_SHIFT);
}

/* Started by AICoder, pid:3af6338867kbf78143960af8d048754ed7c52124 */
static int eta6937_charger_set_limit2_current(struct eta6937_charger_info *info, u32 limit_cur)
{
	u8 reg_val;
	int ret = 0;
	int i;

	for (i = 1; i < ARRAY_SIZE(eta6937_iin_lim2_tbl); ++i)
		if (limit_cur < eta6937_iin_lim2_tbl[i])
			break;

	reg_val = i - 1;

	ret = eta6937_update_bits(info, ETA6937_REG_7, ETA6937_REG_IIN_LIMIT_2_MASK,
				    reg_val << ETA6937_REG_IIN_LIMIT_2_SHIFT);

	limit_cur = limit_cur / 1000;
	if (limit_cur <= 100) {
		ret = eta6937_charger_set_vindpm(info, info->vindpm_value);
		if (ret)
			pr_err("set eta6937 vindpm default error");
	} else {
		if (info->enable_dynamic_mivr) {
			ret = eta6937_charger_tuning_vindpm(info);
			if (ret)
				pr_err("set eta6937 tuning vindpm error");
		} else {
			ret = eta6937_charger_set_vindpm(info, info->vindpm_value);
			pr_err("set vindpm default success!");
			if (ret)
				pr_err("set eta6937 vindpm default error");
		}
	}

	eta6937_charger_dump_register(info);
	return  ret;
}
/* Ended by AICoder, pid:3af6338867kbf78143960af8d048754ed7c52124 */

static int eta6937_charger_set_limit_current(struct eta6937_charger_info *info, u32 limit_cur)
{
	int ret;

	if (ETA6937_IIN_LIM_SEL)
		ret = eta6937_charger_set_limit2_current(info, limit_cur);
	else
		ret = eta6937_charger_set_limit1_current(info, limit_cur);

	if (ret)
		dev_err(info->dev, "set eta6937 ichg[2:0] cur failed ret = %d\n", ret);

	return ret;
}

static int eat6937_charger_get_limit1_current(struct eta6937_charger_info *info, u32 *limit_cur)
{
	u8 reg_val;
	int ret;

	ret = eta6937_read(info, ETA6937_REG_1, &reg_val);
	if (ret < 0) {
		dev_err(info->dev, "set eta6937 limit1 cur failed ret = %d\n", ret);
		return ret;
	}

	reg_val &= ETA6937_REG_IIN_LIMIT_1_MASK;
	reg_val = reg_val >> ETA6937_REG_IIN_LIMIT_1_SHIFT;

	if (reg_val >= ARRAY_SIZE(eta6937_iin_lim1_tbl))
		*limit_cur = ETA6937_REG_IIN_LIMIT1_MAX;
	else
		*limit_cur = eta6937_iin_lim1_tbl[reg_val];

	return ret;
}

static int eat6937_charger_get_limit2_current(struct eta6937_charger_info *info, u32 *limit_cur)
{
	u8 reg_val;
	int ret;

	ret = eta6937_read(info, ETA6937_REG_7, &reg_val);
	if (ret < 0) {
		dev_err(info->dev, "set eta6937 limit2 cur failed ret = %d\n", ret);
		return ret;
	}

	reg_val &= ETA6937_REG_IIN_LIMIT_2_MASK;
	reg_val = reg_val >> ETA6937_REG_IIN_LIMIT_2_SHIFT;

	if (reg_val >= ARRAY_SIZE(eta6937_iin_lim2_tbl))
		reg_val = ARRAY_SIZE(eta6937_iin_lim2_tbl) - 1;

	*limit_cur = eta6937_iin_lim2_tbl[reg_val];

	return ret;
}

static int eta6937_charger_get_limit_current(struct eta6937_charger_info *info, u32 *limit_cur)
{
	int ret;

	if (ETA6937_IIN_LIM_SEL)
		ret = eat6937_charger_get_limit2_current(info, limit_cur);
	else
		ret = eat6937_charger_get_limit1_current(info, limit_cur);

	if (ret)
		dev_err(info->dev, "set eta6937  get limit cur failed ret = %d\n", ret);

	return ret;
}

static int eta6937_charger_get_health(struct eta6937_charger_info *info, u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int eta6937_charger_get_status(struct eta6937_charger_info *info)
{
	if (info->charging)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int eta6937_charger_set_status(struct eta6937_charger_info *info, int val)
{
	int ret = 0;

	if (val > CM_FAST_CHARGE_NORMAL_CMD)
		return 0;

	if (!val && info->charging) {
		eta6937_charger_stop_charge(info);
		info->charging = false;
	} else if (val && !info->charging) {
		ret = eta6937_charger_start_charge(info);
		if (ret)
			dev_err(info->dev, "start charge failed\n");
		else
			info->charging = true;
	}

	return ret;
}

static int eta6937_charger_dump_register(struct eta6937_charger_info *info)
{
	int i = 0;
	u8 value[8] = {0};

	for (i = 0; i < ARRAY_SIZE(value); i++) {
		eta6937_read(info, i, &(value[i]));
		if (i == ARRAY_SIZE(value) - 1) {
			pr_info("eta6937: 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
				value[0], value[1], value[2], value[3], value[4], value[5],
				value[6], value[7]);
		}
	}

	return 0;
}

/* Started by AICoder, pid:m1ecco91adkaec9149d2096a101abf2c0e22450a */
static int eta6937_chip_name_show(struct seq_file *m, void *v)
{
	struct eta6937_charger_info *pinfo = m->private;

	if (pinfo->vendor_id == ETA6937_DEV_ID) {
		seq_printf(m, "eta6937\n");
	} else if (pinfo->vendor_id == ETA6947_DEV_ID) {
		seq_printf(m, "eta6947\n");
	} else {
		seq_printf(m, "unknown\n");
	}
	return 0;
}

static int eta6937_chip_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, eta6937_chip_name_show, PDE_DATA(inode));
}

static const struct proc_ops eta6937_chip_name_node = {
	.proc_open = eta6937_chip_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};
/* Ended by AICoder, pid:m1ecco91adkaec9149d2096a101abf2c0e22450a */


static int eta6937_charger_usb_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct eta6937_charger_info *info = power_supply_get_drvdata(psy);
	u32 cur, health;
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (info->is_charger_online)
			val->intval = eta6937_charger_get_status(info);
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = eta6937_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = eta6937_charger_get_limit_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
			val->intval = 0;
		} else {
			ret = eta6937_charger_get_health(info, &health);
			if (ret)
				goto out;

			val->intval = health;
		}
		break;

	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int eta6937_charger_usb_set_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    const union power_supply_propval *val)
{
	struct eta6937_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = eta6937_charger_set_current(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set charge current failed\n");
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = eta6937_charger_set_limit_current(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set input current limit failed\n");
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = eta6937_charger_set_status(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set charge status failed\n");
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = eta6937_charger_set_termina_vol(info, val->intval / 1000);
		if (ret < 0)
			dev_err(info->dev, "failed to set terminate voltage\n");
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		info->is_charger_online = val->intval;
		if (val->intval == true)
			schedule_delayed_work(&info->wdt_work, 0);
		else
			cancel_delayed_work_sync(&info->wdt_work);
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int eta6937_charger_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_PRESENT:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property eta6937_usb_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_HEALTH,
};

static const struct power_supply_desc eta6937_charger_desc = {
	.name			= "charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= eta6937_usb_props,
	.num_properties		= ARRAY_SIZE(eta6937_usb_props),
	.get_property		= eta6937_charger_usb_get_property,
	.set_property		= eta6937_charger_usb_set_property,
	.property_is_writeable	= eta6937_charger_property_is_writeable,
};

/* Started by AICoder, pid:df936e0e186fa9314eaa08bb40ca761ebfd307ca */
static int eta6937_charger_feed_watchdog(struct eta6937_charger_info *info)
{
    int ret;

    ret = eta6937_update_bits(info, ETA6937_REG_0, ETA6937_REG_TMR_RST_OTG_STAT_MASK,
                  ETA6937_REG_TMR_RST_OTG_STAT_MASK);
    if (ret) {
        dev_err(info->dev, "eta6937 feed watchdog failed ret = %d\n", ret);
        return ret;
    }

    return 0;
}
/* Ended by AICoder, pid:df936e0e186fa9314eaa08bb40ca761ebfd307ca */

static void eta6937_charger_feed_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct eta6937_charger_info *info = container_of(dwork,
							 struct eta6937_charger_info,
							 wdt_work);
	int ret;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	ret = eta6937_charger_feed_watchdog(info);
	if (ret)
		schedule_delayed_work(&info->wdt_work, HZ * 5);
	else
		schedule_delayed_work(&info->wdt_work, HZ * 15);
}

#if IS_ENABLED(CONFIG_REGULATOR)
static void eta6937_charger_otg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct eta6937_charger_info *info = container_of(dwork,
			struct eta6937_charger_info, otg_work);
	int ret;
	u8 val;

	dev_info(info->dev, "%s, line = %d\n", __func__, __LINE__);
	ret = eta6937_read(info, ETA6937_REG_0, &val);
	if (ret) {
		dev_err(info->dev, "failed to read eta6937 reg0\n");
	}

	if (!extcon_get_state(info->edev, EXTCON_USB)) {
		if ((val & 0x0f) == 0x0A) {
			ret = eta6937_update_bits(info, ETA6937_REG_1,
						   ETA6937_REG_HZ_MODE_MASK |
						   ETA6937_REG_OPA_MODE_MASK,
						   ETA6937_REG_OPA_MODE_MASK);
		}
		ret = eta6937_update_bits(info, ETA6937_REG_1,
					   ETA6937_REG_HZ_MODE_MASK |
					   ETA6937_REG_OPA_MODE_MASK,
					   ETA6937_REG_OPA_MODE_MASK);
		if (ret)
			dev_err(info->dev, "restart eta6937 charger otg failed, ret = %d\n", ret);
	}

	schedule_delayed_work(&info->otg_work, msecs_to_jiffies(500));
}

static int eta6937_charger_enable_otg(struct regulator_dev *dev)
{
	struct eta6937_charger_info *info = rdev_get_drvdata(dev);
	int ret;

	dev_info(info->dev, "%s, line = %d\n", __func__, __LINE__);
	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	/*
	 * Disable charger detection function in case
	 * affecting the OTG timing sequence.
	 */
	if (!info->use_typec_extcon) {
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, BIT_DP_DM_BC_ENB);
		if (ret) {
			dev_err(info->dev, "failed to disable bc1.2 detect function. ret = %d\n", ret);
			return ret;
		}
	}

	ret = eta6937_update_bits(info, ETA6937_REG_1,
				   ETA6937_REG_HZ_MODE_MASK |
				   ETA6937_REG_OPA_MODE_MASK,
				   ETA6937_REG_OPA_MODE_MASK);
	if (ret) {
		dev_err(info->dev, "enable eta6937 otg failed ret = %d\n", ret);
		regmap_update_bits(info->pmic, info->charger_detect,
				   BIT_DP_DM_BC_ENB, 0);
		return ret;
	}

	info->otg_enable = true;
	schedule_delayed_work(&info->wdt_work,
			      msecs_to_jiffies(ETA6937_FEED_WATCHDOG_VALID_MS));
	schedule_delayed_work(&info->otg_work,
			      msecs_to_jiffies(ETA6937_OTG_VALID_MS));

	return 0;
}

static int eta6937_charger_disable_otg(struct regulator_dev *dev)
{
	struct eta6937_charger_info *info = rdev_get_drvdata(dev);
	int ret;

	dev_info(info->dev, "%s, line = %d\n", __func__, __LINE__);
	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	info->otg_enable = false;
	info->need_reinit = true;
	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);
	ret = eta6937_update_bits(info, ETA6937_REG_1,
				   ETA6937_REG_HZ_MODE_MASK |
				   ETA6937_REG_OPA_MODE_MASK,
				   0);
	if (ret) {
		dev_err(info->dev, "disable eta6937 otg failed ret = %d\n", ret);
		return ret;
	}

	/* Enable charger detection function to identify the charger type */
	if (!info->use_typec_extcon) {
		ret = regmap_update_bits(info->pmic, info->charger_detect, BIT_DP_DM_BC_ENB, 0);
		if (ret)
			pr_err("enable BC1.2 failed\n");
	}
	return ret;
}

static int eta6937_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct eta6937_charger_info *info = rdev_get_drvdata(dev);
	int ret;
	u8 val;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	ret = eta6937_read(info, ETA6937_REG_0, &val);
	if (ret) {
		dev_err(info->dev, "failed to read eta6937 reg0 \n");
		return ret;
	}

	dev_info(info->dev, "%s, ETA6937_REG_0==0x%x\n", __func__, val);

	if ((val & 0x0f) == 0x0A) {
		ret = eta6937_update_bits(info, ETA6937_REG_1,
				ETA6937_REG_HZ_MODE_MASK |
				ETA6937_REG_OPA_MODE_MASK,
				ETA6937_REG_OPA_MODE_MASK);
		ret = eta6937_update_bits(info, ETA6937_REG_1,
				ETA6937_REG_HZ_MODE_MASK |
				ETA6937_REG_OPA_MODE_MASK,
				ETA6937_REG_OPA_MODE_MASK);
	}

	ret = eta6937_read(info, ETA6937_REG_1, &val);
	if (ret) {
		dev_err(info->dev, "failed to get eta6937 otg status\n");
		return ret;
	}

	val &= ETA6937_REG_OPA_MODE_MASK;
	val = info->otg_enable;

	return val;
}

static const struct regulator_ops eta6937_charger_vbus_ops = {
	.enable = eta6937_charger_enable_otg,
	.disable = eta6937_charger_disable_otg,
	.is_enabled = eta6937_charger_vbus_is_enabled,
};

static const struct regulator_desc eta6937_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &eta6937_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int
eta6937_charger_register_vbus_regulator(struct eta6937_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = info->dev;
	cfg.driver_data = info;
	reg = devm_regulator_register(info->dev,
				      &eta6937_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(info->dev, "Can't register regulator:%d\n", ret);
	}

	return ret;
}

#else
static int
eta6937_charger_register_vbus_regulator(struct eta6937_charger_info *info)
{
	return 0;
}
#endif

static int eta6937_charger_usb_change(struct notifier_block *nb,
				      unsigned long limit, void *data)
{
	pr_info("eta6937_charger_usb_change: %d\n", limit);

	return NOTIFY_OK;
}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
#if !(IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX))
static int zte_sqc_set_prop_by_name(const char *name, enum zte_power_supply_property psp, int data)
{
	struct zte_power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int rc = 0;

	if (name == NULL) {
		pr_info("eta6937:psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = zte_power_supply_get_by_name(name);
	if (!psy) {
		pr_info("eta6937:get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = zte_power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("eta6937:Failed to set %s property:%d rc=%d\n", name, psp, rc);
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
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	arg = arg ? arg : NULL;
	*chg_type = SQC_PMIC_TYPE_BUCK;

	return 0;
}

static int sqc_mp_get_chg_status(void *arg, unsigned int *charing_status)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	*charing_status = info->chg_status;

	return 0;
}

/* Started by AICoder, pid:8d12a480740053a147dd0918202596397440c38a */
static int sqc_mp_set_enable_chging(void *arg, unsigned int en)
{
    struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

    if (en && info->enable_powerpath) {
        pr_info("%s powerptah enabled, force disable chging\n", __func__);
        en = false;
    }

    if (en) {
        eta6937_charger_feed_watchdog(info);

        eta6937_charger_start_charge(info);

        info->charging = !!en;

        eta6937_charger_dump_register(info);
    } else {
        eta6937_charger_stop_charge(info);

        eta6937_charger_dump_register(info);
        info->charging = !!en;
    }

    pr_info("%s %d\n", __func__, en);

    return 0;
}
/* Ended by AICoder, pid:8d12a480740053a147dd0918202596397440c38a */

static int sqc_mp_get_enable_chging(void *arg, unsigned int *en)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	*en = info->charging;

	pr_info("eta6937 %s %d\n", __func__, *en);

	return 0;
}

static int sqc_mp_get_ichg(void *arg, u32 *ichg_ma)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	ret = eta6937_charger_get_current(info, ichg_ma);

	*ichg_ma = *ichg_ma / 1000;

	pr_info("eta6937 %s %d\n", __func__, *ichg_ma);

	return ret;
}

/* Started by AICoder, pid:j169ded11fhe604144050863403ce123dbe13e47 */
static int sqc_mp_set_ichg(void *arg, u32 mA)
{
    struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
    int ret = 0;

    ret = eta6937_charger_set_current(info, mA * 1000);
    if (ret) {
        pr_err("Failed to set current: %d\n", ret);
        return ret;
    }

/* Started by AICoder, pid:k7f60vcaff2049e1458d09a6c031e400d913d86f */
    ret = eta6937_charger_set_max_chg_cur(info, ETA6937_MAX_CHG_CUR_MA * 1000);
/* Ended by AICoder, pid:k7f60vcaff2049e1458d09a6c031e400d913d86f */
    if (ret) {
        pr_err("Failed to set maximum charge current: %d\n", ret);
        return ret;
    }

    pr_info("eta6937 %s %d\n", __func__, mA);

    return ret;
}
/* Ended by AICoder, pid:j169ded11fhe604144050863403ce123dbe13e47 */

static int sqc_mp_set_ieoc(void *arg, u32 mA)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	ret = eta6937_charger_set_termina_cur(info, mA);

	pr_info("eta6937 %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_get_ieoc(void *arg, u32 *ieoc)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	ret = eta6937_charger_get_termia_cur(info, ieoc);

	pr_info("eta6937 %s %d\n", __func__, *ieoc);

	return ret;
}

static int sqc_mp_get_aicr(void *arg, u32 *aicr_ma)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	ret = eta6937_charger_get_limit_current(info, aicr_ma);

	pr_info("eta6937 %s %d\n", __func__, *aicr_ma);

	return ret;
}

/* Started by AICoder, pid:m1563c381d9654b1462f082af068e124f772f235 */
static int sqc_mp_set_aicr(void *arg, u32 aicr_ma)
{
    struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
    int ret = 0;

    ret = eta6937_update_bits(info, ETA6937_REG_7, ETA6937_REG_EN_ILIM_2_MASK,
                  ETA6937_IIN_LIM_SEL << ETA6937_REG_EN_ILIM_2_SHIFT);
    if (ret) {
        pr_err("Failed to update bits in register: %d\n", ret);
        return ret;
    }

    ret = eta6937_charger_set_limit_current(info, aicr_ma * 1000);
    if (ret) {
        pr_err("Failed to set limit current: %d\n", ret);
        return ret;
    }

    pr_info("eta6937 %s %d\n", __func__, aicr_ma);

    return ret;
}
/* Ended by AICoder, pid:m1563c381d9654b1462f082af068e124f772f235 */

static int sqc_mp_get_cv(void *arg, u32 *cv_mv)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	ret = eta6937_charger_get_termina_vol(info, cv_mv);

	pr_info("eta6937 %s %d\n", __func__, *cv_mv);

	return ret;
}

/* Started by AICoder, pid:c403bp687agcbb614c62099b607cf62f87f1543a */
static int sqc_mp_set_cv(void *arg, u32 cv_mv)
{
    struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
    int ret = 0;

    ret = eta6937_charger_set_termina_vol(info, cv_mv);
    if (ret) {
        pr_err("Failed to set termination voltage: %d\n", ret);
        return ret;
    }

    ret = eta6937_charger_set_max_batt_reg_vol(info, cv_mv);
    if (ret) {
        pr_err("Failed to set maximum battery regulation voltage: %d\n", ret);
        return ret;
    }

    pr_info("eta6937 %s %d\n", __func__, cv_mv);

    return ret;
}
/* Ended by AICoder, pid:c403bp687agcbb614c62099b607cf62f87f1543a */

static int sqc_mp_get_rechg_voltage(void *arg, u32 *rechg_volt_mv)
{
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	*rechg_volt_mv = ETA6937_RECHARGE_VOL_MV;

	pr_info("eta6937 %s %d\n", __func__, *rechg_volt_mv);

	return ret;
}

static int sqc_mp_set_rechg_voltage(void *arg, u32 rechg_volt_mv)
{
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	pr_info("eta6937 %s %d\n", __func__, rechg_volt_mv);

	return 0;
}

static int sqc_mp_ovp_volt_get(void *arg, u32 *ac_ovp_mv)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	*ac_ovp_mv = info->ac_ovp_mv;

	pr_info("eta6937 %s %d\n", __func__, *ac_ovp_mv);

	return ret;
}

static int sqc_mp_ovp_volt_set(void *arg, u32 ac_ovp_mv)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	int ret = 0;

	info->ac_ovp_mv = ac_ovp_mv;

	pr_info("eta6937 %s %d\n", __func__, ac_ovp_mv);

	return ret;
}

static int sqc_mp_get_vbat(void *arg, u32 *mV)
{
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	union power_supply_propval batt_vol_uv;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(ETA6937_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);

	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("%s: get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n", __func__);
		return ret1;
	}

	*mV = batt_vol_uv.intval / 1000;

	return 0;
}

static int sqc_mp_get_ibat(void *arg, u32 *mA)
{
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(ETA6937_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("%s: get POWER_SUPPLY_PROP_CURRENT_NOW failed!\n", __func__);
		return ret1;
	}

	*mA = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_vbus(void *arg, u32 *mV)
{
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(ETA6937_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("%s: get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n", __func__);
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
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	info->enable_powerpath = !!enabled;

	return 0;
}

static int sqc_enable_powerpath_get(void *arg, unsigned int *enabled)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	*enabled = info->enable_powerpath;

	return 0;
}

static int sqc_enable_hiz_set(void *arg, unsigned int enable)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	info->hiz_enable = !!enable;
	return 0;
}

static int sqc_enable_hiz_get(void *arg, unsigned int *enable)
{
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)arg;

	*enable = info->hiz_enable;

	return 0;
}

static struct sqc_pmic_chg_ops eta6937_sqc_chg_ops = {

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
	struct eta6937_charger_info *info = (struct eta6937_charger_info *)sqc_bc1d2_proto_node.arg;
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

	pr_info("sqc_chg_type: %d, chg_type: %d\n", sqc_chg_type, *chg_type);

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
	int sleep_mode_enable = 0;
	//struct eta6937_charger_info *info = (struct eta6937_charger_info *)sqc_bc1d2_proto_node.arg;

	sscanf(val, "%d", &sleep_mode_enable);

	pr_info("sleep_mode_enable = %d\n", sleep_mode_enable);

	if (sleep_mode_enable) {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			pr_info("sleep on status");

			/*disabel sqc-daemon*/
			sqc_chg_type = SQC_SLEEP_MODE_TYPE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);

			/*mtk enter sleep mode*/
			zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);

		}
	} else {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			pr_info("sleep off status");
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

/* Started by AICoder, pid:7f747r418elbc7c1449009ae913b043975e4d189 */
static int eta6937_charger_hw_init(struct eta6937_charger_info *info)
{
    struct sprd_battery_info bat_info = {};
    int voltage_max_microvolt, termination_cur;
    int ret;

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	ret = sprd_battery_get_battery_info_sqc(info->psy_usb, &bat_info, 0);
#else
	ret = sprd_battery_get_battery_info(info->psy_usb, &bat_info);
#endif
    if (ret) {
        dev_warn(info->dev, "no battery information is supplied\n");

        info->cur.sdp_limit = 500000;
        info->cur.sdp_cur = 500000;
        info->cur.dcp_limit = 1500000;
        info->cur.dcp_cur = 1500000;
        info->cur.cdp_limit = 1000000;
        info->cur.cdp_cur = 1000000;
        info->cur.unknown_limit = 500000;
        info->cur.unknown_cur = 500000;

        /*
         * If no battery information is supplied, we should set
         * default charge termination current to 120 mA, and default
         * charge termination voltage to 4.44V.
         */
        voltage_max_microvolt = 4440;
        termination_cur = 120;
        info->termination_cur = termination_cur;
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

        voltage_max_microvolt = bat_info.constant_charge_voltage_max_uv / 1000;
        termination_cur = bat_info.charge_term_current_ua / 1000;
        info->termination_cur = termination_cur;
        sprd_battery_put_battery_info(info->psy_usb, &bat_info);
    }

    if (of_device_is_compatible(info->dev->of_node,
                    "eta,eta6937_chg")) {
        ret = eta6937_update_bits(info, ETA6937_REG_4,
                      ETA6937_REG_RESET_MASK,
                      ETA6937_REG_RESET_MASK);
        if (ret) {
            dev_err(info->dev, "reset eta6937 failed ret = %d\n", ret);
            return ret;
        }

        ret = eta6937_charger_set_max_batt_reg_vol(info,
                    voltage_max_microvolt);
        if (ret) {
            dev_err(info->dev, "set eta6937 safety vol failed ret = %d\n", ret);
            return ret;
        }

        ret = eta6937_charger_set_max_chg_cur(info,
                    info->cur.dcp_cur);
        if (ret) {
            dev_err(info->dev, "set eta6937 safety cur failed, ret = %d\n", ret);
            return ret;
        }
    }

    /* do not force charge to 550mA */
    ret = eta6937_update_bits(info, ETA6937_REG_5, ETA6937_REG_LOW_CHG_MASK, 0);
    if (ret) {
        dev_err(info->dev, "release eta6937 force charge failed ret = %d\n", ret);
        return ret;
    }
    /* set wake battery voltage threshold 3.4V */
    ret = eta6937_update_bits(info, ETA6937_REG_1, ETA6937_REG_VLOWV_MASK, 0);
    if (ret) {
        dev_err(info->dev, "set eta6937 weak voltage threshold failed ret = %d\n", ret);
        return ret;
    }
    /* set ichg offset , ichg begain from 550mA or 650mA*/
    ret = eta6937_update_bits(info, ETA6937_REG_4, ETA6937_REG_ICHG_OFFSET_MASK,
                  ETA6937_REG_ICHG_OFFSET << ETA6937_REG_ICHG_OFFSET_SHIFT);
    if (ret) {
        dev_err(info->dev, "set eta6937 io level failed ret = %d\n", ret);
        return ret;
    }
    /*set VOREG 4.3V?*/
    ret = eta6937_update_bits(info, ETA6937_REG_2, ETA6937_REG_VOREG_MASK,
                  ETA6937_REG_VOREG << ETA6937_REG_VOREG_SHIFT);
    if (ret) {
        dev_err(info->dev, "set eta6937 VOREG failed ret = %d\n", ret);
        return ret;
    }

    /*Enable termination charge current function*/
    ret = eta6937_update_bits(info, ETA6937_REG_1, ETA6937_REG_TE_MASK, 1);
    if (ret) {
        dev_err(info->dev, "set eta6937 terminal cur failed ret = %d\n", ret);
        return ret;
    }

    /*Feed Watchdog*/
    ret = eta6937_update_bits(info, ETA6937_REG_0, ETA6937_REG_TMR_RST_OTG_STAT_MASK,
                  ETA6937_REG_TMR_RST_OTG_STAT_MASK);
    if (ret) {
        dev_err(info->dev, "feed eta6937 watchdog failed ret = %d\n", ret);
        return ret;
    }

    /*input current limit sel*/
    ret = eta6937_update_bits(info, ETA6937_REG_7, ETA6937_REG_EN_ILIM_2_MASK,
                  ETA6937_IIN_LIM_SEL << ETA6937_REG_EN_ILIM_2_SHIFT);
    if (ret) {
        dev_err(info->dev, "set eta6937 input current limit failed ret = %d\n", ret);
        return ret;
    }

    ret = eta6937_charger_set_termina_vol(info, voltage_max_microvolt);
    if (ret) {
        dev_err(info->dev, "set eta6937 terminal vol failed ret = %d\n", ret);
        return ret;
    }

    ret = eta6937_charger_set_limit_current(info, info->cur.unknown_cur);
    if (ret) {
        dev_err(info->dev, "set eta6937 limit current failed ret = %d\n", ret);
        return ret;
    }

    return ret;
}
/* Ended by AICoder, pid:7f747r418elbc7c1449009ae913b043975e4d189 */

static int eta6937_charger_detect_device(struct eta6937_charger_info *info)
{
	int ret, part_id;
	u8 reg_val;

	ret = eta6937_read(info, ETA6937_REG_3, &reg_val);
	if (ret < 0) {
		dev_err(info->dev, "%s, failed to get device id, ret = %d\n", __func__, ret);
		return ret;
	}

	part_id = (reg_val & (ETA6937_REG_VENDOR_CODE_MASK | ETA6937_REG_PN_CODE_MASK)) >> ETA6937_REG_PN_CODE_SHIFT;
	pr_err("%s part_id=0x%x\n", __func__, part_id);
	if (part_id != ETA6937_DEV_ID && part_id != ETA6947_DEV_ID) {
		dev_err(info->dev, "%s, the device id is 0x%x\n", __func__, part_id);
		return -EINVAL;
	}
	info->vendor_id = part_id;

	return ret;
}

static int eta6937_charger_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct power_supply_config charger_cfg = { };
	struct eta6937_charger_info *info;
	struct device_node *regmap_np;
	struct platform_device *regmap_pdev;
	struct sqc_pmic_chg_ops *sqc_ops = NULL;
	int ret;

	pr_info("%s %d\n", __func__, __LINE__);
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}
	pr_info("%s %d\n", __func__, __LINE__);
	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "failed to alloc memery\n");
		return -ENOMEM;
	}
	info->client = client;
	info->dev = dev;

	ret = eta6937_charger_detect_device(info);
	if (ret) {
		dev_err(dev, "%s, failed to detect device, ret = %d\n", __func__, ret);
		return -ENODEV;
	}

	alarm_init(&info->wdg_timer, ALARM_BOOTTIME, NULL);

	mutex_init(&info->lock);

	i2c_set_clientdata(client, info);

	info->usb_phy = devm_usb_get_phy_by_phandle(info->dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		dev_err(dev, "failed to find USB phy\n");
	}

	info->edev = extcon_get_edev_by_phandle(info->dev, 0);
	if (IS_ERR(info->edev)) {
		dev_err(dev, "failed to find vbus extcon device.\n");
		return PTR_ERR(info->edev);
	}

	info->use_typec_extcon = device_property_read_bool(info->dev, "use-typec-extcon");
#ifndef ZTE_FEATURE_PV_AR
	info->enable_dynamic_mivr = device_property_read_bool(info->dev, "enable-dynamic-mivr");
#else
	info->enable_dynamic_mivr = false;
#endif

	ret = eta6937_charger_register_vbus_regulator(info);
	if (ret) {
		dev_err(dev, "failed to register vbus regulator.\n");
		return ret;
	}

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,sc27xx-syscon");
	if (!regmap_np) {
		dev_err(dev, "unable to get syscon node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 1,
					 &info->charger_detect);
	if (ret) {
		dev_err(dev, "failed to get charger_detect\n");
		return -EINVAL;
	}
	/*
	info->gpiod = devm_gpiod_get(dev, "chg-enable", ENABLE_CHARGE);
	if (IS_ERR(info->gpiod)) {
		dev_err(dev, "failed to get enable gpio\n");
		return PTR_ERR(info->gpiod);
	}
	*/

	ret = of_property_read_u32_index(regmap_np, "reg", 2,
					 &info->charger_pd);
	if (ret) {
		dev_err(dev, "failed to get charger_pd reg\n");
		return ret;
	}

	if (of_property_read_u32(dev->of_node, "vindpm-value", &info->vindpm_value) >= 0) {
		dev_info(dev, "vindpm-value use value is %d\n", info->vindpm_value);
	} else {
		dev_err(dev, "failed to get vindpm-value use default value %d\n", VINDPM_DEFAULT_MV);
		info->vindpm_value = VINDPM_DEFAULT_MV;
	}

	if (of_device_is_compatible(regmap_np->parent, "sprd,sc2730"))
		info->charger_pd_mask = ETA6937_DISABLE_PIN_MASK_2730;
	else if (of_device_is_compatible(regmap_np->parent, "sprd,sc2721"))
		info->charger_pd_mask = ETA6937_DISABLE_PIN_MASK_2721;
	else if (of_device_is_compatible(regmap_np->parent, "sprd,sc2720"))
		info->charger_pd_mask = ETA6937_DISABLE_PIN_MASK_2720;
	else {
		dev_err(dev, "failed to get charger_pd mask\n");
		return -EINVAL;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if (!regmap_pdev) {
		of_node_put(regmap_np);
		dev_err(dev, "unable to get syscon device\n");
		return -ENODEV;
	}

	of_node_put(regmap_np);
	info->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!info->pmic) {
		dev_err(dev, "unable to get pmic regmap device\n");
		return -ENODEV;
	}

	info->usb_notify.notifier_call = eta6937_charger_usb_change;
	ret = usb_register_notifier(info->usb_phy, &info->usb_notify);
	if (ret) {
		pr_err("failed to register notifier:%d\n", ret);
		goto GET_USB_PHY_FAILED;
	}

	charger_cfg.drv_data = info;
	charger_cfg.of_node = dev->of_node;
	info->psy_usb = devm_power_supply_register(dev,
						   &eta6937_charger_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		dev_err(dev, "failed to register power supply\n");
		return PTR_ERR(info->psy_usb);
	}

	ret = eta6937_charger_hw_init(info);
	if (ret)
		goto USB_REGISTER_NOTIFIER_FAILED;

	proc_create_data("driver/chg_name", 0664, NULL, &eta6937_chip_name_node, info);
	device_init_wakeup(info->dev, true);

	INIT_DELAYED_WORK(&info->otg_work, eta6937_charger_otg_work);
	INIT_DELAYED_WORK(&info->wdt_work,
			  eta6937_charger_feed_watchdog_work);

	sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
	memcpy(sqc_ops, &eta6937_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
	sqc_ops->arg = (void *)info;

	ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PRIMARY);
	if (ret < 0) {
		pr_err("register sqc hal fail(%d)\n", ret);
	}

#if !(IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX))
	sqc_bc1d2_proto_node.arg = (void *)info;
	sqc_hal_bc1d2_register(&sqc_bc1d2_proto_node);

	zte_misc_register_callback(&qc3dp_sleep_mode_node, info);
#endif
	pr_info("%s %d use_typec_extcon=%d enable_dynamic_mivr=%d success\n",
			__func__, __LINE__, info->use_typec_extcon, info->enable_dynamic_mivr);

	return 0;

USB_REGISTER_NOTIFIER_FAILED:
	usb_unregister_notifier(info->usb_phy, &info->usb_notify);
GET_USB_PHY_FAILED:
	mutex_destroy(&info->lock);
	if (info)
		devm_kfree(info->dev, info);

	return -EPROBE_DEFER;
}


static void eta6937_charger_shutdown(struct i2c_client *client)
{
	struct eta6937_charger_info *info = i2c_get_clientdata(client);
	int ret = 0;

	eta6937_charger_set_hiz(info, 0);
	ret = eta6937_charger_set_vindpm(info, info->vindpm_value);
	if (ret)
		pr_err("set eta6937 vindpm error\n");

	cancel_delayed_work_sync(&info->wdt_work);
	if (info->otg_enable) {
		info->otg_enable = false;
		info->need_reinit = true;
		cancel_delayed_work_sync(&info->otg_work);
		ret = eta6937_update_bits(info, ETA6937_REG_1,
					  ETA6937_REG_HZ_MODE_MASK |
					  ETA6937_REG_OPA_MODE_MASK,
					  0);
		if (ret)
			dev_err(info->dev, "disable eta6937 otg failed ret = %d\n", ret);

		/* Enable charger detection function to identify the charger type */
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);
		if (ret)
			dev_err(info->dev,
				"enable charger detection function failed ret = %d\n", ret);
	}
}

static int eta6937_charger_remove(struct i2c_client *client)
{
	struct eta6937_charger_info *info = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);

	mutex_destroy(&info->lock);

	return 0;
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int eta6937_charger_alarm_prepare(struct device *dev)
{
	struct eta6937_charger_info *info = dev_get_drvdata(dev);
	ktime_t now, add;

	if (!info) {
		pr_err("%s: info is null!\n", __func__);
		return 0;
	}

	if (!info->otg_enable)
		return 0;

	now = ktime_get_boottime();
	add = ktime_set(ETA6937_WDG_TIMER_S, 0);
	alarm_start(&info->wdg_timer, ktime_add(now, add));
	return 0;
}

static void eta6937_charger_alarm_complete(struct device *dev)
{
	struct eta6937_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!info->otg_enable)
		return;

	alarm_cancel(&info->wdg_timer);
}

static int eta6937_charger_suspend(struct device *dev)
{
	struct eta6937_charger_info *info = dev_get_drvdata(dev);

	if (info->otg_enable || info->is_charger_online)
		/* feed watchdog first before suspend */
		eta6937_charger_feed_watchdog(info);

	if (!info->otg_enable)
		return 0;

	cancel_delayed_work_sync(&info->wdt_work);

	return 0;
}

static int eta6937_charger_resume(struct device *dev)
{
	struct eta6937_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (info->otg_enable || info->is_charger_online)
		/* feed watchdog first before suspend */
		eta6937_charger_feed_watchdog(info);

	if (!info->otg_enable)
		return 0;

	schedule_delayed_work(&info->wdt_work, HZ * 15);

	return 0;
}
#endif

static const struct dev_pm_ops eta6937_charger_pm_ops = {
	.prepare = eta6937_charger_alarm_prepare,
	.complete = eta6937_charger_alarm_complete,
	SET_SYSTEM_SLEEP_PM_OPS(eta6937_charger_suspend,
				eta6937_charger_resume)
};

static const struct i2c_device_id eta6937_i2c_id[] = {
	{"eta6937_chg", 0},
	{}
};

static const struct of_device_id eta6937_charger_of_match[] = {
	{ .compatible = "eta,eta6937_chg", },
	{ }
};

MODULE_DEVICE_TABLE(of, eta6937_charger_of_match);

static struct i2c_driver eta6937_charger_driver = {
	.driver = {
		.name = "eta6937_chg",
		.of_match_table = eta6937_charger_of_match,
		.pm = &eta6937_charger_pm_ops,
	},
	.probe = eta6937_charger_probe,
	.shutdown = eta6937_charger_shutdown,
	.remove = eta6937_charger_remove,
	.id_table = eta6937_i2c_id,
};

module_i2c_driver(eta6937_charger_driver);
MODULE_DESCRIPTION("ETA6937 Charger Driver");
MODULE_LICENSE("GPL v2");
