// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/
#define pr_fmt(fmt)	"[ZTE_LDD_CHARGE][CHARGE_IC][SC888X]: %s: " fmt, __func__

#include <linux/alarmtimer.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/usb/phy.h>
#include <uapi/linux/usb/charger.h>
#include <linux/proc_fs.h>

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>
int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

#define SC888X_DRV_VERSION              "1.0.0_G"

#define SC888X_BATTERY_NAME             "battery-fgu"
#define BIT_DP_DM_BC_ENB                BIT(0)
#define SC888X_OTG_VALID_MS             1000
#define SC888X_FEED_WATCHDOG_VALID_MS   30000
#define SC888X_OTG_ALARM_TIMER_MS       15000

#define SC888X_DISABLE_PIN_MASK         BIT(0)
#define SC888X_DISABLE_PIN_MASK_2730    BIT(0)
#define SC888X_DISABLE_PIN_MASK_2721    BIT(15)
#define SC888X_DISABLE_PIN_MASK_2720    BIT(0)

#define SC888X_OTG_RETRY_TIMES          10

#define SC888X_WAKE_UP_MS               1000

#define SC888X_PD_HARD_RESET_MS         500
#define SC888X_PD_RECONNECT_MS          3000

#define SC8885_DEVICE_ID                0x68
#define SC8886_DEVICE_ID                0x66
#define SGM41573_DEVICE_ID              0x88

static bool boot_calibration;

enum sc888x_reg_range {
    SC888X_CHG_CURR,
    SC888X_CHG_VOLT,
    SC888X_OTG_VOLT,
    SC888X_OTG_CURR,
    SC888X_VINREG_VOLT,
    SC888X_IINPUT_CURR,
    SC888X_VSYS_MIN,
};

struct reg_range {
    u32 min;
    u32 max;
    u32 step;
    u32 offset;
    const u32 *table;
    u16 num_table;
    bool round_up;
};

#define SC888X_CHG_RANGE(_min, _max, _step, _offset, _ru) \
{ \
    .min = _min, \
    .max = _max, \
    .step = _step, \
    .offset = _offset, \
    .round_up = _ru, \
}

#define SC888X_CHG_RANGE_T(_table, _ru) \
    { .table = _table, .num_table = ARRAY_SIZE(_table), .round_up = _ru, }


static const struct reg_range sc888x_reg_range[] = {
    [SC888X_CHG_CURR] = SC888X_CHG_RANGE(64, 8128, 64, 0, false),
    [SC888X_CHG_VOLT] = SC888X_CHG_RANGE(3000, 19200, 8, 0, false),
    [SC888X_OTG_VOLT] = SC888X_CHG_RANGE(4280, 24000, 8, 1280, false),
    [SC888X_OTG_CURR] = SC888X_CHG_RANGE(50, 6400, 50, 0, false),
    [SC888X_VINREG_VOLT] = SC888X_CHG_RANGE(3200, 19520, 64, 3200, false),
    [SC888X_IINPUT_CURR] = SC888X_CHG_RANGE(50, 6400, 50, 50, false),
    [SC888X_VSYS_MIN] = SC888X_CHG_RANGE(1024, 16128, 256, 160, false),
};

enum sc888x_fields {
    EN_LWPWR, WDT_ADJ, IDPM_AUTO_DIS, OTG_ON_CHRGOK, EN_OOA, PWM_FREQ, 
    LOW_PTM_RIPPLE, SYSSHORT_DIS, EN_LEARN, IADPT_GAIN, IBAT_GAIN, 
    EN_LDO, EN_IDPM, EN_CHRG, /*reg00*/
    CHARGE_CURR, /*reg02*/
    CHARGE_VOLT, /*reg04*/
    OTG_VOLT, /*reg06*/
    OTG_CURR, /*reg08*/
    VINREG_VOLT, /*reg0A*/
    VSYSMIN_VOLT, /*reg0C*/
    INPUT_CURR, /*reg0E*/
    CHARGER_STATUS, /*reg20*/
    PROCHOT_STATUS, /*reg22*/
    IINDPM_CURR, /*reg24*/
    ADC_VBUS, ADC_VPSYS, /*reg26*/
    ADC_ICHG, ADC_IDCHG, /*reg28*/
    ADC_IN, ADC_CMPIN, /*reg2A*/
    ADC_VSYS, ADC_VBAT, /*reg2C*/
    DEVICE_ID, MANU_ID, /*reg2E*/
    EN_IBAT, EN_PROCHOT_LPWR, EN_PSYS, RSNS_RAC, RSNS_RSR, PSYS_RATIO,
    CMP_REF, CMP_POL, CMP_DEG, FORCE_LATCHOFF, EN_SHIP_DCHG, AUTO_WAKEUP_EN, /*reg30*/
    PKPWR_TOVLD_REG, EN_PKPWR_IDPM, EN_PKPWR_VSYS, PKPWER_OVLD_STAT,
    PKPWR_RELAX_STAT, PKPWER_TMAX, EN_EXTILIM, EN_ICHG_IDCHG, Q2_OCP,
    EN_ACOC, ACOC_VTH, EN_BATOC, BATCOC_VTH, /*reg32h*/
    EN_HIZ, RESET_REG, RESET_VINDPM, EN_OTG, EN_ICO_MODE, OTG_RANGE_LOW, 
    BATFETOFF_HIZ, PSYS_OTG_IDCHG, /*reg34h*/
    ILIM2_VTH, ICRIT_DEG, VSYS_VTH, VSYS_TH2, INORM_DEG, /*reg36h*/
    IDCHG_VTH, IDCHG_DEG, PROCHOT_PROFILE_COMP, PROCHOT_PROFILE_ICRIT,
    PROCHOT_PROFILE_INOM, PROCHOT_PROFILE_IDCHG,
    PROCHOT_PROFILE_VSYS, PROCHOT_PROFILE_BATPRES, PROCHOT_PROFILE_ACOK, /*reg38h*/
    ADC_CONV, ADC_START, EN_ADC_CMPIN, EN_ADC_VBUS,
    EN_ADC_PSYS, EN_ADC_IIN, EN_ADC_IDCHG, EN_ADC_ICHG, EN_ADC_VSYS,
    EN_ADC_VBAT,/*reg3Ah*/
    F_MAX_FIELDS
};

static const int sc888x_adc_offset[] = {
    [ADC_VBUS] = 3200,
    [ADC_VPSYS] = 0,
    [ADC_ICHG] = 0,
    [ADC_IDCHG] = 0,
    [ADC_IN] = 0,
    [ADC_CMPIN] = 0,
    [ADC_VSYS] = 2880,
    [ADC_VBAT] = 2880,
};

static const int sc888x_adc_lsb[] = {
    [ADC_VBUS] = 64,
    [ADC_VPSYS] = 12,
    [ADC_ICHG] = 64,
    [ADC_IDCHG] = 256,
    [ADC_IN] = 50,
    [ADC_CMPIN] = 12,
    [ADC_VSYS] = 64,
    [ADC_VBAT] = 64,
};

//REGISTER
static const struct reg_field sc8886_reg_fields[] = {
    /*reg00*/
    [EN_LWPWR] = REG_FIELD(0x00, 15, 15),
    [WDT_ADJ] = REG_FIELD(0x00, 13, 14),
    [IDPM_AUTO_DIS] = REG_FIELD(0x00, 12, 12),
    [OTG_ON_CHRGOK] = REG_FIELD(0x00, 11, 11),
    [EN_OOA] = REG_FIELD(0x00, 10, 10),
    [PWM_FREQ] = REG_FIELD(0x00, 9, 9),
    [LOW_PTM_RIPPLE] = REG_FIELD(0x00, 8, 8),
    [SYSSHORT_DIS] = REG_FIELD(0x00, 6, 6),
    [EN_LEARN] = REG_FIELD(0x00, 5, 5),
    [IADPT_GAIN] = REG_FIELD(0x00, 4, 4),
    [IBAT_GAIN] = REG_FIELD(0x00, 3, 3),
    [EN_LDO] = REG_FIELD(0x00, 2, 2),
    [EN_IDPM] = REG_FIELD(0x00, 1, 1),
    [EN_CHRG] = REG_FIELD(0x00, 0, 0),
    /*reg02*/
    [CHARGE_CURR] = REG_FIELD(0x02, 6, 12),
    /*reg04*/
    [CHARGE_VOLT] = REG_FIELD(0x04, 3, 14),
    /*reg06*/
    [OTG_VOLT] = REG_FIELD(0x06, 2, 13),
    /*reg08*/
    [OTG_CURR] = REG_FIELD(0x08, 8, 14),
    /*reg0A*/
    [VINREG_VOLT] = REG_FIELD(0x0A, 6, 13),
    /*reg0C*/
    [VSYSMIN_VOLT] = REG_FIELD(0x0C, 8, 13),
    /*reg0E*/
    [INPUT_CURR] = REG_FIELD(0x0E, 8, 14),
    /*reg20*/
    [CHARGER_STATUS] = REG_FIELD(0x20, 0, 15),
    /*reg22*/
    [PROCHOT_STATUS] = REG_FIELD(0x22, 0, 15),
    /*reg24*/
    [IINDPM_CURR] = REG_FIELD(0x24, 8, 14),
    /*reg26*/
    [ADC_VBUS] = REG_FIELD(0x26, 8, 15),
    [ADC_VPSYS] = REG_FIELD(0x26, 0, 7),
    /*reg28*/
    [ADC_ICHG] = REG_FIELD(0x28, 8, 15),
    [ADC_IDCHG] = REG_FIELD(0x28, 0, 7),
    /*reg2A*/
    [ADC_IN] = REG_FIELD(0x2A, 8, 15),
    [ADC_CMPIN] = REG_FIELD(0x2A, 0, 7),
    /*reg2C*/
    [ADC_VSYS] = REG_FIELD(0x2C, 8, 15),
    [ADC_VBAT] = REG_FIELD(0x2C, 0, 7),
    /*reg2E*/
    [DEVICE_ID] = REG_FIELD(0x2E, 8, 15),
    [MANU_ID] = REG_FIELD(0x2E, 0, 7),
    /*REG30*/
    [EN_IBAT] = REG_FIELD(0x30, 15, 15),
    [EN_PROCHOT_LPWR] = REG_FIELD(0x30, 13, 14),
    [EN_PSYS] = REG_FIELD(0x30, 12, 12),
    [RSNS_RAC] = REG_FIELD(0x30, 11, 11),
    [RSNS_RSR] = REG_FIELD(0x30, 10, 10),
    [PSYS_RATIO] = REG_FIELD(0x30, 9, 9),
    [CMP_REF] = REG_FIELD(0x30, 7, 7),
    [CMP_POL] = REG_FIELD(0x30, 6, 6),
    [CMP_DEG] = REG_FIELD(0x30, 4, 5),
    [FORCE_LATCHOFF] = REG_FIELD(0x30, 3, 3),
    [EN_SHIP_DCHG] = REG_FIELD(0x30, 1, 1),
    [AUTO_WAKEUP_EN] = REG_FIELD(0x30, 0, 0),
    /*REG32*/
    [PKPWR_TOVLD_REG] = REG_FIELD(0x32, 14, 15),
    [EN_PKPWR_IDPM] = REG_FIELD(0x32, 13, 13),
    [EN_PKPWR_VSYS] = REG_FIELD(0x32, 12, 12),
    [PKPWER_OVLD_STAT] = REG_FIELD(0x32, 11, 11),
    [PKPWR_RELAX_STAT] = REG_FIELD(0x32, 10, 10),
    [PKPWER_TMAX] = REG_FIELD(0x32, 8, 9),
    [EN_EXTILIM] = REG_FIELD(0x32, 7, 7),
    [EN_ICHG_IDCHG] = REG_FIELD(0x32, 6, 6),
    [Q2_OCP] = REG_FIELD(0x32, 5, 5),
    [EN_ACOC] = REG_FIELD(0x32, 3, 3),
    [ACOC_VTH] = REG_FIELD(0x32, 2, 2),
    [EN_BATOC] = REG_FIELD(0x32, 1, 1),
    [BATCOC_VTH] = REG_FIELD(0x32, 0, 0),
    /*REG34*/
    [EN_HIZ] = REG_FIELD(0x34, 15, 15),
    [RESET_REG] = REG_FIELD(0x34, 14, 14),
    [RESET_VINDPM] = REG_FIELD(0x34, 13, 13),
    [EN_OTG] = REG_FIELD(0x34, 12, 12),
    [EN_ICO_MODE] = REG_FIELD(0x34, 11, 11),
    [OTG_RANGE_LOW] = REG_FIELD(0x34, 2, 2),
    [BATFETOFF_HIZ] = REG_FIELD(0x34, 1, 1),
    [PSYS_OTG_IDCHG] = REG_FIELD(0x34, 0, 0),
    /*REG36*/
    [ILIM2_VTH] = REG_FIELD(0x36, 11, 15),
    [ICRIT_DEG] = REG_FIELD(0x36, 9, 10),
    [VSYS_VTH] = REG_FIELD(0x36, 4, 7),
    [VSYS_TH2] = REG_FIELD(0x36, 2, 3),
    [INORM_DEG] = REG_FIELD(0x36, 1, 1),
    /*REG38*/
    [IDCHG_VTH] = REG_FIELD(0x38, 10, 15),
    [IDCHG_DEG] = REG_FIELD(0x38, 8, 9),
    [PROCHOT_PROFILE_COMP] = REG_FIELD(0x38, 6, 6),
    [PROCHOT_PROFILE_ICRIT] = REG_FIELD(0x38, 5, 5),
    [PROCHOT_PROFILE_INOM] = REG_FIELD(0x38, 4, 4),
    [PROCHOT_PROFILE_IDCHG] = REG_FIELD(0x38, 3, 3),
    [PROCHOT_PROFILE_VSYS] = REG_FIELD(0x38, 2, 2),
    [PROCHOT_PROFILE_BATPRES] = REG_FIELD(0x38, 1, 1),
    [PROCHOT_PROFILE_ACOK] = REG_FIELD(0x38, 0, 0),
    /*REG3A*/
    [ADC_CONV] = REG_FIELD(0x3A, 15, 15),
    [ADC_START] = REG_FIELD(0x3A, 14, 14),
    [EN_ADC_CMPIN] = REG_FIELD(0x3A, 7, 7),
    [EN_ADC_VBUS] = REG_FIELD(0x3A, 6, 6),
    [EN_ADC_PSYS] = REG_FIELD(0x3A, 5, 5),
    [EN_ADC_IIN] = REG_FIELD(0x3A, 4, 4),
    [EN_ADC_IDCHG] = REG_FIELD(0x3A, 3, 3),
    [EN_ADC_ICHG] = REG_FIELD(0x3A, 2, 2),
    [EN_ADC_VSYS] = REG_FIELD(0x3A, 1, 1),
    [EN_ADC_VBAT] = REG_FIELD(0x3A, 0, 0),
};

static const struct reg_field sy6969_reg_fields[] = {
    /*reg00*/
    [EN_LWPWR] = REG_FIELD(0x12, 15, 15),
    [WDT_ADJ] = REG_FIELD(0x12, 13, 14),
    [IDPM_AUTO_DIS] = REG_FIELD(0x12, 12, 12),
    [OTG_ON_CHRGOK] = REG_FIELD(0x12, 11, 11),
    [EN_OOA] = REG_FIELD(0x12, 10, 10),
    [PWM_FREQ] = REG_FIELD(0x12, 9, 9),
    [LOW_PTM_RIPPLE] = REG_FIELD(0x12, 8, 8),
    [SYSSHORT_DIS] = REG_FIELD(0x12, 6, 6),
    [EN_LEARN] = REG_FIELD(0x12, 5, 5),
    [IADPT_GAIN] = REG_FIELD(0x12, 4, 4),
    [IBAT_GAIN] = REG_FIELD(0x12, 3, 3),
    [EN_LDO] = REG_FIELD(0x12, 2, 2),
    [EN_IDPM] = REG_FIELD(0x12, 1, 1),
    [EN_CHRG] = REG_FIELD(0x12, 0, 0),
    /*reg02*/
    [CHARGE_CURR] = REG_FIELD(0x14, 6, 12),
    /*reg04*/
    [CHARGE_VOLT] = REG_FIELD(0x15, 3, 14),
    /*reg06*/
    [OTG_VOLT] = REG_FIELD(0x3B, 2, 13),
    /*reg08*/
    [OTG_CURR] = REG_FIELD(0x3C, 8, 14),
    /*reg0A*/
    [VINREG_VOLT] = REG_FIELD(0x3D, 6, 13),
    /*reg0C*/
    [VSYSMIN_VOLT] = REG_FIELD(0x3E, 8, 13),
    /*reg0E*/
    [INPUT_CURR] = REG_FIELD(0x3F, 8, 14),
    /*reg20*/
    [CHARGER_STATUS] = REG_FIELD(0x20, 0, 15),
    /*reg22*/
    [PROCHOT_STATUS] = REG_FIELD(0x21, 0, 15),
    /*reg24*/
    [IINDPM_CURR] = REG_FIELD(0x22, 8, 14),
    /*reg26*/
    [ADC_VBUS] = REG_FIELD(0x23, 8, 15),
    [ADC_VPSYS] = REG_FIELD(0x23, 0, 7),
    /*reg28*/
    [ADC_ICHG] = REG_FIELD(0x24, 8, 15),
    [ADC_IDCHG] = REG_FIELD(0x24, 0, 7),
    /*reg2A*/
    [ADC_IN] = REG_FIELD(0x25, 8, 15),
    [ADC_CMPIN] = REG_FIELD(0x25, 0, 7),
    /*reg2C*/
    [ADC_VSYS] = REG_FIELD(0x26, 8, 15),
    [ADC_VBAT] = REG_FIELD(0x26, 0, 7),
    /*reg2E*/
    [DEVICE_ID] = REG_FIELD(0xFF, 8, 15),
    [MANU_ID] = REG_FIELD(0xFE, 0, 7),
    /*REG30*/
    [EN_IBAT] = REG_FIELD(0x30, 15, 15),
    [EN_PROCHOT_LPWR] = REG_FIELD(0x30, 13, 14),
    [EN_PSYS] = REG_FIELD(0x30, 12, 12),
    [RSNS_RAC] = REG_FIELD(0x30, 11, 11),
    [RSNS_RSR] = REG_FIELD(0x30, 10, 10),
    [PSYS_RATIO] = REG_FIELD(0x30, 9, 9),
    [CMP_REF] = REG_FIELD(0x30, 7, 7),
    [CMP_POL] = REG_FIELD(0x30, 6, 6),
    [CMP_DEG] = REG_FIELD(0x30, 4, 5),
    [FORCE_LATCHOFF] = REG_FIELD(0x30, 3, 3),
    [EN_SHIP_DCHG] = REG_FIELD(0x30, 1, 1),
    [AUTO_WAKEUP_EN] = REG_FIELD(0x30, 0, 0),
    /*REG31*/
    [PKPWR_TOVLD_REG] = REG_FIELD(0x31, 14, 15),
    [EN_PKPWR_IDPM] = REG_FIELD(0x31, 13, 13),
    [EN_PKPWR_VSYS] = REG_FIELD(0x31, 12, 12),
    [PKPWER_OVLD_STAT] = REG_FIELD(0x31, 11, 11),
    [PKPWR_RELAX_STAT] = REG_FIELD(0x31, 10, 10),
    [PKPWER_TMAX] = REG_FIELD(0x31, 8, 9),
    [EN_EXTILIM] = REG_FIELD(0x31, 7, 7),
    [EN_ICHG_IDCHG] = REG_FIELD(0x31, 6, 6),
    [Q2_OCP] = REG_FIELD(0x31, 5, 5),
    [EN_ACOC] = REG_FIELD(0x31, 3, 3),
    [ACOC_VTH] = REG_FIELD(0x31, 2, 2),
    [EN_BATOC] = REG_FIELD(0x31, 1, 1),
    [BATCOC_VTH] = REG_FIELD(0x31, 0, 0),
    /*REG32*/
    [EN_HIZ] = REG_FIELD(0x32, 15, 15),
    [RESET_REG] = REG_FIELD(0x32, 14, 14),
    [RESET_VINDPM] = REG_FIELD(0x32, 13, 13),
    [EN_OTG] = REG_FIELD(0x32, 12, 12),
    [EN_ICO_MODE] = REG_FIELD(0x32, 11, 11),
    [OTG_RANGE_LOW] = REG_FIELD(0x32, 2, 2),
    [BATFETOFF_HIZ] = REG_FIELD(0x32, 1, 1),
    [PSYS_OTG_IDCHG] = REG_FIELD(0x32, 0, 0),
    /*REG33*/
    [ILIM2_VTH] = REG_FIELD(0x33, 11, 15),
    [ICRIT_DEG] = REG_FIELD(0x33, 9, 10),
    [VSYS_VTH] = REG_FIELD(0x33, 4, 7),
    [VSYS_TH2] = REG_FIELD(0x33, 2, 3),
    [INORM_DEG] = REG_FIELD(0x33, 1, 1),
    /*REG34*/
    [IDCHG_VTH] = REG_FIELD(0x34, 10, 15),
    [IDCHG_DEG] = REG_FIELD(0x34, 8, 9),
    [PROCHOT_PROFILE_COMP] = REG_FIELD(0x34, 6, 6),
    [PROCHOT_PROFILE_ICRIT] = REG_FIELD(0x34, 5, 5),
    [PROCHOT_PROFILE_INOM] = REG_FIELD(0x34, 4, 4),
    [PROCHOT_PROFILE_IDCHG] = REG_FIELD(0x34, 3, 3),
    [PROCHOT_PROFILE_VSYS] = REG_FIELD(0x34, 2, 2),
    [PROCHOT_PROFILE_BATPRES] = REG_FIELD(0x34, 1, 1),
    [PROCHOT_PROFILE_ACOK] = REG_FIELD(0x34, 0, 0),
    /*REG35*/
    [ADC_CONV] = REG_FIELD(0x35, 15, 15),
    [ADC_START] = REG_FIELD(0x35, 14, 14),
    [EN_ADC_CMPIN] = REG_FIELD(0x35, 7, 7),
    [EN_ADC_VBUS] = REG_FIELD(0x35, 6, 6),
    [EN_ADC_PSYS] = REG_FIELD(0x35, 5, 5),
    [EN_ADC_IIN] = REG_FIELD(0x35, 4, 4),
    [EN_ADC_IDCHG] = REG_FIELD(0x35, 3, 3),
    [EN_ADC_ICHG] = REG_FIELD(0x35, 2, 2),
    [EN_ADC_VSYS] = REG_FIELD(0x35, 1, 1),
    [EN_ADC_VBAT] = REG_FIELD(0x35, 0, 0),
};

static const struct regmap_config sc888x_regmap_config = {
    .reg_bits = 8,
    .val_bits = 16,
    
    .max_register = 0xFF,
    .val_format_endian = REGMAP_ENDIAN_LITTLE,
};

struct sc888x_cfg_e {
    const char *chg_name;
    int charge_current;
    int en_extlimt;
    int max_charge_voltage;
    int vsys_min_voltage;
    int vindpm_voltage;
    int iindpm_current;
    int otg_voltage;
    int otg_current;
    int wd_timeout;
    int en_low_power;
    int adc_mode;
    int adc_cmpin_en;
    int adc_vbus_en;
    int adc_vpsys_en;
    int adc_iin_en;
    int adc_idchg_en;
    int adc_ichg_en;
    int adc_vsys_en;
    int adc_vbat_en;
};

struct sc888x_charger_info {
    struct i2c_client *client;
    struct device *dev;
    struct regmap *regmap;
    struct regmap_field *rmap_fields[F_MAX_FIELDS];
    struct sc888x_cfg_e cfg;
    struct power_supply *psy_usb;
    struct power_supply_charge_current cur;
    struct mutex lock;
    struct gpio_desc *gpiod;
    struct delayed_work otg_work;
    struct delayed_work wdt_work;
    struct alarm otg_timer;
    u32 limit;
    u32 last_limit_current;
    u32 actual_limit_cur;
    u32 otg_check_cnt;
    u8 dev_id;
    bool charging;
    bool pd_hard_reset;
    bool pd_extcon_enable;
    bool typec_online;
    bool otg_enable;
    bool disable_power_path;
    bool is_compatible_sc888x;
    bool sqc_powerpath_enable;
};

/********************COMMON API***********************/
__maybe_unused static u16 val2reg(enum sc888x_reg_range id, u32 val)
{
    int i;
    u16 reg;
    const struct reg_range *range= &sc888x_reg_range[id];

    if (!range)
        return val;

    if (range->table) {
        if (val <= range->table[0])
            return 0;
        for (i = 1; i < range->num_table - 1; i++) {
            if (val == range->table[i])
                return i;
            if (val > range->table[i] &&
                val < range->table[i + 1])
                return range->round_up ? i + 1 : i;
        }
        return range->num_table - 1;
    }
    if (val <= range->min)
        reg = 0;
    else if (val >= range->max)
        reg = (range->max - range->offset) / range->step;
    else if (range->round_up)
        reg = (val - range->offset) / range->step + 1;
    else
        reg = (val - range->offset) / range->step;
    return reg;
}

__maybe_unused static u32 reg2val(enum sc888x_reg_range id, u16 reg)
{
    const struct reg_range *range= &sc888x_reg_range[id];
    if (!range)
        return reg;
    return range->table ? range->table[reg] :
                range->offset + range->step * reg;
}

/************************I2C API**************************/
static int sc888x_field_read(struct sc888x_charger_info *info,
                enum sc888x_fields field_id, int *val)
{
    int ret;

    ret = regmap_field_read(info->rmap_fields[field_id], val);
    if (ret < 0) {
        if (info->is_compatible_sc888x)
            dev_err(info->dev, "sc888x read reg 0x%02X fail: %d\n", sc8886_reg_fields[field_id].reg, ret);
        else
            dev_err(info->dev, "sc888x read reg 0x%02X fail: %d\n", sy6969_reg_fields[field_id].reg, ret);
    }
    
    return ret;
}

static int sc888x_field_write(struct sc888x_charger_info *info,
                enum sc888x_fields field_id, int val)
{
    int ret;

    ret = regmap_field_write(info->rmap_fields[field_id], val);
    if (ret < 0) {
        if (info->is_compatible_sc888x)
            dev_err(info->dev, "sc888x write reg 0x%02X fail: %d\n", sc8886_reg_fields[field_id].reg, ret);
        else
            dev_err(info->dev, "sc888x write reg 0x%02X fail: %d\n", sy6969_reg_fields[field_id].reg, ret);
    }

    return ret;
}

static int sc888x_write_field_reg(struct sc888x_charger_info *info,
                enum sc888x_fields field_id, int val)
{
    int ret;

    ret = regmap_write(info->regmap, sc8886_reg_fields[field_id].reg, val);
    if (ret < 0) {
        if (info->is_compatible_sc888x)
            dev_err(info->dev, "sc888x write_field_reg 0x%02X fail: %d\n", sc8886_reg_fields[field_id].reg, ret);
        else
            dev_err(info->dev, "sc888x write_field_reg 0x%02X fail: %d\n", sy6969_reg_fields[field_id].reg, ret);
    }

    return ret;
}

/************************CHIP API************************/
static int sc888x_charger_set_limit_current(struct sc888x_charger_info *info, u32 mA)
{
    int val = val2reg(SC888X_IINPUT_CURR, mA);

    dev_info(info->dev, "%s :%dmA, 0x%04x\n",__func__, mA, val);

    return sc888x_field_write(info, INPUT_CURR, val);
}


static u32 sc888x_charger_get_limit_current(struct sc888x_charger_info *info,
                          u32 *limit_cur)
{
    int ret;
    int val;

    ret = sc888x_field_read(info, INPUT_CURR, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *limit_cur = reg2val(SC888X_IINPUT_CURR, val);

    return ret;
}

static int sc888x_set_wd_timeout(struct sc888x_charger_info *info, bool enable)
{
    //int val = enable ? 2 : 0;
    int val = 0;

    return sc888x_field_write(info, WDT_ADJ, val);
}

static int sc888x_reg_reset(struct sc888x_charger_info *info)
{
    return sc888x_field_write(info, RESET_REG, 1);
}

static int sc888x_charger_set_vsysmin_vol(struct sc888x_charger_info *info, u32 vol)
{
    int val = val2reg(SC888X_VSYS_MIN, vol);

    dev_info(info->dev, "%s :%dmV, 0x%04x\n",__func__, vol, val);

    return sc888x_field_write(info, VSYSMIN_VOLT, val);
}

static int sc888x_enable_charger(struct sc888x_charger_info *info)
{
    return sc888x_field_write(info, EN_CHRG, 0);
}

static int sc888x_disable_charger(struct sc888x_charger_info *info)
{
    return sc888x_field_write(info, EN_CHRG, 1);
}

static int sc888x_get_charger_enabled(struct sc888x_charger_info *info, u32 *charger_enabled)
{
    int ret = 0;
    int val = 0;

    ret = sc888x_field_read(info, EN_CHRG, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    if (charger_enabled)
        *charger_enabled = !val;

    return !val;
}

static int sc888x_enter_hiz_mode(struct sc888x_charger_info *info)
{
    return sc888x_field_write(info, EN_HIZ, 1);
}

static int sc888x_exit_hiz_mode(struct sc888x_charger_info *info)
{
    return sc888x_field_write(info, EN_HIZ, 0);
}

static int sc888x_charger_get_hiz_mode(struct sc888x_charger_info *info, u32 *hiz_mode)
{
    int ret = 0;

    ret = sc888x_field_read(info, EN_HIZ, hiz_mode);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    return ret;
}

static int sc888x_charger_set_termina_vol(struct sc888x_charger_info *info, u32 vol)
{
    int val = val2reg(SC888X_CHG_VOLT, vol);

    /*for feed watchdog*/
    info->cfg.max_charge_voltage = val;

    dev_info(info->dev, "%s :%dmV, 0x%04x\n",__func__, vol, val);

    return sc888x_field_write(info, CHARGE_VOLT, val);
}

static int sc888x_charger_get_termina_vol(struct sc888x_charger_info *info, u32 *vol)
{
    int ret = 0;
    int val = 0;

    ret = sc888x_field_read(info, CHARGE_VOLT, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *vol = reg2val(SC888X_CHG_VOLT, val);

    return ret;
}

static int sc888x_charger_set_current(struct sc888x_charger_info *info, u32 cur)
{
    int val = val2reg(SC888X_CHG_CURR, cur);

    dev_info(info->dev, "%s :%dmA, 0x%04x\n",__func__, cur, val);

    return sc888x_field_write(info, CHARGE_CURR, val);
}

static int sc888x_charger_get_current(struct sc888x_charger_info *info, u32 *cur)
{
    int ret = 0;
    int val = 0;

    ret = sc888x_field_read(info, CHARGE_CURR, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *cur = reg2val(SC888X_CHG_CURR, val);

    return ret;
}

static int sc888x_charger_set_vindpm(struct sc888x_charger_info *info, u32 vol)
{
    int val = val2reg(SC888X_VINREG_VOLT, vol);

    dev_info(info->dev, "%s :%dmV, 0x%04x\n",__func__, vol, val);

    return sc888x_field_write(info, VINREG_VOLT, val);
}

static int sc888x_get_adc(struct sc888x_charger_info *info, 
        enum sc888x_fields field_id, int *adc_val)
{
    int ret = 0;
    int val = 0;

    ret = sc888x_field_read(info, field_id, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *adc_val = val * sc888x_adc_lsb[field_id] + sc888x_adc_offset[field_id];

    return ret;
}

static int sc888x_charger_get_current_by_adc(struct sc888x_charger_info *info, u32 *cur)
{
    int val = 0;
    int ret = 0;

    ret = sc888x_get_adc(info, ADC_ICHG, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *cur = val;
    return 0;
}

static int sc888x_charger_get_vbus_by_adc(struct sc888x_charger_info *info, u32 *vol)
{
    int val = 0;
    int ret = 0;

    ret = sc888x_get_adc(info, ADC_VBUS, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *vol = val;
    return 0;
}

static int sc888x_detect_device(struct sc888x_charger_info *info)
{
    int ret = 0;
    int dev_id = 0, manu_id = 0;

    ret = sc888x_field_read(info, DEVICE_ID, &dev_id);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    ret = sc888x_field_read(info, MANU_ID, &manu_id);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    dev_info(info->dev, "%s: device id = 0x%02x, manu id = 0x%02x\n", __func__,
            dev_id, manu_id);

    return ret;
}

#define DUMP_BUFF_LEN (18 * 10) //strlen("REG[0x00]:0X0000, ") * 10

static void sc888x_dump_register(struct sc888x_charger_info *info)
{
    int ret = 0, i = 0, j = 0;
    int val = 0, reg = -1;
    char log_buf[DUMP_BUFF_LEN] = {0,};

    if (info->is_compatible_sc888x) {
        for (i = 0; i < ARRAY_SIZE(sc8886_reg_fields); i++) {
            if ((sc8886_reg_fields[i].reg < 0x26 || sc8886_reg_fields[i].reg >= 0x2C)
                    && (reg != sc8886_reg_fields[i].reg)) {
                ret = regmap_read(info->regmap, sc8886_reg_fields[i].reg, &val);
                reg = sc8886_reg_fields[i].reg;
                if (j < 8) {
                    snprintf(log_buf + strlen(log_buf), sizeof(log_buf) - strlen(log_buf), "REG[0x%02X]:0x%04X, ", reg, val);
                } else {
                    dev_err(info->dev, "%s %s\n", __func__, log_buf);
                    memset(log_buf, 0, DUMP_BUFF_LEN);
                    j = 0;
                    snprintf(log_buf + strlen(log_buf), sizeof(log_buf) - strlen(log_buf), "REG[0x%02X]:0x%04X, ", reg, val);
                }

                j++;

                if (reg == sc8886_reg_fields[EN_OTG].reg) {
                    if ((info->otg_enable) && (val != 0x1030)) {
                        dev_err(info->dev, "%s check otg reg [0x%04X], force setting 0x1030\n", __func__, val);
                        sc888x_write_field_reg(info, EN_OTG, 0x1030);
                    } else if ((!info->otg_enable) && (val != 0x0030)) {
                        dev_err(info->dev, "%s check otg reg [0x%04X], force setting 0x0030\n", __func__, val);
                        sc888x_write_field_reg(info, EN_OTG, 0x0030);
                    }
                }
            }

            if (i == (ARRAY_SIZE(sc8886_reg_fields) - 1)) {
                dev_err(info->dev, "%s %s\n", __func__, log_buf);
            }
        }
    } else {
        for (i = 0; i < ARRAY_SIZE(sy6969_reg_fields); i++) {
            if ((sy6969_reg_fields[i].reg < 0x23 || sy6969_reg_fields[i].reg > 0x26)
                    && (reg != sy6969_reg_fields[i].reg)) {
                ret = regmap_read(info->regmap, sy6969_reg_fields[i].reg, &val);
                reg = sy6969_reg_fields[i].reg;
                if (j < 8) {
                    snprintf(log_buf + strlen(log_buf), sizeof(log_buf) - strlen(log_buf), "REG[0x%02X]:0x%04X, ", reg, val);
                } else {
                    dev_err(info->dev, "%s %s\n", __func__, log_buf);
                    memset(log_buf, 0, DUMP_BUFF_LEN);
                    j = 0;
                    snprintf(log_buf + strlen(log_buf), sizeof(log_buf) - strlen(log_buf), "REG[0x%02X]:0x%04X, ", reg, val);
                }

                j++;
            }

            if (i == (ARRAY_SIZE(sy6969_reg_fields) - 1)) {
                dev_err(info->dev, "%s %s\n", __func__, log_buf);
            }
        }
    }

    return;
}

/****************************SYSTEM API*****************************/
/*static ssize_t sc888x_store_disable_batfet(struct device *dev,
                         struct device_attribute *attr,
                         const char *buf,
                         size_t count)
{
    struct sc888x_charger_info *info = dev_get_drvdata(dev);

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return count;
    }

    return count;
}
static DEVICE_ATTR(disable_batfet, 0644, NULL, sc888x_store_disable_batfet);*/


__maybe_unused static void power_path_control(struct sc888x_charger_info *info)
{
    char *saved_command_line;
    char result[5];
    char *match = strstr(saved_command_line, "androidboot.mode=");

    if (match) {
        memcpy(result, (match + strlen("androidboot.mode=")),
               sizeof(result) - 1);
        if ((!strcmp(result, "cali")) || (!strcmp(result, "auto")))
            info->disable_power_path = true;
    }
}

static bool sc888x_charger_is_bat_present(struct sc888x_charger_info *info)
{
    struct power_supply *psy;
    union power_supply_propval val;
    bool present = false;
    int ret;

    psy = power_supply_get_by_name(SC888X_BATTERY_NAME);
    if (!psy) {
        dev_err(info->dev, "Failed to get psy of sc27xx_fgu\n");
        return present;
    }
    ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE,
                    &val);
    if (ret == 0 && val.intval)
        present = true;
    power_supply_put(psy);

    if (ret)
        dev_err(info->dev,
            "Failed to get property of present:%d\n", ret);

    return present;
}

static int sc888x_charger_is_fgu_present(struct sc888x_charger_info *info)
{
    struct power_supply *psy = NULL;

    psy = power_supply_get_by_name(SC888X_BATTERY_NAME);
    if (!psy) {
        dev_err(info->dev, "Failed to find psy of %s\n", SC888X_BATTERY_NAME);
        return -ENODEV;
    }
    power_supply_put(psy);

    return 0;
}

static int sc888x_init_device(struct sc888x_charger_info *info)
{
    int ret = 0, i;
    struct {
        enum sc888x_fields field_id;
        int conv_data;
    } props[] = {
        {CHARGE_CURR, info->cfg.charge_current},
        {CHARGE_VOLT, info->cfg.max_charge_voltage},
        {VSYSMIN_VOLT, info->cfg.vsys_min_voltage},
        {VINREG_VOLT, info->cfg.vindpm_voltage},
        {IINDPM_CURR, info->cfg.iindpm_current},
        {OTG_VOLT, info->cfg.otg_voltage},
        {OTG_CURR, info->cfg.otg_current},
        {WDT_ADJ, info->cfg.wd_timeout},
        {EN_LWPWR, info->cfg.en_low_power},
        {ADC_CONV, info->cfg.adc_mode},
        {EN_ADC_CMPIN, info->cfg.adc_cmpin_en},
        {EN_ADC_VBUS, info->cfg.adc_vbus_en},
        {EN_ADC_PSYS, info->cfg.adc_vpsys_en},
        {EN_ADC_IIN, info->cfg.adc_iin_en},
        {EN_ADC_IDCHG, info->cfg.adc_idchg_en},
        {EN_ADC_ICHG, info->cfg.adc_ichg_en},
        {EN_ADC_VSYS, info->cfg.adc_vsys_en},
        {EN_ADC_VBAT, info->cfg.adc_vbat_en},
        {EN_EXTILIM, info->cfg.en_extlimt},
    };

    ret = sc888x_reg_reset(info);
    if (ret < 0) {
        dev_err(info->dev, "%s Failed to reset registers(%d)\n", __func__, ret);
        return ret;
    }
    msleep(10);

    for (i = 0; i < ARRAY_SIZE(props); i++) {
        ret = sc888x_field_write(info, props[i].field_id, props[i].conv_data);
        if (ret < 0) {
            dev_err(info->dev, "%s Failed to write %d(%d)\n", __func__, props[i].field_id, ret);
            return ret;
        }
    }

    return ret;
}

static int sc888x_charger_hw_init(struct sc888x_charger_info *info)
{

    return sc888x_init_device(info);
}

static int sc888x_charger_get_charge_voltage(struct sc888x_charger_info *info,
                           u32 *charge_vol)
{
    struct power_supply *psy;
    union power_supply_propval val;
    int ret;

    psy = power_supply_get_by_name(SC888X_BATTERY_NAME);
    if (!psy) {
        dev_err(info->dev, "failed to get SC888X_BATTERY_NAME\n");
        return -ENODEV;
    }

    ret = power_supply_get_property(psy,
                    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
                    &val);
    power_supply_put(psy);
    if (ret) {
        dev_err(info->dev, "failed to get CONSTANT_CHARGE_VOLTAGE\n");
        return ret;
    }

    *charge_vol = val.intval;

    return 0;
}

static int sc888x_charger_get_vbat(struct sc888x_charger_info *info,
                           u32 *vbat_vol)
{
    struct power_supply *psy;
    union power_supply_propval val;
    int ret;

    psy = power_supply_get_by_name(SC888X_BATTERY_NAME);
    if (!psy) {
        dev_err(info->dev, "failed to get SC888X_BATTERY_NAME\n");
        return -ENODEV;
    }

    ret = power_supply_get_property(psy,
                    POWER_SUPPLY_PROP_VOLTAGE_NOW,
                    &val);
    power_supply_put(psy);
    if (ret) {
        dev_err(info->dev, "failed to get CONSTANT_CHARGE_VOLTAGE\n");
        return ret;
    }

    *vbat_vol = val.intval;

    return 0;
}

static int sc888x_charger_start_charge(struct sc888x_charger_info *info)
{
    int ret = 0;

    dev_info(info->dev, "agenew %s, %d\n",__func__, __LINE__);

    ret = sc888x_exit_hiz_mode(info);
    if (ret)
        dev_err(info->dev, "disable HIZ mode failed\n");


    ret = sc888x_set_wd_timeout(info, true);
    if (ret) {
        dev_err(info->dev, "Failed to enable sc888x watchdog\n");
        return ret;
    }

    ret = sc888x_enable_charger(info);
    if (ret)
        dev_err(info->dev, "enable sc888x charger failed\n");

    return ret;
}

static void sc888x_charger_stop_charge(struct sc888x_charger_info *info)
{
    int ret;

    if (boot_calibration) {
        ret = sc888x_enter_hiz_mode(info);
        if (ret)
            dev_err(info->dev, "enable HIZ mode failed\n");
    }

    ret = sc888x_disable_charger(info);
    if (ret)
        dev_err(info->dev, "disable sc888x charger failed\n");

    if (info->disable_power_path) {
        ret = sc888x_enter_hiz_mode(info);
        if (ret)
            dev_err(info->dev, "Failed to disable power path\n");
    }

    ret = sc888x_set_wd_timeout(info, false);
    if (ret)
        dev_err(info->dev, "Failed to disable sc888x watchdog\n");
}

static int sc888x_charger_get_health(struct sc888x_charger_info *info, u32 *health)
{
    *health = POWER_SUPPLY_HEALTH_GOOD;

    return 0;
}

static int sc888x_charger_get_online(struct sc888x_charger_info *info, u32 *online)
{
    if (info->limit)
        *online = true;
    else
        *online = false;

    return 0;
}

static int sc888x_charger_feed_watchdog(struct sc888x_charger_info *info)
{
    int ret;

    ret = sc888x_field_write(info, CHARGE_VOLT, info->cfg.max_charge_voltage);
    if (ret) {
        dev_err(info->dev, "reset sc888x failed\n");
        return ret;
    }

    return 0;
}

static int sc888x_charger_get_status(struct sc888x_charger_info *info)
{
    if (info->charging)
        return POWER_SUPPLY_STATUS_CHARGING;
    else
        return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int sc888x_charger_set_status(struct sc888x_charger_info *info, int val)
{
    int ret = 0;
    u32 input_vol;

    if (val == CM_FAST_CHARGE_OVP_DISABLE_CMD) {
        ret = sc888x_charger_get_charge_voltage(info, &input_vol);
        if (ret) {
            dev_err(info->dev, "tianjianwei failed to get 9V charge voltage\n");
            return ret;
        }
    } else if (val == false) {
        ret = sc888x_charger_get_charge_voltage(info, &input_vol);
        if (ret) {
            dev_err(info->dev, "tianjianwei failed to get 5V charge voltage\n");
            return ret;
        }
    }

    if (val > CM_FAST_CHARGE_NORMAL_CMD)
        return 0;

    if (!val && info->charging) {
        sc888x_charger_stop_charge(info);
        info->charging = false;
    } else if (val && !info->charging) {
        ret = sc888x_charger_start_charge(info);
        if (ret)
            dev_err(info->dev, "start charge failed\n");
        else
            info->charging = true;
    }

    return ret;
}

static int sc888x_charger_usb_get_property(struct power_supply *psy,
                         enum power_supply_property psp,
                         union power_supply_propval *val)
{
    struct sc888x_charger_info *info = power_supply_get_drvdata(psy);
    u32 cur, vol, online, health;
    int ret = 0;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    mutex_lock(&info->lock);
    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        if (info->limit)
            val->intval = sc888x_charger_get_status(info);
        else
            val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        break;
    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
        if (!info->charging) {
            val->intval = 0;
        } else {
            ret = sc888x_charger_get_current_by_adc(info, &cur);
            if (ret)
                goto out;
            val->intval = cur * 1000;
        }
        break;
    case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
        ret = sc888x_charger_get_vbus_by_adc(info, &vol);
        if (ret)
            goto out;
        val->intval = vol * 1000;
        break;
    case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
        if (!info->charging) {
            val->intval = 0;
        } else {
            ret = sc888x_charger_get_limit_current(info, &cur);
            if (ret)
                goto out;
            val->intval = cur * 1000;
        }
        break;
    case POWER_SUPPLY_PROP_ONLINE:
        ret = sc888x_charger_get_online(info, &online);
        if (ret)
            goto out;
        val->intval = online;
        break;
    case POWER_SUPPLY_PROP_HEALTH:
        if (info->charging) {
            val->intval = 0;
        } else {
            ret = sc888x_charger_get_health(info, &health);
            if (ret)
                goto out;
            val->intval = health;
        }
        break;
    case POWER_SUPPLY_PROP_USB_TYPE:
        val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
        break;
    default:
        ret = -EINVAL;
    }

out:
    mutex_unlock(&info->lock);
    return ret;
}

static int sc888x_charger_usb_set_property(struct power_supply *psy,
                         enum power_supply_property psp,
                         const union power_supply_propval *val)
{
    struct sc888x_charger_info *info = power_supply_get_drvdata(psy);
    int ret;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    return 0;

    mutex_lock(&info->lock);

    switch (psp) {
    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
        ret = sc888x_charger_set_current(info, val->intval / 1000);
        if (ret < 0)
            dev_err(info->dev, "set charge current failed\n");
        break;
    case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
        ret = sc888x_charger_set_limit_current(info, val->intval / 1000);
        if (ret < 0)
            dev_err(info->dev, "set input current limit failed\n");
        break;
    case POWER_SUPPLY_PROP_STATUS:
        ret = sc888x_charger_set_status(info, val->intval);
        if (ret < 0)
            dev_err(info->dev, "set charge status failed\n");
        break;
    /*case POWER_SUPPLY_PROP_FEED_WATCHDOG:
        ret = sc888x_charger_feed_watchdog(info);
        if (ret < 0)
            dev_err(info->dev, "feed charger watchdog failed\n");
        break;*/
    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
        ret = sc888x_charger_set_termina_vol(info, val->intval / 1000);
        if (ret < 0)
            dev_err(info->dev, "failed to set terminate voltage\n");
        break;
    /*case POWER_SUPPLY_PROP_CHARGE_ENABLED:
        dev_err(info->dev, "tianjianwei POWER_SUPPLY_PROP_CHARGE_ENABLED = %d\n", val->intval);
        if (val->intval == true) {
            ret = sc888x_charger_start_charge(info);
            if (ret)
                dev_err(info->dev, "start charge failed\n");
        } else if (val->intval == false) {
            sc888x_charger_stop_charge(info);
        }
        break;*/
    default:
        ret = -EINVAL;
    }

    mutex_unlock(&info->lock);
    return ret;
}

static int sc888x_charger_property_is_writeable(struct power_supply *psy,
                          enum power_supply_property psp)
{
    int ret;

    switch (psp) {
    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
    case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
    /*case POWER_SUPPLY_PROP_CHARGE_ENABLED:*/
    case POWER_SUPPLY_PROP_STATUS:
        ret = 1;
        break;

    default:
        ret = 0;
    }

    return ret;
}

static enum power_supply_usb_type sc888x_charger_usb_types[] = {
    POWER_SUPPLY_USB_TYPE_UNKNOWN,
    POWER_SUPPLY_USB_TYPE_SDP,
    POWER_SUPPLY_USB_TYPE_DCP,
    POWER_SUPPLY_USB_TYPE_CDP,
    POWER_SUPPLY_USB_TYPE_C,
    POWER_SUPPLY_USB_TYPE_PD,
    POWER_SUPPLY_USB_TYPE_PD_DRP,
    POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};

static enum power_supply_property sc888x_usb_props[] = {
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
    POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
    POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_HEALTH,
    POWER_SUPPLY_PROP_USB_TYPE,
    /*POWER_SUPPLY_PROP_CHARGE_ENABLED,*/
};

static const struct power_supply_desc sc888x_charger_desc = {
    .name            = "charger_psy",
    .type            = POWER_SUPPLY_TYPE_USB,
    .properties        = sc888x_usb_props,
    .num_properties        = ARRAY_SIZE(sc888x_usb_props),
    .get_property        = sc888x_charger_usb_get_property,
    .set_property        = sc888x_charger_usb_set_property,
    .property_is_writeable    = sc888x_charger_property_is_writeable,
    .usb_types        = sc888x_charger_usb_types,
    .num_usb_types        = ARRAY_SIZE(sc888x_charger_usb_types),
};

static const struct power_supply_desc sc888x_slave_charger_desc = {
    .name            = "charger_psy_slave",
    .type            = POWER_SUPPLY_TYPE_USB,
    .properties        = sc888x_usb_props,
    .num_properties        = ARRAY_SIZE(sc888x_usb_props),
    .get_property        = sc888x_charger_usb_get_property,
    .set_property        = sc888x_charger_usb_set_property,
    .property_is_writeable    = sc888x_charger_property_is_writeable,
    .usb_types        = sc888x_charger_usb_types,
    .num_usb_types        = ARRAY_SIZE(sc888x_charger_usb_types),
};

static void sc888x_charger_feed_watchdog_work(struct work_struct *work)
{
    struct delayed_work *dwork = to_delayed_work(work);
    struct sc888x_charger_info *info = container_of(dwork,
                              struct sc888x_charger_info,
                              wdt_work);
    int ret;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return;
    }

    dev_err(info->dev, "%s feed dog!!!, max_charge_voltage %d\n", __func__, info->cfg.max_charge_voltage);

    ret = sc888x_field_write(info, CHARGE_VOLT, info->cfg.max_charge_voltage);
    if (ret) {
        dev_err(info->dev, "reset sc888x failed\n");
        return;
    }

    schedule_delayed_work(&info->wdt_work, msecs_to_jiffies(SC888X_FEED_WATCHDOG_VALID_MS));
}

#ifdef CONFIG_REGULATOR
static bool sc888x_charger_check_otg_valid(struct sc888x_charger_info *info)
{
    int ret;
    int value = 0;
    bool status = false;

    ret = sc888x_field_read(info, EN_OTG, &value);
    if (ret) {
        dev_err(info->dev, "get sc888x charger otg valid status failed\n");
        return status;
    }

    if (value)
        status = true;
    else
        dev_err(info->dev, "otg is not valid\n");

    return status;
}

static bool sc888x_charger_check_otg_fault(struct sc888x_charger_info *info)
{
    return false;
}

static void sc888x_charger_otg_work(struct work_struct *work)
{
    struct delayed_work *dwork = to_delayed_work(work);
    struct sc888x_charger_info *info = container_of(dwork,
                              struct sc888x_charger_info, otg_work);
    bool otg_valid;
    bool otg_fault;
    int ret, retry = 0;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return;
    }

    if ((info->otg_check_cnt % 30 == 0) || (info->otg_check_cnt == 0))
        sc888x_dump_register(info);

    info->otg_check_cnt++;

    otg_valid = sc888x_charger_check_otg_valid(info);
    pr_err("%s: otg_valid %d\n", __func__, otg_valid);
    if (otg_valid)
        goto out;

    do {
        otg_fault = sc888x_charger_check_otg_fault(info);
        if (!otg_fault) {
            ret = sc888x_field_write(info, EN_OTG, 1);
            if (ret)
                dev_err(info->dev, "restart sc888x charger otg failed\n");
        }
        otg_valid = sc888x_charger_check_otg_valid(info);
    } while (!otg_valid && retry++ < SC888X_OTG_RETRY_TIMES);

    if (retry >= SC888X_OTG_RETRY_TIMES) {
        dev_err(info->dev, "Restart OTG failed\n");
        return;
    }

out:
    schedule_delayed_work(&info->otg_work, msecs_to_jiffies(SC888X_OTG_VALID_MS));
}

static int sc888x_charger_enable_otg(struct regulator_dev *dev)
{
    struct sc888x_charger_info *info = rdev_get_drvdata(dev);
    int ret = 0;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    dev_err(info->dev, "sc888x_charger_enable_otg into\n");

    mutex_lock(&info->lock);

    dev_err(info->dev, "set sc888x EN_LWPWR to 0\n");
    ret = sc888x_field_write(info, EN_LWPWR, 0);
    if (ret) {
        dev_err(info->dev, "set sc888x EN_LWPWR to 0 failed\n");
    }

    if (info->dev_id == SGM41573_DEVICE_ID) {
        dev_err(info->dev, "set sgm41573 EN_OTG reg to 0x1030\n");
        sc888x_write_field_reg(info, EN_OTG, 0x1030);
        dev_err(info->dev, "set sgm41573 EN_OOA reg to 0\n");
        sc888x_field_write(info, EN_OOA, 0);
    } else {
        ret = sc888x_field_write(info, EN_OTG, 1);
        if (ret) {
            dev_err(info->dev, "enable sc888x otg failed\n");
            goto out;
        } else {
            dev_info(info->dev, "enable sc888x otg success\n");
        }
    }

    info->otg_enable = true;
    info->otg_check_cnt = 0;

    //sc888x_dump_register(info);

    schedule_delayed_work(&info->wdt_work,
                  msecs_to_jiffies(0));
    schedule_delayed_work(&info->otg_work,
                  msecs_to_jiffies(SC888X_OTG_VALID_MS));

out:
    mutex_unlock(&info->lock);
    return ret;
}

static int sc888x_charger_disable_otg(struct regulator_dev *dev)
{
    struct sc888x_charger_info *info = rdev_get_drvdata(dev);
    int ret = 0;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    dev_err(info->dev, "sc888x_charger_disable_otg into\n");

    mutex_lock(&info->lock);
    info->otg_enable = false;
    cancel_delayed_work_sync(&info->wdt_work);
    cancel_delayed_work_sync(&info->otg_work);


    dev_err(info->dev, "set sc888x EN_LWPWR to 1\n");
    ret = sc888x_field_write(info, EN_LWPWR, 1);
    if (ret) {
        dev_err(info->dev, "set sc888x EN_LWPWR to 1 failed\n");
    }

    if (info->dev_id == SGM41573_DEVICE_ID) {
        dev_err(info->dev, "set sgm41573 reg to 0x0030\n");
        sc888x_write_field_reg(info, EN_OTG, 0x0030);
    } else {
        ret = sc888x_field_write(info, EN_OTG, 0);
        if (ret) {
            dev_err(info->dev, "disable sc888x otg failed\n");
            goto out;
        }
    }

    info->otg_check_cnt = 0;

    //sc888x_dump_register(info);

out:
    mutex_unlock(&info->lock);
    return ret;
}

static int sc888x_charger_vbus_is_enabled(struct regulator_dev *dev)
{
    struct sc888x_charger_info *info = rdev_get_drvdata(dev);

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    return info->otg_enable;
}

static const struct regulator_ops sc888x_charger_vbus_ops = {
    .enable = sc888x_charger_enable_otg,
    .disable = sc888x_charger_disable_otg,
    .is_enabled = sc888x_charger_vbus_is_enabled,
};

static const struct regulator_desc sc888x_charger_vbus_desc = {
    .name = "otg-vbus",
    .of_match = "otg-vbus",
    .type = REGULATOR_VOLTAGE,
    .owner = THIS_MODULE,
    .ops = &sc888x_charger_vbus_ops,
    .fixed_uV = 5000000,
    .n_voltages = 1,
};

static int sc888x_charger_register_vbus_regulator(struct sc888x_charger_info *info)
{
    struct regulator_config cfg = { };
    struct regulator_dev *reg;
    int ret = 0;

    cfg.dev = info->dev;
    cfg.driver_data = info;
    reg = devm_regulator_register(info->dev, &sc888x_charger_vbus_desc, &cfg);
    if (IS_ERR(reg)) {
        ret = PTR_ERR(reg);
        dev_err(info->dev, "Can't register regulator:%d\n", ret);
    }

    return ret;
}

#else
static int sc888x_charger_register_vbus_regulator(struct sc888x_charger_info *info)
{
    return 0;
}
#endif

static int sqc_mp_get_chg_type(void *arg, unsigned int *chg_type)
{
	arg = arg ? arg : NULL;

	*chg_type = SQC_PMIC_TYPE_BUCKBOOST;

	return 0;
}

static int sqc_mp_get_chg_status(void *arg, unsigned int *charing_status)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;

    info = info ? info : NULL;

	*charing_status = 0;

	return 0;
}

static int sqc_mp_set_enable_chging(void *arg, unsigned int en_now)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    u32 charger_enabled = sc888x_get_charger_enabled(info, NULL);
    int present = sc888x_charger_is_bat_present(info);

    if (!present) {
        dev_info(info->dev, "bat is not present, forcing chging\n");
        sc888x_set_wd_timeout(info, false);
        sc888x_enable_charger(info);
        return 0;
    }

    if (en_now == charger_enabled) {
        dev_info(info->dev, "charging status is already %d\n", charger_enabled);
        sc888x_dump_register(info);
        return 0;
    }

	if (en_now) {
        sc888x_enable_charger(info);

		info->charging = true;

        sc888x_set_wd_timeout(info, true);

        sc888x_dump_register(info);

        //schedule_delayed_work(&info->wdt_work, msecs_to_jiffies(0));
	} else {
		sc888x_disable_charger(info);

		//sc888x_set_wd_timeout(info, false);

		sc888x_dump_register(info);

		info->charging = false;

        //cancel_delayed_work_sync(&info->wdt_work);
	}

	dev_info(info->dev, "sc888x %s %d\n", __func__, en_now);

	return 0;
}

static int sqc_mp_get_enable_chging(void *arg, unsigned int *en)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0;

	*en = info->charging;

	dev_info(info->dev, "sc888x %s %d\n", __func__, *en);

	return ret;
}

static int sqc_mp_get_ichg(void *arg, u32 *ichg_ma)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    int ret = 0;

    ret = sc888x_charger_get_current(info, ichg_ma);

	dev_info(info->dev, "sc888x %s %d\n", __func__, *ichg_ma);

	return ret;
}

static int sqc_mp_set_ichg(void *arg, u32 mA)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0, vbat_mv = 0;

    if (info->sqc_powerpath_enable) {
        dev_info(info->dev, "sc888x %s powerpath enabled force disching\n", __func__);
        mA = 0;
    } else {
        ret = sc888x_charger_get_vbat(info, &vbat_mv);
        if (ret < 0) {
            dev_err(info->dev, "%s get FG_VBAT fail(%d)\n", __func__, ret);
        }

        if ((vbat_mv / 1000) <= 6000) {
            sc888x_field_write(info, EN_LDO, 0);
            mA = 256;
        }
    }

	ret = sc888x_charger_set_current(info, mA);

	dev_info(info->dev, "sc888x %s vbat_mv:%d, %dmA\n", __func__, vbat_mv, mA);

	return ret;
}

static int sqc_mp_get_aicr(void *arg, u32 *aicr_ma)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0;

	ret = sc888x_charger_get_limit_current(info, aicr_ma);

	dev_info(info->dev, "sc888x %s %d\n", __func__, *aicr_ma);

	return ret;
}

static int sqc_mp_set_aicr(void *arg, u32 aicr_ma)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0;

	ret = sc888x_charger_set_limit_current(info, aicr_ma);

	dev_info(info->dev, "sc888x %s %d\n", __func__, aicr_ma);

	return ret;
}

static int sqc_mp_get_cv(void *arg, u32 *cv_mv)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0;

	ret = sc888x_charger_get_termina_vol(info, cv_mv);

	dev_info(info->dev, "sc888x %s %d\n", __func__, *cv_mv);

	return ret;
}

static int sqc_mp_set_cv(void *arg, u32 cv_mv)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0;

	ret = sc888x_charger_set_termina_vol(info, cv_mv);

	dev_info(info->dev, "sc888x %s %d\n", __func__, cv_mv);

	return ret;
}

static int sqc_mp_ovp_volt_get(void *arg, u32 *ac_ovp_mv)
{
    int ret = 0;

	arg = arg ? arg : NULL;

	return ret;
}

static int sqc_mp_ovp_volt_set(void *arg, u32 ac_ovp_mv)
{
    int ret = 0;

	arg = arg ? arg : NULL;

	return ret;
}

static int sqc_mp_get_vbat(void *arg, u32 *mV)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    int val = 0;
    int ret = 0;

    ret = sc888x_get_adc(info, ADC_VBAT, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *mV = val;

	return 0;
}

static int sqc_mp_get_ibat(void *arg, u32 *mA)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    int val = 0;
    int ret = 0;

    ret = sc888x_get_adc(info, ADC_ICHG, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *mA = val;

	return 0;
}

static int sqc_mp_get_vbus(void *arg, u32 *mV)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    int val = 0;
    int ret = 0;

    ret = sc888x_get_adc(info, ADC_VBUS, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *mV = val;

	return 0;
}

static int sqc_mp_set_vbus(void *arg, u32 mV)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    int val = 0;

    if (mV == 5000)
        val = 4700;
    else if (mV == 9000)
        val = 8200;
    else if (mV == 12000)
        val = 11000;
    else if (mV == 20000)
        val = 19000;
    else
        val = mV - 1000;

    dev_info(info->dev, "sc888x %s vbus:%d, vindpm:%d\n", __func__, mV, val);

    sc888x_charger_set_vindpm(info, val);

	return 0;
}

static int sqc_mp_get_ibus(void *arg, u32 *mA)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    int val = 0;
    int ret = 0;

    ret = sc888x_get_adc(info, ADC_IN, &val);
    if (ret < 0) {
        dev_err(info->dev, "%s fail(%d)\n", __func__, ret);
        return ret;
    }

    *mA = val;

	return 0;
}

static int sqc_mp_set_ibus(void *arg, u32 mA)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
    
    dev_info(info->dev, "sc888x %s ibus:%d\n", __func__, mA);

	return 0;
}

static int sqc_enable_powerpath_set(void *arg, unsigned int enabled)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;

	info->sqc_powerpath_enable = enabled;

    dev_info(info->dev, "%s: %d\n", __func__, enabled);

	return 0;
}

static int sqc_enable_powerpath_get(void *arg, unsigned int *enabled)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;

	*enabled = info->sqc_powerpath_enable;

	return 0;
}

static int sqc_enable_hiz_set(void *arg, unsigned int hiz_mode)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;

	if (hiz_mode)
        sc888x_enter_hiz_mode(info);
    else
        sc888x_exit_hiz_mode(info);

    dev_info(info->dev, "%s: %d\n", __func__, hiz_mode);

	return 0;
}

static int sqc_enable_hiz_get(void *arg, unsigned int *hiz_mode)
{
	struct sc888x_charger_info *info = (struct sc888x_charger_info *)arg;
	int ret = 0;

	ret = sc888x_charger_get_hiz_mode(info, hiz_mode);

	return ret;
}

static struct sqc_pmic_chg_ops sc888x_sqc_chg_ops = {

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

	.set_chging_topoff = NULL,
	.get_chging_topoff = NULL,

	.set_rechg_volt = NULL,
	.get_rechg_volt = NULL,

	.ac_ovp_volt_set = sqc_mp_ovp_volt_set,
	.ac_ovp_volt_get = sqc_mp_ovp_volt_get,

	.batt_ibat_get = sqc_mp_get_ibat,
	.batt_vbat_get = sqc_mp_get_vbat,

	.usb_ibus_get = sqc_mp_get_ibus,
	.usb_vbus_get = sqc_mp_get_vbus,

    .usb_ibus_set = sqc_mp_set_ibus,
	.usb_vbus_set = sqc_mp_set_vbus,

	.enable_path_set = sqc_enable_powerpath_set,
	.enable_path_get = sqc_enable_powerpath_get,

	.enable_hiz_set = sqc_enable_hiz_set,
	.enable_hiz_get = sqc_enable_hiz_get,
};

static int sc888x_parse_dt(struct sc888x_charger_info *info, struct device *dev)
{
    struct device_node *np = dev->of_node;
    int i;
    int ret;
    struct {
        char *name;
        int *conv_data;
    } props[] = {
        {"sc888x,charge-current", &(info->cfg.charge_current)},
        {"sc888x,max-charge-voltge", &(info->cfg.max_charge_voltage)},
        {"sc888x,vsys-min-voltage", &(info->cfg.vsys_min_voltage)},
        {"sc888x,vindpm-voltage", &(info->cfg.vindpm_voltage)},
        {"sc888x,iindpm-current", &(info->cfg.iindpm_current)},
        {"sc888x,otg-voltage", &(info->cfg.otg_voltage)},
        {"sc888x,otg-current", &(info->cfg.otg_current)},
        {"sc888x,wd-timeout", &(info->cfg.wd_timeout)},
        {"sc888x,en-low-power", &(info->cfg.en_low_power)},
        {"sc888x,adc-mode", &(info->cfg.adc_mode)},
        {"sc888x,adc-cmpin-en", &(info->cfg.adc_cmpin_en)},
        {"sc888x,adc-vbus-en", &(info->cfg.adc_vbus_en)},
        {"sc888x,adc-vpsys-en", &(info->cfg.adc_vpsys_en)},
        {"sc888x,adc-iin-en", &(info->cfg.adc_iin_en)},
        {"sc888x,adc-idchg-en", &(info->cfg.adc_idchg_en)},
        {"sc888x,adc-ichg-en", &(info->cfg.adc_ichg_en)},
        {"sc888x,adc-vsys-en", &(info->cfg.adc_vsys_en)},
        {"sc888x,adc-vbat-en", &(info->cfg.adc_vbat_en)},
    };

    /* initialize data for optional properties */
    for (i = 0; i < ARRAY_SIZE(props); i++) {
        ret = of_property_read_u32(np, props[i].name,
                        props[i].conv_data);
        if (ret < 0) {
            dev_err(info->dev, "can not read %s \n", props[i].name);
            return ret;
        }
    }

    return ret;
}

#define MSG_LEN                      (2)
static int sc888x_i2c_read_block(struct sc888x_charger_info *info,
	u8 *value, u8 reg, unsigned int num_bytes)
{
	struct i2c_msg msg[MSG_LEN];
	u8 buf = 0;
	int ret = 0;

	buf = reg;

	msg[0].addr = info->client->addr;
	msg[0].flags = 0;
	msg[0].buf = &buf;
	msg[0].len = 1;

	msg[1].addr = info->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = value;
	msg[1].len = num_bytes;

	ret = i2c_transfer(info->client->adapter, msg, MSG_LEN);

	/* i2c_transfer returns number of messages transferred */
	if (ret != MSG_LEN) {
		pr_err("%s: read_block failed[%x]\n", __func__, reg);
		if (ret < 0)
			return ret;
		else
			return -EIO;
	} else {
		return 0;
	}
}

static int sc888x_i2c_read_byte(struct sc888x_charger_info *info, u8 reg, u8 *value)
{
	return sc888x_i2c_read_block(info, value, reg, 1);
}

static int sc888x_check_is_sc888x_chip(struct sc888x_charger_info *info)
{
    u8 value = 0;
    int ret = 0;

    pr_err("%s: checking chg ic is SC888X\n", __func__);

	ret = sc888x_i2c_read_byte(info, 0x2F, &value);
    if (!ret) {
        pr_err("%s: read device id is 0x%02X, check SC888X successed\n", __func__, value);
        info->dev_id = value;
        return true;
    }

    pr_err("%s: checking chg ic is SY6969\n", __func__);

    info->client->addr = 0x09;

    ret = sc888x_i2c_read_byte(info, 0xFF, &value);
    if (!ret) {
        pr_err("%s: read device id is 0x%02X, check SY6969 successed\n", __func__, value);
        info->dev_id = value;
        return false;
    }

    pr_err("%s: check device id failed!!!\n", __func__);

    return -EIO;
}

static int sc888x_chip_name_show(struct seq_file *m, void *v)
{
	struct sc888x_charger_info *info = m->private;

   if (info->is_compatible_sc888x == true) {
        if ((info->dev_id == SC8885_DEVICE_ID) || (info->dev_id == SC8886_DEVICE_ID)) {
            seq_printf(m, "%s\n", "SC888X");
        } else if (info->dev_id == SGM41573_DEVICE_ID) {
            seq_printf(m, "%s\n", "SGM41573");
        } else {
            seq_printf(m, "%s\n", "UNKNOWN");
        }
    } else if (info->is_compatible_sc888x == false) {
        seq_printf(m, "%s\n", "SY6969C");
    } else {
        seq_printf(m, "%s\n", "UNKNOWN");
    }

    return 0;
}

static int sc888x_chip_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, sc888x_chip_name_show, PDE_DATA(inode));
}

static const struct proc_ops sc888x_chip_name_node = {
	.proc_open = sc888x_chip_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};

static int sc888x_charger_probe(struct i2c_client *client,
                  const struct i2c_device_id *id)
{
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct device *dev = &client->dev;
    struct power_supply_config charger_cfg = { };
    struct sc888x_charger_info *info = NULL;
    struct sqc_pmic_chg_ops *sqc_ops = NULL;
    int bat_present = 0;
    int ret = 0;
    int i = 0;

    if (!adapter) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    dev_err(&client->dev, "%s (%s)\n", __func__, SC888X_DRV_VERSION);

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
        return -ENODEV;
    }

    info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
    if (!info) {
        return -ENOMEM;
    }
        
    info->client = client;
    info->dev = dev;

    ret = sc888x_charger_is_fgu_present(info);
    if (ret) {
        dev_err(dev, "fg %s is not ready.\n", SC888X_BATTERY_NAME);
        devm_kfree(&client->dev, info);
        return -EPROBE_DEFER;
    }

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    info->is_compatible_sc888x = sc888x_check_is_sc888x_chip(info);

    if (info->is_compatible_sc888x) {

        dev_err(info->dev, "initialize sc888x regmap !!!\n");

        info->regmap = devm_regmap_init_i2c(client,
                                &sc888x_regmap_config);
        if (IS_ERR(info->regmap)) {
            dev_err(info->dev, "Failed to initialize regmap\n");
            devm_kfree(&client->dev, info);
            return -ENODEV;
        }

        for (i = 0; i < ARRAY_SIZE(sc8886_reg_fields); i++) {
            const struct reg_field *reg_fields = sc8886_reg_fields;

            info->rmap_fields[i] =
                devm_regmap_field_alloc(info->dev,
                            info->regmap,
                            reg_fields[i]);
            if (IS_ERR(info->rmap_fields[i])) {
                dev_err(info->dev, "cannot allocate regmap field\n");
                devm_kfree(&client->dev, info);
                return -EINVAL;
            }
        }
    } else {

        dev_err(info->dev, "initialize sy6969 regmap !!!\n");

        client->addr = 0x09;

        info->regmap = devm_regmap_init_i2c(client,
                                &sc888x_regmap_config);
        if (IS_ERR(info->regmap)) {
            dev_err(info->dev, "Failed to initialize regmap\n");
            devm_kfree(&client->dev, info);
            return -ENODEV;
        }

        for (i = 0; i < ARRAY_SIZE(sy6969_reg_fields); i++) {
            const struct reg_field *reg_fields = sy6969_reg_fields;

            info->rmap_fields[i] =
                devm_regmap_field_alloc(info->dev,
                            info->regmap,
                            reg_fields[i]);
            if (IS_ERR(info->rmap_fields[i])) {
                dev_err(info->dev, "cannot allocate regmap field\n");
                devm_kfree(&client->dev, info);
                return -EINVAL;
            }
        }
    }

    sc888x_detect_device(info);

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    i2c_set_clientdata(client, info);

    mutex_init(&info->lock);

    charger_cfg.drv_data = info;
    charger_cfg.of_node = dev->of_node;
    info->psy_usb = devm_power_supply_register(dev,
                            &sc888x_charger_desc,
                            &charger_cfg);
    if (IS_ERR(info->psy_usb)) {
        dev_err(dev, "failed to register power supply\n");
        ret = PTR_ERR(info->psy_usb);
        goto out;
    }

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    ret = sc888x_parse_dt(info, &client->dev);
    if (ret < 0) {
        dev_err(info->dev, "%s parse dt failed(%d)\n", __func__, ret);
        goto out;
    }

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    bat_present = sc888x_charger_is_bat_present(info);
    if (!bat_present) {
        sc888x_set_wd_timeout(info, false);
        sc888x_charger_set_limit_current(info, 6000);
        sc888x_charger_set_vsysmin_vol(info, 7800);
        dev_info(info->dev, "bat is not present, SGM41573/SC888X forcing dischging\n");
        sc888x_charger_stop_charge(info);
        sc888x_dump_register(info);
        //return 0;
    } else {
        ret = sc888x_charger_hw_init(info);
        printk("agenew %s,%d,ret:%d\n",__func__,__LINE__,ret);
        //if (ret)
            //goto out;

        dev_info(info->dev, "bat is present, forcing dischging\n");
        sc888x_charger_stop_charge(info);
    }

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    device_init_wakeup(info->dev, true);

    alarm_init(&info->otg_timer, ALARM_BOOTTIME, NULL);
    INIT_DELAYED_WORK(&info->otg_work, sc888x_charger_otg_work);
    INIT_DELAYED_WORK(&info->wdt_work, sc888x_charger_feed_watchdog_work);

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    ret = sc888x_charger_register_vbus_regulator(info);
    if (ret) {
        dev_err(dev, "failed to register vbus regulator.\n");
        goto out;
    }

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    if (bat_present) {
        sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
        memcpy(sqc_ops, &sc888x_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
        sqc_ops->arg = (void *)info;
        ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PRIMARY);
        if (ret < 0) {
            dev_err(dev, "%s register sqc hal fail(%d)\n", __func__, ret);
        }
    }

    dev_err(dev, "func:%s, line:%d.\n", __func__, __LINE__);

    proc_create_data("driver/chg_name", 0664, NULL,
			&sc888x_chip_name_node, info);

    pr_info("%s successful.\n", __func__);

    return 0;

out:
    mutex_destroy(&info->lock);
    devm_kfree(&client->dev, info);
    return ret;
}

static void sc888x_charger_shutdown(struct i2c_client *client)
{
    struct sc888x_charger_info *info = i2c_get_clientdata(client);
    int ret = 0;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return;
    }

    cancel_delayed_work_sync(&info->wdt_work);
    if (info->otg_enable) {
        info->otg_enable = false;
        cancel_delayed_work_sync(&info->otg_work);
        ret = sc888x_field_write(info, EN_OTG, 0);
        if (ret)
            dev_err(info->dev, "disable sc888x otg failed ret = %d\n", ret);
    }
}

static int sc888x_charger_remove(struct i2c_client *client)
{
    struct sc888x_charger_info *info = i2c_get_clientdata(client);

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sc888x_charger_suspend(struct device *dev)
{
    struct sc888x_charger_info *info = dev_get_drvdata(dev);
    ktime_t now, add;
    unsigned int wakeup_ms = SC888X_OTG_ALARM_TIMER_MS;
    int ret;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    if (!info->otg_enable)
        return 0;

    cancel_delayed_work_sync(&info->wdt_work);

    /* feed watchdog first before suspend */
    ret = sc888x_charger_feed_watchdog(info);
    if (ret)
        dev_warn(info->dev, "reset sc888x failed before suspend\n");

    now = ktime_get_boottime();
    add = ktime_set(wakeup_ms / MSEC_PER_SEC,
            (wakeup_ms % MSEC_PER_SEC) * NSEC_PER_MSEC);
    alarm_start(&info->otg_timer, ktime_add(now, add));

    return 0;
}

static int sc888x_charger_resume(struct device *dev)
{
    struct sc888x_charger_info *info = dev_get_drvdata(dev);
    int ret;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    if (!info->otg_enable)
        return 0;

    alarm_cancel(&info->otg_timer);

    /* feed watchdog first after resume */
    ret = sc888x_charger_feed_watchdog(info);
    if (ret)
        dev_warn(info->dev, "reset sc888x failed after resume\n");

    schedule_delayed_work(&info->wdt_work, msecs_to_jiffies(0));

    return 0;
}
#endif

static const struct dev_pm_ops sc888x_charger_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(sc888x_charger_suspend,
                sc888x_charger_resume)
};

static const struct i2c_device_id sc888x_i2c_id[] = {
    {"sc888x_chg", 0},
    {}
};

static const struct of_device_id sc888x_charger_of_match[] = {
    { .compatible = "sc,sc8886", },
    { }
};

MODULE_DEVICE_TABLE(of, sc888x_charger_of_match);

static struct i2c_driver sc888x_charger_driver = {
    .driver = {
        .name = "sc888x",
        .of_match_table = sc888x_charger_of_match,
        .pm = &sc888x_charger_pm_ops,
    },
    .probe = sc888x_charger_probe,
    .shutdown = sc888x_charger_shutdown,
    .remove = sc888x_charger_remove,
    .id_table = sc888x_i2c_id,
};

__maybe_unused static int __init get_calibration_mode(char *str)
{
    if (!str)
        return 0;
    if (!strncmp(str, "cali", strlen("cali")))
        boot_calibration = true;

    return 0;
}
__setup("androidboot.mode=", get_calibration_mode);

module_i2c_driver(sc888x_charger_driver);

MODULE_DESCRIPTION("SC888X Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aiden-yu@southchip.com");
