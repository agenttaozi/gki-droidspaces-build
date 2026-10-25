#include "nt36xxx.h"

extern uint8_t bTouchIsAwake;

bool zte_test_flag = false;
bool zte_get_noise_flag = false;
int zte_output_offset;
bool adb_upgrade_fw_flag = false;

#define MAX_NAME_LEN_50  50
char novatek_vendor_name[MAX_NAME_LEN_50] = { 0 };
char novatek_firmware_name[MAX_NAME_LEN_50] = { 0 };
char novatek_mp_firmware_name[MAX_NAME_LEN_50] = { 0 };
char novatek_mp_csv_name[MAX_NAME_LEN_50] = { 0 };

struct tpvendor_t novatek_vendor_l[] = {
	{NOVATEK_VENDOR_ID_0, NOVATEK_VENDOR_NAME_0},
	{NOVATEK_VENDOR_ID_1, NOVATEK_VENDOR_NAME_1},
	{NOVATEK_VENDOR_ID_2, NOVATEK_VENDOR_NAME_2},
	{NOVATEK_VENDOR_ID_3, NOVATEK_VENDOR_NAME_3},
	{VENDOR_END, "Unknown"},
};

enum ovt_tcm_sensibility_level {
	MIN_SENSI = 0,
	NORMAL_SENSI = 1,
	HIGER_SENSI = 2,
	HIGEST_SENSI = 3,
	MAX_SENSI = 1000,
};

void novatek_get_module_info_from_lcd(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(novatek_vendor_l); i++) {
		NVT_LOG("%d ---> %s", (i + 1), novatek_vendor_l[i].vendor_name);
		if (strnstr(lcd_name, novatek_vendor_l[i].vendor_name, strlen(lcd_name))) {
			strlcpy(novatek_vendor_name, novatek_vendor_l[i].vendor_name, sizeof(novatek_vendor_name));
			goto out;
		}
	}
	strlcpy(novatek_vendor_name, NOVATEK_VENDOR_NAME_0, sizeof(novatek_vendor_name));

out:
	NVT_LOG("novatek vendor name: %s", novatek_vendor_name);

	snprintf(novatek_firmware_name, sizeof(novatek_firmware_name),
                "novatek_ts_fw_%s.bin", novatek_vendor_name);
	NVT_LOG("novatek firmware name: %s", novatek_firmware_name);

	snprintf(novatek_mp_firmware_name, sizeof(novatek_mp_firmware_name),
                "novatek_ts_mp_%s.bin", novatek_vendor_name);
	NVT_LOG("novatek mp firmware name: %s", novatek_mp_firmware_name);

	snprintf(novatek_mp_csv_name, sizeof(novatek_mp_csv_name),
                "novatek_ts_mp_%s.csv", novatek_vendor_name);
	NVT_LOG("novatek mp csv name: %s", novatek_mp_csv_name);
}

static int novatek_init_tpinfo(struct ztp_device *cdev)
{
	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	snprintf(cdev->ic_tpinfo.vendor_name, sizeof(cdev->ic_tpinfo.vendor_name), novatek_vendor_name);
	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "novatek");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_NOVATEK;
	cdev->ic_tpinfo.firmware_ver = ts->fw_ver;
	cdev->ic_tpinfo.spi_num = SPI_NUM;

	NVT_LOG("tp_name: %s, vendor_name: %s, firmware_ver = %d",
		cdev->ic_tpinfo.tp_name, cdev->ic_tpinfo.vendor_name, cdev->ic_tpinfo.firmware_ver);

	return 0;
}

static int novatek_get_tp_suspend(struct ztp_device *cdev)
{
	cdev->tp_suspend = !bTouchIsAwake;

	NVT_LOG("tp_suspend = %d", cdev->tp_suspend);

	return cdev->tp_suspend;
}

static int novatek_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	NVT_LOG("enter, enable = %d", enable);

	if (enable)
		change_tp_state(LCD_OFF);
	else
		change_tp_state(LCD_ON);

	return 0;
}

static int novatek_resume_func(void *unused_ts)
{
	NVT_LOG("enter");

	if (ts) {
		return nvt_ts_resume(&ts->client->dev);
	} else {
		NVT_ERR("not implement");
		return 0;
	}
}

static int novatek_suspend_func(void *unused_ts)
{
	NVT_LOG("enter");

	if (ts) {
		return nvt_ts_suspend(&ts->client->dev);
	} else {
		NVT_ERR("not implement");
		return 0;
	}
}

void novatek_enable_irq(bool value)
{
	NVT_LOG("enter, value = %d", value);
	nvt_irq_enable(value);
}

static int novatek_shutdown(struct ztp_device *cdev)
{
	return 0;
}

#if NVT_TOUCH_SUPPORT_HW_RST
static void novatek_reset_gpio_output(bool value)
{
	NVT_LOG("enter, value = %d", value);

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	gpio_direction_output(ts->reset_gpio, value);
}
#endif

#if NVT_TOUCH_EXT_PROC
static int novatek_charger_state_notify(struct ztp_device *cdev)
{
	bool charger_mode_old = false;
	int ret = 0;

	NVT_LOG("enter, charger_mode = %d", cdev->charger_mode);

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	charger_mode_old = ts->ac_power_plug_state;
	ts->ac_power_plug_state = cdev->charger_mode;
	if (ts->ac_power_plug_state == charger_mode_old) {
		NVT_ERR("ts->ac_power_plug_state not change, return");
		return ret;
	}

	ret = nvt_ac_power_plug_set(ts->ac_power_plug_state);
	if (ret) {
		NVT_ERR("Set power %s failed, ret = %d", ts->ac_power_plug_state? "plug" : "unplug", ret);
	} else {
		NVT_LOG("Set power %s success", ts->ac_power_plug_state? "plug" : "unplug");
	}

	return ret;
}

static int novatek_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = 0;
	int display_rotation_old = 0;

	NVT_LOG("enter, mrotation = %d", mrotation);

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	display_rotation_old = ts->edge_reject_state;
	ts->edge_reject_state = mrotation;
	if (ts->edge_reject_state == display_rotation_old) {
		NVT_ERR("ts->edge_reject_state not change, return");
		return ret;
	}

	ret = nvt_edge_reject_set(ts->edge_reject_state);
	if (ret) {
		NVT_ERR("Set display rotation failed");
	} else {
		NVT_LOG("Set display rotation success");
	}

	cdev->display_rotation = ts->edge_reject_state;

	return ret;
}

#ifdef NVT_SUPPORT_HEADSET_STATE
static int novatek_get_headset_state(struct ztp_device *cdev)
{
	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	cdev->headset_state = ts->headset_line_reject_state;

	NVT_LOG("headset_state = %d", cdev->headset_state);

	return cdev->headset_state;
}

static int novatek_set_headset_state(struct ztp_device *cdev, int enable)
{
	int ret = 0;
	int headset_mode_old = 0;

	NVT_LOG("enter, headset_state = %d", enable);

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	headset_mode_old = ts->headset_line_reject_state;
	ts->headset_line_reject_state = enable;
	if (ts->headset_line_reject_state == headset_mode_old) {
		NVT_ERR("ts->headset_line_reject_state not change, return");
		return ret;
	}

	ret = nvt_headset_line_reject_set(ts->headset_line_reject_state);
	if (ret) {
		NVT_ERR("Set headset %s failed, ret = %d", ts->headset_line_reject_state? "plug" : "unplug", ret);
	} else {
		NVT_LOG("Set headset %s success", ts->headset_line_reject_state? "plug" : "unplug");
	}

	return ret;
}
#endif

static int novatek_bbat_test(struct ztp_device *cdev)
{
	int ret = 0;

	NVT_LOG("enter");

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	cdev->bbat_test_enter = true;
	cdev->bbat_test_result = 0;

	/*set int to high*/
	ret = nvt_int_ctrl_set(1);
	if (ret) {
		NVT_ERR("nvt_int_ctrl_set 1 failed");
		cdev->bbat_test_result = cdev->bbat_test_result | TP_INT_BBAT_TEST_FAIL;
	} else {
		NVT_LOG("nvt_int_ctrl_set 1 success");
	}

	msleep(10);

	/*set int to low*/
	ret = nvt_int_ctrl_set(0);
	if (ret) {
		NVT_ERR("nvt_int_ctrl_set 0 failed");
		cdev->bbat_test_result = cdev->bbat_test_result | TP_INT_BBAT_TEST_FAIL;
	} else {
		NVT_LOG("nvt_int_ctrl_set 0 success");
	}

	cdev->bbat_test_enter = false;

	NVT_LOG("exit");

	return cdev->bbat_test_result;
}

static int novatek_ex_mode_recovery(struct ztp_device *cdev)
{
	int ret = 0;

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (cdev->charger_mode) {
		ret = nvt_ac_power_plug_set(cdev->charger_mode);
		if (ret) {
			NVT_ERR("set power plug failed, ret = %d", ret);
		} else {
			NVT_LOG("set power plug success");
		}
	}
#ifdef NVT_SUPPORT_HEADSET_STATE
	if (cdev->headset_state) {
		ret = nvt_headset_line_reject_set(cdev->headset_state);
		if (ret) {
			NVT_ERR("set headset plug failed, ret = %d", ret);
		} else {
			NVT_LOG("set headset plug success");
		}
	}
#endif
#ifdef NVT_SUPPORT_PEN_ONLY_MODE
	if (cdev->pen_only_mode) {
		ret = nvt_ext_cmd_store(0x7B, 0x04, cdev->pen_only_mode);
		if (ret) {
			NVT_ERR("set pen only enable mode failed, ret = %d", ret);
		} else {
			NVT_LOG("set pen only enable mode success");
		}
	}
#endif
	ret = nvt_edge_reject_set(ts->edge_reject_state);
	if (ret) {
		NVT_ERR("set edge_reject value %d failed, ret = %d", ts->edge_reject_state, ret);
	} else {
		NVT_LOG("set edge_reject value %d success", ts->edge_reject_state);
	}
	return ret;
}

static int novatek_get_noise(struct ztp_device *cdev)
{
	int ret = 0, i;

	NVT_LOG("enter");

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (tp_alloc_tp_firmware_data(30 * RT_DATA_LEN)) {
		NVT_ERR("error, alloc tp noise data memmory fail, return");
		return -ENOMEM;
	}

	zte_get_noise_flag = true;

	zte_output_offset = 0;

	zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
		RT_DATA_LEN * 30 - zte_output_offset, "fw_ver=0x%02x, x_num=%d, y_num=%d, max_button_num=%d, PID=0x%04x\n\n",
		ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num, ts->nvt_pid);

	for (i = 0 ; i < 1 ; i++) {
		zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
			RT_DATA_LEN * 30 - zte_output_offset, "RAW_DATA[%d]:\n", (i+1));
		ret = nvt_do_raw_open();
		if (ret) {
			NVT_ERR("nvt_do_raw_open failed");
			tpd_zlog_record_notify(TP_GET_NOISE_ERROR_NO);
		} else {
			NVT_LOG("nvt_do_raw_open pass");
		}

		zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
			RT_DATA_LEN * 30 - zte_output_offset, "DIFF_DATA[%d]:\n", (i+1));
		ret = nvt_do_diff_open();
		if (ret) {
			NVT_ERR("nvt_do_diff_open failed");
			tpd_zlog_record_notify(TP_GET_NOISE_ERROR_NO);
		} else {
			NVT_LOG("nvt_do_diff_open pass");
		}
	}

	zte_get_noise_flag = false;

	return ret;
}

#ifdef NVT_SUPPORT_PEN_ONLY_MODE
static int nvt_get_pen_only_mode(struct ztp_device *cdev)
{
	NVT_LOG("pen only mode is %d", cdev->pen_only_mode);
	return cdev->pen_only_mode;
}

static int nvt_set_pen_only_mode(struct ztp_device *cdev, u8 pen_only_mode)
{
	int ret = 0;

	NVT_LOG("enter, pen_only_mode = %d", pen_only_mode);

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	cdev->pen_only_mode = pen_only_mode;

	ret = nvt_ext_cmd_store(0x7B, 0x04, cdev->pen_only_mode);
	if (ret) {
		NVT_ERR("set pen only %s mode failed, ret = %d", cdev->pen_only_mode? "enable" : "disable", ret);
	} else {
		NVT_LOG("set pen only %s mode success", cdev->pen_only_mode? "enable" : "disable");
	}

	NVT_LOG("exit");

	return ret;
}
#endif
#endif

static int novatek_get_wakegesture(struct ztp_device *cdev)
{
	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	cdev->b_gesture_enable = cdev->ztp_ctl.is_wakeup_gesture;

	NVT_LOG("gesture_enable = %d", cdev->b_gesture_enable);

	return 0;
}

static int novatek_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	NVT_LOG("enter, gesture_enable = %d", enable);

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (!bTouchIsAwake) {
		NVT_ERR("error, novatek in suspend");
		cdev->tp_suspend_write_gesture = true;
		tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
	} else {
		NVT_LOG("novatek in resume");
		ts->gesture_enabled = enable;
	}

	cdev->ztp_ctl.is_wakeup_gesture = enable;
	return 0;
}

static bool novatek_suspend_need_awake(struct ztp_device *cdev)
{
	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return false;
	}

	if (!cdev->tp_suspend_write_gesture && ts->gesture_enabled) {
		NVT_LOG("novatek suspend need awake");
		return true;
	}

	cdev->tp_suspend_write_gesture = false;
	NVT_LOG("novatek suspend dont need awake");

	return false;
}

#if BOOT_UPDATE_FIRMWARE
struct firmware *adb_fw;
static int novatek_adb_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int ret = 0;

	NVT_LOG("enter");

	if (cdev->tp_firmware == NULL || cdev->tp_firmware->data == NULL) {
		NVT_ERR("error, cdev->tp_firmware is NULL, return");
		return -EIO;
	}

	if (adb_fw == NULL) {
		adb_fw = kzalloc(sizeof(struct firmware), GFP_KERNEL);
		if (adb_fw == NULL) {
			NVT_ERR("Request firmware alloc failed");
			return -EIO;
		}
	}

	if (adb_fw->data != NULL) {
		vfree(adb_fw->data);
	}

	adb_fw->size = cdev->tp_firmware->size;
	adb_fw->data = (u8 *)vmalloc(cdev->tp_firmware->size);
	if (adb_fw->data == NULL) {
		NVT_ERR("alloc firmware data failed");
		kfree(adb_fw);
		adb_fw = NULL;
		return -EIO;
	}
	memcpy((u8 *)adb_fw->data, cdev->tp_firmware->data, cdev->tp_firmware->size);

	adb_upgrade_fw_flag = true;
	ret = nvt_update_firmware(NULL);
	if (ret) {
		NVT_ERR("novatek adb fw upgrade failed");
	} else {
		NVT_LOG("novatek adb fw upgrade pass");
	}

	return ret;
}
#endif

#if NVT_TOUCH_MP
static int novatek_test_cmd_store(struct ztp_device *cdev)
{
	int ret = 0, retry = 0;

	NVT_LOG("novatek selftest start");

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	zte_test_flag = true;

	do {
		cdev->zte_tp_selftest_result = 0;
		ret = nvt_do_selftest();
		if (ret || cdev->zte_tp_selftest_result) {
			retry++;
		} else {
			break;
		}
	} while (retry < 3);

	zte_test_flag = false;

	if (cdev->zte_tp_selftest_result) {
		NVT_ERR("novatek selftest failed");
		tpd_zlog_record_notify(TP_SELF_TEST_ERROR_NO);
	} else {
		NVT_LOG("novatek selftest pass");
	}

	NVT_LOG("novatek selftest end");

	return ret;
}

static int novatek_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	if (!bTouchIsAwake || !cdev->fw_ready) {
		NVT_ERR("error, novatek in suspend or fw not ready, return");
		return -EIO;
	}

	NVT_LOG("rawdata_col: %d, rawdata_row: %d", ts->x_num, ts->y_num);

    num_read_chars = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", cdev->zte_tp_selftest_result,
        ts->x_num, ts->y_num, 0);
	NVT_LOG("novatek test result: %s", buf);

	return num_read_chars;
}
#endif

static int novatek_ghost_check_reset(struct ztp_device *cdev)
{
	int ret = 0;

	NVT_LOG("enter");

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}
	if (!cdev->pen_only_mode) {
		ret = nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	} else {
		NVT_LOG("pen only mode, do nothing");
	}
	mutex_unlock(&ts->lock);

	NVT_LOG("exit");

	return ret;
}

#ifdef NVT_GET_HALL_STATE
static int novatek_set_hall_state(struct ztp_device *cdev, int enable)
{
	uint8_t buf[4] = {0};

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return -EIO;
	}

	NVT_LOG("enter, enable = %d", enable);

	if (enable) {
		if ((!bTouchIsAwake) && (ts->gesture_enabled)) {
			NVT_LOG("already screen off and open gesture, hall switch is on, need to enter idle");
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = 0x11;
			CTP_SPI_WRITE(ts->client, buf, 2);
		}
	} else {
		/* To recovery */
	}

	NVT_LOG("end");

	return enable;
}
#endif

void novatek_register_fw_class(void)
{
	NVT_LOG("enter");

	if (!ts) {
		NVT_ERR("error, novatek is NULL, return");
		return;
	}

	novatek_get_module_info_from_lcd();
	tpd_cdev->private = (void *)ts;

	tpd_cdev->get_tpinfo = novatek_init_tpinfo;

	tpd_cdev->tp_suspend_show = novatek_get_tp_suspend;
	tpd_cdev->set_tp_suspend = novatek_set_tp_suspend;

	tpd_cdev->tp_data = ts;
	tpd_cdev->tp_resume_func = novatek_resume_func;
	tpd_cdev->tp_suspend_func = novatek_suspend_func;

	tpd_cdev->tpd_enable_irq = novatek_enable_irq;

	tpd_cdev->tpd_shutdown = novatek_shutdown;

#if NVT_TOUCH_SUPPORT_HW_RST
	tpd_cdev->tp_reset_gpio_output = novatek_reset_gpio_output;
#endif

#ifdef NOVATEK_SUSPEND_AFTER_LCD_CMD_OFF_END
	tpd_cdev->tp_suspend_after_lcd_cmd_off_end = true;
#endif

#if NVT_TOUCH_EXT_PROC
	tpd_cdev->charger_state_notify = novatek_charger_state_notify;

	tpd_cdev->set_display_rotation = novatek_set_display_rotation;
#ifdef NVT_SUPPORT_HEADSET_STATE
	tpd_cdev->headset_state_show = novatek_get_headset_state;
	tpd_cdev->set_headset_state = novatek_set_headset_state;
#endif
	tpd_cdev->tp_bbat_test = novatek_bbat_test;

	tpd_cdev->tpd_send_cmd = novatek_ex_mode_recovery;

	tpd_cdev->get_noise = novatek_get_noise;
#ifdef NVT_SUPPORT_PEN_ONLY_MODE
	tpd_cdev->get_pen_only_mode = nvt_get_pen_only_mode;
	tpd_cdev->set_pen_only_mode = nvt_set_pen_only_mode;
#endif
#endif
	tpd_cdev->get_gesture = novatek_get_wakegesture;
	tpd_cdev->wake_gesture = novatek_enable_wakegesture;
	tpd_cdev->tpd_suspend_need_awake = novatek_suspend_need_awake;
#if BOOT_UPDATE_FIRMWARE
	tpd_cdev->tp_fw_upgrade = novatek_adb_fw_upgrade;
#endif
#if NVT_TOUCH_MP
	tpd_cdev->tp_self_test = novatek_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = novatek_test_cmd_show;
#endif
	tpd_cdev->ghost_check_reset = novatek_ghost_check_reset;

#ifdef NVT_GET_HALL_STATE
	tpd_cdev->set_hall_state = novatek_set_hall_state;
#endif

	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));

	tpd_cdev->max_x = TOUCH_MAX_WIDTH;
	tpd_cdev->max_y = TOUCH_MAX_HEIGHT;
	tpd_cdev->input = ts->input_dev;
	tpd_cdev->TP_have_registered = true;
	tpd_cdev->tp_resume_before_lcd_cmd = true;

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = novatek_vendor_name;
	zlog_tp_dev.ic_name = "novatek";
	TPD_ZLOG("device_name: %s, ic_name: %s", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif

	NVT_LOG("exit");
}
