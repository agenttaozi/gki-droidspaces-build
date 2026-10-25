/************************************************************************
*
* File Name: fts_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/

#include "focaltech_core.h"
#include "focaltech_test.h"
#include <linux/kernel.h>
#include <linux/power_supply.h>
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

char g_fts_ini_filename[MAX_INI_FILE_NAME_LEN] = {0};
char fts_vendor_name[20] = { 0 };

extern struct fts_ts_data *fts_data;
extern struct fts_test *fts_ftest;
extern int fts_ts_suspend(struct device *dev);
extern int fts_ts_resume(struct device *dev);
extern int fts_test_init_basicinfo(struct fts_test *tdata);
extern int fts_test_entry(char *ini_file_name);
extern int fts_ex_mode_switch(enum _ex_mode mode, u8 value);


struct tpvendor_t fts_vendor_info[] = {
	{FTS_MODULE_ID, FTS_MODULE_NAME },
	{FTS_MODULE2_ID, FTS_MODULE2_NAME },
	{FTS_MODULE3_ID, FTS_MODULE3_NAME },
	{VENDOR_END, "Unknown"},
};

int get_fts_module_info_from_lcd(void)
{
	int i = 0;

	for (i = 0 ; i < (ARRAY_SIZE(fts_vendor_info) - 1) ; i++) {
		FTS_DEBUG("%d--->%s", i, fts_vendor_info[i].vendor_name);
		if (strnstr(lcd_name, fts_vendor_info[i].vendor_name, strlen(lcd_name))) {
			strlcpy(fts_vendor_name, fts_vendor_info[i].vendor_name, sizeof(fts_vendor_name));
			FTS_DEBUG("get_lcd_panel_name find");
			goto out;
		}
	}

	strlcpy(fts_vendor_name, fts_vendor_info[0].vendor_name, sizeof(fts_vendor_name));

out:
	FTS_DEBUG("fts_vendor_name:%s", fts_vendor_name);
	snprintf(g_fts_ini_filename, sizeof(g_fts_ini_filename), "fts_test_sensor_%s.ini",
			fts_vendor_name);
	FTS_DEBUG("fts_test_file:%s", g_fts_ini_filename);

	return i;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	u8 fwver_in_chip = 0;
	u8 vendorid_in_chip = 0;
	u8 chipid_in_chip = 0;
	u8 lcdver_in_chip = 0;
	u8 moduleid_in_chip = 0;
	u8 retry = 0;

	if (fts_data->suspended) {
		FTS_ERROR("fts tp in suspned");
		return -EIO;
	}

	while (retry++ < 5) {
		fts_read_reg(FTS_REG_CHIP_ID, &chipid_in_chip);
		fts_read_reg(FTS_REG_VENDOR_ID, &vendorid_in_chip);
		fts_read_reg(FTS_REG_FW_VER, &fwver_in_chip);
		fts_read_reg(FTS_REG_LIC_VER, &lcdver_in_chip);
		fts_read_reg(FTS_REG_PANEL_ID, &moduleid_in_chip);
		if ((chipid_in_chip != 0) && (vendorid_in_chip != 0) && (fwver_in_chip != 0)) {
			FTS_DEBUG("chip_id = %x,vendor_id =%x,fw_version=%x,lcd_version=%x",
				  chipid_in_chip, vendorid_in_chip, fwver_in_chip, lcdver_in_chip);
			FTS_DEBUG("module_id = %x", moduleid_in_chip);
			break;
		}
		FTS_DEBUG("chip_id = %x,vendor_id =%x,fw_version=%x, lcd_version=%x",
			  chipid_in_chip, vendorid_in_chip, fwver_in_chip, lcdver_in_chip);
		msleep(20);
	}

	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Focal");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_FOCAL;
	strlcpy(cdev->ic_tpinfo.vendor_name, fts_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	cdev->ic_tpinfo.chip_part_id = chipid_in_chip;
	cdev->ic_tpinfo.module_id = vendorid_in_chip;
	cdev->ic_tpinfo.chip_ver = 0;
	cdev->ic_tpinfo.firmware_ver = fwver_in_chip;
	cdev->ic_tpinfo.display_ver = lcdver_in_chip;
	cdev->ic_tpinfo.i2c_type = 0;
	cdev->ic_tpinfo.i2c_addr = 0x38;

	return 0;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	cdev->b_gesture_enable = cdev->ztp_ctl.is_wakeup_gesture;
	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	if (ts_data->suspended) {
		FTS_INFO("In suspend");
		cdev->tp_suspend_write_gesture = true;
		tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
	} else {
		ts_data->gesture_support = enable;
	}
	cdev->ztp_ctl.is_wakeup_gesture = enable;
	return enable;
}

static bool fts_suspend_need_awake(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	if (!ts_data->ic_info.is_incell)
		return false;

	if (ts_data->fw_loading || ts_data->gesture_support) {
		FTS_INFO("tp suspend need awake");
		return true;
	}
#if FTS_PSENSOR_EN
	else if (!ts_data->tpd_proximity_detect_is_far) {
		FTS_INFO("tp suspend need awake");
		return true;
	}
#endif
	else {
#if FTS_PSENSOR_EN
		ts_data->tpd_proximity_flag = 0;
#endif
		FTS_INFO("tp suspend dont need awake");
		return false;
	}
}

static int fts_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev = ts_data->input_dev;

	mutex_lock(&input_dev->mutex);
	fts_upgrade_bin(NULL, 0);
	mutex_unlock(&input_dev->mutex);

	return 0;
}

int fts_tp_suspend(void *fts_data)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_suspend(ts_data->dev);
	return 0;
}

int fts_tp_resume(void *fts_data)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_resume(ts_data->dev);
	ts_data->gesture_support = tpd_cdev->ztp_ctl.is_wakeup_gesture;
	return 0;
}

static int fts_tp_suspend_show(struct ztp_device *cdev)
{
	cdev->tp_suspend = fts_data->suspended;
	return cdev->tp_suspend;
}

static int fts_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable)
		change_tp_state(LCD_OFF);
	else
		change_tp_state(LCD_ON);
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_INFO("enter");
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", cdev->zte_tp_selftest_result, tdata->node.tx_num,
			tdata->node.rx_num, 0);
	num_read_chars = i_len;
	return num_read_chars;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int ret = 0, retry = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}

	input_dev = ts_data->input_dev;

	mutex_lock(&input_dev->mutex);
	fts_irq_disable();

#if FTS_ESDCHECK_EN
	fts_esdcheck_switch(ts_data, DISABLE);
#endif

	do {
		cdev->zte_tp_selftest_result = 0;
		ret = fts_enter_test_environment(1);
		if (ret < 0) {
			FTS_ERROR("enter test environment fail");
		} else {
			fts_test_entry(g_fts_ini_filename);
		}
		ret = fts_enter_test_environment(0);
		if (ret < 0) {
			FTS_ERROR("enter normal environment fail");
		}
		if (cdev->zte_tp_selftest_result) {
			retry++;
			FTS_ERROR("TP self test fail, retry:%d", retry);
			fts_reset_proc(ts_data, false, 200);
		} else {
			FTS_INFO("TP self test success");
			break;
		}
	} while (retry < 3);

	if (cdev->zte_tp_selftest_result) {
		FTS_ERROR("TP self test fail");
		tpd_zlog_record_notify(TP_SELF_TEST_ERROR_NO);
	}

#if FTS_ESDCHECK_EN
	 fts_esdcheck_switch(ts_data, ENABLE);
#endif

	fts_irq_enable();
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int fts_headset_state_show(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->headset_state = ts_data->earphone_mode;
	return cdev->headset_state;
}

static int fts_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;

	ts_data->earphone_mode = enable;
	FTS_INFO("headset_state = %d", ts_data->earphone_mode);
	if (!ts_data->suspended) {
		fts_ex_mode_switch(MODE_EARPHONE, ts_data->earphone_mode);
	}
	return ts_data->earphone_mode;
}

static int fts_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;

	cdev->display_rotation = mrotation;
	if (ts_data->suspended)
		return 0;
	FTS_INFO("display_rotation = %d", cdev->display_rotation);
	switch (cdev->display_rotation) {
		case mRotatin_0:
			ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 0);
			if (ret < 0) {
				FTS_ERROR("write display_rotation fail");
			}
			break;
		case mRotatin_90:
			ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 2);
			if (ret < 0) {
				FTS_ERROR("write display_rotation fail");
			}
			break;
		case mRotatin_180:
			ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 0);
			if (ret < 0) {
				FTS_ERROR("write display_rotation fail");
			}
			break;
		case mRotatin_270:
			ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 1);
			if (ret < 0) {
				FTS_ERROR("write display_rotation fail");
			}
			break;
		default:
			break;
	}
	return cdev->display_rotation;
}

static int tpd_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	struct fts_ts_data *ts_data = fts_data;

	if (ts_data->sensibility_level  == enable)  {
		FTS_INFO("same sensibility level,return");
		return 0;
	}

	ts_data->sensibility_level = enable;
	cdev->sensibility_level = enable;
	if (ts_data->suspended)
		return 0;
	FTS_INFO("sensibility_level = %d", cdev->sensibility_level);
	retval = fts_write_reg(FTS_REG_SENSIBILITY_MODE_EN, ts_data->sensibility_level - 1);
	if (retval < 0) {
		FTS_ERROR("write sensibility_level fail");
	}
	return retval;
}

static int fts_get_rst_level(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret = 0;

	if (!ts_data) {
		FTS_ERROR("Bad param, return");
		return -EINVAL;
	}

	FTS_INFO("reset_gpio = %d", ts_data->pdata->reset_gpio);
	if (ts_data->pdata->reset_gpio) {
		ret = gpio_get_value(ts_data->pdata->reset_gpio);
	} else {
		ret = -EINVAL;
	}

	cdev->tp_rst_level = ret;
	return ret;
}

static int fts_set_rst_level(struct ztp_device *cdev, int rst_level)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret = 0;

	if (!ts_data) {
		FTS_ERROR("Bad param, return");
		return -EINVAL;
	}

	FTS_INFO("reset_gpio = %d", ts_data->pdata->reset_gpio);
	if (ts_data->pdata->reset_gpio) {
		FTS_INFO("rst new level = %d", rst_level);
		gpio_set_value(ts_data->pdata->reset_gpio, rst_level);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int fts_charger_state_notify(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;
	bool charger_mode_old = ts_data->charger_mode;

	ts_data->charger_mode = cdev->charger_mode;

	if (!ts_data->suspended && (ts_data->charger_mode != charger_mode_old)) {
		FTS_INFO("write charger mode:%d", ts_data->charger_mode);
		fts_ex_mode_switch(MODE_CHARGER, ts_data->charger_mode);
	}
	return 0;
}

/*static int tpd_fts_shutdown(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_suspend(ts_data->dev);
	return 0;
}*/

static int fts_tp_get_diffdata(int *data, int byte_num)
{
	int ret = 0;
	u8 old_mode = 0;
	u8 val = 0;
	u8 addr = 0;
	u8 rawdata_addr = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_TEST_FUNC_ENTER();
	ret = fts_test_read_reg(FACTORY_REG_DATA_SELECT, &old_mode);
	if (ret < 0) {
		FTS_TEST_ERROR("read reg06 fail");
		goto test_err;
	}
	ret =  fts_test_write_reg(FACTORY_REG_DATA_SELECT, 0x01);
	if (ret < 0) {
		FTS_TEST_ERROR("write 1 to reg06 fail");
		goto restore_reg;
	}
	/* tart Scanning */
	ret = start_scan();
	if (ret < 0) {
		FTS_TEST_ERROR("Failed to Scan ...");
		return ret;
	}
	/* read rawdata */
	if (tdata->func->hwtype == IC_HW_INCELL) {
		val = 0xAD;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR;
	} else if (tdata->func->hwtype == IC_HW_MC_SC) {
		val = 0xAA;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
	} else {
		val = 0x0;
		addr = FACTORY_REG_RAWDATA_SADDR_SC;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_SC;
	}
	/* read diffdata */
	ret = read_rawdata(tdata, addr, val, rawdata_addr, byte_num, data);
	if (ret < 0) {
		FTS_TEST_ERROR("read diffdata fail");
		goto restore_reg;
	}
restore_reg:
	ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, old_mode);
	if (ret < 0) {
		FTS_TEST_ERROR("restore reg06 fail");
	}
test_err:
	FTS_TEST_FUNC_EXIT();
	return ret;
}

static int fts_tp_get_rawdata(int *data, int byte_num) {
	int ret = 0;
	u8 val = 0;
	u8 addr = 0;
	u8 rawdata_addr = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_TEST_FUNC_ENTER();
	/* tart Scanning */
	ret = start_scan();
	if (ret < 0) {
		FTS_TEST_ERROR("Failed to Scan ...");
		return ret;
	}
	/* read rawdata */
	if (tdata->func->hwtype == IC_HW_INCELL) {
		val = 0xAD;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR;
	} else if (tdata->func->hwtype == IC_HW_MC_SC) {
		val = 0xAA;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
	} else {
		val = 0x0;
		addr = FACTORY_REG_RAWDATA_SADDR_SC;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_SC;
	}
	/* read rawdata */
	ret = read_rawdata(tdata, addr, val, rawdata_addr, byte_num, data);
	if (ret < 0) {
		FTS_TEST_ERROR("read rawdata failed");
		return ret;
	}
	return ret;
}
static int fts_data_request(struct ztp_device *cdev, int *frame_data_words,
	enum tp_test_type  test_type, int byte_num)
{
	int  ret = 0;

	switch (test_type) {
	case RAWDATA_TEST:
		ret = fts_tp_get_rawdata(frame_data_words, byte_num);
		if (ret) {
			FTS_ERROR("Get raw data failed %d", ret);
		}
		break;
	case DELTA_TEST:
		ret = fts_tp_get_diffdata(frame_data_words, byte_num);
		if (ret) {
			FTS_ERROR("Get diff data failed %d", ret);
		}
		break;
	default:
		FTS_ERROR("the Para is error!");
		ret = -1;
	}

	return ret;
}

static int fts_testing_delta_raw_report(struct ztp_device *cdev, u8 num_of_reports)
{
	int *frame_data_words = NULL;
	int retval = 0;
	int len = 0;
	int i = 0;
	u8 tx_num = 0;
	u8 rx_num = 0;
	u8 col = 0;
	u8 row = 0;
	u8 idx = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev;

	if (ts_data->suspended) {
        FTS_INFO("In suspend, no proc, return now");
        return -EINVAL;
    }

	input_dev = ts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    fts_irq_disable();

#if defined(FTS_ESDCHECK_EN) && (FTS_ESDCHECK_EN)
    fts_esdcheck_switch(ts_data, DISABLE);
#endif

	retval = fts_write_reg(0xEE,1);/* disable Auto Clb */
	if (retval < 0) {
		FTS_TEST_ERROR("disable auto clb fail, ret=%d", retval);
		goto err_disable_clb_fail;
	}
	retval = enter_factory_mode();
	if (retval < 0) {
		FTS_TEST_ERROR("enter factory mode fail, ret=%d", retval);
		goto exit;
	}
	retval = fts_read_reg(FACTORY_REG_CHX_NUM, &tx_num);
	if (retval < 0) {
		FTS_TEST_ERROR("get tx_num fail, ret=%d", retval);
		goto exit;
	}
	retval = fts_read_reg(FACTORY_REG_CHY_NUM, &rx_num);
	if (retval < 0) {
		FTS_TEST_ERROR("get rx_num fail, ret=%d", retval);
		goto exit;
	}

	row = tx_num;
	col = rx_num;
	frame_data_words = kcalloc((row * col), sizeof(int), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		FTS_ERROR("Failed to allocate frame_data_words mem");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", idx, row, col);
		retval = fts_data_request(cdev, frame_data_words, RAWDATA_TEST, tx_num * rx_num * 2);
		if (retval < 0) {
			FTS_ERROR("data_request failed!");
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
		retval = fts_data_request(cdev, frame_data_words, DELTA_TEST, tx_num * rx_num * 2);
		if (retval < 0) {
			FTS_ERROR("data_request failed!");
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

DATA_REQUEST_FAILED:
	len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
	fts_reset_proc(ts_data, false, 200);
	FTS_INFO("get tp delta raw data end!");
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
exit:
	enter_work_mode();
err_disable_clb_fail:
#if defined(FTS_ESDCHECK_EN) && (FTS_ESDCHECK_EN)
   fts_esdcheck_switch(ts_data, ENABLE);
#endif
    fts_irq_enable();
    mutex_unlock(&input_dev->mutex);
	FTS_TEST_FUNC_EXIT();
	return retval;
}

static int fts_get_noise(struct ztp_device *cdev)
{
	int ret =0;

	if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
		FTS_ERROR("alloc tp firmware data fail");
		return -ENOMEM;
	}
	ret = fts_testing_delta_raw_report(cdev, 5);
	if (ret) {
		FTS_ERROR("get_noise failed");
		tpd_zlog_record_notify(TP_GET_NOISE_ERROR_NO);
		return ret;
	} else {
		FTS_INFO("get_noise success");
	}
	return 0;
}

int fts_bbat_test_int_pin(void)
{
	int ret;

	ret = fts_enter_test_environment(1);
	if (ret < 0) {
		FTS_ERROR("enter test environment fail");
		return ret;
	}
	ret = enter_factory_mode();
	if (ret < 0) {
		FTS_ERROR("enter factory mode fail, ret=%d", ret);
		return ret;
	}
	fts_write_reg(FTS_REG_INT_OUT_TEST, 0);
	usleep_range(10000, 11000);
	fts_write_reg(FTS_REG_INT_OUT_TEST, 1);
	usleep_range(10000, 11000);

	ret = enter_work_mode();
	if (ret < 0) {
		FTS_ERROR("enter work mode fail, ret=%d", ret);
		return ret;
	}
	ret = fts_enter_test_environment(0);
	if (ret < 0) {
		FTS_ERROR("enter normal environment fail");
		return ret;
	}
    return ret;
}

int fts_bbat_test_reset_pin(void)
{
	int ret = 0;
	u8 report_rate = 0;
	u8 report_rate_old = 0;
	struct fts_ts_data *ts_data = fts_data;

	ret = fts_read_reg(FTS_REG_REPORT_RATE, &report_rate);
	if (ret < 0) {
		FTS_ERROR("read report_rate fail");
		return ret;
	}
	FTS_INFO("report_rate val:0x%x", report_rate);
	report_rate_old = report_rate;
	msleep(20);
	ret = fts_write_reg(FTS_REG_REPORT_RATE, report_rate + 1);
	if (ret < 0) {
		FTS_ERROR("write report_rate fail");
		return ret;
	}
	msleep(20);
	ret = fts_read_reg(FTS_REG_REPORT_RATE, &report_rate);
	if (ret < 0) {
		FTS_ERROR("read report_rate fail");
		return ret;
	}
	FTS_INFO("write report_rate + 1, read report_rate val:0x%x", report_rate);
	if (report_rate != (report_rate_old + 1)) {
		FTS_INFO("write report_rate fail");
		return -EINVAL;
	}
	fts_reset_proc(ts_data, false, 200);
	ret = fts_read_reg(FTS_REG_REPORT_RATE, &report_rate);
	if (ret < 0) {
		FTS_ERROR("read report_rate fail");
		return ret;
	}
	if (report_rate_old == report_rate) {
		FTS_INFO("reset test success");
		ret = 0;
	} else {
		FTS_ERROR("reset test fail");
		ret = -EINVAL;
	}
    return ret;
}

static int fts_bbat_test(struct ztp_device *cdev)
{
	int ret = 0;

/*tp int test*/
	cdev->bbat_test_enter = true;
	cdev->bbat_int_test = false;
	cdev->bbat_test_result = 0;
	reinit_completion(&cdev->bbat_test_completion);
	ret = fts_bbat_test_int_pin();
	if (ret) {
		cdev->bbat_test_result = cdev->bbat_test_result | TP_INT_BBAT_TEST_FAIL;
	}
	if (cdev->bbat_int_test == false) {
		ret = wait_for_completion_timeout(&cdev->bbat_test_completion, msecs_to_jiffies(700));
		if (!ret) {
			FTS_ERROR("tp int test fail");
			cdev->bbat_test_result = TP_INT_BBAT_TEST_FAIL;
		}
	}
/* tp rest test*/
	ret = fts_bbat_test_reset_pin();
	if (ret) {
		cdev->bbat_test_result = cdev->bbat_test_result | TP_RST_BBAT_TEST_FAIL;
	}
	cdev->bbat_test_enter = false;
	return cdev->bbat_test_result;
}

static int fts_ghost_check_reset(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret = 0;

	if (!ts_data) {
		FTS_ERROR("ts_data is Null");
		return -EINVAL;
	}

	fts_reset_proc(ts_data, false, 200);
	FTS_INFO("fts_reset_proc mdelay(200)");

	fts_tp_state_recovery(ts_data);
	FTS_INFO("fts_tp_state_recovery");

	return ret;
}

int fts_tpd_register_fw_class(struct fts_ts_data *data)
{
	get_fts_module_info_from_lcd();
	tpd_cdev->private = (void *)data;
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;
	tpd_cdev->tp_fw_upgrade = fts_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = fts_tp_suspend_show;
	tpd_cdev->set_tp_suspend = fts_set_tp_suspend;
	tpd_cdev->tpd_suspend_need_awake = fts_suspend_need_awake;
	tpd_cdev->set_display_rotation = fts_set_display_rotation;
	tpd_cdev->headset_state_show = fts_headset_state_show;
	tpd_cdev->set_headset_state = fts_set_headset_state;
	tpd_cdev->set_sensibility = tpd_set_sensibility;
	tpd_cdev->tp_rst_level_get = fts_get_rst_level;
	tpd_cdev->tp_rst_level_set = fts_set_rst_level;
	tpd_cdev->tp_data = data;
	tpd_cdev->tp_resume_func = fts_tp_resume;
	tpd_cdev->tp_suspend_func = fts_tp_suspend;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	//tpd_cdev->tpd_shutdown = tpd_fts_shutdown;
	tpd_cdev->get_noise = fts_get_noise;
	tpd_cdev->max_x = data->pdata->x_max;
	tpd_cdev->max_y = data->pdata->y_max;
	tpd_cdev->input = data->input_dev;
	data->sensibility_level = 1;
	tpd_cdev->charger_state_notify = fts_charger_state_notify;
	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));
	tpd_cdev->tp_bbat_test = fts_bbat_test;
	tpd_cdev->ghost_check_reset = fts_ghost_check_reset;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	zlog_tp_dev.device_name = fts_vendor_name;
	zlog_tp_dev.ic_name = "focal_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}

