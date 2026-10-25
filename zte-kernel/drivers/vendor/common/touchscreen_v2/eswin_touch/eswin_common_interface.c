/* Started by AICoder, pid:z1bbdl9b7drcb461469a0ab5f4e60f597b2441b3 */
/************************************************************************
*
* File Name: eswin_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

#include "eswin_eph861x.h"
#include "eswin_eph861x_types.h"

#define MAX_NAME_LEN_50 50
#define ESWIN_TOUCH_FW_NAME "eswin_touch_fw_"
#define ESWIN_TOUCH_DEVICE_SETTINGS_NAME "eswin_touch_fw_settings_"
#define ESWIN_TOUCH_CSV_NAME "eswin_touch_criteria_"

char eswin_touch_vendor_name[MAX_NAME_LEN_50] = { 0 };
char eswin_touch_fw_name[MAX_NAME_LEN_50] = { 0 };
char eswin_touch_device_settings_name[MAX_NAME_LEN_50] = { 0 };
char eswin_touch_criteria_name[MAX_NAME_LEN_50] = { 0 };
#ifdef ESWIN_TOUCH_DEFAULT_FW_NAME
char eswin_touch_default_fw_name[MAX_NAME_LEN_50] = { 0 };
#endif

extern int eswin_product_self_test(struct eph_data *ephdata);
extern int eph_report_touch_enable(struct eph_data *ephdata, int enable);
extern int eph_deep_mode_enable(struct eph_data *ephdata, int enable);
extern void eph_clear_all_host_touch_slots(struct eph_data* ephdata);
extern int eph_trigger_baseline(struct eph_data *ephdata);
extern int eph_read_report_touch_enable_status(struct eph_data *ephdata, u8 *buffer);
extern int eph_gesture_mode_set(struct eph_data *ephdata, u8 gesture_mode);
extern void eph_regulator_disable(struct eph_data *ephdata);
extern int eph_charger_status_enable(struct eph_data *ephdata, int enable);
extern int eph_headset_status_enable(struct eph_data *ephdata, int enable);
extern int eph_sensibility_status_enable(struct eph_data *ephdata, int enable);
extern int eph_mRotatin_status_enable(struct eph_data *ephdata, int enable);
extern int heartbeat_work_start(struct eph_data *ephdata);
extern int heartbeat_work_stop(struct eph_data *ephdata);

extern struct eph_data *zte_ephdata;
static int rst_gpio = 0;

struct tpvendor_t eswin_touch_vendor_info[] = {
    {ESWIN_TOUCH_MODULE1_ID, ESWIN_TOUCH_MODULE1_LCD_NAME},
    {ESWIN_TOUCH_MODULE2_ID, ESWIN_TOUCH_MODULE2_LCD_NAME},
    {ESWIN_TOUCH_MODULE3_ID, ESWIN_TOUCH_MODULE3_LCD_NAME},
    {VENDOR_END, "Unknown"},
};

int get_eswin_touch_module_info_from_lcd(void) {
    int i;

    for (i = 0; i < ARRAY_SIZE(eswin_touch_vendor_info) - 1; i++) {
        eswin_log(INFO_LOG, "%d--->%s", i, eswin_touch_vendor_info[i].vendor_name);
        if (strnstr(lcd_name, eswin_touch_vendor_info[i].vendor_name, strlen(lcd_name))) {
            eswin_log(INFO_LOG, "get_lcd_panel_name find");
            break;
        }
    }

    snprintf(eswin_touch_vendor_name, sizeof(eswin_touch_vendor_name), "%s", eswin_touch_vendor_info[i].vendor_name);
    eswin_log(INFO_LOG, "eswin_touch_vendor_name: %s", eswin_touch_vendor_name);

    snprintf(eswin_touch_fw_name, sizeof(eswin_touch_fw_name), "%s%s.bin",
        ESWIN_TOUCH_FW_NAME, eswin_touch_vendor_info[i].vendor_name);
    eswin_log(INFO_LOG, "eswin_touch_fw_name: %s", eswin_touch_fw_name);

    snprintf(eswin_touch_device_settings_name, sizeof(eswin_touch_device_settings_name), "%s%s.bin",
        ESWIN_TOUCH_DEVICE_SETTINGS_NAME, eswin_touch_vendor_info[i].vendor_name);
    eswin_log(INFO_LOG, "eswin_touch_device_settings_name: %s", eswin_touch_device_settings_name);

    snprintf(eswin_touch_criteria_name, sizeof(eswin_touch_criteria_name), "%s%s.csv",
        ESWIN_TOUCH_CSV_NAME, eswin_touch_vendor_info[i].vendor_name);
    eswin_log(INFO_LOG, "eswin_touch_criteria_name: %s", eswin_touch_criteria_name);

#ifdef ESWIN_TOUCH_DEFAULT_FW_NAME
    snprintf(eswin_touch_default_fw_name, sizeof(eswin_touch_default_fw_name), "%s_%s.bin",
        ESWIN_TOUCH_DEFAULT_FW_NAME, eswin_touch_vendor_info[i].vendor_name);
    eswin_log(INFO_LOG, "eswin_touch_default_fw_name: %s", eswin_touch_default_fw_name);
#endif

    return eswin_touch_vendor_info[i].vendor_id;
}

static int eswin_touch_init_tpinfo(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL!");
        return -EIO;
    }

    strlcpy(cdev->ic_tpinfo.vendor_name, eswin_touch_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
    snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "ESWIN");
    cdev->ic_tpinfo.chip_model_id = TS_CHIP_ESWIN;
    cdev->ic_tpinfo.firmware_ver = ephdata->ephdeviceinfo.application_version_minor;
    cdev->ic_tpinfo.config_ver = ephdata->zte_ctrl.settings_ver;
    cdev->ic_tpinfo.spi_num = SPI_NUM;

    eswin_log(INFO_LOG, "tp_name:%s, vendor_name:%s, firmware_ver=%d, config_ver=%d",
        cdev->ic_tpinfo.tp_name, cdev->ic_tpinfo.vendor_name,
        cdev->ic_tpinfo.firmware_ver, cdev->ic_tpinfo.config_ver);
    return 0;
}

static int eswin_touch_test_cmd_store(struct ztp_device *cdev) {
    int retry = 0, ret = 0;
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL!");
        return -EIO;
    }

    eswin_log(INFO_LOG, "eswin selftest start");

    do {
        cdev->zte_tp_selftest_result = 0;

        ret = eswin_product_self_test(ephdata);

        if (ret || cdev->zte_tp_selftest_result) {
            retry ++;
        } else {
            break;
        }
    } while (retry < 3);

    if (cdev->zte_tp_selftest_result) {
        eswin_log(ERR_LOG, "eswin selftest failed");
    } else {
        eswin_log(INFO_LOG, "eswin selftest pass");
    }

    eswin_log(INFO_LOG, "eswin selftest end");
    return 0;
}

static int eswin_touch_test_cmd_show(struct ztp_device *cdev, char *buf) {
    ssize_t num_read_chars = 0;
	struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL!");
        return -EIO;
    }

    eswin_log(INFO_LOG, "RAWDATA_COL:%d", TX_NUM_MAX);
    eswin_log(INFO_LOG, "RAWDATA_ROW:%d", RX_NUM_MAX);
    num_read_chars = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", cdev->zte_tp_selftest_result,
        TX_NUM_MAX, RX_NUM_MAX, 0);
    eswin_log(INFO_LOG, "eswin touch test:%s", buf);

	return num_read_chars;
}

static int eswin_touch_bbat_test(struct ztp_device *cdev) {
    return 0;
}

static int eswin_touch_get_headset_state(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL!");
        return -EIO;
    }

    cdev->headset_state = ephdata->zte_ctrl.headset_state;
    eswin_log(INFO_LOG, "headset_state=%d", cdev->headset_state);

    return cdev->headset_state;
}

static int eswin_touch_set_headset_state(struct ztp_device *cdev, int enable) {
    int ret_val = 0;
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    ephdata->zte_ctrl.headset_state = enable;
    eswin_log(INFO_LOG, "headset_state=%d", ephdata->zte_ctrl.headset_state);

    ret_val = eph_headset_status_enable(ephdata, enable);

    return ret_val;
}

static int eswin_touch_get_sensibility(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    cdev->sensibility_enable = ephdata->zte_ctrl.sensibility_level;
    eswin_log(INFO_LOG, "sensibility_level=%d", cdev->sensibility_enable);

    return 0;
}

static int eswin_touch_set_sensibility(struct ztp_device *cdev, u8 enable) {
    int ret_val = 0;
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    ephdata->zte_ctrl.sensibility_level = enable;

    switch (enable) {
        case normal_sensibility:
            eswin_log(INFO_LOG, "eswin touch is normal sensibility");
            ret_val = eph_sensibility_status_enable(ephdata, normal_sensibility);
            break;
        case higher_sensibility:
            eswin_log(INFO_LOG, "eswin touch is higher sensibility");
            ret_val = eph_sensibility_status_enable(ephdata, higher_sensibility);
            break;
        case highest_sensibility:
            eswin_log(INFO_LOG, "eswin touch is highest sensibility");
            ret_val = eph_sensibility_status_enable(ephdata, highest_sensibility);
            break;
        default:
            eswin_log(ERR_LOG, "Unsupport tp sensibility level %d", enable);
            break;
    }

    return ret_val;
}

static int eswin_touch_get_tp_suspend(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    eswin_log(INFO_LOG, "tp_suspend=%d", cdev->tp_suspend);

    return cdev->tp_suspend;
}

static int eswin_touch_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable) {
    eswin_log(INFO_LOG, "enter, enable=%d", enable);

    if (enable)
        change_tp_state(LCD_OFF);
    else
        change_tp_state(LCD_ON);

    return 0;
}

/* Started by AICoder, pid:09c1by24a4c28ac14a3e089270053d3abd87a477 */
static int eswin_touch_resume_func(void *unused_ephdata) {
    int ret = 0;
    eswin_log(INFO_LOG, "enter");

    if (zte_ephdata == NULL) {
        eswin_log(ERR_LOG, "zte_ephdata is not initialized");
        return -1;
    }

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
/* Started by AICoder, pid:g7ea67b947baabc14124097c809a1e12b0077219 */
    if (ufp_tp_ops.aod_fp_down) {
        eswin_log(INFO_LOG, "tpd:aod finger down!");
        ufp_tp_ops.wait_completion = true;
        if (!wait_for_completion_timeout(&ufp_tp_ops.ufp_completion, msecs_to_jiffies(1000))) {
            eswin_log(INFO_LOG, "tpd:aod finger down timeout!");
        }
    }
    ufp_tp_ops.wait_completion = false;

    zte_ephdata->zte_ctrl.is_single_tap = (zte_ephdata->zte_ctrl.is_single_aod || zte_ephdata->zte_ctrl.is_single_fp) ? 5 : 0;
    zte_ephdata->zte_ctrl.is_one_key = zte_ephdata->zte_ctrl.is_set_onekey_in_suspend;

    eswin_log(INFO_LOG, "zte_ephdata->zte_ctrl.is_single_tap is %d", zte_ephdata->zte_ctrl.is_single_tap);
    eswin_log(INFO_LOG, "zte_ephdata->zte_ctrl.is_single_fp is %d", zte_ephdata->zte_ctrl.is_single_fp);
    eswin_log(INFO_LOG, "zte_ephdata->zte_ctrl.is_one_key is %d", zte_ephdata->zte_ctrl.is_one_key);
/* Ended by AICoder, pid:g7ea67b947baabc14124097c809a1e12b0077219 */
#endif

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
    if (!(zte_ephdata->gesture_wakeup_enable || zte_ephdata->zte_ctrl.is_single_tap)) {
#else
    if (!zte_ephdata->gesture_wakeup_enable) {
#endif
        ret = eph_deep_mode_enable(zte_ephdata, 0);
        eswin_log(INFO_LOG, "eph_deep_mode_enable 0 ret=%d", ret);
    }

    eph_clear_all_host_touch_slots(zte_ephdata);

    ret = eph_trigger_baseline(zte_ephdata);
    eswin_log(INFO_LOG, "eph_trigger_baseline ret=%d", ret);

    usleep_range(500, 500);

    ret = eph_gesture_mode_set(zte_ephdata, 0);
    eswin_log(INFO_LOG, "eph_gesture_mode_set 0 ret=%d", ret);

    ret = eph_report_touch_enable(zte_ephdata, 1);
    eswin_log(INFO_LOG, "eph_report_touch_enable 1 ret=%d", ret);

    usleep_range(500, 500);

    zte_ephdata->suspended = false;

#ifdef ESWIN_ESD_CHECK
    heartbeat_work_start(zte_ephdata);
#endif

    return ret;
}

static int eswin_touch_suspend_func(void *unused_ephdata) {
    int ret = 0;
    eswin_log(INFO_LOG, "enter");

    if (zte_ephdata == NULL) {
        eswin_log(ERR_LOG, "zte_ephdata is not initialized");
        return -1;
    }

#ifdef ESWIN_ESD_CHECK
    heartbeat_work_stop(zte_ephdata);
#endif

    zte_ephdata->suspended = true;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
    eswin_log(INFO_LOG, "zte_ephdata->zte_ctrl.is_single_tap is %d", zte_ephdata->zte_ctrl.is_single_tap);
    eswin_log(INFO_LOG, "zte_ephdata->zte_ctrl.is_single_fp is %d", zte_ephdata->zte_ctrl.is_single_fp);
    eswin_log(INFO_LOG, "zte_ephdata->zte_ctrl.is_one_key is %d", zte_ephdata->zte_ctrl.is_one_key);
    if (zte_ephdata->gesture_wakeup_enable || zte_ephdata->zte_ctrl.is_single_tap) {
#else
    if (zte_ephdata->gesture_wakeup_enable) {
#endif
        ret = eph_gesture_mode_set(zte_ephdata, 7);
        eswin_log(INFO_LOG, "eph_gesture_mode_set 7 ret=%d", ret);
    }

    ret = eph_report_touch_enable(zte_ephdata, 0);
    eswin_log(INFO_LOG, "eph_report_touch_enable 0 ret=%d", ret);

    usleep_range(500, 500);

    eph_clear_all_host_touch_slots(zte_ephdata);

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
    if (!(zte_ephdata->gesture_wakeup_enable || zte_ephdata->zte_ctrl.is_single_tap)) {
#else
    if (!zte_ephdata->gesture_wakeup_enable) {
#endif
        ret = eph_deep_mode_enable(zte_ephdata, 1);
        eswin_log(INFO_LOG, "eph_deep_mode_enable 1 ret=%d", ret);
    }

    return ret;
}
/* Ended by AICoder, pid:09c1by24a4c28ac14a3e089270053d3abd87a477 */

static int eswin_touch_get_wakegesture(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    cdev->b_gesture_enable = ephdata->gesture_wakeup_enable;
    eswin_log(INFO_LOG, "gesture_enable=%d", cdev->b_gesture_enable);

    return 0;
}

static int eswin_touch_enable_wakegesture(struct ztp_device *cdev, int enable) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    if (ephdata->suspended) {
        cdev->tp_suspend_write_gesture = true;
        eswin_log(ERR_LOG, "error, eswin touch in suspend");
    } else {
        eswin_log(INFO_LOG, "eswin touch in resume");
    }
    ephdata->gesture_wakeup_enable = enable;

    eswin_log(INFO_LOG, "gesture_enable=%d", enable);

    return 0;
}

static bool eswin_touch_suspend_need_awake(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return false;
    }

    if (!cdev->tp_suspend_write_gesture && ephdata->gesture_wakeup_enable) {
        eswin_log(INFO_LOG, "eswin touch suspend need awake");
        return true;
    }

    cdev->tp_suspend_write_gesture = false;
    eswin_log(INFO_LOG, "eswin touch suspend dont need awake");
    return false;
}

static int eswin_touch_set_display_rotation(struct ztp_device *cdev, int mrotation) {
    int ret_val = 0;
    struct eph_data *ephdata = (struct eph_data *)cdev->private;
    int display_rotation_old = 0;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    display_rotation_old = ephdata->zte_ctrl.display_rotation;

    cdev->display_rotation = mrotation;
    ephdata->zte_ctrl.display_rotation = cdev->display_rotation;
    if (ephdata->suspended || !(cdev->fw_ready)) {
        eswin_log(ERR_LOG, "error, eswin touch in suspend or fw not ready");
        return -EIO;
    }

    eswin_log(INFO_LOG, "display_rotation=%d", cdev->display_rotation);
    if (ephdata->zte_ctrl.display_rotation == display_rotation_old) {
        eswin_log(INFO_LOG, "ephdata->display_rotation not change");
        return -EIO;
    }

    switch (cdev->display_rotation) {
        case mRotatin_0:
            eswin_log(INFO_LOG, "mRotatin_0");
            ret_val = eph_mRotatin_status_enable(ephdata, mRotatin_0);
            break;
        case mRotatin_90: /* USB on right */
            eswin_log(INFO_LOG, "mRotatin_90");
            ret_val = eph_mRotatin_status_enable(ephdata, mRotatin_90);
            break;
        case mRotatin_180:
            eswin_log(INFO_LOG, "mRotatin_180");
            ret_val = eph_mRotatin_status_enable(ephdata, mRotatin_180);
            break;
        case mRotatin_270: /* USB on left */
            eswin_log(INFO_LOG, "mRotatin_270");
            ret_val = eph_mRotatin_status_enable(ephdata, mRotatin_270);
            break;
        default:
            eswin_log(ERR_LOG, "Unsupport tp tp mRotatin %d", cdev->display_rotation);
            break;
    }

    return ret_val;
}

static int eswin_touch_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len) {
    return 0;
}

static int eswin_touch_get_noise(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    return 0;
}

static int eswin_touch_shutdown(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    eph_regulator_disable(ephdata);
    return 0;
}

int eswin_touch_ex_mode_recovery(struct ztp_device *cdev) {
    int ret_val = 0;
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    if (ephdata->zte_ctrl.charger_state) {
        ret_val = eph_charger_status_enable(ephdata, ephdata->zte_ctrl.charger_state);
    }

    if (ephdata->zte_ctrl.headset_state) {
        ret_val = eph_headset_status_enable(ephdata, ephdata->zte_ctrl.headset_state);
    }

    return ret_val;
}

static int eswin_touch_charger_state_notify(struct ztp_device *cdev) {
    int ret_val = 0;
    struct eph_data *ephdata = (struct eph_data *)cdev->private;
    bool charger_mode_old = false;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    charger_mode_old = ephdata->zte_ctrl.charger_state;

    ephdata->zte_ctrl.charger_state = cdev->charger_mode;
    if (ephdata->suspended || !(cdev->fw_ready)) {
        eswin_log(ERR_LOG, "error, eswin touch in suspend or fw not ready");
        return -EIO;
    }

    eswin_log(INFO_LOG, "charger_state=%d", ephdata->zte_ctrl.charger_state);
    if (ephdata->zte_ctrl.charger_state != charger_mode_old) {
        ret_val = eph_charger_status_enable(ephdata, ephdata->zte_ctrl.charger_state);
    }

    return ret_val;
}

static int eswin_touch_ghost_check_reset(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    return 0;
}

void eswin_touch_enable_irq(bool value) {
    if (value) {
        //enable_irq(zte_ephdata->chg_irq);
    } else {
        //disable_irq(zte_ephdata->chg_irq);
    }
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
/* Started by AICoder, pid:a458afdc3eq8bfa14b8c0a3b1062f202f678995a */
static int eswin_get_singleaodgesture(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    cdev->b_single_aod_enable = ephdata->zte_ctrl.is_single_aod;
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_aod=%d", ephdata->zte_ctrl.is_single_aod);
    eswin_log(INFO_LOG, "cdev->b_single_aod_enable=%d", cdev->b_single_aod_enable);
    return 0;
}
/* Ended by AICoder, pid:a458afdc3eq8bfa14b8c0a3b1062f202f678995a */

/* Started by AICoder, pid:lbe5851339f72991471c0b27006666179ba549cf */
static int eswin_set_singleaodgesture(struct ztp_device *cdev, int enable) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    eswin_log(INFO_LOG, "enable=%d", enable);
    ephdata->zte_ctrl.is_single_aod = enable;
    if (!ephdata->suspended) {
        ephdata->zte_ctrl.is_single_tap = (ephdata->zte_ctrl.is_single_aod || ephdata->zte_ctrl.is_single_fp) ? 5 : 0;
    } else {
        eswin_log(ERR_LOG, "error, change set in suspend!");
    }
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_fp=%d", ephdata->zte_ctrl.is_single_fp);
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_aod=%d", ephdata->zte_ctrl.is_single_aod);
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_tap=%d", ephdata->zte_ctrl.is_single_tap);
    return 0;
}
/* Ended by AICoder, pid:lbe5851339f72991471c0b27006666179ba549cf */

/* Started by AICoder, pid:857dbe95ebge9fe147690b48006e49062858dc7c */
static int eswin_get_singlefpgesture(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    cdev->b_single_tap_enable = ephdata->zte_ctrl.is_single_fp;
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_fp=%d", ephdata->zte_ctrl.is_single_fp);
    eswin_log(INFO_LOG, "cdev->b_single_tap_enable=%d", cdev->b_single_tap_enable);
    return 0;
}
/* Ended by AICoder, pid:857dbe95ebge9fe147690b48006e49062858dc7c */

/* Started by AICoder, pid:zeb602799e50b8c142ee086810476b1212b5e19d */
static int eswin_set_singlefpgesture(struct ztp_device *cdev, int enable) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    eswin_log(INFO_LOG, "enable=%d", enable);
    ephdata->zte_ctrl.is_single_fp = enable;
    if (!ephdata->suspended) {
        ephdata->zte_ctrl.is_single_tap = (ephdata->zte_ctrl.is_single_aod || ephdata->zte_ctrl.is_single_fp) ? 5 : 0;
    } else {
        eswin_log(ERR_LOG, "error, change set in suspend!");
    }
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_fp=%d", ephdata->zte_ctrl.is_single_fp);
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_aod=%d", ephdata->zte_ctrl.is_single_aod);
    eswin_log(INFO_LOG, "ephdata->zte_ctrl.is_single_tap=%d", ephdata->zte_ctrl.is_single_tap);
    return 0;
}
/* Ended by AICoder, pid:zeb602799e50b8c142ee086810476b1212b5e19d */

/* Started by AICoder, pid:a767elb210qcc1714a260827c0dc441cde0802ae */
static int eswin_set_one_key(struct ztp_device *cdev, int enable) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    eswin_log(INFO_LOG, "enable=%d", enable);
    ephdata->zte_ctrl.is_set_onekey_in_suspend = enable;
    if (!ephdata->suspended) {
        ephdata->zte_ctrl.is_one_key = enable;
    } else {
        eswin_log(ERR_LOG, "error, change set in suspend!");
    }
    return 0;
}

static int eswin_get_one_key(struct ztp_device *cdev) {
    struct eph_data *ephdata = (struct eph_data *)cdev->private;

    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return -EIO;
    }

    cdev->one_key_enable = ephdata->zte_ctrl.is_one_key;
    return 0;
}
/* Ended by AICoder, pid:a767elb210qcc1714a260827c0dc441cde0802ae */
#endif

void eswin_tpd_register_fw_class(struct eph_data *ephdata) {
    if (!ephdata) {
        eswin_log(ERR_LOG, "error, ephdata is NULL");
        return;
    }

    eswin_log(DEBUG_LOG, "enter");

    //get_eswin_touch_module_info_from_lcd();
    tpd_cdev->private = (void *)ephdata;
    tpd_cdev->get_tpinfo = eswin_touch_init_tpinfo;

    tpd_cdev->tp_self_test = eswin_touch_test_cmd_store;
    tpd_cdev->get_tp_self_test_result = eswin_touch_test_cmd_show;
    tpd_cdev->tp_bbat_test = eswin_touch_bbat_test;

    tpd_cdev->headset_state_show = eswin_touch_get_headset_state;
    tpd_cdev->set_headset_state = eswin_touch_set_headset_state;

    tpd_cdev->get_sensibility = eswin_touch_get_sensibility;
    tpd_cdev->set_sensibility = eswin_touch_set_sensibility;

    tpd_cdev->tp_suspend_show = eswin_touch_get_tp_suspend;
    tpd_cdev->set_tp_suspend = eswin_touch_set_tp_suspend;

    tpd_cdev->tp_data = ephdata;

    tpd_cdev->tp_resume_func = eswin_touch_resume_func;
    tpd_cdev->tp_suspend_func = eswin_touch_suspend_func;

    tpd_cdev->get_gesture = eswin_touch_get_wakegesture;
    tpd_cdev->wake_gesture = eswin_touch_enable_wakegesture;

    tpd_cdev->tpd_suspend_need_awake = eswin_touch_suspend_need_awake;

    tpd_cdev->set_display_rotation = eswin_touch_set_display_rotation;

    tpd_cdev->tp_fw_upgrade = eswin_touch_fw_upgrade;
    tpd_cdev->get_noise = eswin_touch_get_noise;
    tpd_cdev->tpd_shutdown = eswin_touch_shutdown;
    tpd_cdev->tpd_send_cmd = eswin_touch_ex_mode_recovery;

    tpd_cdev->charger_state_notify = eswin_touch_charger_state_notify;
    tpd_cdev->ghost_check_reset = eswin_touch_ghost_check_reset;
    queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));

    rst_gpio = ephdata->ephplatform->gpio_reset;
    eswin_log(INFO_LOG, "rst_gpio=%d", rst_gpio);

    tpd_cdev->tpd_enable_irq = eswin_touch_enable_irq;
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
/* Started by AICoder, pid:vab0289461e5a3714ea80979a0b14802f6f73aa8 */
    tpd_cdev->get_singleaod = eswin_get_singleaodgesture;
    tpd_cdev->set_singleaod = eswin_set_singleaodgesture;
    tpd_cdev->get_singletap = eswin_get_singlefpgesture;
    tpd_cdev->set_singletap = eswin_set_singlefpgesture;
    tpd_cdev->set_one_key = eswin_set_one_key;
    tpd_cdev->get_one_key = eswin_get_one_key;
/* Ended by AICoder, pid:vab0289461e5a3714ea80979a0b14802f6f73aa8 */
#endif

#ifdef ESWIN_TOUCH_SUSPEND_AFTER_LCD_CMD_OFF_END
    tpd_cdev->tp_suspend_after_lcd_cmd_off_end = true;
#endif
    tpd_cdev->max_x = ephdata->ephplatform->panel_max_x;
    tpd_cdev->max_y = ephdata->ephplatform->panel_max_y;
    ephdata->zte_ctrl.charger_state = false;
    ephdata->zte_ctrl.headset_state = false;
    ephdata->zte_ctrl.display_rotation = 0;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    zlog_tp_dev.device_name = eswin_touch_vendor_name;
    zlog_tp_dev.ic_name = "eswin";
    TPD_ZLOG("device_name:%s, ic_name: %s", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
    eswin_log(DEBUG_LOG, "exit");
}
/* Ended by AICoder, pid:z1bbdl9b7drcb461469a0ab5f4e60f597b2441b3 */