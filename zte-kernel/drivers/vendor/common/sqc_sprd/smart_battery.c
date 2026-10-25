/*
 * Smart battery driver
 *
 * Copyright (C) 2024 Zhongxing Telecom Equipment
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"[ZTE_LDD_CHARGE][FG_IC][BQ40Z50]: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/workqueue.h>

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
#include <vendor/common/zte_power_supply.h>
#endif

/*#include <linux/platform_data/smart_battery.h>*/

static int dbg_enable = 1;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define smart_battery_info(fmt, args...) pr_info("battery_fg: "fmt, ##args)

#define DRVNAME "smart_battery"

#define TIMER_MS_COUNTS			1000
#define DEFAULT_MONITOR_SEC		5
#define BATTER_FULL				0x20

/* Config register values */
#define SMTBAT_SHUTDOWN_MODE_EN      1

#define SMTBAT_LEARN_MODE_DISABLE    0

/* Register data is read (and cached) at most once per second. */
#define SMTBAT_UPDATE_INTERVAL    HZ

/* Battery Status*/
#define BATTERY_INIT		0x80
#define BATTERY_DSG			0x40
#define BATTERY_FC			0x20
#define BATTERY_FD			0x20
#define BATTERY_MAX_TIME	0xFFFF

/* Battery Cell Voltage Number */
#define CELLVOL_1			1
#define CELLVOL_2			2
#define CELLVOL_3			3

/* virtual params */
#define VIRTUAL_CURRENT				666
#define VIRTUAL_VOLTAGE				3666
#define VIRTUAL_SOC					66
#define VIRTUAL_SOH					100
#define VIRTUAL_PRESET				1
#define VIRTUAL_UNPRESET			0
#define VIRTUAL_TEMPERATURE			166
#define VIRTUAL_STATUS				POWER_SUPPLY_STATUS_CHARGING
#define VIRTUAL_CYCLECOUNT			66
#define VIRTUAL_CAPACITY_REMAIN		666
#define VIRTUAL_CAPACITY_FULL		1666
#define VIRTUAL_CAPACITY_DESIGN		1666

/* default param */
#define DEFAULT_BAT_RES			135
#define DEFAULT_SLP_ENTER_CUR		300
#define DEFAULT_SLP_EXIT_CUR		300
#define DEFAULT_SLP_FILTER_CUR		100
#define DEFAULT_PWROFF_VOL_THRESD	6400
#define DEFAULT_MONITOR_SEC		5
#define DEFAULT_ALGR_VOL_THRESD1	3850
#define DEFAULT_ALGR_VOL_THRESD2	3950
#define DEFAULT_CHRG_VOL_SEL		CHRG_VOL4200MV
#define DEFAULT_CHRG_CUR_SEL		CHRG_CUR1400MA
#define DEFAULT_CHRG_CUR_INPUT		INPUT_CUR2000MA
#define DEFAULT_POFFSET			42
#define DEFAULT_MAX_SOC_OFFSET		60
#define DEFAULT_FB_TEMP			TEMP_115C
#define DEFAULT_ENERGY_MODE		0
#define DEFAULT_ZERO_RESERVE_DSOC	10
#define DEFAULT_SAMPLE_RES		20

#define REPORT_INTERVAL_SEC		13


/* Temperature 0.1°K->0.1°C  */
#define TEMPERATURE_K_TO_C		2732

/* Battery Model */
#define BAT_UNKNOW			0
#define BAT_25Wh			25
#define BAT_28Wh			28
#define BAT_38Wh			38
#define BAT_40Wh			40
#define BAT_45Wh			45

enum typec_num {
	DUAL_TYPEC = 0,
	SINGLE_TYPEC,
};

enum bat_mode {
	MODE_BATTERY = 0,
	MODE_VIRTUAL,
};

enum smtbat_regs {
	ManufacturerAccess     = 0x00,
	RemainingCapacityAlarm = 0x01,
	RemainingTimeAlarm     = 0x02,
	BatteryMode            = 0x03,
	AtRate                 = 0x04,
	AtRateTimeToFull       = 0x05,
	AtRateTimeToEmpty      = 0x06,
	AtRateOK               = 0x07,
	Temperature            = 0x08,
	VoltageNow             = 0x09,
	CurrentNow             = 0x0A,
	AverageCurrent         = 0x0B,
	MaxError               = 0x0C,
	RelativeStateOfCharge  = 0x0D,
	AbsoluteStateOfCharge  = 0x0E,
	RemainingCapacity      = 0x0F,
	FullChargeCapacity     = 0x10,
	RunTimeToEmpty         = 0x11,
	AverageTimeToEmpty     = 0x12,
	AverageTimeToFull      = 0x13,
	DesignChargingCurrent  = 0x14,
	DesignChargingVoltage  = 0x15,
	BatteryStatus          = 0x16,
	CycleCount             = 0x17,
	DesignCapacity         = 0x18,
	DesignVoltage          = 0x19,
	SpecificationInfo      = 0x1A,
	ManufacturerDate       = 0x1B,
	SerialNumber           = 0x1C,
	ManufacturerName       = 0x20,
	DeviceName             = 0x21,
	CellVoltage4           = 0x3C,
	CellVoltage3           = 0x3D,
	CellVoltage2           = 0x3E,
	CellVoltage1           = 0x3F,
	StateOfHealth          = 0x4F,
};

struct battery_platform_data {
	int single_typec;
	u32 bat_mode;
	u32 monitor_sec;
	const char *bat_name_gf_25wh;
	const char *bat_name_scud_25wh;
	const char *bat_name_gf_28wh;
	const char *bat_name_gf_38wh;
	const char *bat_name_scud_38wh;
	const char *bat_name_gf_40wh;
};

struct smart_battery_device {
	struct i2c_client *client;
	struct device		*dev;
	/* update mutex */
	struct mutex update_lock;
	struct power_supply			*bat;
	struct power_supply			*bat_sys;
	struct power_supply			*interface_phy;
#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	struct zte_power_supply			*zte_fgu;
#endif
	struct power_supply 		*charge_psy;
	struct power_supply 		*main_pd_psy;
	struct power_supply 		*vice_pd_psy;
	struct battery_platform_data	*pdata;
	struct workqueue_struct	*bat_monitor_wq;
	struct delayed_work	bat_delay_work;
	struct gpio_desc *batid_gpio;//battery dectet pin
	struct work_struct		resume_work;
	/*struct wake_lock		wake_lock;*/
	int shutdown_mode;
	int charge_count;
	int charge_state;
	int main_charging;
	int vice_charging;
	int monitor_ms;
	char model_name[32];
	bool valid;
	unsigned long last_updated; /* in jiffies */
	int temperature;/*reg08h*/
	int old_temp;
	int debug_temperature;;
	u16 voltage_now;/*reg09h*/
	signed short current_now;/*reg0Ah*/
	signed short average_current;/*reg0Bh*/
	u8 max_error;
	/* This read-word function returns the predicted remaining battery capacity*/
	u8 remain_percentage_capacity;/*reg0Dh*/
	u8 soc;/*reg0Eh*/
	u8 old_soc;
	time64_t old_soc_time;
	u8 debug_soc;
	u16 remain_capacity;/*reg0Fh*/
	u16 fullcharge_capacity;
	u16 run_time_to_empty;
	u16 empty_time_at_average;
	int full_time_at_avergae;
	u16 design_chargingcurrent;
	u16 design_chargingvoltage;
	u16 design_chgvolt_debug;
	u16 battery_status;
	u16 cycle_count;
	u16 design_capacity;
	u16 design_voltage;
	u16 design_current;
	u32 rated_energy;
	u8  rated_type;
	/* Day + Month*32 + (Year–1980)*5 */
	u16 manufacturer_date;
	u8  device_name[I2C_SMBUS_BLOCK_MAX + 1];
	u16 serial_number;
	u16 cell_voltage1;
	u16 cell_voltage2;
	u16 cell_voltage3;
	u8  soh;
	bool				change; /* Battery status change, report information */
};

static enum power_supply_property smart_bat_props[] = {
	/* Get property */
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

/* Function declaration */
static void smtbat_resume_work(struct work_struct *work);
static void smart_battery_update_info(struct smart_battery_device *battery);
static void smart_battery_power_supply_changed(struct smart_battery_device *battery);

#if 0
static int check_main_pd_psy(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct smart_battery_device *battery = (struct smart_battery_device *)data;

	if (psy->desc->type == POWER_SUPPLY_TYPE_USB_PD_DRP) {
		battery->main_pd_psy = psy;
		return 1;
	}
	return 0;
}

static int smart_battery_get_main_pd_int_property(struct smart_battery_device *battery, int psp)
{
	struct device *dev = battery->dev;
	union power_supply_propval val;
	int ret = -ENODEV;

	if (!battery->main_pd_psy)
		class_for_each_device(power_supply_class, NULL, battery,
				      check_main_pd_psy);

	if (battery->main_pd_psy) {
		ret = battery->main_pd_psy->desc->get_property(battery->main_pd_psy, psp, &val);
		DBG("%s property[%d], ret=%d\n", __func__, psp, ret);
		if (!ret)
			return val.intval;
		else
			dev_err(dev, "Main PD psy get property[%d] failed, ret=%d\n", psp, ret);
	} else
		dev_err(dev, "Main PD psy didn't find\n");

	return ret;
}

static int check_vice_pd_psy(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct smart_battery_device *battery = (struct smart_battery_device *)data;

	if (psy->desc->type == POWER_SUPPLY_TYPE_USB_PD) {
		battery->vice_pd_psy = psy;
		return 1;
	}
	return 0;
}

static int smart_battery_get_vice_pd_int_property(struct smart_battery_device *battery, int psp)
{
	struct device *dev = battery->dev;
	union power_supply_propval val;
	int ret = -ENODEV;

	if (!battery->vice_pd_psy)
		class_for_each_device(power_supply_class, NULL, battery,
				      check_vice_pd_psy);

	if (battery->vice_pd_psy) {
		ret = battery->vice_pd_psy->desc->get_property(battery->vice_pd_psy, psp, &val);
		DBG("%s property[%d], ret=%d\n", __func__, psp, ret);
		if (!ret)
			return val.intval;
		else
			dev_err(dev, "Vice PD psy get property[%d] failed, ret=%d\n", psp, ret);
	} else
		dev_err(dev, "Vice PD psy didn't find\n");

	return ret;
}
#endif

static bool smart_battery_is_exist(struct smart_battery_device *battery)
{
	return (gpiod_get_value(battery->batid_gpio));
}

static void smart_battery_get_current(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, CurrentNow);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "%s read CurrentNow Error:%d\n", __func__, ret);
	} else {
		battery->current_now = (signed short)ret;
		DBG("%s read CurrentNow:%d\n", __func__, battery->current_now);
	}
}

static void smart_battery_get_voltage(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, VoltageNow);
	mutex_unlock(&battery->update_lock);
	if (ret < 0)
		dev_err(battery->dev, "%s read VoltageNow Error:%d\n", __func__, ret);
	else {
		battery->voltage_now = ret;
		DBG("%s read VoltageNow:%d\n", __func__, battery->voltage_now);
	}
}

static void smart_battery_get_soc(struct smart_battery_device *battery)
{
	unsigned int raw_soc = 0;
	unsigned int remain_capacity = battery->remain_capacity;
	unsigned int fullcharge_capacity = battery->fullcharge_capacity;
	union power_supply_propval chg_status = {0, };
	int rc = 0, is_charging = 0, low_temp_level = 0;
	struct timespec64 ts;

	if (battery->debug_soc) {
		battery->soc = battery->debug_soc;
		dev_info(battery->dev, "debug_soc is setting, force report %d\n", battery->soc);
		return;
	}

	if (!IS_ERR_OR_NULL(battery->bat_sys)) {
		rc = power_supply_get_property(battery->bat_sys, POWER_SUPPLY_PROP_STATUS, &chg_status);
		if (rc < 0) {
			dev_info(battery->dev, "Failed to get POWER_SUPPLY_PROP_STATUS rc=%d\n", rc);
		}

		is_charging = ((chg_status.intval == POWER_SUPPLY_STATUS_CHARGING)
							|| (chg_status.intval == POWER_SUPPLY_STATUS_FULL)) ? true : false;
	}

	raw_soc = remain_capacity * 10000 / fullcharge_capacity;

	battery->soc = (raw_soc / 95);

	battery->soc = (battery->soc > 100) ? 100 : battery->soc;

	DBG("%s raw_soc:%d, report:%d, old_soc_time: %lld\n", __func__, raw_soc, battery->soc, battery->old_soc_time);

	if (is_charging) {
		if (battery->soc == 0) {
			dev_info(battery->dev, "voltage_now:%d mV, charging force report 1%%\n", battery->voltage_now);
			battery->soc = 1;
		}
	} else {
		low_temp_level = (battery->temperature > 50) ? 0 : 100;
		if (battery->voltage_now >= (DEFAULT_PWROFF_VOL_THRESD + low_temp_level)) {
			if (battery->soc == 0) {
				dev_info(battery->dev, "temp:%d, vbat:%d, force report 1%%\n",
					battery->temperature, battery->voltage_now);
				battery->soc = 1;
			} else {
				if ((battery->soc > battery->old_soc) && (battery->old_soc_time != 0)) {
					dev_info(battery->dev, "soc:%d, old_soc:%d, discharging prevent soc jump\n",
						battery->soc, battery->old_soc);
					battery->soc = battery->old_soc;
				}
			}
		} else if (battery->old_soc_time != 0) {
			if (battery->soc != 0) {
				ts = ktime_to_timespec64(ktime_get_boottime());
				if ((ts.tv_sec - battery->old_soc_time) >= REPORT_INTERVAL_SEC) {
					battery->soc = battery->old_soc ? (battery->old_soc - 1) : 0;
					dev_info(battery->dev, "temp:%d, vbat:%d, dischg force reduce soc to %d\n", 
							battery->temperature, battery->voltage_now, battery->soc);
				} else {
					battery->soc = battery->old_soc;
					dev_info(battery->dev, "temp:%d, vbat:%d, dischg force reduce soc to %d\n", 
							battery->temperature, battery->voltage_now, battery->soc);
				}
			} else { //if (battery->soc == 0)
				if (battery->old_soc != 0) {
					ts = ktime_to_timespec64(ktime_get_boottime());
					if ((ts.tv_sec - battery->old_soc_time) >= REPORT_INTERVAL_SEC) {
						battery->soc = battery->old_soc ? (battery->old_soc - 1) : 0;
						dev_info(battery->dev, "temp:%d, vbat:%d, dischg force reduce soc to %d\n", 
								battery->temperature, battery->voltage_now, battery->soc);
					} else {
						battery->soc = battery->old_soc;
						dev_info(battery->dev, "temp:%d, vbat:%d, dischg force reduce soc to %d\n", 
								battery->temperature, battery->voltage_now, battery->soc);
					}
				}
			}
		}
	}

}

static int smart_battery_is_batt_present(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret = 0;
	int is_batt_present = 0;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_byte_data(client, RelativeStateOfCharge);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		is_batt_present = false;
		dev_err(battery->dev, "%s is_batt_present:%d, Error:%d\n", __func__, is_batt_present, ret);
	} else {
		is_batt_present = true;
		DBG("%s is_batt_present:%d\n", __func__, is_batt_present);
	}

	return is_batt_present;
}

static void smart_battery_get_temperature(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int temp_k, temp_c;
	int ret;

	if (battery->debug_temperature) {
		battery->temperature = battery->debug_temperature;
		dev_info(battery->dev, "debug_temperature is setting, force report %d\n", battery->temperature);
		return;
	}

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, Temperature);//units 0.1°K
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "%s read temperature Error:%d\n", __func__, ret);
		return;
	}

	temp_k = ret & 0xFFFF;
	temp_c = temp_k - TEMPERATURE_K_TO_C;//°K->°C, units 0.1°C
	/* Filter >300°C */
	if(temp_c > 3000) {
		dev_err(battery->dev, "%s temperature °K->°C Error:%d\n", __func__, temp_c);
		return;
	} else {
		battery->temperature = temp_c;
		DBG("%s read temperature °K->°C: %d.%d°C(%d.%d°K)\n", __func__, 
				temp_c/10, temp_c%10, temp_k/10, temp_k%10);
	}
}

static int smart_battery_get_capacity_leve(struct smart_battery_device *battery)
{
	int dsoc = battery->soc;
	int bat_st = battery->battery_status;

	if((dsoc >= 95) && ((bat_st & BATTERY_FC) != 0))
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (dsoc >= 70)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (dsoc >= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (dsoc >= 1)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
}

static int smart_battery_get_health(struct smart_battery_device *battery)
{
	int bat_tmp;

	bat_tmp = battery->temperature;
	if (bat_tmp <= 0) 						//0℃
		return POWER_SUPPLY_HEALTH_COLD;
	else if (bat_tmp <= 150)				//15℃
		return POWER_SUPPLY_HEALTH_COOL;
	else if (bat_tmp <= 450)				//45℃
		return POWER_SUPPLY_HEALTH_GOOD;
	else if (bat_tmp <= 550)				//55℃
		return POWER_SUPPLY_HEALTH_WARM;
	else
		return POWER_SUPPLY_HEALTH_HOT;
}

static int smart_battery_get_status(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret = 0;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, BatteryStatus);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read BatteryStatus Error:%d\n", ret);
		return VIRTUAL_STATUS;
	} else {
		battery->battery_status = ret;
	}

	DBG("%s read BatteryStatus:0x%X\n", __func__, battery->battery_status);

	return ret;
}

#if 0
static int smart_battery_get_status(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int main_pd_status, vice_pd_status;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, BatteryStatus);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read BatteryStatus Error:%d\n", ret);
		return VIRTUAL_STATUS;
	} else {
		battery->battery_status = ret;
		DBG("%s read BatteryStatus:0x%X\n", __func__, battery->battery_status);
	}

	main_pd_status = smart_battery_get_main_pd_int_property(battery, POWER_SUPPLY_PROP_STATUS);
	if (main_pd_status < 0) {
		dev_err(battery->dev, "read main_pd_status Error:%d\n", ret);
		main_pd_status = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	if(battery->pdata->single_typec == SINGLE_TYPEC) {
		vice_pd_status = POWER_SUPPLY_STATUS_UNKNOWN;
	} else {
		vice_pd_status = smart_battery_get_vice_pd_int_property(battery, POWER_SUPPLY_PROP_STATUS);
		if (vice_pd_status < 0) {
			dev_err(battery->dev, "read vice_pd_status Error:%d\n", ret);
			vice_pd_status = POWER_SUPPLY_STATUS_UNKNOWN;
		}
	}

	if ((main_pd_status == POWER_SUPPLY_STATUS_CHARGING) || (vice_pd_status == POWER_SUPPLY_STATUS_CHARGING)) {
		if (((battery->battery_status & BATTERY_FC) != 0) || (battery->soc >= 100))
			return POWER_SUPPLY_STATUS_FULL;
		else
			return POWER_SUPPLY_STATUS_CHARGING;
	} else
		return POWER_SUPPLY_STATUS_DISCHARGING;
}
#endif

static void smart_battery_get_cycle_count(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, CycleCount);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read CycleCount Error:%d\n", ret);
	} else {
		battery->cycle_count = ret;
		DBG("%s read CycleCount:%d\n", __func__, ret);
	}
}

static void smart_battery_get_capacity_remain(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, RemainingCapacity);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read RemainingCapacity Error:%d\n", ret);
	} else {
		battery->remain_capacity = ret;
		DBG("%s read RemainingCapacity:%d\n", __func__, ret);
	}
}

static void smart_battery_get_capacity_full(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, FullChargeCapacity);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read FullChargeCapacity Error:%d\n", ret);
	} else {
		battery->fullcharge_capacity = ret;
		DBG("%s read FullChargeCapacity:%d\n", __func__, ret);
	}
}

static void smart_battery_get_capacity_design(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, DesignCapacity);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read DesignCapacity Error:%d\n", ret);
	} else {
		battery->design_capacity = ret;
		DBG("%s read DesignCapacity:%d\n", __func__, ret);
	}
}

static void smart_battery_get_voltage_design(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, DesignVoltage);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read DesignVoltage Error:%d\n", ret);
	} else {
		battery->design_voltage = ret;
		DBG("%s read DesignVoltage:%d\n", __func__, ret);
	}
}

static void smart_battery_get_charging_current(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, DesignChargingCurrent);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read DesignChargingCurrent Error:%d\n", ret);
	} else {
		battery->design_chargingcurrent = ret;
		DBG("%s read DesignChargingCurrent:%d\n", __func__, ret);
	}
}

static void smart_battery_get_charging_voltage(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, DesignChargingVoltage);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read DesignChargingVoltage Error:%d\n", ret);
	} else {
		battery->design_chargingvoltage = (battery->design_chgvolt_debug) ? battery->design_chgvolt_debug : ret;
		DBG("%s read DesignChargingVoltage:%d, debug %d\n", __func__, ret, battery->design_chgvolt_debug);
	}
}

static void smart_battery_get_soh(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_byte_data(client, StateOfHealth);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read StateOfHealth Error:%d\n", ret);
	} else {
		battery->soh = ret;
		DBG("%s read StateOfHealth:%d\n", __func__, ret);
	}
}

static void smart_battery_get_cell_voltage(struct smart_battery_device *battery, int cell_num)
{
	struct i2c_client *client = battery->client;
	int cell_reg[4] = {CellVoltage1, CellVoltage2, CellVoltage3 ,CellVoltage4};
	int ret;

	if((cell_num < 1) || (cell_num > 4)) {
		dev_err(battery->dev, "cell_num err:%d\n", cell_num);
		return;
	}

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, cell_reg[cell_num - 1]);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "read CellVoltage[%d] Error:%d\n", cell_num, ret);
		return;
	}

	DBG("%s read CellVoltage%d[%#X]:%d\n", __func__, cell_num, cell_reg[cell_num - 1], ret);
	switch(cell_num) {
		case CELLVOL_1:
			battery->cell_voltage1 = ret;
			break;
		case CELLVOL_2:
			battery->cell_voltage2 = ret;
			break;
		case CELLVOL_3:
			battery->cell_voltage3 = ret;
			break;
		default:
			return;
	}
}

__maybe_unused static int smart_battery_set_shutdown_mode(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	u16 shutdownmode_cmd = 0x0010;
	int ret;

	dev_info(battery->dev, "%s smart_battery will enter shutdown mode\n", __func__);

	/* Set twice enter shutdown mode */
	ret = i2c_smbus_write_word_data(client, ManufacturerAccess, shutdownmode_cmd);
	if (ret < 0) {
		dev_err(battery->dev, "%s set_shutdown_mode(1) failed:%d\n", __func__, ret);
		return -EINVAL;
	}

	ret = i2c_smbus_write_word_data(client, ManufacturerAccess, shutdownmode_cmd);
	if (ret < 0) {
		dev_err(battery->dev, "%s set_shutdown_mode(2) failed:%d\n", __func__, ret);
		return -EINVAL;
	}
	battery->shutdown_mode = 1;

	return battery->shutdown_mode;
}

static void smart_battery_get_time_to_full(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	int time_sec = 0;
	int ret;

	mutex_lock(&battery->update_lock);
	ret = i2c_smbus_read_word_data(client, AverageTimeToFull);
	mutex_unlock(&battery->update_lock);
	if (ret < 0) {
		dev_err(battery->dev, "%s read AverageTimeToFull Error:%d\n", __func__, ret);
		return;
	}

	time_sec = (ret & BATTERY_MAX_TIME);
	if(time_sec == BATTERY_MAX_TIME)//Discharge mode
		battery->full_time_at_avergae = 3600* 24;//One day
	else {
		battery->full_time_at_avergae = time_sec * 60;// Minute to second
		DBG("%s read AverageTimeToFull:%d\n", __func__, time_sec);
	}
}

static int smart_battery_get_device_name(struct smart_battery_device *battery)
{
	struct i2c_client *client = battery->client;
	u8 block_buffer[I2C_SMBUS_BLOCK_MAX + 1] = {0};
	int len, ret = 0;

	memset(battery->device_name, 0 ,sizeof(battery->device_name));

	/* Read Battery Device Name */
	len = i2c_smbus_read_byte_data(client, DeviceName);
	if ((len <= 0) || (len >= I2C_SMBUS_BLOCK_MAX)){
		dev_err(battery->dev, "Battery Failed to read DeviceNameLength, ret=%d\n", len);
		return -EINVAL;
	}

	ret = i2c_smbus_read_i2c_block_data(client, DeviceName, (len + 1), block_buffer);
	if (ret < 0) {
		dev_err(battery->dev, "Battery Failed to read DeviceName, ret=%d\n", ret);
		return -EINVAL;
	}

	block_buffer[len + 1] = '\0';
	strncpy(battery->device_name, (char *)&block_buffer[1], (len + 1));
	dev_info(battery->dev, "DeviceName:%s, length:%d\n", battery->device_name, len);

	return 0;
}

static void smart_battery_get_type(struct smart_battery_device *battery)
{
	if ((battery->design_capacity != 0) && (battery->design_voltage != 0)) {
		battery->rated_energy = (battery->design_capacity * battery->design_voltage) / 1000;
		dev_info(battery->dev, "Rated capacity:%dmWh(%dmA %dmV)\n",
				battery->rated_energy, battery->design_capacity, battery->design_voltage);
	} else {
		battery->rated_energy = 0;
		dev_err(battery->dev, "Rated capacity Err(%dmA %dmV)\n",
				battery->design_capacity, battery->design_voltage);
	}

	if (battery->rated_energy >= BAT_45Wh * 1000) {
		battery->rated_type = BAT_UNKNOW;
	} else if (battery->rated_energy >= BAT_40Wh * 1000) {
		battery->rated_type = BAT_40Wh;
	} else if (battery->rated_energy >= BAT_38Wh * 1000) {
		battery->rated_type = BAT_38Wh;
	} else if (battery->rated_energy >= BAT_28Wh * 1000) {
		battery->rated_type = BAT_28Wh;
	} else if (battery->rated_energy >= BAT_25Wh * 1000) {
		battery->rated_type = BAT_25Wh;
	} else {
		battery->rated_type = BAT_UNKNOW;
	}

	dev_info(battery->dev, "Battery Type:%dWh(%dmWh)\n", 
			battery->rated_type, battery->rated_energy);
}

static int smart_battery_get_property(struct power_supply *psy,
					  enum power_supply_property psp,
					  union power_supply_propval *val)
{
	struct smart_battery_device *battery = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
			val->intval = VIRTUAL_PRESET;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_UNPRESET;
		else
			val->intval = VIRTUAL_PRESET;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_CURRENT * 1000;
		else
			val->intval = battery->current_now * 1000;/*uA*/
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_VOLTAGE * 1000;
		else
			val->intval = battery->voltage_now * 1000;/*uV*/
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_SOC;
		else {
			val->intval = battery->soc;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		else
			val->intval = smart_battery_get_capacity_leve(battery);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		else
			val->intval = smart_battery_get_health(battery);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_TEMPERATURE;
		else
			val->intval = battery->temperature;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING; /* NEED TODO */
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = battery->remain_capacity * 1000; /* uAh */
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = battery->remain_capacity * 1000;/* uAh */
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = battery->fullcharge_capacity * 1000;/* uAh */
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = battery->design_capacity * 1000;/* uAh */
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = battery->full_time_at_avergae;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = battery->design_voltage * 1000;/* uV */
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = battery->design_chargingvoltage * 1000;/* uV */
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = battery->design_chargingcurrent * 1000;/* uA */
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = battery->cycle_count;
		break;	
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = battery->device_name;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smart_bat_is_writeable(struct power_supply *psy,
                          enum power_supply_property psp)
{
    int ret;

    switch (psp) {
    case POWER_SUPPLY_PROP_TEMP:
    case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
        ret = 1;
        break;

    default:
        ret = 0;
    }

    return ret;
}

static int smart_battery_set_property(struct power_supply *psy,
						enum power_supply_property psp,
						const union power_supply_propval *val)
{
	struct smart_battery_device *battery = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP:
		battery->debug_temperature = val->intval;
		dev_info(battery->dev, "%s debug_temperature: %d\n", __func__, battery->debug_temperature);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		battery->debug_soc = val->intval;
		dev_info(battery->dev, "%s debug_soc: %d\n", __func__, battery->debug_soc);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		battery->design_chgvolt_debug = val->intval;/* uV */
		dev_info(battery->dev, "%s design_chgvolt_debug: %d\n", __func__, battery->design_chgvolt_debug);
		break;
	default:
		dev_err(battery->dev, "%s psp input error:%d\n", __func__, psp);
		ret = -EINVAL;
	}

	return ret;
}

static void smart_battery_external_power_changed(struct power_supply *psy)
{
	struct smart_battery_device *battery = power_supply_get_drvdata(psy);

	DBG("%s\n", __func__);
	if (battery->pdata->bat_mode == MODE_BATTERY) {
		smart_battery_update_info(battery);
		smart_battery_power_supply_changed(battery);
	}
}

static char *smart_bat_supplied_to[] = {
	"charger",
};

static const struct power_supply_desc smart_bat_desc = {
	.name		= "battery-fgu",   //"smart_battery",    //
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= smart_bat_props,
	.num_properties	= ARRAY_SIZE(smart_bat_props),
	.get_property	= smart_battery_get_property,
	.set_property	= smart_battery_set_property,
	.property_is_writeable = smart_bat_is_writeable,
	.external_power_changed = smart_battery_external_power_changed,
};

static int smart_bat_init_power_supply(struct smart_battery_device *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	psy_cfg.supplied_to = smart_bat_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(smart_bat_supplied_to);

	battery->bat = devm_power_supply_register(battery->dev,
								  &smart_bat_desc, 
								  &psy_cfg);
	if (IS_ERR(battery->bat)) {
		dev_err(battery->dev, "register smart_battery supply fail\n");
		return PTR_ERR(battery->bat);
	}

	pr_info("register smart_battery supply sucess\n");

	return 0;
}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
static enum zte_power_supply_property zte_smart_bat_props[] = {
	POWER_SUPPLY_PROP_SOH,
	POWER_SUPPLY_PROP_CELL1_VOLTAGE,
	POWER_SUPPLY_PROP_CELL2_VOLTAGE,
	POWER_SUPPLY_PROP_CELL3_VOLTAGE,
	POWER_SUPPLY_PROP_FULL_STATUS,
	POWER_SUPPLY_PROP_RATED_ENERGY,
	POWER_SUPPLY_PROP_RATED_TYPE,
	POWER_SUPPLY_PROP_SHUTDOWN_MODE, 
};

static int zte_fgu_get_property(struct zte_power_supply *psy,
					  enum zte_power_supply_property psp,
					  union power_supply_propval *val)
{
	struct smart_battery_device *battery = zte_power_supply_get_drvdata(psy);

	switch (psp) {	
	case POWER_SUPPLY_PROP_SOH:
		val->intval = battery->soh;
		break;
	case POWER_SUPPLY_PROP_CELL1_VOLTAGE:
		val->intval = battery->cell_voltage1 * 1000;/* uV */
		break;
	case POWER_SUPPLY_PROP_CELL2_VOLTAGE:
		val->intval = battery->cell_voltage2 * 1000;/* uV */
		break;
	case POWER_SUPPLY_PROP_CELL3_VOLTAGE:
		val->intval = battery->cell_voltage3 * 1000;/* uV */
		break;
	case POWER_SUPPLY_PROP_FULL_STATUS:
		val->intval = !!(battery->battery_status & BATTERY_FC);
		break;
	case POWER_SUPPLY_PROP_RATED_ENERGY:
		val->intval = battery->rated_energy;
		break;
	case POWER_SUPPLY_PROP_RATED_TYPE:
		val->intval = battery->rated_type;
		break;
	case POWER_SUPPLY_PROP_SHUTDOWN_MODE:
		val->intval = battery->shutdown_mode;
	default:
		return -EINVAL;
	}

	return 0;
}

static int zte_fgu_set_property(struct zte_power_supply *psy,
						enum zte_power_supply_property psp,
						const union power_supply_propval *val)
{
	struct smart_battery_device *battery = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_SHUTDOWN_MODE:
		if(val->intval == 1) {
			ret = smart_battery_set_shutdown_mode(battery);
			if(ret < 0)
				return -EINVAL;
		} else {
			dev_err(battery->dev, "%s shutdown mode input error:%d\n", __func__, val->intval);
		}
		break;
	default:
		dev_err(battery->dev, "%s psp input error:%d\n", __func__, psp);
		ret = -EINVAL;
	}

	return ret;
}

static void zte_fgu_external_power_changed(struct zte_power_supply *psy)
{
	DBG("%s\n", __func__);
}

static int zte_fgu_property_is_writeable(struct zte_power_supply *psy,
					enum zte_power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_SHUTDOWN_MODE:
		return 1;
	default:
		break;
	}

	return 0;
}

static char *zte_smart_bat_supplied_to[] = {
	"charger",
};

static const struct zte_power_supply_desc zte_smart_bat_desc = {
	.name		= "zte-fgu",
	.type		= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties	= zte_smart_bat_props,
	.num_properties	= ARRAY_SIZE(zte_smart_bat_props),
	.get_property	= zte_fgu_get_property,
	.set_property	= zte_fgu_set_property,
	.property_is_writeable = zte_fgu_property_is_writeable,
	.external_power_changed = zte_fgu_external_power_changed,
};

static int smart_bat_init_zte_power_supply(struct smart_battery_device *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	psy_cfg.supplied_to = zte_smart_bat_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(zte_smart_bat_supplied_to);

	battery->zte_fgu = zte_devm_power_supply_register(battery->dev,
								  &zte_smart_bat_desc, 
								  &psy_cfg);
	if (IS_ERR(battery->zte_fgu)) {
		dev_err(battery->dev, "register zte_fgu supply fail\n");
		return PTR_ERR(battery->zte_fgu);
	}

	pr_info("register zte-fgu supply sucess\n");

	return 0;
}
#endif

#if 0
static void smart_battery_update_charging_status(struct smart_battery_device *battery)
{
	bool main_charging, vice_charging;
	int ret;

	ret = smart_battery_get_main_pd_int_property(battery, POWER_SUPPLY_PROP_STATUS);
	if(ret == POWER_SUPPLY_STATUS_CHARGING)
		main_charging = true;
	else
		main_charging = false;

	if(battery->pdata->single_typec == SINGLE_TYPEC) {
		vice_charging = false;
	} else {
		ret = smart_battery_get_vice_pd_int_property(battery, POWER_SUPPLY_PROP_STATUS);
		if(ret == POWER_SUPPLY_STATUS_CHARGING)
			vice_charging = true;
		else
			vice_charging = false;
	}

	if ((main_charging == battery->main_charging) && (vice_charging == battery->vice_charging))
		return;

	battery->change = true;
	battery->main_charging = main_charging;
	battery->vice_charging = vice_charging;
	if (main_charging || vice_charging)
		battery->charge_count++;
}
#endif

static void smart_battery_update_info(struct smart_battery_device *battery)
{
	if (IS_ERR_OR_NULL(battery->bat_sys)) {
		battery->bat_sys = power_supply_get_by_name("battery");
	}

	smart_battery_get_current(battery);
	smart_battery_get_voltage(battery);
	smart_battery_get_capacity_remain(battery);
	smart_battery_get_capacity_full(battery);
	smart_battery_get_temperature(battery);
	smart_battery_get_soc(battery);
	smart_battery_get_soh(battery);
	smart_battery_get_cycle_count(battery);
	smart_battery_get_capacity_design(battery);
	smart_battery_get_voltage_design(battery);
	smart_battery_get_charging_current(battery);
	smart_battery_get_charging_voltage(battery);
	smart_battery_get_time_to_full(battery);
	smart_battery_get_cell_voltage(battery, 1);
	smart_battery_get_cell_voltage(battery, 2);
	smart_battery_get_cell_voltage(battery, 3);
	battery->charge_state = smart_battery_get_status(battery);
	/*smart_battery_update_charging_status(battery);*/
}

static void smart_battery_power_supply_changed(struct smart_battery_device *battery)
{
	int need_notify_change = 0;
	int temperature = battery->temperature / 10;
	struct timespec64 ts;

	if (battery->soc != battery->old_soc) {
		ts = ktime_to_timespec64(ktime_get_boottime());
		battery->old_soc_time = ts.tv_sec;
		need_notify_change = 1;
	}

	if (temperature != battery->old_temp)
		need_notify_change = 1;

	if ((need_notify_change) && !IS_ERR_OR_NULL(battery->bat_sys)) {
		DBG("%s\n", __func__);
		power_supply_changed(battery->bat_sys);
	}

	battery->old_soc = battery->soc;
	battery->old_temp = temperature;
}

static void smart_battery_cycle_volt(struct smart_battery_device *battery)
{
	union power_supply_propval val = {0, };
	int rc = 0;

	if (IS_ERR_OR_NULL(battery->interface_phy)) {
		battery->interface_phy = power_supply_get_by_name("interface");
	} else {
		rc = power_supply_get_property(battery->interface_phy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
		if (rc < 0) {
			dev_err(battery->dev, "Failed to get interface fcv\n");
		}

		DBG("%s interface fcv:%d, fg fcv:%d\n", __func__, val.intval, (battery->design_chargingvoltage * 1000));

		if ((battery->design_chargingvoltage * 1000) != val.intval) {
			val.intval = (battery->design_chargingvoltage * 1000);
			DBG("%s voting fcv:%d\n", __func__, val.intval);
			rc = power_supply_set_property(battery->interface_phy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
			if (rc < 0) {
				dev_err(battery->dev, "Failed to vote fcv\n");
			}
		}
	}
}

static void smart_battery_work(struct work_struct *work)
{
	struct smart_battery_device *battery =
		container_of(work,
			     struct smart_battery_device,
			     bat_delay_work.work);

	if (battery->pdata->bat_mode == MODE_BATTERY) {
		smart_battery_update_info(battery);
		smart_battery_cycle_volt(battery);
		smart_battery_power_supply_changed(battery);
	}

	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(battery->monitor_ms));
}

static void smart_battery_init_info(struct smart_battery_device *battery)
{
	//battery->design_cap = battery->pdata->design_capacity;
	//battery->qmax = battery->pdata->design_qmax;
	//battery->bat_res = battery->pdata->bat_res;
	battery->monitor_ms = battery->pdata->monitor_sec * TIMER_MS_COUNTS;
	DBG("battery->monitor_ms :%d\n", battery->monitor_ms);
}

static ssize_t smart_battery_info_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct smart_battery_device *battery = dev_get_drvdata(dev);

	smart_battery_update_info(battery);
	DBG("smart battery info show:\n"
	    "soc: %d\n"
	    "current: %d\n"
	    "voltage: %d\n"
	    "temperature: %d.%dC\n"
	    "battery state: 0x%X\n"
	    "charge_state: %d\n"
	    "fullcharge_capacity: %d\n"
	    "design_capacity: %d\n"
	    "full_time_at_avergae: %d\n"
	    "charge_count: %d\n"
	    "main_charging: %d\n"
	    "vice_charging: %d\n"
	    "virtual_power: %d\n",
	    battery->soc,
	    battery->current_now,
	    battery->voltage_now,
	    battery->temperature/10, battery->temperature%10,
	    battery->battery_status,
	    battery->charge_state,
	    battery->fullcharge_capacity,
	    battery->design_capacity,
	    battery->full_time_at_avergae,
	    battery->charge_count,
	    battery->main_charging,
	    battery->vice_charging,
	    battery->pdata->bat_mode);

	return 0;
}

static struct device_attribute smart_battery_attr[] = {
	__ATTR(smart_battery_info, 0664, smart_battery_info_show, NULL),
};

static void smart_battery_init_sysfs(struct smart_battery_device *battery)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(smart_battery_attr); i++) {
		ret = sysfs_create_file(&battery->dev->kobj,
					&smart_battery_attr[i].attr);
		if (ret)
			dev_err(battery->dev, "create smart_battery node(%s) error\n",
				smart_battery_attr[i].attr.name);
	}
}

static int smart_battery_init_power(struct smart_battery_device *battery)
{
	struct device *dev = battery->dev;
	struct regulator *battery_pwr = NULL;
	int ret;

	battery_pwr = devm_regulator_get(dev, "vdd");
	if (IS_ERR_OR_NULL(battery_pwr)) {
		dev_err(dev, ">>>%s:failed to get vdd, no defined\n", __func__);
		return -EINVAL;
	} else {
		dev_info(dev, ">>>%s:get vdd regulator success\n", __func__);
	}

	ret = regulator_set_voltage(battery_pwr, 3300000, 3300000);
	if (ret) {
		dev_err(dev, ">>>%s:regulator_set_voltage failed, ret=%d\n", __func__, ret);
		return -EINVAL;
	} else {
		dev_info(dev, ">>>%s:regulator_set_voltage success, power_voltage 3300000\n", __func__);
	}

	ret = regulator_enable(battery_pwr);
	if (ret) {
		dev_err(dev, ">>>%s:regulator_enable failed, ret=%d\n", __func__, ret);
		return -EINVAL;
	} else {
		dev_info(dev, ">>>%s:regulator_enable success, power_voltage 3300000\n", __func__);
	}

	return 0;
}


static int smart_battery_parse_dt(struct smart_battery_device *battery)
{
	struct battery_platform_data *pdata;
	struct device_node *np = battery->dev->of_node;
	struct device *dev = battery->dev;
	int ret;

	pdata = devm_kzalloc(battery->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	battery->pdata = pdata;
	/* init default param */
	pdata->single_typec = DUAL_TYPEC;
	pdata->bat_mode = MODE_BATTERY;
	pdata->monitor_sec = DEFAULT_MONITOR_SEC;

	battery->batid_gpio = devm_gpiod_get_optional(battery->dev, "batid", GPIOD_IN);
	if (IS_ERR_OR_NULL(battery->batid_gpio)) {
		dev_err(dev, "failed to find batid_gpio pin, no defined\n");
	}

	ret = of_property_read_u32(np, "virtual_power", &pdata->bat_mode);
	if (ret < 0) {
		dev_err(dev, "virtual_power missing!\n");
	}

	if(pdata->bat_mode == MODE_VIRTUAL) {
		dev_info(dev, "Enable virtual_power, enter MODE_VIRTUAL!\n");
	} else {
		dev_info(dev, "Disable virtual_power, detect battery!\n");
		ret = smart_battery_is_exist(battery);
		dev_info(dev, "smart_battery is %d\n", ret);
		/*
		if (!smart_battery_is_exist(battery)) {
			pdata->bat_mode = MODE_VIRTUAL;
			dev_info(dev, "smart_battery not exist, enter MODE_VIRTUAL!\n");
		} else {
			dev_info(dev, "smart_battery exist, enter MODE_BATTERY!\n");
		}*/
	}

	ret = of_property_read_u32(np, "monitor_sec", &pdata->monitor_sec);
	if (ret < 0) {
		dev_err(battery->dev, "monitor_sec missing, using default sec!\n");
	}

	ret = of_property_read_u32(np, "single_typec", &pdata->single_typec);
	if (ret < 0) {
		dev_err(battery->dev, "single_typec missing, using default val!\n");
	}

	dev_info(dev, "Battery dts info dump:\n"
	    "single_typec:%d\n"
	    "bat_mode:%d\n"
	    "monitor_sec:%d\n",
	    pdata->single_typec,
	    pdata->bat_mode,
	    pdata->monitor_sec);

	return 0;
}

static int smtbat_name_show(struct seq_file *m, void *v)
{
	struct smart_battery_device *battery = m->private;

	if (!IS_ERR_OR_NULL(battery)) {
		seq_printf(m, "%s\n", battery->device_name);
	} else {
		seq_printf(m, "%s\n", "UNKNOWN");
	}

    return 0;
}

static int smtbat_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, smtbat_name_show, PDE_DATA(inode));
}

static const struct proc_ops smtbat_name_node = {
	.proc_open = smtbat_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};

static int smtbat_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct smart_battery_device *battery;

	int ret = 0;

	dev_err(dev, "%s \n", __func__);
	pr_info("%s pr_info \n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(dev, "i2c_check_functionality Error\n");
		//return -EIO;
	}

	battery = devm_kzalloc(&client->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->client = client;
	battery->dev = dev;

	i2c_set_clientdata(client, battery);
	mutex_init(&battery->update_lock);

	ret = smart_battery_parse_dt(battery);
	if (ret < 0) {
		dev_err(dev, "parse dt failed!\n");
		return ret;
	}

	smart_battery_init_sysfs(battery);
	smart_battery_init_info(battery);
	smart_battery_init_power(battery);

	if (!smart_battery_is_batt_present(battery)) {
		dev_err(dev, "##### battery is not batt present, MODE_VIRTUAL!\n");
		battery->pdata->bat_mode = MODE_VIRTUAL;
	}

	if (battery->pdata->bat_mode == MODE_BATTERY) {
		smart_battery_update_info(battery);
		smart_battery_get_device_name(battery);
		smart_battery_get_type(battery);
		proc_create_data("driver/slave_chg_name", 0664, NULL,
			&smtbat_name_node, battery);
	}

	/*wake_lock_init(&battery->wake_lock, WAKE_LOCK_SUSPEND, "smart_bat_lock");*/

	battery->bat_monitor_wq = alloc_ordered_workqueue("%s",
					WQ_MEM_RECLAIM | WQ_FREEZABLE,
					"smart-bat-monitor-wq");
	INIT_DELAYED_WORK(&battery->bat_delay_work, smart_battery_work);
	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));
	INIT_WORK(&battery->resume_work, smtbat_resume_work);

	ret = smart_bat_init_power_supply(battery);
	if (ret) {
		dev_err(battery->dev, "smart battery supply register failed!\n");
		return ret;
	}

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
	ret = smart_bat_init_zte_power_supply(battery);
	if (ret) {
		dev_err(battery->dev, "smart battery zte fg supply register failed!\n");
		return ret;
	}
#endif

	dev_err(dev, "%s finish\n", __func__);
	pr_info("smart_battery_probe finished\n");
	return 0;
}

void smtbat_shutdown(struct i2c_client *client)
{
	struct smart_battery_device *battery = i2c_get_clientdata(client);

	dev_info(battery->dev, "%s\n", __func__);
}


#ifdef CONFIG_PM_SLEEP
static int smtbat_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct smart_battery_device *battery = dev_get_drvdata(&pdev->dev);

	cancel_delayed_work_sync(&battery->bat_delay_work);
	dev_info(battery->dev, "%s\n", __func__);

	return 0;
}

static void smtbat_resume_work(struct work_struct *work)
{
	struct smart_battery_device *battery = container_of(work, struct smart_battery_device, resume_work);

	smart_battery_update_info(battery);
	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work, msecs_to_jiffies(1000));
}

static int smtbat_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct smart_battery_device *battery = dev_get_drvdata(&pdev->dev);

	queue_work(battery->bat_monitor_wq, &battery->resume_work);
	dev_info(battery->dev, "%s\n", __func__);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(smtbat_pm_ops, smtbat_pm_suspend, smtbat_pm_resume);

static const struct i2c_device_id smtbat_i2c_ids[] = {
	{ "248596-3S", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, smtbat_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id smtbat_dt_match[] = {
	{ .compatible = "zte,smart_battery", },
	{ },
};
MODULE_DEVICE_TABLE(of, smtbat_dt_match);
#else
static const struct of_device_id smtbat_dt_match[] = {
	{ .compatible = "zte,smart_battery", },
	{ },
};
#endif

static struct i2c_driver smtbat_driver = {
	.driver = {
		.name = DRVNAME,
		.pm		= &smtbat_pm_ops,
		.of_match_table = of_match_ptr(smtbat_dt_match),
	},
	.probe	  = smtbat_probe,
	.shutdown	= smtbat_shutdown,
	.id_table = smtbat_i2c_ids,
};

module_i2c_driver(smtbat_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.xian_bsp");
MODULE_DESCRIPTION("Smart Battery Driver");
