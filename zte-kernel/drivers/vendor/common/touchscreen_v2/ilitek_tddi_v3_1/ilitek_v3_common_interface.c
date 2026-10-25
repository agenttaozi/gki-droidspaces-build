/* Started by AICoder, pid:r9d1543772wbad614e350a39b617195d1393bfae */
/************************************************************************
*
* File Name: ilitek_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/

#include "ilitek_v3.h"
#include <linux/kernel.h>

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/

#define MAX_FILE_NAME_LEN       64
#define MAX_FILE_PATH_LEN       64
#define MAX_NAME_LEN_20         20
#define FW_TP_STATUS_BUFFER     9

int ilitek_vendor_id = 0;
char ilitek_vendor_name[MAX_NAME_LEN_20] = { 0 };
char ilitek_firmware_name[MAX_FILE_NAME_LEN] = {0};
char ilitek_default_firmware_name[MAX_FILE_NAME_LEN] = {0};
char ini_rq_path[MAX_FILE_NAME_LEN] = { 0 };

enum ilitek_cmd {
    HEADSET = 0,
    CHARGER,
    MROTATION,
};

struct tpvendor_t ilitek_vendor_l[] = {
    {ILI_VENDOR_ID_0, ILI_VENDOR_0_NAME},
    {ILI_VENDOR_ID_1, ILI_VENDOR_1_NAME},
    {ILI_VENDOR_ID_2, ILI_VENDOR_2_NAME},
    {ILI_VENDOR_ID_3, ILI_VENDOR_3_NAME},
    {VENDOR_END, "Unknown"},
};

#ifdef CONFIG_TP_GET_LCD_ID
char* ilitek_lcd_id[] = {
    ILI_LCD_ID_0,
    ILI_LCD_ID_1,
    ILI_LCD_ID_2,
    ILI_LCD_ID_3,
    ILI_LCD_ID_END,
};
#endif

#ifdef ILITEK_V3_PINCTRL_EN
#define ILITEK_V3_PINCTRL_INIT_STATE "pmx_ts_init"
int ilitek_pinctrl_init(struct spi_device *spi, struct ilitek_ts_data *ilits)
{
    int ret = 0;
    ILI_INFO("%s enter\n", __func__);

    /* Get pinctrl if target uses pinctrl */
    ilits->ts_pinctrl = devm_pinctrl_get(&spi->dev);
    if (IS_ERR_OR_NULL(ilits->ts_pinctrl)) {
        ret = PTR_ERR(ilits->ts_pinctrl);
        ILI_ERR("%s: devm_pinctrl_get failed, ret=%d\n", __func__, ret);
        goto err_pinctrl_get;
    } else {
        ILI_INFO("%s: devm_pinctrl_get success\n", __func__);
    }

    ilits->pinctrl_state_init = pinctrl_lookup_state(ilits->ts_pinctrl, ILITEK_V3_PINCTRL_INIT_STATE);
    if (IS_ERR_OR_NULL(ilits->pinctrl_state_init)) {
        ret = PTR_ERR(ilits->pinctrl_state_init);
        ILI_ERR("%s: pinctrl_lookup_state %s failed, ret=%d\n", __func__, ILITEK_V3_PINCTRL_INIT_STATE, ret);
        goto err_pinctrl_lookup;
    } else {
        ILI_INFO("%s: pinctrl_lookup_state %s success\n", __func__, ILITEK_V3_PINCTRL_INIT_STATE);
    }

    ret = pinctrl_select_state(ilits->ts_pinctrl, ilits->pinctrl_state_init);
    if (ret < 0) {
        ILI_ERR("%s: failed to select pin to init state, ret=%d\n", __func__, ret);
        goto err_select_init_state;
    } else {
        ILI_INFO("%s: success to select pin to init state\n", __func__);
    }

    ILI_INFO("%s exit\n", __func__);
    return 0;

err_select_init_state:
err_pinctrl_lookup:
    devm_pinctrl_put(ilits->ts_pinctrl);
err_pinctrl_get:
    ilits->ts_pinctrl = NULL;
    return ret;
}
#endif

int ilitek_get_fw(void)
{
    int i = 0;
    int ret = -EIO;

    if (ILI_MODULE_NUM == 1) {
        ilitek_vendor_id = ILI_VENDOR_ID_0;
        strlcpy(ilitek_vendor_name, ILI_VENDOR_0_NAME, sizeof(ilitek_vendor_name));
        ret = 0;
        goto out;
    }

#ifdef CONFIG_TP_GET_LCD_ID
	ret = get_lcd_panel_id();
	if (ret == 0) {
        for (i = 0; i < ARRAY_SIZE(ilitek_vendor_l); i++) {
            ILI_INFO("%d--->%s ID%x\n", i, ilitek_vendor_l[i].vendor_name, ilitek_vendor_l[i].vendor_id);
            if (strnstr(lcd_id, ilitek_lcd_id[i], strlen(lcd_id))) {
                ilitek_vendor_id = ilitek_vendor_l[i].vendor_id;
                strlcpy(ilitek_vendor_name, ilitek_vendor_l[i].vendor_name, sizeof(ilitek_vendor_name));
                goto out;
            }
        }
        strlcpy(ilitek_vendor_name, "unknown", sizeof(ilitek_vendor_name));
    }
#else
    for (i = 0; i < ARRAY_SIZE(ilitek_vendor_l); i++) {
        ILI_INFO("%d--->%s\n", i, ilitek_vendor_l[i].vendor_name);
        if (strnstr(lcd_name, ilitek_vendor_l[i].vendor_name, strlen(lcd_name))) {
            ilitek_vendor_id = ilitek_vendor_l[i].vendor_id;
            strlcpy(ilitek_vendor_name, ilitek_vendor_l[i].vendor_name, sizeof(ilitek_vendor_name));
            ret = 0;
            goto out;
        }
    }
#endif

out:
    ILI_INFO("ilitek_vendor_name = %s\n", ilitek_vendor_name);
    snprintf(ilitek_firmware_name, sizeof(ilitek_firmware_name), "ilitek_firmware_%s.hex", ilitek_vendor_name);
    ILI_INFO("ilitek_firmware_name = %s\n", ilitek_firmware_name);
    snprintf(ilitek_default_firmware_name, sizeof(ilitek_default_firmware_name), "%s_%s.hex", ILITEK_DEFAULT_FIRMWARE, ilitek_vendor_name);
    ILI_INFO("ilitek_default_firmware_name = %s\n", ilitek_default_firmware_name);
    snprintf(ini_rq_path, sizeof(ini_rq_path), "ilitek_mp_%s.ini", ilitek_vendor_name);
    ILI_INFO("ini_rq_path = %s\n", ini_rq_path);

    return ret;
}

void ilitek_update_module_info(void)
{
    ilits->md_name = ilitek_vendor_name;
    ilits->md_fw_rq_path = ilitek_firmware_name;
    ilits->md_ini_rq_path = ini_rq_path;
    ilits->md_ini_path = ini_rq_path;
    ilits->md_fw_ili_size = 0;
    ilits->md_fw_default_path = ilitek_default_firmware_name;
    ILI_INFO("Found %s module\n", ilits->md_name);
    ilits->tp_module = ilitek_vendor_id;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
    u8 vendor_id = 0;

#if (TDDI_INTERFACE == BUS_I2C)
    ilits->info_from_hex = DISABLE;
#endif
    if (atomic_read(&ilits->ice_stat))
        ili_ice_mode_ctrl(DISABLE, OFF);
    ili_ic_get_fw_ver();
#if (TDDI_INTERFACE == BUS_I2C)
    ilits->info_from_hex = ENABLE;
#endif
    ILI_INFO("Firmware version = 0x%x.\n", ilits->chip->fw_ver);
    vendor_id = (ilits->chip->fw_ver >> 24) & 0xff;
    ILI_INFO("vendor_id = 0x%x.\n", vendor_id);
    ilitek_get_fw();
    ilitek_update_module_info();
    strlcpy(cdev->ic_tpinfo.tp_name, "ilitek", sizeof(cdev->ic_tpinfo.tp_name));
    strlcpy(cdev->ic_tpinfo.vendor_name, ilitek_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
    cdev->ic_tpinfo.chip_model_id = TS_CHIP_ILITEK;
    cdev->ic_tpinfo.firmware_ver = ilits->chip->fw_ver;
    cdev->ic_tpinfo.module_id = ilitek_vendor_id;
    cdev->ic_tpinfo.i2c_type = 0;
    return 0;
}

static int ilitek_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
    bool esd_en = ilits->wq_esd_ctrl, bat_en = ilits->wq_bat_ctrl;

    ILI_INFO("to upgrade firmware\n");

    mutex_lock(&ilits->touch_mutex);
    if (esd_en)
        ili_wq_ctrl(WQ_ESD, DISABLE);
    if (bat_en)
        ili_wq_ctrl(WQ_BAT, DISABLE);

    ilits->force_fw_update = ENABLE;
    ilits->node_update = true;
    ilits->fw_open = TP_FIRMWARE_DATA;
    ili_fw_upgrade_handler(NULL);
    ilits->force_fw_update = DISABLE;
    ilits->node_update = false;
    if (esd_en)
        ili_wq_ctrl(WQ_ESD, ENABLE);
    if (bat_en)
        ili_wq_ctrl(WQ_BAT, ENABLE);

    mutex_unlock(&ilits->touch_mutex);
    return 0;
}

static void ilitek_reset_cmd_display_rotation(void)
{
    u8 edge_palm_para = 0;

    switch (ilits->display_rotation) {
    case mRotatin_0:
        edge_palm_para = 1;
        break;
    case mRotatin_90:
        edge_palm_para = 2;
        break;
    case mRotatin_180:
        edge_palm_para = 1;
        break;
    case mRotatin_270:
        edge_palm_para = 0;
        break;
    default:
        break;
    }
    ILI_INFO("edge_palm_para : 0X%x.\n", edge_palm_para);
    if (ili_ic_func_ctrl("edge_palm", edge_palm_para) < 0)
        ILI_ERR("Write edge_palm failed\n");
}

void ili_touch_reset_send_cmd(enum ilitek_cmd cmd)
{
    if (atomic_read(&ilits->fw_stat)) {
        ILI_INFO("tp fw upgrading, ignore cmd..\n");
        return;
    }
    switch (cmd) {
    case HEADSET:
        if (ili_ic_func_ctrl("ear_phone", ilits->headset_mode) < 0)
            ILI_ERR("Write headset plug in failed\n");
        break;
    case CHARGER:
        if (ili_ic_func_ctrl("plug", !ilits->charger_mode) < 0)
            ILI_ERR("Write plug out failed\n");
        break;
    case MROTATION:
        ilitek_reset_cmd_display_rotation();
        break;
    default:
        ILI_ERR("unknown cmd.\n");
    }
}

void ilitek_ex_mode_recovery(void)
{
    ILI_INFO("send ex cmd msleep 100ms");
    msleep(100);
    if (ilits->charger_mode) {
        if (ili_ic_func_ctrl("plug", ENABLE) < 0)
            ILI_ERR("Write plug out failed\n");
    }
    if (ilits->headset_mode) {
        if (ili_ic_func_ctrl("ear_phone", ENABLE) < 0)
            ILI_ERR("Write headset plug in failed\n");
    }
}

static int ilitek_headset_state_show(struct ztp_device *cdev)
{
    cdev->headset_state = ilits->headset_mode;
    return cdev->headset_state;
}

static int ilitek_set_headset_state(struct ztp_device *cdev, int enable)
{
    ilits->headset_mode = enable;
    ILI_INFO("%s: headset_state = %d.\n", __func__, ilits->headset_mode);
    if (!ilits->tp_suspend) {
        ili_touch_reset_send_cmd(HEADSET);
    }
    return ilits->headset_mode;
}

static int ilitek_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
    ilits->display_rotation = mrotation;
    if (ilits->tp_suspend)
        return 0;
    ILI_INFO("%s: display_rotation = %d.\n", __func__, ilits->display_rotation);
    ili_touch_reset_send_cmd(MROTATION);
    return ilits->display_rotation;
}

static void ilitek_tp_reset_gpio_output(bool value)
{
    ILI_INFO("ilitek tp reset gpio set value: %d", value);
    if (gpio_is_valid(ilits->tp_rst))
        gpio_direction_output(ilits->tp_rst, value);
}

static int ilitek_ts_resume(void *ilitek_data)
{
#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
	if (!tpd_cdev->tpd_proximity_state) {
		ilits->tp_suspend = false;
		tpd_cdev->tp_resume_before_lcd_cmd = true;
		tpd_cdev->tpd_proximity_state = 1;
		ILI_INFO("enable tp proximity, end tp resume.");
		return 0;
	}
#endif
    return ili_sleep_handler(TP_RESUME);
}

static int ilitek_ts_suspend(void *ilitek_data)
{
#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
	if(tpd_cdev->tpd_proximity_enable && !tpd_cdev->tpd_proximity_state) {
		ilits->tp_suspend = true;
		tpd_cdev->tp_resume_before_lcd_cmd = false;
		tpd_cdev->tpd_proximity_suspended = true;
		ILI_INFO("enable tp proximity, end tp suspend.");
		return 0;
		}
#endif
    return ili_sleep_handler(TP_DEEP_SLEEP);
}

static int ilitek_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
    if (enable) {
        change_tp_state(LCD_OFF);
    } else {
        change_tp_state(LCD_ON);
    }
    cdev->tp_suspend = ilits->tp_suspend;
    return cdev->tp_suspend;
}

static int ilitek_tp_suspend_show(struct ztp_device *cdev)
{
    cdev->tp_suspend = ilits->tp_suspend;
    return cdev->tp_suspend;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
    cdev->b_gesture_enable = ilits->gesture;
    return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
    if (ilits->tp_suspend) {
        cdev->tp_suspend_write_gesture = true;
        tpd_zlog_record_notify(TP_SUSPEND_GESTURE_OPEN_NO);
    }
    ilits->gesture = enable;
    return enable;
}

static bool tpd_suspend_need_awake(struct ztp_device *cdev)
{
    if (!cdev->tp_suspend_write_gesture &&
        (atomic_read(&ilits->fw_stat) || ilits->gesture)) {
        ILI_INFO("tp suspend need awake.\n");
        return true;
    }
#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
	else if (tpd_cdev->tpd_proximity_enable && !tpd_cdev->tpd_proximity_state) {
		ILI_INFO("tp proximity enable, tp suspend need awake.");
		return true;
	}
#endif
    else {
        tpd_cdev->tp_suspend_write_gesture = false;
        ILI_INFO("tp suspend don't need awake.\n");
        return false;
    }
}

static int ilitek_data_request(s16 *frame_data_words, enum tp_test_type test_type)
{
    int row = 0, col = 0;
    int index = 0, ret = 0, i = 0;
    int read_length = 0;
    u8 cmd[2] = { 0 };
    u8 *data = NULL;

    row = ilits->ych_num;
    col = ilits->xch_num;
    read_length = 4 + 2 * row * col + 1;

    switch (test_type) {
    case RAWDATA_TEST:
        cmd[0] = 0xB7;
        cmd[1] = 0x2;  // get rawdata
        read_length += FW_TP_STATUS_BUFFER * 2;
        break;
    case DELTA_TEST:
        cmd[0] = 0xB7;
        cmd[1] = 0x1;  // get diffdata
        break;
    default:
        ILI_ERR("err command,\n");
        ret = -1;
        goto out;
    }

    ILI_INFO("read length = %d\n", read_length);
    data = kcalloc(read_length + 1, sizeof(u8), GFP_KERNEL);
    if (ERR_ALLOC_MEM(data)) {
        ILI_ERR("Failed to allocate data mem\n");
        ret = -1;
        goto out;
    }

    ret = ilits->wrapper(cmd, sizeof(cmd), NULL, 0, OFF, OFF);
    if (ret < 0) {
        ILI_ERR("Failed to write 0XB7 command, %d\n", ret);
        goto enter_normal_mode;
    }

    msleep(20);

    // read debug packet header
    ret = ilits->wrapper(NULL, 0, data, read_length, OFF, OFF);
    if (ret < 0) {
        ILI_ERR("Read debug packet header failed, %d\n", ret);
        goto enter_normal_mode;
    }
    if (test_type == RAWDATA_TEST) {
        for (i = 4, index = 0; index < row * col + FW_TP_STATUS_BUFFER; i += 2, index++) {
            frame_data_words[index] = (data[i] << 8) + data[i + 1];
        }
    } else {
        for (i = 4, index = 0; index < row * col; i += 2, index++) {
            frame_data_words[index] = (data[i] << 8) + data[i + 1];
        }
    }

enter_normal_mode:
    cmd[1] = 0x03;  // switch to normal mode
    ret = ilits->wrapper(cmd, sizeof(cmd), NULL, 0, ON, OFF);
    if (ret < 0) {
        ILI_ERR("Failed to write 0xB7,0x3 command, %d\n", ret);
        goto out;
    }
    msleep(20);
out:
    ipio_kfree((void **)&data);
    return ret;
}

static int ilitek_testing_delta_raw_report(struct ztp_device *cdev, unsigned int num_of_reports)
{
    s16 *frame_data_words = NULL;
    unsigned int col = 0;
    unsigned int row = 0;
    unsigned int idx = 0;
    int retval = 0;
    int len = 0;
    int i = 0;

    ili_wq_ctrl(WQ_ESD, DISABLE);
    ili_wq_ctrl(WQ_BAT, DISABLE);
    mutex_lock(&ilits->touch_mutex);
    ili_irq_disable();
    row = ilits->ych_num;
    col = ilits->xch_num;
    ILI_INFO("row= %d, col= %d!\n", row, col);
    frame_data_words = kcalloc((row * col + FW_TP_STATUS_BUFFER), sizeof(s16), GFP_KERNEL);
    if (ERR_ALLOC_MEM(frame_data_words)) {
        ILI_ERR("Failed to allocate frame_data_words mem\n");
        retval = -1;
        goto MEM_ALLOC_FAILED;
    }
    for (idx = 0; idx < num_of_reports; idx++) {
        retval = ilitek_data_request(frame_data_words, RAWDATA_TEST);
        if (retval < 0) {
            ILI_ERR("data_request failed!\n");
            goto DATA_REQUEST_FAILED;
        }

        len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "RawData:\n");
        for (i = 0; i < row * col + FW_TP_STATUS_BUFFER; i++) {
            len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "%5d,", frame_data_words[i]);
            if ((i + 1) % row == 0)
                len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
        }
        retval = ilitek_data_request(frame_data_words, DELTA_TEST);
        len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
        if (retval < 0) {
            ILI_ERR("data_request failed!\n");
            goto DATA_REQUEST_FAILED;
        }
        len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "Delta:\n");
        for (i = 0; i < row * col; i++) {
            len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "%5d,", frame_data_words[i]);
            if ((i + 1) % row == 0)
                len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
        }
        len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
    }

    retval = 0;
    ILI_INFO("get tp delta raw data end!\n");
DATA_REQUEST_FAILED:
    kfree(frame_data_words);
    frame_data_words = NULL;
MEM_ALLOC_FAILED:
    //ILI_INFO("TP HW RST\n");
    //ili_tp_reset(); //force switch ap mode will return err
    ili_switch_tp_mode(P5_X_FW_AP_MODE);
    ili_irq_enable();
    mutex_unlock(&ilits->touch_mutex);
    ili_wq_ctrl(WQ_ESD, ENABLE);
    ili_wq_ctrl(WQ_BAT, ENABLE);
    return retval;
}

static int ilitek_tpd_get_noise(struct ztp_device *cdev)
{
    int retval = 0;

    if (ilits->tp_suspend)
        return -EIO;

    if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
        ILI_ERR("alloc tp firmware data fail");
        return -ENOMEM;
    }
    retval = ilitek_testing_delta_raw_report(cdev, 3);
    if (retval) {
        ILI_ERR("%s: get_noise failed\n", __func__);
        return retval;
    } else {
        ILI_ERR("%s: get_noise success\n", __func__);
    }
    return 0;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
    int ret = 0, retry = 0;
    bool esd_en = ilits->wq_esd_ctrl, bat_en = ilits->wq_bat_ctrl;
    unsigned char *g_user_buf = NULL;

    ILI_INFO("Run MP test with LCM on\n");

    mutex_lock(&ilits->touch_mutex);

    if (esd_en)
        ili_wq_ctrl(WQ_ESD, DISABLE);
    if (bat_en)
        ili_wq_ctrl(WQ_BAT, DISABLE);

    g_user_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if (ERR_ALLOC_MEM(g_user_buf)) {
        ILI_ERR("Failed to allocate g_user_buf.\n");
        mutex_unlock(&ilits->touch_mutex);
        return 0;
    }

    do {
        cdev->zte_tp_selftest_result = 0;
        ret = ili_mp_test_handler(g_user_buf, ON);
        if (ret || cdev->zte_tp_selftest_result) {
            retry++;
            ILI_ERR("TP self test fail, retry:%d", retry);
            ili_tp_reset();
        } else {
            break;
        }
    } while (retry < 3);

    if (cdev->zte_tp_selftest_result) {
        ILI_ERR("ilitek selftest failed");
        tpd_zlog_record_notify(TP_SELF_TEST_ERROR_NO);
    } else {
        ILI_INFO("ilitek selftest pass");
    }

    if (esd_en)
        ili_wq_ctrl(WQ_ESD, ENABLE);
    if (bat_en)
        ili_wq_ctrl(WQ_BAT, ENABLE);
    kfree(g_user_buf);
    mutex_unlock(&ilits->touch_mutex);

    ILI_INFO("MP test end");

    return ret;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
    ssize_t num_read_chars = 0;
    int i_len = 0;

    i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", cdev->zte_tp_selftest_result, ilits->xch_num, ilits->ych_num, 0);
    ILI_INFO("tpd test: %s.\n", buf);
    num_read_chars = i_len;
    return num_read_chars;
}

static int tpd_ilitek_shutdown(struct ztp_device *cdev)
{
    bool esd_en = ilits->wq_esd_ctrl, bat_en = ilits->wq_bat_ctrl;

    ILI_INFO("disable irq");
    ili_irq_disable();
    if (esd_en)
        ili_wq_ctrl(WQ_ESD, DISABLE);
    if (bat_en)
        ili_wq_ctrl(WQ_BAT, DISABLE);
    gpio_direction_output(ilits->tp_rst, 0);
    return 0;
}

static int ilitek_ghost_check_reset(struct ztp_device *cdev)
{
    int ret = 0;

#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
    if (cdev->tpd_proximity_enable) {
        ILI_INFO("proximity enable, don't need ili_ic_code_reset");
        return ret;
    }
#endif

    ILI_INFO("ili_ic_code_reset\n");
    ret = ili_ic_code_reset(OFF);
    if (ret < 0) {
        ILI_ERR("IC Code reset failed\n");
    } else {
        ILI_INFO("IC Code reset success\n");

        ILI_INFO("charger_mode : %d", ilits->charger_mode);
        ILI_INFO("headset_mode : %d", ilits->headset_mode);
        ilitek_ex_mode_recovery();
    }

    return ret;
}

static int ilitek_charger_state_notify(struct ztp_device *cdev)
{
    ilits->charger_mode = cdev->charger_mode;
    ILI_INFO("charger_mode = %d", ilits->charger_mode);

    if (ilits->tp_suspend) {
        ILI_INFO("tp in suspend, return");
        return 0;
    }

    ili_touch_reset_send_cmd(CHARGER);

    return ilits->charger_mode;
}

static int ilitek_bbat_cmd_write(void)
{
	u8 cmd[1] = {0x21};

	if (mutex_is_locked(&ilits->touch_mutex)) {
		ILI_INFO("touch is locked, ignore tp-int check\n");
		return -EIO;
	}

	mutex_lock(&ilits->touch_mutex);

	if (ilits->wrapper(cmd, sizeof(u8), NULL, 0, ON, ON)) {
		ILI_ERR("Failed to write data\n");
		return -EIO;
	}

	mutex_unlock(&ilits->touch_mutex);

	return 0;
}

static int ilitek_int_check(struct ztp_device *cdev)
{
	int ret = 0;

	/*tp int test*/
	cdev->bbat_test_enter = true;
	cdev->bbat_int_test = false;
	cdev->bbat_test_result = 0;
	reinit_completion(&cdev->bbat_test_completion);
	ret = ilitek_bbat_cmd_write();
	if (ret) {
		ILI_INFO("ilitek_bbat_cmd_write fail");
		return cdev->bbat_test_result;
	}
	if (cdev->bbat_int_test == false) {
		ret = wait_for_completion_timeout(&cdev->bbat_test_completion, msecs_to_jiffies(700));
		if (!ret) {
			ILI_INFO("tp int test fail");
			cdev->bbat_test_result = TP_INT_BBAT_TEST_FAIL;
		}
	}
	cdev->bbat_test_enter = false;
	return cdev->bbat_test_result;
}

#ifdef CONFIG_TOUCHSCREEN_ZTE_ESD_CHECK
static int ilitek_esd_check(struct ztp_device *cdev)
{
	return ilitek_int_check(cdev);
}

static void ilitek_esd_check_recovery(struct ztp_device *cdev)
{
	if (mutex_is_locked(&ilits->touch_mutex)) {
		ILI_INFO("touch is locked, ignore esd check recovery\n");
		return;
	}

	mutex_lock(&ilits->touch_mutex);
	ili_spi_recovery();
	mutex_unlock(&ilits->touch_mutex);

	complete_all(&ilits->esd_done);
	ili_wq_ctrl(WQ_ESD, ENABLE);

	ili_irq_enable();
	ilits->tp_suspend = false;
	ilits->report = ENABLE;
	ilits->power_status = true;
	ILI_INFO("ilitek_esd_check_recovery success\n");
	ILI_INFO("charger_mode : %d", ilits->charger_mode);
	ILI_INFO("headset_mode : %d", ilits->headset_mode);
	ilitek_ex_mode_recovery();
}
#endif

static int ilitek_bbat_test(struct ztp_device *cdev)
{
	int ret = 0;
	if(ilits->tp_suspend) {
		ILI_INFO("tp_suspend is true, return.\n");
		return 0;
	}

	ret = ilitek_int_check(cdev);
	if (!ret) {
		ILI_INFO("tpd int BBAT test success");
	}

	return ret;
}

int ilitek_register_fw_class(void)
{
    tpd_cdev->get_tpinfo = tpd_init_tpinfo;
    tpd_cdev->tp_fw_upgrade = ilitek_tp_fw_upgrade;
    tpd_cdev->tp_suspend_show = ilitek_tp_suspend_show;
    tpd_cdev->set_tp_suspend = ilitek_set_tp_suspend;
    tpd_cdev->headset_state_show = ilitek_headset_state_show;
    tpd_cdev->set_headset_state = ilitek_set_headset_state;
    tpd_cdev->set_display_rotation = ilitek_set_display_rotation;
    tpd_cdev->get_gesture = tpd_get_wakegesture;
    tpd_cdev->wake_gesture = tpd_enable_wakegesture;
    tpd_cdev->tpd_suspend_need_awake = tpd_suspend_need_awake;
    tpd_cdev->tp_reset_gpio_output = ilitek_tp_reset_gpio_output;
    tpd_cdev->get_noise = ilitek_tpd_get_noise;
    tpd_cdev->max_x = TOUCH_SCREEN_X_MAX;
    tpd_cdev->max_y = TOUCH_SCREEN_Y_MAX;
    tpd_cdev->tp_self_test = tpd_test_cmd_store;
    tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
    tpd_cdev->tp_resume_before_lcd_cmd = true;
    tpd_cdev->tp_data = ilits;
    tpd_cdev->tp_resume_func = ilitek_ts_resume;
    tpd_cdev->tp_suspend_func = ilitek_ts_suspend;
    tpd_cdev->tpd_shutdown = tpd_ilitek_shutdown;
    tpd_cdev->ghost_check_reset = ilitek_ghost_check_reset;
    tpd_cdev->charger_state_notify = ilitek_charger_state_notify;
    tpd_cdev->input = ilits->input;
#if defined (HUB_TP_PS_ENABLE) && (HUB_TP_PS_ENABLE == 1)
	tpd_cdev->tpd_proximity_input_dev = ilits->ili_proximity_input_dev;
#endif
#ifdef CONFIG_TOUCHSCREEN_ZTE_ESD_CHECK
    tpd_cdev->tpd_esd_check = ilitek_esd_check;
    tpd_cdev->tpd_esd_recovery = ilitek_esd_check_recovery;
#endif
    tpd_cdev->tp_bbat_test = ilitek_bbat_test;
    tpd_init_tpinfo(tpd_cdev);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
    zlog_tp_dev.device_name = ilitek_vendor_name;
    zlog_tp_dev.ic_name = "ilitek_tp";
    TPD_ZLOG("device_name: %s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
    return 0;
}
/* Ended by AICoder, pid:r9d1543772wbad614e350a39b617195d1393bfae */