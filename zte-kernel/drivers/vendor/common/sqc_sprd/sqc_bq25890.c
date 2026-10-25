#include "sqc_bq25890.h"

static int bq25890_read(struct bq25890_charger_info *info, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(info->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}

static int bq25890_write(struct bq25890_charger_info *info, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(info->client, reg, data);
}

static int bq25890_update_bits(struct bq25890_charger_info *info, u8 reg,
			       u8 mask, u8 data)
{
	u8 v;
	int ret;

	ret = bq25890_read(info, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= (data & mask);

	return bq25890_write(info, reg, v);
}

static void bq25890_charger_dump_register(struct bq25890_charger_info *info)
{
	int i, ret, len, idx = 0;
	u8 reg_val;
	char buf[512];

	memset(buf, '\0', sizeof(buf));
	for (i = 0; i < BQ25890_REG_NUM; i++) {
		ret = bq25890_read(info, reg_tab[i].addr, &reg_val);
		if (ret == 0) {
			len = snprintf(buf + idx, sizeof(buf) - idx,
				       "[REG_0x%.2x]=0x%.2x; ", reg_tab[i].addr,
				       reg_val);
			idx += len;
		}
	}

	dev_info(info->dev, "%s: %s", __func__, buf);
}

static int bq25890_charger_is_fgu_present(struct bq25890_charger_info *info)
{
	struct power_supply *psy;

	psy = power_supply_get_by_name(BQ25890_BATTERY_NAME);
	if (!psy) {
		dev_err(info->dev, "Failed to find psy of sc27xx_fgu\n");
		return -ENODEV;
	}
	power_supply_put(psy);

	return 0;
}

static int bq25890_charger_set_boost_cur(struct bq25890_charger_info *info, u32 cur)
{
	u8 reg_val;

	if (cur < 750)
		reg_val = REG0A_BOOSTV_LIM_500MA;
	else if (cur < 1200)
		reg_val = REG0A_BOOSTV_LIM_750MA;
	else if (cur < 1400)
		reg_val = REG0A_BOOSTV_LIM_1200MA;
	else if (cur < 1650)
		reg_val = REG0A_BOOSTV_LIM_1400MA;
	else if (cur < 1875)
		reg_val = REG0A_BOOSTV_LIM_1650MA;
	else if (cur < 2150)
		reg_val = REG0A_BOOSTV_LIM_1875MA;
	else 
		reg_val = REG0A_BOOSTV_LIM_2150MA;

	return bq25890_update_bits(info, BQ25890_REG_0A, REG0A_BOOSTV_LIM_MASK, reg_val);

}

static int bq25890_charger_set_boost_vol(struct bq25890_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < REG0A_BOOSTV_MIN)
		vol = REG0A_BOOSTV_MIN;
	else if (vol > REG0A_BOOSTV_MAX)
		vol = REG0A_BOOSTV_MAX;
	reg_val = (vol - REG0A_BOOSTV_OFFSET) / REG0A_BOOSTV_STEP;

	return bq25890_update_bits(info, BQ25890_REG_0A, REG0A_BOOSTV_MASK, reg_val << REG0A_BOOSTV_SHIFT);

}

static void bq25890_charger_enable_hiz(struct bq25890_charger_info *info, bool enable)
{
	int ret = 0;
	u8 reg_val = REG00_HIZ_DISABLE;

	if (enable) {
		bq_info("bq25601: enable hiz #####\n");
		reg_val |= REG00_HIZ_ENABLE;
	} else {
		bq_info("bq25601: disable hiz #####\n");
	}
 
	ret = bq25890_update_bits(info, BQ25890_REG_00,
				REG00_ENHIZ_MASK,
				reg_val << REG00_ENHIZ_SHIFT);
	if (ret)
		bq_err("set HIZ mode  failed\n");
}

static int bq25890_charger_get_hiz_status(struct bq25890_charger_info *info, unsigned int *enable)
{
	int ret = 0;
	u8 reg_val = 0x00;

	ret = bq25890_read(info, BQ25890_REG_00, &reg_val);
	if (ret) {
		*enable = 0;
		bq_err("get bq25890 hiz status failed\n");
		return 0;
	}

	*enable = (reg_val & REG00_ENHIZ_MASK) ? 1 : 0;

	return 0;
}

static int bq25890_charger_set_powerpath(struct bq25890_charger_info *info,
					  unsigned int val)
{
	int ret = 0;

	bq_info("bq25890: set bq25890 powerpath val:%d  #####\n", val);

	return ret;
}

static int bq25890_charger_get_powerpath(struct bq25890_charger_info *info, u32 *val)
{
	u8 data = 0;
	int ret = 0;

	ret = bq25890_read(info, BQ25890_REG_09, &data);
	if (ret < 0)
		return ret;
	data = (data & REG09_BATFET_DIS_MASK) >> REG09_BATFET_DIS_SHIFT;
	*val = !!data;

	return 0;
}


static int bq25890_charger_set_ovp(struct bq25890_charger_info *info, u32 vol)
{
	return 0;
}

static int bq25890_charger_get_ovp(struct bq25890_charger_info *info, u32 *vol)
{
	*vol = 14 * 1000;
	return 0;
}

static int
bq25890_charger_set_recharge_voltage(struct bq25890_charger_info *info,
						u32 recharge_voltage_uv)
{
	u8 reg_val = 0;
	int ret = 0;

	reg_val = (recharge_voltage_uv > 100000) ? REG06_VRECHG_200MV : REG06_VRECHG_100MV;
	ret = bq25890_update_bits(info, BQ25890_REG_06,
				   REG06_VRECHG_MASK,
				   reg_val << REG06_VRECHG_SHIFT);
	if (ret) {
		bq_err("set bq25601 recharge_voltage failed\n");
		return ret;
	}

	return 0;
}

static int
bq25890_charger_get_recharge_voltage(struct bq25890_charger_info *info,
						u32 *recharge_voltage_uv)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq25890_read(info, BQ25890_REG_06, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= REG06_VRECHG_MASK;
	reg_val = reg_val >> REG06_VRECHG_SHIFT;

	*recharge_voltage_uv = reg_val ? 200000 : 100000;

	return 0;
}

static int bq25890_charger_set_vindpm(struct bq25890_charger_info *info, u32 vol)
{
	u8 reg_val;
	int ret;

	ret = bq25890_update_bits(info, BQ25890_REG_0D, REG0D_FORCE_VINDPM_MASK,
				  REG0D_FORCE_VINDPM_ENABLE << REG0D_FORCE_VINDPM_SHIFT);
	if (ret) {
		dev_err(info->dev, "set force vindpm failed\n");
		return ret;
	}

	if (vol < REG0D_VINDPM_MIN)
		vol = REG0D_VINDPM_MIN;
	else if (vol > REG0D_VINDPM_MAX)
		vol = REG0D_VINDPM_MAX;
	reg_val = (vol - REG0D_VINDPM_OFFSET) / REG0D_VINDPM_STEP;

	return bq25890_update_bits(info, BQ25890_REG_0D, REG0D_VINDPM_MASK, reg_val);
}

static int bq25890_charger_get_vindpm(struct bq25890_charger_info *info,
					u32 *vol)
{
	u8 reg_val;
	int ret, force_vindpm;

	ret = bq25890_read(info, BQ25890_REG_0D, &reg_val);
	if (ret < 0)
		return ret;

	force_vindpm = reg_val & REG0D_FORCE_VINDPM_MASK;

	if (force_vindpm) {
		reg_val &= REG0D_VINDPM_MASK;
		reg_val = reg_val >> REG0D_VINDPM_SHIFT;
		*vol = reg_val * REG0D_VINDPM_STEP + REG0D_VINDPM_OFFSET;
	} else{
		bq_info("force vindpm is not enable, vindpm set by charegr");
		*vol = -1;
	}

	return 0;
}

static int bq25890_charger_set_termina_vol(struct bq25890_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < REG06_VREG_MIN)
		vol = REG06_VREG_MIN;
	else if (vol >= REG06_VREG_MAX)
		vol = REG06_VREG_MAX;
	reg_val = (vol - REG06_VREG_OFFSET) / REG06_VREG_STEP;

	return bq25890_update_bits(info, BQ25890_REG_06, REG06_VREG_MASK,
				   reg_val << REG06_VREG_SHIFT);
}

static int bq25890_charger_get_termina_vol(struct bq25890_charger_info *info, u32 *vol)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq25890_read(info, BQ25890_REG_06, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= REG06_VREG_MASK;
	reg_val = reg_val >> REG06_VREG_SHIFT;

	*vol = (reg_val * REG06_VREG_STEP +
				REG06_VREG_OFFSET) * 1000;

	return 0;
}

static int bq25890_charger_set_termina_cur(struct bq25890_charger_info *info, u32 cur)
{
	u8 reg_val;

	if (cur <= REG05_ITERM_MIN)
		cur = REG05_ITERM_MIN;
	else if (cur >= REG05_ITERM_MAX)
		cur = REG05_ITERM_MAX;
	reg_val = (cur - REG05_ITERM_OFFSET) / REG05_ITERM_STEP;

	return bq25890_update_bits(info, BQ25890_REG_05, REG05_ITERM_MASK, reg_val);
}

static int bq25890_charger_get_termia_cur(struct bq25890_charger_info *info, u32 *cur)
{
	u8 reg_val = 0;
	int ret = 0;

	ret = bq25890_read(info, BQ25890_REG_05, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= REG05_ITERM_MASK;
	reg_val = reg_val >> REG05_ITERM_SHIFT;

	*cur = (reg_val * REG05_ITERM_STEP +
						REG05_ITERM_OFFSET) * 1000;
	
	return 0;
}

static int bq25890_charger_hw_init(struct bq25890_charger_info *info)
{
	struct sprd_battery_info bat_info = {};
	int voltage_max_microvolt, termination_cur;
	int ret;

	ret = sprd_battery_get_battery_info(info->psy_usb, &bat_info, 0);
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

	ret = bq25890_update_bits(info, BQ25890_REG_14, REG14_REG_RESET_MASK,
				  REG14_REG_RESET << REG14_REG_RESET_SHIFT);
	if (ret) {
		dev_err(info->dev, "reset bq25890 failed\n");
		return ret;
	}

	ret = bq25890_charger_set_vindpm(info, info->vol_max_mv);
	if (ret) {
		dev_err(info->dev, "set bq25890 vindpm vol failed\n");
		return ret;
	}

	ret = bq25890_charger_set_termina_vol(info, info->vol_max_mv);
	if (ret) {
		dev_err(info->dev, "set bq25890 terminal vol failed\n");
		return ret;
	}

	ret = bq25890_charger_set_termina_cur(info, info->termination_cur);
	if (ret) {
		dev_err(info->dev, "set bq25890 terminal cur failed\n");
		return ret;
	}

	ret = bq25890_charger_set_limit_current(info, info->cur.unknown_cur, false);
	if (ret)
		dev_err(info->dev, "set bq25890 limit current failed\n");

	return ret;
}

static int bq25890_charger_set_wdg_timer(struct bq25890_charger_info *info, u32 timer)
{
	u8 reg_val = 0;

	if (timer < 40)
		reg_val = REG07_WDT_DISABLE;
	else if (timer < 80)
		reg_val = REG07_WDT_40S;
	else if (timer < 160)
		reg_val = REG07_WDT_80S;
	else
		reg_val = REG07_WDT_160S;

	return bq25890_update_bits(info, BQ25890_REG_07,
					REG07_WDT_MASK,
					reg_val << REG07_WDT_SHIFT);
}

static int bq25890_enable_charger(struct bq25890_charger_info *info)
{
	return bq25890_update_bits(info, BQ25890_REG_03, REG03_CHG_CONFIG_MASK,
				   REG03_CHG_ENABLE << REG03_CHG_CONFIG_SHIFT);
}

static int bq25890_disable_charger(struct bq25890_charger_info *info)
{
	return bq25890_update_bits(info, BQ25890_REG_03, REG03_CHG_CONFIG_MASK,
				   REG03_CHG_DISABLE << REG03_CHG_CONFIG_SHIFT);
}

static int bq25890_charger_start_charge(struct bq25890_charger_info *info)
{
	int ret;

/*
	ret = regmap_update_bits(info->pmic, info->charger_pd,
				 info->charger_pd_mask, 0);
	if (ret)
		dev_err(info->dev, "enable bq25890 charge_pd failed\n");
*/
	ret = bq25890_enable_charger(info);
	if (ret)
		dev_err(info->dev, "enable bq25890 charge failed\n");

	return ret;
}

static void bq25890_charger_stop_charge(struct bq25890_charger_info *info)
{
	int ret;

	ret = bq25890_disable_charger(info);
	if (ret)
		dev_err(info->dev, "disable bq25890 charge, ret = %d\n", ret);
/*
	ret = regmap_update_bits(info->pmic, info->charger_pd,
				 info->charger_pd_mask,
				 info->charger_pd_mask);
	if (ret)
		dev_err(info->dev, "disable bq25890 charge_pd failed\n");
*/
}

static int bq25890_charger_get_charge_status(struct bq25890_charger_info *info, unsigned int *en)
{
	int ret = 0;
	u8 reg_val = 0x00;

	ret = bq25890_read(info, BQ25890_REG_03, &reg_val);
	if (ret) {
		*en = 0;
		bq_err("get bq25890 charge status failed\n");
		return 0;
	}

	*en = (reg_val & REG03_CHG_CONFIG_MASK) ? 1 : 0;

	return 0;
}

static int bq25890_charger_set_current(struct bq25890_charger_info *info, u32 cur)
{
	u8 reg_val;
	int ret;

	cur = cur / 1000;
	if (cur <= REG04_ICHG_MIN)
		cur = REG04_ICHG_MIN;
	else if (cur >= REG04_ICHG_MAX)
		cur = REG04_ICHG_MAX;
	reg_val = cur / REG04_ICHG_STEP;

	ret = bq25890_update_bits(info, BQ25890_REG_04, REG04_ICHG_MASK, reg_val);

	return ret;
}

static int bq25890_charger_get_current(struct bq25890_charger_info *info, u32 *cur)
{
	u8 reg_val;
	int ret;

	ret = bq25890_read(info, BQ25890_REG_04, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= REG04_ICHG_MASK;
	*cur = reg_val * REG04_ICHG_STEP * 1000;

	return 0;
}

static int bq25890_charger_set_limit_current(struct bq25890_charger_info *info,
					     u32 limit_cur, bool enable)
{
	u8 reg_val;
	int ret = 0;

	mutex_lock(&info->input_limit_cur_lock);
	if (enable) {
		ret = bq25890_charger_get_limit_current(info, &limit_cur);
		if (ret) {
			dev_err(info->dev, "get limit cur failed\n");
			goto out;
		}

		if (limit_cur == info->actual_limit_cur)
			goto out;
		limit_cur = info->actual_limit_cur;
	}

	ret = bq25890_update_bits(info, BQ25890_REG_00, REG00_EN_ILIM_MASK,
				  REG00_EN_ILIM_DISABLE);
	if (ret) {
		dev_err(info->dev, "disable en_ilim failed\n");
		goto out;
	}

	limit_cur = limit_cur / 1000;
	if (limit_cur >= REG00_IINLIM_MAX)
		limit_cur = REG00_IINLIM_MAX;
	if (limit_cur <= REG00_IINLIM_MIN)
		limit_cur = REG00_IINLIM_MIN;

	info->last_limit_current = limit_cur * 1000;
	reg_val = (limit_cur - REG00_IINLIM_OFFSET) / REG00_IINLIM_STEP;
	info->actual_limit_cur =
		(reg_val * REG00_IINLIM_STEP + REG00_IINLIM_OFFSET) * 1000;
	ret = bq25890_update_bits(info, BQ25890_REG_00, REG00_IINLIM_MASK, reg_val);
	if (ret)
		dev_err(info->dev, "set bq25890 limit cur failed\n");

	dev_info(info->dev, "set limit current reg_val = %#x, actual_limit_cur = %d\n",
		 reg_val, info->actual_limit_cur);

out:
	mutex_unlock(&info->input_limit_cur_lock);

	return ret;
}

static u32 bq25890_charger_get_limit_current(struct bq25890_charger_info *info, u32 *limit_cur)
{
	u8 reg_val;
	int ret;

	ret = bq25890_read(info, BQ25890_REG_00, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= REG00_IINLIM_MASK;
	*limit_cur = reg_val * REG00_IINLIM_STEP + REG00_IINLIM_OFFSET;
	if (*limit_cur >= REG00_IINLIM_MAX)
		*limit_cur = REG00_IINLIM_MAX * 1000;
	else
		*limit_cur = *limit_cur * 1000;

	return 0;
}

static inline int bq25890_charger_get_health(struct bq25890_charger_info *info,
					     u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int bq25890_charger_feed_watchdog(struct bq25890_charger_info *info)
{
	int ret = 0;

	ret = bq25890_update_bits(info, BQ25890_REG_03, REG03_WDT_RESET_MASK,
				  REG03_WDT_RESET << REG03_WDT_RESET_SHIFT);
	if (ret) {
		dev_err(info->dev, "reset bq25890 failed\n");
		return ret;
	}

	return ret;
}

static inline int bq25890_charger_get_status(struct bq25890_charger_info *info)
{
	if (info->charging)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int bq25890_charger_config_is_enabled(struct bq25890_charger_info *info)
{
	int ret;
	u8 val;

	ret = bq25890_read(info, BQ25890_REG_03, &val);
	if (ret) {
		bq_err("failed to get bq25890 otg status\n");
		return ret;
	}

	val &= REG03_CHG_CONFIG_MASK;

	return val;
}

static int bq25890_charger_get_shipmode(struct bq25890_charger_info *info, u32 *val)
{
	u8 data = 0;
	int ret = 0;

	ret = bq25890_read(info, BQ25890_REG_09, &data);
	if (ret < 0)
		return ret;
	data = (data & REG09_BATFET_DIS_MASK) >> REG09_BATFET_DIS_SHIFT;
	*val = !data;

	return 0;
}

static int bq25890_charger_set_shipmode(struct bq25890_charger_info *info,
					  u32 val)
{
	int ret = 0;

	ret = bq25890_update_bits(info, BQ25890_REG_07, REG07_WDT_MASK,
			REG07_WDT_DISABLE << REG07_WDT_SHIFT);
	if (ret)
		bq_err("disable bq25890 wdt failed\n");

	ret = bq25890_update_bits(info, BQ25890_REG_09, REG09_BATFET_DIS_MASK,
			REG09_BATFET_DIS_ENABLE << REG09_BATFET_DIS_SHIFT);

	if (ret)
		bq_err("set_shipmode failed\n");
	bq_info("bq25890: set shipmode #####\n");

	return ret;
}

static ssize_t bq25890_reg_val_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct bq25890_charger_sysfs *bq25890_sysfs =
		container_of(attr, struct bq25890_charger_sysfs,
			     attr_bq25890_reg_val);
	struct bq25890_charger_info *info = bq25890_sysfs->info;
	u8 val;
	int ret;

	if (!info)
		return sprintf(buf, "%s bq25890_sysfs->info is null\n", __func__);

	ret = bq25890_read(info, reg_tab[info->reg_id].addr, &val);
	if (ret) {
		dev_err(info->dev, "fail to get bq25890_REG_0x%.2x value, ret = %d\n",
			reg_tab[info->reg_id].addr, ret);
		return sprintf(buf, "fail to get bq25890_REG_0x%.2x value\n",
			       reg_tab[info->reg_id].addr);
	}

	return sprintf(buf, "bq25890_REG_0x%.2x = 0x%.2x\n",
		       reg_tab[info->reg_id].addr, val);
}

static ssize_t bq25890_reg_val_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct bq25890_charger_sysfs *bq25890_sysfs =
		container_of(attr, struct bq25890_charger_sysfs,
			     attr_bq25890_reg_val);
	struct bq25890_charger_info *info = bq25890_sysfs->info;
	u8 val;
	int ret;

	if (!info) {
		dev_err(dev, "%s bq25890_sysfs->info is null\n", __func__);
		return count;
	}

	ret =  kstrtou8(buf, 16, &val);
	if (ret) {
		dev_err(info->dev, "fail to get addr, ret = %d\n", ret);
		return count;
	}

	ret = bq25890_write(info, reg_tab[info->reg_id].addr, val);
	if (ret) {
		dev_err(info->dev, "fail to wite 0x%.2x to REG_0x%.2x, ret = %d\n",
				val, reg_tab[info->reg_id].addr, ret);
		return count;
	}

	dev_info(info->dev, "wite 0x%.2x to REG_0x%.2x success\n", val,
		 reg_tab[info->reg_id].addr);
	return count;
}

static ssize_t bq25890_reg_id_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct bq25890_charger_sysfs *bq25890_sysfs =
		container_of(attr, struct bq25890_charger_sysfs,
			     attr_bq25890_sel_reg_id);
	struct bq25890_charger_info *info = bq25890_sysfs->info;
	int ret, id;

	if (!info) {
		dev_err(dev, "%s bq25890_sysfs->info is null\n", __func__);
		return count;
	}

	ret =  kstrtoint(buf, 10, &id);
	if (ret) {
		dev_err(info->dev, "%s store register id fail\n", bq25890_sysfs->name);
		return count;
	}

	if (id < 0 || id >= BQ25890_REG_NUM) {
		dev_err(info->dev, "%s store register id fail, id = %d is out of range\n",
			bq25890_sysfs->name, id);
		return count;
	}

	info->reg_id = id;

	dev_info(info->dev, "%s store register id = %d success\n", bq25890_sysfs->name, id);
	return count;
}

static ssize_t bq25890_reg_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bq25890_charger_sysfs *bq25890_sysfs =
		container_of(attr, struct bq25890_charger_sysfs,
			     attr_bq25890_sel_reg_id);
	struct bq25890_charger_info *info = bq25890_sysfs->info;

	if (!info)
		return sprintf(buf, "%s bq25890_sysfs->info is null\n", __func__);

	return sprintf(buf, "Cuurent register id = %d\n", info->reg_id);
}

static ssize_t bq25890_reg_table_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct bq25890_charger_sysfs *bq25890_sysfs =
		container_of(attr, struct bq25890_charger_sysfs,
			     attr_bq25890_lookup_reg);
	struct bq25890_charger_info *info = bq25890_sysfs->info;
	int i, len, idx = 0;
	char reg_tab_buf[2000];

	if (!info)
		return sprintf(buf, "%s bq25890_sysfs->info is null\n", __func__);

	memset(reg_tab_buf, '\0', sizeof(reg_tab_buf));
	len = snprintf(reg_tab_buf + idx, sizeof(reg_tab_buf) - idx,
		       "Format: [id] [addr] [desc]\n");
	idx += len;

	for (i = 0; i < BQ25890_REG_NUM; i++) {
		len = snprintf(reg_tab_buf + idx, sizeof(reg_tab_buf) - idx,
			       "[%d] [REG_0x%.2x] [%s]; \n",
			       reg_tab[i].id, reg_tab[i].addr, reg_tab[i].name);
		idx += len;
	}

	return sprintf(buf, "%s\n", reg_tab_buf);
}

static ssize_t bq25890_dump_reg_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct bq25890_charger_sysfs *bq25890_sysfs =
		container_of(attr, struct bq25890_charger_sysfs,
			     attr_bq25890_dump_reg);
	struct bq25890_charger_info *info = bq25890_sysfs->info;

	if (!info)
		return sprintf(buf, "%s bq25890_sysfs->info is null\n", __func__);

	bq25890_charger_dump_register(info);

	return sprintf(buf, "%s\n", bq25890_sysfs->name);
}

static int bq25890_register_sysfs(struct bq25890_charger_info *info)
{
	struct bq25890_charger_sysfs *bq25890_sysfs;
	int ret;

	bq25890_sysfs = devm_kzalloc(info->dev, sizeof(*bq25890_sysfs), GFP_KERNEL);
	if (!bq25890_sysfs)
		return -ENOMEM;

	info->sysfs = bq25890_sysfs;
	bq25890_sysfs->name = "bq25890_sysfs";
	bq25890_sysfs->info = info;
	bq25890_sysfs->attrs[0] = &bq25890_sysfs->attr_bq25890_dump_reg.attr;
	bq25890_sysfs->attrs[1] = &bq25890_sysfs->attr_bq25890_lookup_reg.attr;
	bq25890_sysfs->attrs[2] = &bq25890_sysfs->attr_bq25890_sel_reg_id.attr;
	bq25890_sysfs->attrs[3] = &bq25890_sysfs->attr_bq25890_reg_val.attr;
	bq25890_sysfs->attrs[4] = NULL;
	bq25890_sysfs->attr_g.name = "debug";
	bq25890_sysfs->attr_g.attrs = bq25890_sysfs->attrs;

	sysfs_attr_init(&bq25890_sysfs->attr_bq25890_dump_reg.attr);
	bq25890_sysfs->attr_bq25890_dump_reg.attr.name = "bq25890_dump_reg";
	bq25890_sysfs->attr_bq25890_dump_reg.attr.mode = 0444;
	bq25890_sysfs->attr_bq25890_dump_reg.show = bq25890_dump_reg_show;

	sysfs_attr_init(&bq25890_sysfs->attr_bq25890_lookup_reg.attr);
	bq25890_sysfs->attr_bq25890_lookup_reg.attr.name = "bq25890_lookup_reg";
	bq25890_sysfs->attr_bq25890_lookup_reg.attr.mode = 0444;
	bq25890_sysfs->attr_bq25890_lookup_reg.show = bq25890_reg_table_show;

	sysfs_attr_init(&bq25890_sysfs->attr_bq25890_sel_reg_id.attr);
	bq25890_sysfs->attr_bq25890_sel_reg_id.attr.name = "bq25890_sel_reg_id";
	bq25890_sysfs->attr_bq25890_sel_reg_id.attr.mode = 0644;
	bq25890_sysfs->attr_bq25890_sel_reg_id.show = bq25890_reg_id_show;
	bq25890_sysfs->attr_bq25890_sel_reg_id.store = bq25890_reg_id_store;

	sysfs_attr_init(&bq25890_sysfs->attr_bq25890_reg_val.attr);
	bq25890_sysfs->attr_bq25890_reg_val.attr.name = "bq25890_reg_val";
	bq25890_sysfs->attr_bq25890_reg_val.attr.mode = 0644;
	bq25890_sysfs->attr_bq25890_reg_val.show = bq25890_reg_val_show;
	bq25890_sysfs->attr_bq25890_reg_val.store = bq25890_reg_val_store;

	ret = sysfs_create_group(&info->psy_usb->dev.kobj, &bq25890_sysfs->attr_g);
	if (ret < 0)
		dev_err(info->dev, "Cannot create sysfs , ret = %d\n", ret);

	return ret;
}

static int bq25890_charger_usb_get_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq25890_charger_info *info = power_supply_get_drvdata(psy);
	u32 cur, health, enabled = 0;
	int ret = 0;

	if (!info)
		return -ENOMEM;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq25890 is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bq25890_charger_get_status(info);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = bq25890_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = bq25890_charger_get_limit_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
			val->intval = 0;
		} else {
			ret = bq25890_charger_get_health(info, &health);
			if (ret)
				goto out;

			val->intval = health;
		}
		break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		ret = regmap_read(info->pmic, info->charger_pd, &enabled);
		if (ret) {
			dev_err(info->dev, "get bq25890 charge status failed\n");
			goto out;
		}

		val->intval = !(enabled & info->charger_pd_mask);
		break;

	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int bq25890_charger_usb_set_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    const union power_supply_propval *val)
{
	struct bq25890_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info)
		return -ENOMEM;

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	return 0;
#endif

	return ret;
}

static int bq25890_charger_property_is_writeable(struct power_supply *psy,
						 enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_CALIBRATE:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct power_supply_desc bq25890_charger_desc = {
	.name			= "charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= bq25890_usb_props,
	.num_properties		= ARRAY_SIZE(bq25890_usb_props),
	.get_property		= bq25890_charger_usb_get_property,
	.set_property		= bq25890_charger_usb_set_property,
	.property_is_writeable	= bq25890_charger_property_is_writeable,
	.usb_types		= bq25890_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq25890_charger_usb_types),
};

static int zte_bq25890_charger_usb_get_property(struct zte_power_supply *psy,
					    enum zte_power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq25890_charger_info *info = zte_power_supply_get_drvdata(psy);
	u32 enabled = 0;
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq25890 is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		enabled = bq25890_charger_config_is_enabled(info);
		val->intval = !!enabled;
		break;
	case POWER_SUPPLY_PROP_TUNING_VINDPM:
		ret = bq25890_charger_get_vindpm(info, &val->intval);
		if (ret < 0)
			bq_err("get vindpm failed\n");
		bq_info("bq25890_1#[REG]: get vindpm: %d!", val->intval);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		ret = bq25890_charger_get_recharge_voltage(info, &val->intval);
		if (ret < 0)
			bq_err("get charge recharge_voltage failed\n");
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = bq25890_charger_get_shipmode(info, &val->intval);
		if (ret < 0)
			bq_err("get shipmode failed\n");
		bq_info("bq25890[REG]: get shipmode: %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq25890_charger_usb_set_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq25890_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq25890 is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		if (val->intval == 0) {
			bq_info("bq25890[REG]: set shipmode %d", val->intval);
			ret = bq25890_charger_set_shipmode(info, val->intval);
			if (ret < 0)
				bq_err("set shipmode failed\n");
		} else
			bq_info("bq25890[REG]: set shipmode invalid val %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq25890_charger_property_is_writeable(struct zte_power_supply *psy,
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

static const struct zte_power_supply_desc zte_bq25890_charger_desc = {
	.name			= "zte_charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_bq25890_usb_props,
	.num_properties		= ARRAY_SIZE(zte_bq25890_usb_props),
	.get_property		= zte_bq25890_charger_usb_get_property,
	.set_property		= zte_bq25890_charger_usb_set_property,
	.property_is_writeable	= zte_bq25890_charger_property_is_writeable,
	.usb_types		= bq25890_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq25890_charger_usb_types),
};

static int zte_bq25890_slave_charger_usb_get_property(struct zte_power_supply *psy,
					    enum zte_power_supply_property psp,
					    union power_supply_propval *val)
{
	struct bq25890_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		val->intval = 0;
		bq_info("%s bq25890 is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		ret = bq25890_charger_get_shipmode(info, &val->intval);
		if (ret < 0)
			bq_err("get shipmode failed\n");
		bq_info("bq25890[REG]: get shipmode: %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static int zte_bq25890_slave_charger_usb_set_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq25890_charger_info *info = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info->init_finished) {
		bq_info("%s bq25890 is not inited", __func__);
		return 0;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		if (val->intval == 0) {
			bq_info("bq25890[REG]: set shipmode %d", val->intval);
			ret = bq25890_charger_set_shipmode(info, val->intval);
			if (ret < 0)
				bq_err("set shipmode failed\n");
		} else
			bq_info("bq25890[REG]: set shipmode invalid val %d!", val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int zte_bq25890_slave_charger_property_is_writeable(struct zte_power_supply *psy,
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

static const struct zte_power_supply_desc zte_bq25890_slave_charger_desc = {
	.name			= "zte_slave_charger_psy",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= zte_bq25890_slave_usb_props,
	.num_properties		= ARRAY_SIZE(zte_bq25890_slave_usb_props),
	.get_property		= zte_bq25890_slave_charger_usb_get_property,
	.set_property		= zte_bq25890_slave_charger_usb_set_property,
	.property_is_writeable	= zte_bq25890_slave_charger_property_is_writeable,
	.usb_types		= bq25890_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq25890_charger_usb_types),
};

static void bq25890_charger_feed_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq25890_charger_info *info = container_of(dwork,
							 struct bq25890_charger_info,
							 wdt_work);
	int ret;

	ret = bq25890_charger_feed_watchdog(info);
	if (ret)
		schedule_delayed_work(&info->wdt_work, HZ * 5);
	else
		schedule_delayed_work(&info->wdt_work, HZ * 15);
}

#if IS_ENABLED(CONFIG_REGULATOR)
static bool bq25890_charger_check_otg_valid(struct bq25890_charger_info *info)
{
	int ret;
	u8 value = 0;
	bool status = false;

	ret = bq25890_read(info, BQ25890_REG_03, &value);
	if (ret) {
		dev_err(info->dev, "get bq25890 charger otg valid status failed\n");
		return status;
	}

	if (value & REG03_OTG_CONFIG_MASK)
		status = true;
	else
		dev_err(info->dev, "otg is not valid, REG_3 = 0x%x\n", value);

	return status;
}

static bool bq25890_charger_check_otg_fault(struct bq25890_charger_info *info)
{
	int ret;
	u8 value = 0;
	bool status = true;

	ret = bq25890_read(info, BQ25890_REG_0C, &value);
	if (ret) {
		dev_err(info->dev, "get bq25890 charger otg fault status failed\n");
		return status;
	}

	if (!(value & REG0C_FAULT_BOOST_MASK))
		status = false;
	else
		dev_err(info->dev, "boost fault occurs, REG_0C = 0x%x\n",
			value);

	return status;
}

static void bq25890_charger_otg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct bq25890_charger_info *info = container_of(dwork,
			struct bq25890_charger_info, otg_work);
	bool otg_valid = bq25890_charger_check_otg_valid(info);
	bool otg_fault;
	int ret, retry = 0;

	if (otg_valid)
		goto out;

	do {
		otg_fault = bq25890_charger_check_otg_fault(info);
		if (!otg_fault) {
			ret = bq25890_update_bits(info, BQ25890_REG_03,
						  REG03_OTG_CONFIG_MASK,
						  REG03_OTG_ENABLE << REG03_OTG_CONFIG_SHIFT);
			if (ret)
				dev_err(info->dev, "restart bq25890 charger otg failed\n");
		}

		otg_valid = bq25890_charger_check_otg_valid(info);
	} while (!otg_valid && retry++ < BQ25890_OTG_RETRY_TIMES);

	if (retry >= BQ25890_OTG_RETRY_TIMES) {
		dev_err(info->dev, "Restart OTG failed\n");
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

static int bq25890_charger_enable_otg(struct regulator_dev *dev)
{
	struct bq25890_charger_info *info = rdev_get_drvdata(dev);
	int ret;

	if (!info->init_finished) {
		bq_info("%s bq25890 is not inited", __func__);
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
			dev_err(info->dev, "failed to disable bc1.2 detect function.\n");
			return ret;
		}
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	zte_bq2560x_set_prop_by_name("zte_cp_charger_psy", POWER_SUPPLY_PROP_USB_OTG, 1);
#endif

	ret = bq25890_charger_set_boost_cur(info, 1200);
	if (ret)
		dev_err(info->dev, "failed to set boost current\n");

	bq25890_charger_set_boost_vol(info, REG0A_BOOSTV_MAX);
	if (ret)
		dev_err(info->dev, "failed to set boost voltage\n");

	ret = bq25890_update_bits(info, BQ25890_REG_03, REG03_OTG_CONFIG_MASK,
				  REG03_OTG_ENABLE << REG03_OTG_CONFIG_SHIFT);
	if (ret) {
		dev_err(info->dev, "enable bq25890 otg failed\n");
		regmap_update_bits(info->pmic, info->charger_detect,
				   BIT_DP_DM_BC_ENB, 0);
		return ret;
	}

	ret = bq25890_charger_set_wdg_timer(info, 40);
	if (ret) {
		dev_err(info->dev, "%s, failed to enable watchdog, ret = %d\n", __func__, ret);
		return ret;
	}

	ret = bq25890_charger_feed_watchdog(info);
	if (ret) {
		dev_err(info->dev, "%s, failed to feed watchdog, ret = %d\n", __func__, ret);
		return ret;
	}

	bq25890_charger_dump_register(info);

	schedule_delayed_work(&info->wdt_work,
			      msecs_to_jiffies(BQ25890_WDT_VALID_MS));
	schedule_delayed_work(&info->otg_work,
			      msecs_to_jiffies(BQ25890_OTG_VALID_MS));

	dev_info(info->dev, "%s[%d], enable_otg\n", __func__, __LINE__);
	return 0;
}

static int bq25890_charger_disable_otg(struct regulator_dev *dev)
{
	struct bq25890_charger_info *info = rdev_get_drvdata(dev);
	int ret = 0;

	info->otg_enable = false;
	pm_wakeup_event(info->dev, MSEC_PER_SEC);

	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);

	ret = bq25890_update_bits(info, BQ25890_REG_03,
				  REG03_OTG_CONFIG_MASK, REG03_OTG_DISABLE);
	if (ret) {
		dev_err(info->dev, "disable bq25890 otg failed\n");
		return ret;
	}

	ret = bq25890_charger_set_wdg_timer(info, 0);
	if (ret) {
		dev_err(info->dev, "%s, failed to disable watchdog, ret = %d\n", __func__, ret);
		return ret;
	}

	/* Enable charger detection function to identify the charger type */
	if (!info->use_typec_extcon) {
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);
		if (ret)
			dev_err(info->dev, "enable BC1.2 failed\n");
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	zte_bq2560x_set_prop_by_name("zte_cp_charger_psy", POWER_SUPPLY_PROP_USB_OTG, 0);
#endif

	dev_info(info->dev, "%s[%d], disable_otg\n", __func__, __LINE__);
	return ret;
}

static int bq25890_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct bq25890_charger_info *info = rdev_get_drvdata(dev);
	int ret;
	u8 val;

	ret = bq25890_read(info, BQ25890_REG_03, &val);
	if (ret) {
		dev_err(info->dev, "failed to get bq25890 otg status\n");
		return ret;
	}

	val &= REG03_OTG_CONFIG_MASK;

	return val;
}

static const struct regulator_ops bq25890_charger_vbus_ops = {
	.enable = bq25890_charger_enable_otg,
	.disable = bq25890_charger_disable_otg,
	.is_enabled = bq25890_charger_vbus_is_enabled,
};

static const struct regulator_desc bq25890_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &bq25890_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int bq25890_charger_register_vbus_regulator(struct bq25890_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = info->dev;
	cfg.driver_data = info;
	reg = devm_regulator_register(info->dev,
				      &bq25890_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(info->dev, "Can't register regulator:%d\n", ret);
	}

	return ret;
}

static int bq25890_charger_register_external_vbus_regulator(struct bq25890_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;
	struct device_node *otg_nd;
	struct device_node *otg_parent_nd;
	struct platform_device *otg_parent_nd_pdev;

	otg_nd = of_find_node_by_name(NULL, "otg-vbus");
	if (!otg_nd) {
		dev_warn(info->dev, "%s, unable to get otg node\n", __func__);
		return -EPROBE_DEFER;
	}

	otg_parent_nd = of_get_parent(otg_nd);
	of_node_put(otg_nd);
	if (!otg_parent_nd) {
		dev_warn(info->dev, "%s, unable to get otg parent node\n", __func__);
		return -EPROBE_DEFER;
	}

	otg_parent_nd_pdev = of_find_device_by_node(otg_parent_nd);
	of_node_put(otg_parent_nd);
	if (!otg_parent_nd_pdev) {
		dev_warn(info->dev, "%s, unable to get otg parent node device\n", __func__);
		return -EPROBE_DEFER;
	}

	cfg.dev = &otg_parent_nd_pdev->dev;
	platform_device_put(otg_parent_nd_pdev);
	cfg.driver_data = info;
	reg = devm_regulator_register(cfg.dev, &bq25890_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_warn(info->dev, "%s, failed to register vddvbus regulator:%d\n",
			 __func__, ret);
	}

	return ret;
}

#else
static int bq25890_charger_register_vbus_regulator(struct bq25890_charger_info *info)
{
	return 0;
}

static int bq25890_charger_register_external_vbus_regulator(struct bq25890_charger_info *info)
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
		pr_info("bq25890:psy name is NULL!!\n");
		goto failed_loop;
	}

	psy = zte_power_supply_get_by_name(name);
	if (!psy) {
		pr_info("bq25890:get %s psy failed!!\n", name);
		goto failed_loop;
	}

	val.intval = data;

	rc = zte_power_supply_set_property(psy,
				psp, &val);
	if (rc < 0) {
		pr_info("bq25890:Failed to set %s property:%d rc=%d\n", name, psp, rc);
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
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;

	arg = arg ? arg : NULL;
	if (info && info->support_single_buck_9V)
		*chg_type = SQC_PMIC_TYPE_BUCK_5A;
	else
		*chg_type = SQC_PMIC_TYPE_BUCK;

	return 0;
}

static int sqc_mp_get_chg_status(void *arg, unsigned int *charing_status)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;
	u8 reg_val = 0;

	ret = bq25890_read(info, BQ25890_REG_13, &reg_val);
	if (ret) {
		bq_err("get bq25890 aicl status failed\n");
		return 0;
	}

	bq_err("get bq25890 aicl status 0x%02X\n", reg_val);

	*charing_status = (reg_val & REG13_VDPM_STAT_MASK) ? 1 : 0;

	return 0;
}

static int sqc_mp_set_enable_chging(void *arg, unsigned int en)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;

	if (en) {
		bq25890_charger_set_wdg_timer(info, 80);
		bq25890_charger_feed_watchdog(info);

		if (info->hw_mode == BQ25601_SLAVE) {
			if (info->gpiod)
				gpiod_set_value_cansleep(info->gpiod, 1);
			bq_info("bq25890[REG]: set battery hiz false\n");
			bq25890_charger_enable_hiz(info, false);
		}

		bq25890_charger_start_charge(info);

		bq25890_charger_set_vindpm(info, 4600);

		info->charging = true;

		bq25890_charger_dump_register(info);
	} else {
		bq25890_charger_stop_charge(info);

		bq25890_charger_set_wdg_timer(info, 0);

		if (info->hw_mode == BQ25601_SLAVE) {
			bq_info("bq2560x[REG]: set battery hiz true\n");
			bq25890_charger_enable_hiz(info, true);
			if (info->gpiod)
				gpiod_set_value_cansleep(info->gpiod, 0);
		}
		info->charging = false;

		bq25890_charger_dump_register(info);
	}

	bq_info("bq25890 %s %d\n", __func__, en);

	return 0;
}

static int sqc_mp_get_enable_chging(void *arg, unsigned int *en)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_charge_status(info, en);

	bq_info("bq25890 %s %d\n", __func__, *en);

	return ret;
}

static int sqc_mp_get_ichg(void *arg, u32 *ichg_ma)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_current(info, ichg_ma);

	*ichg_ma = *ichg_ma / 1000;

	bq_info("bq25890 %s %d\n", __func__, *ichg_ma);

	return ret;
}

static int sqc_mp_set_ichg(void *arg, u32 mA)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_current(info, mA * 1000);

	bq_info("bq25890 %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_set_ieoc(void *arg, u32 mA)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_termina_cur(info, mA);

	bq_info("bq25890 %s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_get_ieoc(void *arg, u32 *ieoc)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_termia_cur(info, ieoc);

	*ieoc = *ieoc / 1000;

	bq_info("bq25890 %s %d\n", __func__, *ieoc);

	return ret;
}

static int sqc_mp_get_aicr(void *arg, u32 *aicr_ma)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_limit_current(info, aicr_ma);

	bq_info("bq25890 %s %d\n", __func__, *aicr_ma);

	return ret;
}

static int sqc_mp_set_aicr(void *arg, u32 aicr_ma)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_limit_current(info, aicr_ma * 1000, false);

	bq_info("bq25890 %s %d\n", __func__, aicr_ma);

	return ret;
}

static int sqc_mp_get_cv(void *arg, u32 *cv_mv)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_termina_vol(info, cv_mv);

	bq_info("bq25890 %s %d\n", __func__, *cv_mv);

	return ret;
}

static int sqc_mp_set_cv(void *arg, u32 cv_mv)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_termina_vol(info, cv_mv);

	bq_info("bq25890 %s %d\n", __func__, cv_mv);

	return ret;
}

static int sqc_mp_get_rechg_voltage(void *arg, u32 *rechg_volt_mv)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_recharge_voltage(info, rechg_volt_mv);

	*rechg_volt_mv = *rechg_volt_mv / 1000;

	bq_info("bq25890 %s %d\n", __func__, *rechg_volt_mv);

	return ret;
}

static int sqc_mp_set_rechg_voltage(void *arg, u32 rechg_volt_mv)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_recharge_voltage(info, rechg_volt_mv * 1000);

	bq_info("bq25890 %s %d\n", __func__, rechg_volt_mv);

	return ret;
}

static int sqc_mp_ovp_volt_get(void *arg, u32 *ac_ovp_mv)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_ovp(info, ac_ovp_mv);

	bq_info("bq25890 %s %d\n", __func__, *ac_ovp_mv);

	return ret;
}

static int sqc_mp_ovp_volt_set(void *arg, u32 ac_ovp_mv)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_ovp(info, ac_ovp_mv);

	bq_info("bq25890 %s %d\n", __func__, ac_ovp_mv);

	return ret;
}

static int sqc_mp_get_vbat(void *arg, u32 *mV)
{
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	union power_supply_propval batt_vol_uv;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ25890_BATTERY_NAME);
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
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ25890_BATTERY_NAME);
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
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)arg;
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(BQ25890_BATTERY_NAME);
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
	struct bq25890_charger_info *chg_dev = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_set_powerpath(chg_dev, enabled);

	return ret;
}

static int sqc_enable_powerpath_get(void *arg, unsigned int *enabled)
{
	struct bq25890_charger_info *chg_dev = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_powerpath(chg_dev, enabled);

	return ret;
}

static int sqc_enable_hiz_set(void *arg, unsigned int enable)
{
	struct bq25890_charger_info *chg_dev = (struct bq25890_charger_info *)arg;

	bq25890_charger_enable_hiz(chg_dev, !!enable);

	return 0;
}

static int sqc_enable_hiz_get(void *arg, unsigned int *enable)
{
	struct bq25890_charger_info *chg_dev = (struct bq25890_charger_info *)arg;
	int ret = 0;

	ret = bq25890_charger_get_hiz_status(chg_dev, enable);

	return ret;
}

static struct sqc_pmic_chg_ops bq25890_sqc_chg_ops = {

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
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)sqc_bc1d2_proto_node.arg;
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
	struct bq25890_charger_info *info = (struct bq25890_charger_info *)sqc_bc1d2_proto_node.arg;

	sscanf(val, "%d", &sleep_mode_enable);

	bq_info("sleep_mode_enable = %d\n", sleep_mode_enable);

	if (sleep_mode_enable) {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			bq_info("sleep on status");

			/*disabel sqc-daemon*/
			sqc_chg_type = SQC_SLEEP_MODE_TYPE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);

			ret = bq25890_charger_set_watchdog_timer(info, 0);
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

static int bq25890_chip_name_show(struct seq_file *m, void *v)
{
	struct bq25890_charger_info *pinfo = m->private;

	if (pinfo) {
		seq_printf(m, "%s\n", pinfo->name);
	} else {
		seq_printf(m, "%s\n", "unknown");
	}
	return 0;
}

static int bq25890_chip_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, bq25890_chip_name_show, PDE_DATA(inode));
}

static const struct proc_ops bq25890_chip_name_node = {
	.proc_open = bq25890_chip_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};

static int bq25890_charger_detect_device(struct bq25890_charger_info *info)
{
	int ret = 0, part_id = -EINVAL;
	u8 reg_val;

	ret = bq25890_read(info, BQ25890_REG_14, &reg_val);
	if (ret < 0) {
		dev_err(info->dev, "%s, failed to get device id, ret = %d\n", __func__, ret);
		return ret;
	}

	part_id = (reg_val & REG14_PN_MASK) >> REG14_PN_SHIFT;
	if (part_id != BQ25890_DEV_ID && part_id != BQ25892_DEV_ID) {
		dev_err(info->dev, "%s, the device id is 0x%x\n", __func__, part_id);
		return -EINVAL;
	}

	return ret;
}

static int bq25890_slave_chg_id_get(char *val, const void *arg)
{
	pr_info("%s,slave_chg_id: %d\n", __func__, slave_chg_id);

	return snprintf(val, PAGE_SIZE, "%u", slave_chg_id);
}

static struct zte_misc_ops slave_chg_id_node = {
	.node_name = "slave_chg_id",
	.set = NULL,
	.get = bq25890_slave_chg_id_get,
	.free = NULL,
	.arg = NULL,
};

static int bq25890_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct bq25890_charger_info *info = NULL;
	struct device_node *regmap_np = NULL;
	struct platform_device *regmap_pdev = NULL;
	struct sqc_pmic_chg_ops *sqc_ops = NULL;
	struct power_supply_config charger_cfg = { };
	int ret;

	pr_err("bq25890 probe enter");

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->client = client;
	info->dev = dev;
	info->init_finished = 0;

	mutex_init(&info->lock);
	mutex_init(&info->input_limit_cur_lock);

	pr_err("%s:client addr is 0x%x\n", __func__, info->client->addr);

	info->use_typec_extcon = device_property_read_bool(dev, "use-typec-extcon");
	info->host_status_check = device_property_read_bool(dev, "host-status-check");
	info->hw_mode = device_property_read_bool(dev, "role-slave") ? BQ25601_SLAVE : BQ25601_MASTER;
	info->support_single_buck_9V = device_property_read_bool(dev, "support-single-buck-9V");
	pr_info("support_single_buck_9V=%d\n", info->support_single_buck_9V);

	i2c_set_clientdata(client, info);

	ret = bq25890_charger_detect_device(info);
	if (ret) {
		dev_err(dev, "%s, failed to detect device, ret = %d\n", __func__, ret);
		goto err_mutex_lock;
	}
	info->name = "bq25896";

	ret = bq25890_charger_is_fgu_present(info);
	if (ret) {
		dev_err(dev, "sc27xx_fgu not ready.\n");
		ret = -EPROBE_DEFER;
		goto err_mutex_lock;
	}

	info->usb_phy = devm_usb_get_phy_by_phandle(dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		bq_err("failed to find USB phy\n");
		ret = -EPROBE_DEFER;
		goto err_mutex_lock;
	}

	info->edev = extcon_get_edev_by_phandle(info->dev, 0);
	if (IS_ERR(info->edev)) {
		bq_err("failed to find vbus extcon device.\n");
		ret = -EPROBE_DEFER;
		goto err_mutex_lock;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		if (device_property_read_bool(dev, "otg-vbus-node-external"))
			ret = bq25890_charger_register_external_vbus_regulator(info);
		else
			ret = bq25890_charger_register_vbus_regulator(info);

		if (ret) {
			dev_err(dev, "failed to register vbus regulator.\n");
			ret = -EPROBE_DEFER;
			goto err_mutex_lock;
		}
	}

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,sc27xx-syscon");
	if (!regmap_np)
		regmap_np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");

	if (regmap_np) {
		if (of_device_is_compatible(regmap_np->parent, "sprd,sc2721"))
			info->charger_pd_mask = BQ25890_DISABLE_PIN_MASK_2721;
		else
			info->charger_pd_mask = BQ25890_DISABLE_PIN_MASK;
	} else {
		dev_err(dev, "unable to get syscon node\n");
		ret = -ENODEV;
		goto err_mutex_lock;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 1,
					 &info->charger_detect);
	if (ret) {
		dev_err(dev, "failed to get charger_detect\n");
		ret = -EINVAL;
		goto err_mutex_lock;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 2,
					 &info->charger_pd);
	if (ret) {
		dev_err(dev, "failed to get charger_pd reg\n");
		ret = -EINVAL;
		goto err_mutex_lock;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if (!regmap_pdev) {
		of_node_put(regmap_np);
		dev_err(dev, "unable to get syscon device\n");
		ret = -ENODEV;
		goto err_mutex_lock;
	}

	of_node_put(regmap_np);
	info->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!info->pmic) {
		dev_err(dev, "unable to get pmic regmap device\n");
		ret = -ENODEV;
		goto err_mutex_lock;
	}

	info->gpiod = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(info->gpiod)) {
		bq_err("failed to get enable gpiod\n");
		info->gpiod = NULL;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		charger_cfg.drv_data = info;
		charger_cfg.of_node = dev->of_node;
		info->psy_usb = devm_power_supply_register(dev,
							   &bq25890_charger_desc,
							   &charger_cfg);

		if (IS_ERR(info->psy_usb)) {
			dev_err(dev, "failed to register power supply\n");
			ret = PTR_ERR(info->psy_usb);
			goto err_mutex_lock;
		}

		info->zte_psy_usb = zte_devm_power_supply_register(dev,
							   &zte_bq25890_charger_desc,
							   &charger_cfg);
		if (IS_ERR(info->zte_psy_usb))
			bq_err("failed to register zte power supply\n");
	}

	if (info->hw_mode == BQ25601_SLAVE) {
		charger_cfg.drv_data = info;
		charger_cfg.of_node = dev->of_node;


		info->zte_psy_usb = zte_devm_power_supply_register(dev,
							   &zte_bq25890_slave_charger_desc,
							   &charger_cfg);
		if (IS_ERR(info->zte_psy_usb)) 
			bq_err("failed to register zte power supply\n");
	}
	

	ret = bq25890_charger_hw_init(info);
	if (ret) {
		dev_err(dev, "failed to bq25890_charger_hw_init\n");
		goto err_mutex_lock;
	}

	if (info->hw_mode == BQ25601_MASTER) {
		proc_create_data("driver/chg_name", 0664, NULL, &bq25890_chip_name_node, info);
	}

	device_init_wakeup(info->dev, true);

	ret = bq25890_register_sysfs(info);
	if (ret) {
		dev_err(info->dev, "register sysfs fail, ret = %d\n", ret);
		goto err_sysfs;
	}

	ret = bq25890_update_bits(info, BQ25890_REG_07, REG07_WDT_MASK,
				  REG07_WDT_40S << REG07_WDT_SHIFT);
	if (ret) {
		dev_err(info->dev, "Failed to enable bq25890 watchdog\n");
		return ret;
	}

	alarm_init(&info->wdg_timer, ALARM_BOOTTIME, NULL);
	INIT_DELAYED_WORK(&info->otg_work, bq25890_charger_otg_work);
	INIT_DELAYED_WORK(&info->wdt_work, bq25890_charger_feed_watchdog_work);

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	if (info->hw_mode == BQ25601_SLAVE) {
		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq25890_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)info;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PARALLEL1);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto err_sysfs;
		}

		slave_chg_id = 1;
		zte_misc_register_callback(&slave_chg_id_node, info);
	}

	if (info->hw_mode == BQ25601_MASTER) {
		sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
		memcpy(sqc_ops, &bq25890_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
		sqc_ops->arg = (void *)info;
		ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PRIMARY);
		if (ret < 0) {
			bq_err("%s register sqc hal fail(%d)\n", __func__, ret);
			goto err_sysfs;
		}
#if !IS_ENABLED(CONFIG_FAST_CHARGER_SC27XX)
		sqc_bc1d2_proto_node.arg = (void *)info;
		sqc_hal_bc1d2_register(&sqc_bc1d2_proto_node);
		zte_misc_register_callback(&qc3dp_sleep_mode_node, info);
#endif
	}

#endif

	info->suspended = false;
	bq_info("%s ok, use_typec_extcon = %d\n", __func__, info->use_typec_extcon);
	info->init_finished = 1;

	return 0;

err_sysfs:
	sysfs_remove_group(&info->psy_usb->dev.kobj, &info->sysfs->attr_g);
err_mutex_lock:
	mutex_destroy(&info->input_limit_cur_lock);
	mutex_destroy(&info->lock);

	return ret;
}

static void bq25890_charger_shutdown(struct i2c_client *client)
{
	struct bq25890_charger_info *info = i2c_get_clientdata(client);
	int ret = 0;

	cancel_delayed_work_sync(&info->wdt_work);
	if (info->otg_enable) {
		info->otg_enable = false;
		cancel_delayed_work_sync(&info->otg_work);
		ret = bq25890_update_bits(info, BQ25890_REG_03,
					  REG03_OTG_CONFIG_MASK,
					  0);
		if (ret)
			dev_err(info->dev, "disable bq25890 otg failed ret = %d\n", ret);

		/* Enable charger detection function to identify the charger type */
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);
		if (ret)
			dev_err(info->dev,
				"enable charger detection function failed ret = %d\n", ret);
	}
}

static int bq25890_charger_remove(struct i2c_client *client)
{
	struct bq25890_charger_info *info = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);

	mutex_destroy(&info->input_limit_cur_lock);
	mutex_destroy(&info->lock);

	return 0;
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int bq25890_charger_suspend(struct device *dev)
{
	struct bq25890_charger_info *info = dev_get_drvdata(dev);
	ktime_t now, add;
	unsigned int wakeup_ms = BQ25890_WDG_TIMER_MS;

	if (info->otg_enable || info->is_charger_online) {
		/* feed watchdog first before suspend */
		if (bq25890_charger_feed_watchdog(info))
			dev_err(info->dev, "%s, failed to feed watchdog\n", __func__);

		cancel_delayed_work_sync(&info->wdt_work);
	}

	if (!info->otg_enable)
		return 0;

	if (info->disable_wdg) {
		if (bq25890_charger_set_wdg_timer(info, 0)) {
			dev_err(info->dev, "%s, failed to disable watchdog\n", __func__);
			return -EBUSY;
		}
	} else {
		now = ktime_get_boottime();
		add = ktime_set(wakeup_ms / MSEC_PER_SEC,
			       (wakeup_ms % MSEC_PER_SEC) * NSEC_PER_MSEC);
		alarm_start(&info->wdg_timer, ktime_add(now, add));
		pr_info("bq25890_charger set alarm, triggered at [%lld]ms\n",
				ktime_to_ms(ktime_add(now, add)));
	}

	return 0;
}

static int bq25890_charger_resume(struct device *dev)
{
	struct bq25890_charger_info *info = dev_get_drvdata(dev);

	if (info->otg_enable || info->is_charger_online) {
		/* feed watchdog first before suspend */
		bq25890_charger_feed_watchdog(info);
		schedule_delayed_work(&info->wdt_work, HZ * 15);
	}

	if (!info->otg_enable)
		return 0;

	if (info->disable_wdg) {
		if (bq25890_charger_set_wdg_timer(info, 40)) {
			dev_err(info->dev, "%s, failed to enable watchdog\n", __func__);
			return -EBUSY;
		}
	} else {
		alarm_cancel(&info->wdg_timer);
	}

	return 0;
}
#endif

static const struct dev_pm_ops bq25890_charger_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bq25890_charger_suspend,
				bq25890_charger_resume)
};

static const struct i2c_device_id bq25890_i2c_id[] = {
	{"bq25890_chg", 0},
	{}
};

static const struct of_device_id bq25890_charger_of_match[] = {
	{ .compatible = "ti,bq25890_chg", },
	{ .compatible = "sc,cx25890_chg", },
	{ }
};

MODULE_DEVICE_TABLE(of, bq25890_charger_of_match);

static struct i2c_driver bq25890_charger_driver = {
	.driver = {
		.name = "bq25890_chg",
		.of_match_table = bq25890_charger_of_match,
		.pm = &bq25890_charger_pm_ops,
	},
	.probe = bq25890_charger_probe,
	.shutdown = bq25890_charger_shutdown,
	.remove = bq25890_charger_remove,
	.id_table = bq25890_i2c_id,
};

module_i2c_driver(bq25890_charger_driver);

MODULE_AUTHOR("Changhua Zhang <Changhua.Zhang@unisoc.com>");
MODULE_DESCRIPTION("BQ25890 Charger Driver");
MODULE_LICENSE("GPL");

