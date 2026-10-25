/************************************************************************
*
* File Name: gcore_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/
#include "gcore_drv_common.h"
#include <linux/gpio.h>
#define MAX_FILE_NAME_LEN       64
#define MAX_FILE_PATH_LEN  64
#define MAX_NAME_LEN_20  20

char gcore_vendor_name[MAX_NAME_LEN_20] = { 0 };
char gcore_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gcore_mp_firmware_name[MAX_FILE_NAME_LEN] = {0};
char gcore_mp_test_ini_path[MAX_FILE_NAME_LEN] = {0};
#ifdef GCORE_DEFAULT_FIRMWARE
char gcore_default_firmware_name[MAX_FILE_NAME_LEN] = {0};
#endif
int gcore_vendor_id = 0;
int gcore_tptest_result = 0;
struct ts_firmware *gcore_adb_upgrade_firmware = NULL;
unsigned long time_after_fw_upgrade;
extern void gcore_suspend(void);
extern void gcore_resume(void);
struct tpvendor_t gcore_vendor_l[] = {
	{GTP_VENDOR_ID_0, GTP_VENDOR_0_NAME},
	{GTP_VENDOR_ID_1, GTP_VENDOR_1_NAME},
	{GTP_VENDOR_ID_2, GTP_VENDOR_2_NAME},
	{GTP_VENDOR_ID_3, GTP_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};
#ifdef CONFIG_TOUCHSCREEN_KNUCKLE
bool enable_knuckle;
#endif

/* Started by AICoder, pid:k53d8973d0l00d81421808a690c39b3ae208e0b5 */
int gcore_get_fw(void)
{
    int i = 0;
    int ret = -EIO;

    for (i = 0; i < ARRAY_SIZE(gcore_vendor_l); i++) {
        if (strnstr(lcd_name, gcore_vendor_l[i].vendor_name, strlen(lcd_name))) {
            gcore_vendor_id = gcore_vendor_l[i].vendor_id;
            strlcpy(gcore_vendor_name, gcore_vendor_l[i].vendor_name, sizeof(gcore_vendor_name));
            ret = 0;
            break;
        }
    }

    if (ret == -EIO) {
        strlcpy(gcore_vendor_name, "Unknown", sizeof(gcore_vendor_name));
    }

    snprintf(gcore_firmware_name, sizeof(gcore_firmware_name),
             "gcore_firmware_%s.bin", gcore_vendor_name);
    GTP_INFO("gcore_firmware_name:%s", gcore_firmware_name);

    snprintf(gcore_mp_firmware_name, sizeof(gcore_mp_firmware_name),
             "gcore_mp_firmware_%s.bin", gcore_vendor_name);
    GTP_INFO("gcore_mp_firmware_name:%s", gcore_mp_firmware_name);

    snprintf(gcore_mp_test_ini_path, sizeof(gcore_mp_test_ini_path),
             "gcore_mp_test_%s.ini", gcore_vendor_name);
    GTP_INFO("gcore_mp_test_ini_path:%s", gcore_mp_test_ini_path);

#ifdef GCORE_DEFAULT_FIRMWARE
    snprintf(gcore_default_firmware_name, sizeof(gcore_default_firmware_name),
             "%s_%s.bin", GCORE_DEFAULT_FIRMWARE, gcore_vendor_name);
    GTP_INFO("gcore_default_firmware_name:%s", gcore_default_firmware_name);
#endif

    return ret;
}
/* Ended by AICoder, pid:k53d8973d0l00d81421808a690c39b3ae208e0b5 */

int gcore_tp_requeset_firmware(void)
{
	struct ztp_device *cdev = tpd_cdev;


	if (cdev->tp_firmware == NULL || !cdev->tp_firmware->size) {
		GTP_ERROR("cdev->tp_firmware is NULL");
		goto err_free_firmware;
	}

	if (gcore_adb_upgrade_firmware)
		goto copy_firmware_data;
	gcore_adb_upgrade_firmware = kzalloc(sizeof(struct ts_firmware), GFP_KERNEL);
	if (gcore_adb_upgrade_firmware == NULL) {
		GTP_ERROR("Request firmware alloc ts_firmware failed");
		return -ENOMEM;
	}

	gcore_adb_upgrade_firmware->size = cdev->tp_firmware->size;
	gcore_adb_upgrade_firmware->data = vmalloc(gcore_adb_upgrade_firmware->size);
	if (gcore_adb_upgrade_firmware->data == NULL) {
		GTP_ERROR("Request form file alloc firmware data failed");
		goto err_free_firmware;
	}
copy_firmware_data:
	memcpy(gcore_adb_upgrade_firmware->data, (u8 *)cdev->tp_firmware->data, gcore_adb_upgrade_firmware->size);
	return 0;
err_free_firmware:
	kfree(gcore_adb_upgrade_firmware);
	gcore_adb_upgrade_firmware = NULL;
	return -ENOMEM;
}

static int gcore_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int ret = 0;

	if (gcore_tp_requeset_firmware() < 0) {
		GTP_ERROR("Get firmware from adb upgrade failed");
		goto error_fw_upgrade;
	}
	gcore_request_firmware_update_work(NULL);
	return ret;
error_fw_upgrade:
	return -EIO;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	u8 read_data[4] = { 0 };

	gcore_read_fw_version_retry(read_data, sizeof(read_data));
	strlcpy(cdev->ic_tpinfo.tp_name, "gcore_ts", sizeof(cdev->ic_tpinfo.tp_name));
	strlcpy(cdev->ic_tpinfo.vendor_name, gcore_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_GCORE;
	cdev->ic_tpinfo.firmware_ver = read_data[1] << 24 | read_data[0] << 16 | read_data[3] << 8 | read_data[2];
	cdev->ic_tpinfo.module_id = gcore_vendor_id;
	cdev->ic_tpinfo.i2c_type = 1;
	cdev->ic_tpinfo.i2c_addr = 0x26;
	return 0;
}

static int gcore_tp_suspend_show(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	cdev->tp_suspend = gdev->tp_suspend;
	return cdev->tp_suspend;
}

static int gcore_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable) {
		change_tp_state(LCD_OFF);
	} else {
		change_tp_state(LCD_ON);
	}
	return 0;
}

#ifdef GTP_GET_NOISE
static int gcore_data_request(s16 *frame_data_words, enum tp_test_type  test_type)
{
	struct gcore_dev *gdev = fn_data.gdev;
	int index = 0, ret = 0, i = 0;
	int total_size = 0;
	unsigned int x = 0;
	unsigned int y = 0;
	unsigned int col = 0;
	unsigned int row = 0;

	row = RAWDATA_ROW;
	col = RAWDATA_COLUMN;

	total_size = (RAWDATA_ROW * RAWDATA_COLUMN) * 2;
	if (gdev->noise_buffer == NULL) {
		ret = -ENOMEM;
		goto end;
	}
	memset(gdev->noise_buffer, 0, total_size);
	switch (test_type) {
	case RAWDATA_TEST:
		ret = gcore_fw_read_rawdata(gdev->noise_buffer, total_size);
		break;
	case DELTA_TEST:
		ret = gcore_fw_read_diffdata(gdev->noise_buffer, total_size);
		break;
	default:
		GTP_ERROR("the Para is error!");
		ret = -1;
		goto end;
	}
	if (ret >= 0) {
		for (i = 0, index = 0; index < total_size/2; i += 2, index++) {
			frame_data_words[index] = ((gdev->noise_buffer[i + 1] << 8) | gdev->noise_buffer[i]);
		}

		for (y = 0; y < row; y++) {
			pr_cont("GTP[%2d]", (y + 1));
			for (x = 0; x < col; x++) {
				pr_cont("%5d,", frame_data_words[y * col + x]);
			}
			pr_cont("\n");
		}
	}
end:
	return ret;
}

static int  gcore_testing_delta_raw_report(struct ztp_device *cdev, u8 num_of_reports)
{

	s16 *frame_data_words = NULL;
	unsigned int col = 0;
	unsigned int row = 0;
	unsigned int idx = 0;
	int retval = 0;
	int len = 0;
	int i = 0;

	row = RAWDATA_ROW;
	col = RAWDATA_COLUMN;
	GTP_INFO("get tp delta raw data start!");
	frame_data_words = kcalloc((row * col), sizeof(s16), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		GTP_ERROR("Failed to allocate frame_data_words mem");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", idx, row, col);
		retval = gcore_data_request(frame_data_words, RAWDATA_TEST);
		if (retval < 0) {
			GTP_ERROR("data_request failed!");
			goto DATA_REQUEST_FAILED;
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"RawData:\n");
		for (i = 0; i < row * col; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,", frame_data_words[i]);
			if ((i + 1) % col == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
		retval = gcore_data_request(frame_data_words, DELTA_TEST);
		if (retval < 0) {
			GTP_ERROR("data_request failed!");
			goto DATA_REQUEST_FAILED;
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"DiffData:\n");
		for (i = 0; i < row * col; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,", frame_data_words[i]);
			if ((i + 1) % col == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
	}
	retval = 0;

DATA_REQUEST_FAILED:
	gcore_fw_mode_set_proc2(0);
	msleep(20);
	GTP_INFO("get tp delta raw data end!");
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
	return retval;
}

static int gcore_tpd_get_noise(struct ztp_device *cdev)
{
	int retval = 0;
	struct gcore_dev *gdev = fn_data.gdev;
	int total_size = 0;

	if (gdev->tp_suspend)
		return -EIO;

	gdev->ts_stat = TS_MPTEST;
	if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
		GTP_ERROR(" alloc tp firmware data fail");
		retval = -ENOMEM;
		goto BUFFER_ALLOC_FAILED;
	}
	total_size = (RAWDATA_ROW * RAWDATA_COLUMN) * 2;
	if (gdev->noise_buffer == NULL) {
		gdev->noise_buffer = kzalloc(total_size, GFP_KERNEL);
		if (gdev->noise_buffer == NULL) {
			GTP_ERROR(" alloc gdev->noise_buffer fail");
			retval = -ENOMEM;
			goto BUFFER_ALLOC_FAILED;
		}
	}
	retval = gcore_testing_delta_raw_report(cdev, 5);
	if (retval < 0) {
		GTP_ERROR("get_raw_noise failed!");
	}

	kfree(gdev->noise_buffer);
	gdev->noise_buffer = NULL;
	gdev->last_event = FW_EVENT_MAX;

BUFFER_ALLOC_FAILED:
	if (gdev->tp_suspend)
		gdev->ts_stat = TS_SUSPEND;
	else
		gdev->ts_stat = TS_NORMAL;

	return retval;
}
#endif

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int ret = 0;
	int retry = 0;

	GTP_INFO("enter tp selftest");
	do {
		gcore_tptest_result = 0;
		ret = gcore_start_mp_test();
		if (ret) {
			retry++;
			GTP_ERROR("rawdata test failed, retry:%d", retry);
			msleep(20);
		} else {
			break;
		}
	} while (retry < 3);
	if (retry == 3) {
		GTP_ERROR("selftest failed!");
	} else {
		GTP_INFO("selftest success!");
	}
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	GTP_INFO("enter");
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", gcore_tptest_result, RAWDATA_COLUMN, RAWDATA_ROW, 0);
	GTP_INFO("tpd  test:%s.", buf);

	num_read_chars = i_len;
	return num_read_chars;
}

static void gcore_delay_fw_event_notify(enum fw_event_type event)
{
	unsigned long fw_upgrade_end_time;
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend || (time_after_fw_upgrade == 0)) {
		GTP_INFO("save event return");
		gcore_modify_fw_event_cmd(event);
		return;
	}
	fw_upgrade_end_time = jiffies_to_msecs(jiffies - time_after_fw_upgrade);
	GTP_INFO("fw_upgrade_end_time: %d", fw_upgrade_end_time);
	if (fw_upgrade_end_time < 300) {
		gcore_modify_fw_event_cmd(event);
		mod_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->send_cmd_work, msecs_to_jiffies(300 - fw_upgrade_end_time));
	} else {
		gcore_fw_event_notify(event);
	}
}

static int gcore_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	struct gcore_dev *gdev = fn_data.gdev;

	gdev->display_rotation = mrotation;
	GTP_INFO("display_rotation = %d.", gdev->display_rotation);
	switch (gdev->display_rotation) {
	case mRotatin_0:
		gcore_delay_fw_event_notify(FW_EDGE_0);
		break;
	case mRotatin_90:
		gcore_delay_fw_event_notify(FW_EDGE_90);
		break;
	case mRotatin_180:
		gcore_delay_fw_event_notify(FW_EDGE_0);
		break;
	case mRotatin_270:
		gcore_delay_fw_event_notify(FW_EDGE_270);
		break;
	default:
		break;
	};

	return gdev->display_rotation;
}


static int gcore_headset_state_show(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	cdev->headset_state = gdev->headset_mode;
	return cdev->headset_state;
}

static int gcore_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct gcore_dev *gdev = fn_data.gdev;

	gdev->headset_mode = enable;
	GTP_INFO("headset_state = %d.", gdev->headset_mode);
	if (gdev->headset_mode)
		gcore_delay_fw_event_notify(FW_HEADSET_PLUG);
	else
		gcore_delay_fw_event_notify(FW_HEADSET_UNPLUG);
	return gdev->headset_mode;
}

int gcore_ex_mode_recovery(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->charger_mode) {
		gcore_modify_fw_event_cmd(FW_CHARGER_PLUG);
	} else {
		gcore_modify_fw_event_cmd(FW_CHARGER_UNPLUG);
	}
	if (gdev->headset_mode) {
		gcore_modify_fw_event_cmd(FW_HEADSET_PLUG);
	} else {
		gcore_modify_fw_event_cmd(FW_HEADSET_UNPLUG);
	}
	gcore_fw_event_resume();
	return 0;
}

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	cdev->b_gesture_enable = gdev->gesture_wakeup_en;
	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend) {
		cdev->tp_suspend_write_gesture = true;
		tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
	}
	gdev->gesture_wakeup_en = enable;
	return enable;
}
#endif

static bool tpd_suspend_need_awake(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (!cdev->tp_suspend_write_gesture && gdev->gesture_wakeup_en) {
		GTP_INFO("gesture enable, tp suspend need awake.");
		return true;
	} else if (gdev->tp_fw_update) {
		GTP_INFO("tp_fw updating, tp suspend need awake.");
		tpd_cdev->tp_resume_before_lcd_cmd = false;
		return true;
	}
#ifdef HUB_TP_PS_ENABLE
	else if (tpd_cdev->tpd_proximity_enable && !tpd_cdev->tpd_proximity_state) {
		GTP_INFO("tp proximity enable, tp suspend need awake.");
		return true;
	}
#endif
	tpd_cdev->tp_resume_before_lcd_cmd = true;
	GTP_INFO("tp suspend dont need awake.");
	return false;
}

int gcore_ts_resume(void *data)
{
	gcore_resume();
	return 0;
}

int gcore_ts_suspend(void *data)
{
	gcore_suspend();
	return 0;
}

static int gcore_charger_state_notify(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;
	bool charger_mode_old = gdev->charger_mode;

	gdev->charger_mode = cdev->charger_mode;
	if (gdev->charger_mode != charger_mode_old) {
		if (gdev->charger_mode) {
			GTP_INFO("charger in");
			gcore_delay_fw_event_notify(FW_CHARGER_PLUG);
		} else {
			GTP_INFO("charger out");
			gcore_delay_fw_event_notify(FW_CHARGER_UNPLUG);
		}

	}
	return 0;
}

static int tpd_gtp_shutdown(struct ztp_device *cdev)
{
	struct gcore_dev *gdev = fn_data.gdev;

	gcore_suspend();
	gpio_direction_output(gdev->rst_gpio, 0);
	return 0;
}

static int gcore_bbat_test(struct ztp_device *cdev)
{
	int ret = 0;

/*tp int test*/
	cdev->bbat_test_enter = true;
	cdev->bbat_int_test = false;
	cdev->bbat_test_result = 0;
	reinit_completion(&cdev->bbat_test_completion);
	gcore_delay_fw_event_notify(FW_EDGE_0);
	if (cdev->bbat_int_test == false) {
		ret = wait_for_completion_timeout(&cdev->bbat_test_completion, msecs_to_jiffies(700));
		if (!ret) {
			GTP_INFO("tp int test fail");
			cdev->bbat_test_result = TP_INT_BBAT_TEST_FAIL;
		}
	}
	cdev->bbat_test_enter = false;
	return cdev->bbat_test_result;
}

static int gcore_ghost_check_reset(struct ztp_device *cdev)
{
	int ret = 0;

#ifdef HUB_TP_PS_ENABLE
	if(cdev->tpd_proximity_enable) {
		GTP_INFO("proximity enable, dont need gcore_sys_soft_reset");
		return ret;
	}
#endif
	GTP_INFO("gcore_sys_soft_reset");
	/* gcore_sys_soft_reset(); */
	gcore_cpu_remap();

	GTP_INFO("gcore_ex_mode_recovery");
	gcore_ex_mode_recovery(cdev);

	return ret;
}

int gcore_register_fw_class(void)
{
	gcore_get_fw();
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->tp_fw_upgrade = gcore_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = gcore_tp_suspend_show;
	tpd_cdev->set_tp_suspend = gcore_set_tp_suspend;
	tpd_cdev->headset_state_show = gcore_headset_state_show;
	tpd_cdev->set_headset_state = gcore_set_headset_state;
	tpd_cdev->set_display_rotation = gcore_set_display_rotation;
#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;
#endif
	tpd_cdev->tpd_send_cmd = gcore_ex_mode_recovery;
	tpd_cdev->charger_state_notify = gcore_charger_state_notify;
	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));
	tpd_cdev->tp_data = fn_data.gdev;
	tpd_cdev->tp_resume_func = gcore_ts_resume;
	tpd_cdev->tp_suspend_func = gcore_ts_suspend;
	tpd_cdev->tpd_suspend_need_awake = tpd_suspend_need_awake;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->tpd_shutdown = tpd_gtp_shutdown;
	tpd_cdev->max_x = TOUCH_SCREEN_X_MAX;
	tpd_cdev->max_y = TOUCH_SCREEN_Y_MAX;
	tpd_cdev->input = fn_data.gdev->input_device;
	tpd_cdev->tp_resume_before_lcd_cmd = true;
#ifdef GTP_GET_NOISE
	tpd_cdev->get_noise = gcore_tpd_get_noise;
	fn_data.gdev->noise_buffer = NULL;
#endif
	tpd_cdev->tp_bbat_test = gcore_bbat_test;
	tpd_cdev->ghost_check_reset = gcore_ghost_check_reset;
	fn_data.gdev->gesture_wakeup_en = false;
	fn_data.gdev->tp_fw_update = false;
	fn_data.gdev->tp_self_test = false;
	fn_data.gdev->charger_mode = false;
	fn_data.gdev->headset_mode = false;
	fn_data.gdev->gesture_irq_en = false;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = gcore_vendor_name;
	zlog_tp_dev.ic_name = "gcore_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}

