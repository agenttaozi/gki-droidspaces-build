#include "goodix_ts_core.h"

static atomic_t ato_ver = ATOMIC_INIT(0);

extern int goodix_ts_suspend(struct goodix_ts_core *core_data);
extern int goodix_ts_resume(struct goodix_ts_core *core_data);
extern int goodix_ts_power_off(struct goodix_ts_core *cd);
extern void goodix_ts_esd_off(struct goodix_ts_core *cd);
extern int gtp_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len);
#ifdef GTP_GET_NOISE
extern int get_raw_data(struct goodix_ts_core *cd, u8 type);
#endif
/*gtp test func*/
extern int goodix_do_inspect(struct goodix_ts_core *cd);

#define MAX_NAME_LEN_50  50
char gtp_vendor_name[MAX_NAME_LEN_50] = { 0 };
char gtp_cfg_firmware_name[MAX_NAME_LEN_50] = { 0 };
char gtp_firmware_name[MAX_NAME_LEN_50] = { 0 };

struct tpvendor_t goodix_vendor_l[] = {
    {GTP_VENDOR_ID_0, GTP_VENDOR_0_NAME},
    {GTP_VENDOR_ID_1, GTP_VENDOR_1_NAME},
    {GTP_VENDOR_ID_2, GTP_VENDOR_2_NAME},
    {GTP_VENDOR_ID_3, GTP_VENDOR_3_NAME},
    {VENDOR_END, "Unknown"},
};

int gtp_get_vendor_and_firmware(void)
{
    int i = 0;
    int ret = 0;

    for (i = 0; i < ARRAY_SIZE(goodix_vendor_l); i++) {
        ts_info(NULL, "%d--->%s", i, goodix_vendor_l[i].vendor_name);
        if (strnstr(lcd_name, goodix_vendor_l[i].vendor_name, strlen(lcd_name))) {
            strlcpy(gtp_vendor_name, goodix_vendor_l[i].vendor_name, sizeof(gtp_vendor_name));
            ret = 0;
            goto out;
        }
    }
    ret = -1;
    strlcpy(gtp_vendor_name, GTP_VENDOR_0_NAME, sizeof(gtp_vendor_name));

out:
    ts_info(NULL, "%s:GTP vendor name:%s", __func__, gtp_vendor_name);
    snprintf(gtp_firmware_name, sizeof(gtp_firmware_name),
            "goodix_firmware_%s.bin", gtp_vendor_name);
    ts_info(NULL, "%s:GTP firmware name:%s", __func__, gtp_firmware_name);
    snprintf(gtp_cfg_firmware_name, sizeof(gtp_cfg_firmware_name),
            "goodix_cfg_group_%s.bin", gtp_vendor_name);
    ts_info(NULL, "%s:GTP cfg firmware name:%s", __func__, gtp_cfg_firmware_name);
    return ret;
}

static int gtp_init_tpinfo(struct ztp_device *cdev)
{
	int firmware;
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
	struct goodix_fw_version chip_ver;
	int r = 0;
	char *cfg_buf;

	if (!core_data) {
		ts_err(NULL, "%s:error, core_data is NULL", __func__);
		return -EIO;
	}

	if (atomic_read(&core_data->suspended)) {
		ts_err(NULL, "%s:error, gtp in suspend", __func__);
		return -EIO;
	}

	if (atomic_cmpxchg(&ato_ver, 0, 1)) {
		ts_err(NULL, "%s:busy, wait", __func__);
		return -EIO;
	}

	cfg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	ts_info(NULL, "%s:enter", __func__);

	if (core_data->hw_ops->read_version) {
		r = core_data->hw_ops->read_version(core_data, &chip_ver);
		if (!r) {
			snprintf(cdev->ic_tpinfo.tp_name, MAX_VENDOR_NAME_LEN, "GT%s",
				chip_ver.patch_pid);

			firmware = (unsigned int)chip_ver.patch_vid[3] +
					((unsigned int)chip_ver.patch_vid[2] << 8) +
					((unsigned int)chip_ver.patch_vid[1] << 16) +
					((unsigned int)chip_ver.patch_vid[0] << 24);
			cdev->ic_tpinfo.firmware_ver = firmware;
			cdev->ic_tpinfo.chip_model_id = TS_CHIP_GOODIX;
			cdev->ic_tpinfo.module_id = chip_ver.sensor_id;
			cdev->ic_tpinfo.i2c_addr = GOODIX_TP_I2C_ADDR;
			strlcpy(cdev->ic_tpinfo.vendor_name, gtp_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
		}
	} else {
		ts_err(NULL, "%s:read_version failed", __func__);
		goto exit;
	}

	if (core_data->hw_ops->read_config) {
		r = core_data->hw_ops->read_config(core_data, cfg_buf, PAGE_SIZE);
		if (r <= 0)
			goto exit;
		cdev->ic_tpinfo.config_ver = cfg_buf[34];
	}

	ts_info(NULL, "%s:end", __func__);

exit:
	kfree(cfg_buf);
	atomic_cmpxchg(&ato_ver, 1, 0);

	return r;
}

static int gtp_ts_resume(void *_core_data)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)_core_data;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s", __func__);

    return goodix_ts_resume(core_data);
}

static int gtp_ts_suspend(void *_core_data)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)_core_data;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s", __func__);

    return goodix_ts_suspend(core_data);
}

static int gtp_tp_suspend_show(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    cdev->tp_suspend = atomic_read(&core_data->suspended);
    return cdev->tp_suspend;
}

static int gtp_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
    if (enable)
        change_tp_state(LCD_OFF);
    else
        change_tp_state(LCD_ON);
    return 0;
}

static int gtp_get_wakegesture(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    cdev->b_gesture_enable = core_data->ztec.is_wakeup_gesture;
    ts_info(NULL, "%s: gesture_enable %d", __func__, cdev->b_gesture_enable);

    return 0;
}

static int gtp_set_wakegesture(struct ztp_device *cdev, int enable)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s: gesture_enable %d", __func__, enable);

    if (atomic_read(&core_data->suspended)) {
        ts_err(NULL, "%s: error, gtp in suspend", __func__);
        cdev->tp_suspend_write_gesture = true;
        tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
    } else {
        ts_info(NULL, "%s: gtp in resume", __func__);
        if (enable) {
            core_data->gesture_type |= GESTURE_DOUBLE_TAP;
        } else {
            core_data->gesture_type &= ~GESTURE_DOUBLE_TAP;
        }
    }
    core_data->ztec.is_wakeup_gesture = enable;

    return 0;
}

/*static bool gtp_suspend_need_awake(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    if (!cdev->tp_suspend_write_gesture && core_data->ztec.is_wakeup_gesture) {
        ts_info(NULL, "%s: gtp suspend need awake", __func__);
        return true;
    }

    cdev->tp_suspend_write_gesture = false;
    ts_info(NULL, "%s: gtp suspend dont need awake", __func__);
    return false;
}*/

static int gtp_get_sensibility(struct ztp_device *cdev)
{
    int retval = 0;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    cdev->sensibility_enable = core_data->ztec.is_sensibility;

    return retval;
}

static int gtp_set_sensibility(struct ztp_device *cdev, u8 enable)
{
    int retval = 0;
    static u8 sensibility_level = 1;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    if (sensibility_level == enable)  {
        ts_info(NULL, "%s: same sensibility level, return", __func__);
        return 0;
    }

    sensibility_level = enable;
    core_data->ztec.is_sensibility = enable;
    ts_info(NULL, "%s: sensibility level %d", __func__, enable);

    if (atomic_read(&core_data->suspended)) {
        ts_err(NULL, "%s: error, gtp in suspend", __func__);
    } else {
        if (enable == 3) {
            ts_info(NULL, "%s: highest sensibility level", __func__);
        } else if (enable == 2) {
            ts_info(NULL, "%s: higher sensibility level", __func__);
        } else if (enable == 1) {
            ts_info(NULL, "%s: normal sensibility level", __func__);
        }
    }

    return retval;
}

static int gtp_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
    int ret = 0;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    if (atomic_read(&core_data->suspended)) {
        ts_err(NULL, "%s: error, gtp in suspend", __func__);
        return 0;
    }

    cdev->display_rotation = mrotation;
    ts_info(NULL, "%s: display_rotation %d", __func__, cdev->display_rotation);

    switch (cdev->display_rotation) {
        case mRotatin_0:
            break;
        case mRotatin_90: /*USB on right*/
            break;
        case mRotatin_180:
            break;
        case mRotatin_270: /*USB on left*/
            break;
    }

    if (ret) {
        ts_err(NULL, "%s: Write display rotation failed", __func__);
    } else {
        ts_info(NULL, "%s: Write display rotation success", __func__);
    }

    return cdev->display_rotation;
}

#ifdef GOODIX_USB_DETECT_GLOBAL
static int gtp_charger_state_notify(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;
    bool charger_mode_old = false;
    int ret = 0;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    charger_mode_old = core_data->ztec.charger_state;

    core_data->ztec.charger_state = cdev->charger_mode;

    if (atomic_read(&core_data->suspended) || !cdev->fw_ready) {
        ts_err(NULL, "%s: error, gtp in suspend or fw not ready", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s: charger_state %d", __func__, core_data->ztec.charger_state);
    if (core_data->ztec.charger_state != charger_mode_old) {
        if (core_data->ztec.charger_state) {
            ts_info(NULL, "%s: enter_charger, ret=%d", __func__, ret);
        } else {
            ts_info(NULL, "%s: leave_charger, ret=%d", __func__, ret);
        }
    }

    return 0;
}
#endif

static int gtp_get_headset_state(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    cdev->headset_state = core_data->ztec.headset_state;

    ts_info(NULL, "%s: headset_state=%d", __func__, cdev->headset_state);

    return cdev->headset_state;
}

static int gtp_set_headset_state(struct ztp_device *cdev, int enable)
{
    int ret = 0;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    if (atomic_read(&core_data->suspended) || !cdev->fw_ready) {
        ts_err(NULL, "%s: error, gtp in suspend or fw not ready", __func__);
        return -EIO;
    }

    core_data->ztec.headset_state = enable;
    ts_info(NULL, "%s: headset_state=%d", __func__, core_data->ztec.headset_state);

    if (core_data->ztec.headset_state) {
        ts_info(NULL, "%s: enter_headset, ret=%d", __func__, ret);
    } else {
        ts_info(NULL, "%s: leave_headset, ret=%d", __func__, ret);
    }

    return core_data->ztec.headset_state;
}

static int gtp_bbat_test(struct ztp_device *cdev) {
    return 0;
}

#ifdef GTP_GET_NOISE
int zte_output_offset;
static int gtp_get_noise(struct ztp_device *cdev)
{
    int ret = 0;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    if (atomic_read(&core_data->suspended) || !cdev->fw_ready) {
        ts_err(NULL, "%s: error, gtp in suspend or fw not ready", __func__);
        return -EIO;
    }

    if (tp_alloc_tp_firmware_data(30 * RT_DATA_LEN)) {
        ts_err(NULL, "%s:alloc tp noise data memmory fail", __func__);
        return -ENOMEM;
    }

    zte_output_offset = 0;

    ts_info(NULL, "%s:get_rawdata", __func__);
    ret = get_raw_data(core_data, RAWDATA_TEST);
    if (ret) {
        ts_err(NULL, "%s:get rawdata fail", __func__);
        tpd_zlog_record_notify(TP_GET_NOISE_ERROR_NO);
    }

    ts_info(NULL, "%s:get_diffdata", __func__);
    ret = get_raw_data(core_data, DELTA_TEST);
    if (ret) {
        ts_err(NULL, "%s:get diffdata fail", __func__);
        tpd_zlog_record_notify(TP_GET_NOISE_ERROR_NO);
    }

    return ret;
}
#endif

static int gtp_test_cmd_store(struct ztp_device *cdev)
{
    int ret = 0, retry = 0;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    if (atomic_read(&core_data->suspended) || !cdev->fw_ready) {
        ts_err(NULL, "%s: error, gtp in suspend or fw not ready", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s:gtp selftest start", __func__);

    do {
        cdev->zte_tp_selftest_result = 0;
        ret = goodix_do_inspect(core_data);
        if (ret || cdev->zte_tp_selftest_result) {
            core_data->hw_ops->reset(core_data, GOODIX_NORMAL_RESET_DELAY_MS);
            retry++;
        } else {
            break;
        }
    } while (retry < 3);

    if (cdev->zte_tp_selftest_result) {
        ts_err(NULL, "%s:gtp selftest failed", __func__);
        tpd_zlog_record_notify(TP_SELF_TEST_ERROR_NO);
    } else {
        ts_info(NULL, "%s:gtp selftest pass", __func__);
    }

    ts_info(NULL, "%s:gtp selftest end", __func__);

    return ret;
}

static int gtp_test_cmd_show(struct ztp_device *cdev, char *buf)
{
    ssize_t num_read_chars = 0;
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s:RAWDATA_COL:%d", __func__, core_data->ic_info.parm.drv_num);
    ts_info(NULL, "%s:RAWDATA_ROW:%d", __func__, core_data->ic_info.parm.sen_num);
    num_read_chars = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", cdev->zte_tp_selftest_result,
        core_data->ic_info.parm.drv_num, core_data->ic_info.parm.sen_num, 0);
    ts_info(NULL, "%s:gtp test buf:%s", __func__, buf);

    return num_read_chars;
}

/*static int gtp_shutdown(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s: disable irq", __func__);
    core_data->hw_ops->irq_enable(core_data, false);

    if (atomic_read(&core_data->ts_esd.esd_on))
        goodix_ts_esd_off(core_data);

    goodix_ts_power_off(core_data);
    gpio_direction_output(core_data->board_data->reset_gpio, 0);

    return 0;
}*/

static int gtp_ghost_check_reset(struct ztp_device *cdev)
{
    struct goodix_ts_core *core_data = (struct goodix_ts_core *)cdev->private;

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return -EIO;
    }

    ts_info(NULL, "%s", __func__);
    core_data->hw_ops->reset(core_data, GOODIX_NORMAL_RESET_DELAY_MS);

    return 0;
}

void goodix_register_fw_class(struct goodix_ts_core *core_data)
{
    ts_info(NULL, "%s: entry", __func__);

    if (!core_data) {
        ts_err(NULL, "%s: error, core_data is NULL", __func__);
        return;
    }

    tpd_cdev->private = (void *)core_data;
    tpd_cdev->get_tpinfo = gtp_init_tpinfo;

    tpd_cdev->tp_data = core_data;
    tpd_cdev->tp_resume_func = gtp_ts_resume;
    tpd_cdev->tp_suspend_func = gtp_ts_suspend;

    tpd_cdev->tp_suspend_show = gtp_tp_suspend_show;
    tpd_cdev->set_tp_suspend = gtp_set_tp_suspend;

    tpd_cdev->get_gesture = gtp_get_wakegesture;
    tpd_cdev->wake_gesture = gtp_set_wakegesture;
    /*tpd_cdev->tpd_suspend_need_awake = gtp_suspend_need_awake;*/

    tpd_cdev->get_sensibility = gtp_get_sensibility;
    tpd_cdev->set_sensibility = gtp_set_sensibility;

    tpd_cdev->set_display_rotation = gtp_set_display_rotation;

#ifdef GOODIX_USB_DETECT_GLOBAL
    tpd_cdev->charger_state_notify = gtp_charger_state_notify;
    queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));
#endif

    tpd_cdev->headset_state_show = gtp_get_headset_state;
    tpd_cdev->set_headset_state = gtp_set_headset_state;

    tpd_cdev->tp_bbat_test = gtp_bbat_test;
#ifdef GTP_GET_NOISE
    tpd_cdev->get_noise = gtp_get_noise;
#endif
    tpd_cdev->tp_self_test = gtp_test_cmd_store;
    tpd_cdev->get_tp_self_test_result = gtp_test_cmd_show;

    //tpd_cdev->tpd_shutdown = gtp_shutdown;

    tpd_cdev->ghost_check_reset = gtp_ghost_check_reset;

    tpd_cdev->tp_fw_upgrade = gtp_tp_fw_upgrade;

    tpd_cdev->max_x = core_data->board_data->panel_max_x;
    tpd_cdev->max_y = core_data->board_data->panel_max_y;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    zlog_tp_dev.device_name = gtp_vendor_name;
    zlog_tp_dev.ic_name = "goodix_tp";
    TPD_ZLOG("device_name:%s, ic_name:%s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif

    ts_info(NULL, "%s: end", __func__);
}
