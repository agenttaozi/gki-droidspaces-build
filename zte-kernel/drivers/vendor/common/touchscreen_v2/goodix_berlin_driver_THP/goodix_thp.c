/************************************************************************/
/* Copyright <2019-2020> GOODIX                                        */
/*                                                                      */
/* GOODIX Confidential. This software is owned or controlled by GOODIX  */
/* and may only be used strictly in accordance with the applicable      */
/* license terms.  By expressly accepting such terms or by downloading, */
/* installing, activating and/or otherwise using the software, you are  */
/* agreeing that you have read, and that you agree to comply with and   */
/* are bound by, such license terms.                                    */
/* If you do not agree to be bound by the applicable license terms,     */
/* then you may not retain, install, activate or otherwise use the      */
/* software.                                                            */
/*                                                                      */
/************************************************************************/

#include <linux/time.h>
#include <linux/time64.h>

#include "goodix_thp.h"

#ifdef CONFIG_TOUCHSCREEN_DRM_PANEL_NOTIFIER
#include <linux/soc/qcom/panel_event_notifier.h>
static bool panel_enter_low_power = false;
#endif
char DEVICE_NODE_NAME[50];

#define GOODIX_THP_MISC_DEVICE_NAME	"thp"
#define PINCTRL_STATE_ACTIVE		"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND		"pmx_ts_suspend"
#define DEVICE_NAME			"input_agent"

#define QUERYBIT(longlong, bit) 	(!!(longlong[bit/8] & (1 << bit%8)))

struct goodix_thp_core *gdix_thp_core;

#ifdef GOODIX_USB_DETECT_GLOBAL
bool GOODIX_USB_detect_flag;
#endif

#define LAGRE_SUPPRESSION_AREA "large_area=true"

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
extern bool aod_down_flag;
extern int finger_retry;
#endif

static int goodix_thp_suspend(struct goodix_thp_core *core_data);
static int goodix_thp_resume(struct goodix_thp_core *core_data);
extern void goodix_thp_tpd_register_fw_class(struct goodix_thp_core *core_data);

static bool hal_is_prepared;

#ifdef  CONFIG_TOUCHSCREEN_DRM_PANEL_NOTIFIER
static struct drm_panel *active_panel;
static void goodix_ts_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *event, void *client_data);
#endif

extern void ufp_report_lcd_state(void);
extern int ufp_notifier_cb(int in_lp);

static int goodix_thp_spi_trans(struct goodix_thp_core *cd,
                        char *tx_buf, char *rx_buf, unsigned int len)
{
        struct spi_message spi_msg;
        struct spi_device *sdev = cd->sdev;
        struct thp_ts_device *tdev = cd->ts_dev;

        struct spi_transfer xfer = {
                .tx_buf = tx_buf,
                .rx_buf = rx_buf,
                .len    = len,
        };
        int ret;

        spi_message_init(&spi_msg);
        spi_message_add_tail(&xfer, &spi_msg);

        mutex_lock(&tdev->spi_mutex);
        ret = spi_sync(sdev, &spi_msg);
        mutex_unlock(&tdev->spi_mutex);

        return ret;
}

#if 0
static void goodix_thp_set_fp_int_pin(struct thp_ts_device *tdev, u8 level)
{
	static bool is_high = false;

	if (!is_high && level) {
		is_high = true;
		tdev->hw_ops->set_fp_int_pin(tdev, 1);
	} else if (is_high && !level) {
		is_high = false;
		tdev->hw_ops->set_fp_int_pin(tdev, 0);
	}
}
#endif

/*
 * If irq is disabled/enabled, can not disable/enable again
 * disable - status 0; enable - status not 0
 */
static void goodix_thp_set_irq_enable(struct goodix_thp_core *core_data,
					int status)
{
        mutex_lock(&core_data->irq_mutex);
        if (core_data->irq_state != !!status) {
                status ? enable_irq(core_data->irq) : disable_irq(core_data->irq);
                core_data->irq_state = !!status;
                ts_info("%s: %s irq", __func__,
                                status ? "enable" : "disable");
        }
        mutex_unlock(&core_data->irq_mutex);
};

static void goodix_thp_frame_wake_up(struct goodix_thp_core *core_data)
{
        mutex_lock(&(core_data->frame_mutex));
        core_data->frame_waitq_state = WAKEUP_STATE;
        wake_up_interruptible(&(core_data->frame_wq));
        mutex_unlock(&(core_data->frame_mutex));
}

static void goodix_thp_reset_frame_list(struct goodix_thp_core *core_data)
{
        mutex_lock(&core_data->frame_mutex);
        core_data->frame_mmap_list.head = 0;
        core_data->frame_mmap_list.tail = 0;
        memset(core_data->frame_mmap_list.buf, 0, MMAP_BUFFER_SIZE);
        mutex_unlock(&core_data->frame_mutex);
}

static void put_frame_list(struct goodix_thp_core *core_data, int type, u8 *data, int len)
{
        struct driver_request_pkg *req_pkg;
        struct thp_frame_mmap_list *list = &core_data->frame_mmap_list;
        static uint32_t id;

        mutex_lock(&core_data->frame_mutex);
        /* check for max limit */
        if ((list->tail + 1) % GOODIX_THP_MAX_FRAME_BUF_COUNT == list->head) {
                ts_err("frame mmap buffer is full, overwriting oldest data");
                list->head = (list->head + 1) % GOODIX_THP_MAX_FRAME_BUF_COUNT; // Overwrite the oldest data
        }

        req_pkg = (struct driver_request_pkg *)&list->buf[list->tail * GOODIX_THP_MAX_FRAME_LEN];
        req_pkg->size = sizeof(req_pkg->request) + len;
        req_pkg->request.id = id++;
        req_pkg->request.type = type;
        if (len > 0)
                memcpy(req_pkg->request.data, data, len);
        list->tail = (list->tail + 1) % GOODIX_THP_MAX_FRAME_BUF_COUNT;
        core_data->frame_waitq_state = WAKEUP_STATE;
        wake_up_interruptible(&(core_data->frame_wq));
        mutex_unlock(&(core_data->frame_mutex));
}

static int goodix_thp_open(struct inode *inode, struct file *filp)
{
        struct goodix_thp_core *core_data = gdix_thp_core;

        ts_info("%s: called", __func__);

        /* check thp dev status */
        mutex_lock(&core_data->ts_mutex);
        core_data->open_num++;
        if (core_data->open_num > 1) {
                ts_err("%s: dev have be opened", __func__);
                mutex_unlock(&core_data->ts_mutex);
                return 0;
        }
        mutex_unlock(&core_data->ts_mutex);
        /* reset thp dev status */
        core_data->reset_state = 0;//current isn't in reset status
        core_data->get_frame_wait_mode = GET_FRAME_BLOCK_MODE;
        core_data->frame_wait_time = GOODIX_THP_DEFATULT_WAIT_FRAME_TIME;
        return 0;
}

static int goodix_thp_release(struct inode *inode, struct file *filp)
{
        struct goodix_thp_core *core_data = gdix_thp_core;

        ts_info("%s: called", __func__);

        /* check thp dev status */
#ifndef ZTE_FEATURE_PV_AR
        hal_is_prepared = false;
#endif  /* ZTE_FEATURE_PV_AR */
        mutex_lock(&core_data->ts_mutex);
        if (core_data->open_num > 0)
                core_data->open_num--;
        mutex_unlock(&core_data->ts_mutex);
        goodix_thp_frame_wake_up(core_data);
        return 0;
}

static long goodix_thp_ioctl_get_frame(unsigned long arg)
{
        void __user *user_val = (void *)arg;
        struct goodix_thp_core *core_data = gdix_thp_core;
        struct thp_frame_mmap_list *list = &core_data->frame_mmap_list;
        struct thp_ioctl_frame hal_frame;
        long r = 0;

        mutex_lock(&core_data->frame_mutex);
        if (list->head == list->tail) {
                if (core_data->get_frame_wait_mode == GET_FRAME_NONBLOCK_MODE) {
                        ts_err("no frame");
                        r = -ENODATA;
                        goto out;
                } else {
                        core_data->frame_waitq_state = WAIT_STATE;
                        if (core_data->frame_wait_time == 0) {
                                mutex_unlock(&core_data->frame_mutex);
                                r = wait_event_interruptible(core_data->frame_wq,
                                        (core_data->frame_waitq_state == WAKEUP_STATE));
                                mutex_lock(&core_data->frame_mutex);
                                if (r < 0) {
                                        ts_err("Interrupted by a signal");
                                        goto out;
                                }
                        } else {
                                mutex_unlock(&core_data->frame_mutex);
                                r = wait_event_interruptible_timeout(core_data->frame_wq,
                                        (core_data->frame_waitq_state == WAKEUP_STATE),
                                        msecs_to_jiffies(core_data->frame_wait_time));
                                mutex_lock(&core_data->frame_mutex);
                                if (r < 0) {
                                        ts_err("Interrupted by a signal");
                                        goto out;
                                } else if (r == 0) {
                                        ts_err("get frame timeout");
                                        r = -ETIMEDOUT;
                                        goto out;
                                }
                        }
                }
        }

        r = 0;
        if (list->head != list->tail) {
                hal_frame.pos = list->head * GOODIX_THP_MAX_FRAME_LEN;
                hal_frame.tv_us = ktime_get_real_ns() / 1000;
                if(copy_to_user(user_val, &hal_frame, sizeof(hal_frame))) {
                        ts_err("Failed to copy_to_user().");
                        r = -EFAULT;
                }
        } else {
                ts_err("no frame");
                r = -ENODATA;
        }

out:
        mutex_unlock(&core_data->frame_mutex);
        return r;
}

static long goodix_thp_ioctl_set_reset_value(unsigned long reset)
{
        struct goodix_thp_core *ts = gdix_thp_core;

        ts_info("%s:set reset status %ld", __func__, reset);

        gpio_set_value(ts->ts_dev->board_data.reset_gpio, !!reset);

        ts->frame_waitq_state = WAIT_STATE;
        ts->reset_state = !reset;

        return 0;
}

static long goodix_thp_ioctl_set_wait_time(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        unsigned int wait_frame_time = arg;

        if (arg > GOODIX_THP_MAX_TIMEOUT)
                wait_frame_time = GOODIX_THP_MAX_TIMEOUT;

        ts_info("set wait time %d ms.(current %dms)\n",
                        wait_frame_time, ts->frame_wait_time);

        if (wait_frame_time != ts->frame_wait_time) {
                mutex_lock(&(ts->frame_mutex));
                ts->frame_wait_time = wait_frame_time;
                ts->frame_waitq_state = WAKEUP_STATE;
                wake_up_interruptible(&(ts->frame_wq));
                mutex_unlock(&(ts->frame_mutex));
        }

        return 0;
}

static long goodix_thp_ioctl_spi_trans(void __user *data)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        int r = 0;
        u8 *tx_buf = NULL;
        u8 *rx_buf = NULL;
        struct thp_ioctl_spi_trans_data trans_data;

        if (atomic_read(&cd->suspended) && !cd->gesture_enable)
		return 0;

        /* copy data from hal */
        if (copy_from_user(&trans_data, data,
                        sizeof(struct thp_ioctl_spi_trans_data))) {
                ts_err("Failed to copy_from_user().");
                return -EFAULT;
        }

        /* check sync data size */
        if (trans_data.size > GOODIX_THP_MAX_TRANS_DATA_LEN) {
                ts_err("trans_data.size out of range.");
                return -EINVAL;
        }

        /* alloc memory for rx/tx buf */
        rx_buf = kzalloc(trans_data.size, GFP_KERNEL);
        tx_buf = kzalloc(trans_data.size, GFP_KERNEL);
        if (!rx_buf || !tx_buf) {
                ts_err("%s:buf request memory fail,trans_data.size = %d",
                        __func__,trans_data.size);
                goto exit;
        }

        /* copy hal tx buf to driver tx buf */
        r = copy_from_user(tx_buf, trans_data.tx, trans_data.size);
        if (r) {
                ts_err("%s:copy in buff fail", __func__);
                goto exit;
        }

        /* spi transfer */
        r =  goodix_thp_spi_trans(cd, tx_buf, rx_buf, trans_data.size);
        if (r) {
                ts_err("%s: transfer error, ret = %d", __func__, r);
                goto exit;
        }

        /* copy driver rx to hal */
        if (trans_data.rx) {
                r = copy_to_user(trans_data.rx, rx_buf, trans_data.size);
                if (r) {
                        ts_err("%s:copy out buff fail", __func__);
                        goto exit;
                }
        }

exit:
        if(rx_buf){
                kfree(rx_buf);
                rx_buf = NULL;
        }
        if(tx_buf){
                kfree(tx_buf);
                tx_buf = NULL;
        }
        return r;
}

static long goodix_thp_ioctl_notify_update(void __user *data)
{
        struct goodix_thp_board_data *board_data =
                        &gdix_thp_core->ts_dev->board_data;
        struct thp_ioctl_update_info update_info;

        if (copy_from_user((u8 *)&update_info, data,
                        sizeof(update_info))) {
                ts_err("Failed to copy_from_user().");
                return -EFAULT;
        }

        board_data->frame_addr = update_info.frame_addr;
        board_data->cmd_addr = update_info.cmd_addr;
        board_data->ges_addr = update_info.ges_addr;
        ts_info("set frame addr:0x%04X cmd addr:0x%04X ges addr:0x%04X",
                board_data->frame_addr,
                board_data->cmd_addr,
                board_data->ges_addr);
        return 0;
}

static long goodix_thp_ioctl_set_wait_mode(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        unsigned int wait_frame_mode = arg;

        mutex_lock(&(ts->frame_mutex));
        if (wait_frame_mode)
                ts->get_frame_wait_mode = GET_FRAME_BLOCK_MODE;
        else
                ts->get_frame_wait_mode = GET_FRAME_NONBLOCK_MODE;
        ts->frame_waitq_state = WAKEUP_STATE;
        wake_up_interruptible(&(ts->frame_wq));
        mutex_unlock(&(ts->frame_mutex));
        ts_info("set block %d", wait_frame_mode);
        return 0;
}

static long goodix_thp_ioctl_irq_enable(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        unsigned int irq_flag = (unsigned int)arg;
        goodix_thp_set_irq_enable(ts, irq_flag);
        return 0;
}

static long goodix_thp_ioctl_get_frame_buf_num(unsigned long arg)
{
        return 0;
}

static long goodix_thp_ioctl_reset_frame_list(void)
{
        struct goodix_thp_core *ts = gdix_thp_core;

        ts_info("%s called", __func__);
        goodix_thp_reset_frame_list(ts);
        return 0;
}

static long goodix_thp_ioctl_get_driver_state(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        u32 __user *driver_state = (u32 *)arg;
        int suspend_status;

        suspend_status = atomic_read(&ts->suspended);
        ts_info("%s:driver state = %d", __func__, suspend_status);

        if (driver_state == NULL) {
                ts_err("%s: input parameter null", __func__);
                return -EINVAL;
        }

        if(copy_to_user(driver_state, &suspend_status, sizeof(u32))) {
                ts_err("%s:copy driver_state failed", __func__);
                return -EFAULT;
        }

        return 0;
}

static long goodix_thp_ioctl_get_state_change_flag(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        u32 __user *change_flag = (u32 *)arg;

        //ts_info("%s:state_change_flag = %d", __func__, ts->state_change_flag);

        if (change_flag == NULL) {
                ts_err("%s: input parameter null", __func__);
                return -EINVAL;
        }

        if(copy_to_user(change_flag, &ts->state_change_flag, sizeof(u32))) {
                ts_err("%s:copy state_change_flag failed", __func__);
                return -EFAULT;
        }

        return 0;
}

static long goodix_thp_ioctl_set_state_change_flag(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        unsigned int change_flag = arg;

        //ts_info("set state_change_flag = %d", change_flag);

        ts->state_change_flag = change_flag;

        return 0;
}

static int goodix_thp_ioctl_set_spi_speed(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        struct thp_ts_device *dev = ts->ts_dev;
        unsigned int speed = arg;

        //ts_info("set spi_speed = %d", speed);

        if (goodix_thp_set_spi_speed(dev, speed))
                return -EINVAL;

        return 0;
}

static long goodix_thp_ioctl_multi_spi_trans(void __user *data)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        struct thp_ts_device *dev = ts->ts_dev;
        struct spi_device *spi = dev->spi_dev;
        struct thp_ioctl_multi_spi_trans_data multi_data;
        struct thp_ioctl_spi_xfer_data * xfer_data = NULL;
        struct spi_transfer * xfer = NULL;
        struct spi_message msg;
        u8 *tx_buf = NULL;
        u8 *rx_buf = NULL;
        int r = 0, i = 0;
        u32 spi_speed_backup = 0;
        unsigned int tmp_len = 0;

        spi_speed_backup = dev->board_data.spi_setting.spi_max_speed;

        if (copy_from_user(&multi_data, data, sizeof(struct thp_ioctl_multi_spi_trans_data))) {
                return -EFAULT;
        }

        xfer_data =  kzalloc(multi_data.xfer_num * sizeof(*xfer_data), GFP_KERNEL);
        if (!xfer_data) {
                ts_info("failed alloc memory for xfer_data");
                goto exit;
        }

        xfer =  kzalloc(multi_data.xfer_num * sizeof(*xfer), GFP_KERNEL);
        if (!xfer) {
                ts_info("failed alloc memory for xfer");
                goto exit;
        }

        if (copy_from_user(xfer_data, multi_data.xfer_data,
                sizeof(struct thp_ioctl_spi_xfer_data) * multi_data.xfer_num)) {
                ts_info("failed copy from user for xfer_data");
                goto exit;
        }

        rx_buf = kzalloc(5120, GFP_KERNEL);
        if (!rx_buf) {
                ts_info("failed alloc buffer for rx_buf");
                goto exit;
        }
        tx_buf = kzalloc(5120, GFP_KERNEL);
        if (!tx_buf) {
                ts_info("failed alloc buffer for tx_buf");
                goto exit;
        }

        spi_message_init(&msg);
        for(i = 0; i < multi_data.xfer_num; i++) {
                if(xfer_data[i].tx){
                        r = copy_from_user(tx_buf + tmp_len,
                                xfer_data[i].tx, xfer_data[i].len);
                        if (r) {
                                ts_info("failed copy from user:%d", r);
                                goto exit;
                        }
                }
                xfer[i].tx_buf = tx_buf + tmp_len;
                xfer[i].rx_buf = rx_buf + tmp_len;
                xfer[i].len = xfer_data[i].len;
                xfer[i].cs_change = !!xfer_data[i].cs_change;
                spi_message_add_tail(&xfer[i], &msg);
                tmp_len += xfer_data[i].len;
        }

        if (multi_data.speed_hz == GOODIX_SPI_SPEED_WAKEUP) {
                mutex_lock(&dev->spi_mutex);
                goodix_thp_set_spi_speed(dev, GOODIX_SPI_SPEED_WAKEUP);
                spi_sync(spi, &msg);
                goodix_thp_set_spi_speed(dev, spi_speed_backup);
                mutex_unlock(&dev->spi_mutex);
        } else {
                mutex_lock(&dev->spi_mutex);
                r = spi_sync(spi, &msg);
                mutex_unlock(&dev->spi_mutex);
                if(r) {
                        ts_info("failed do spi sync:%d", r);
                        goto exit;
                }
                tmp_len = 0;
                for(i = 0; i < multi_data.xfer_num; i++ ){
                        if (xfer_data[i].rx) {
                                r = copy_to_user(xfer_data[i].rx, rx_buf + tmp_len,
                                        xfer_data[i].len);
                                tmp_len += xfer_data[i].len;
                        } else {
                                tmp_len += xfer_data[i].len;
                        }
                }
        }
exit:
        kfree(tx_buf);
        kfree(rx_buf);
        kfree(xfer_data);
        kfree(xfer);
        return r;
}

static long goodix_thp_ioctl_enter_suspend(unsigned long arg)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        int r = 0;

        cd->gesture_enable = arg;
        ts_info("%s: called", __func__);

        r = goodix_thp_suspend(cd);
        if (r)
                ts_err("%s failed, r %d", __func__, r);

        return r;
}

static long goodix_thp_ioctl_enter_resume(void)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        int r = 0;

        ts_info("%s: called", __func__);

        r = goodix_thp_resume(cd);
        if (r)
                ts_err("%s failed, r %d", __func__, r);

        return r;
}

static long goodix_thp_ioctl_recv_tsc_msg(unsigned long arg)
{
        void __user *argp = (void __user *)arg;
        struct thp_ioctl_tsc_msg tsc_msg;
        u8 ble_mac[6];
        u8 stylus_id[2];

        if (copy_from_user(&tsc_msg, argp,
                        sizeof(struct thp_ioctl_tsc_msg))) {
                ts_err("Failed to copy_from_user .");
                return -EFAULT;
        }

        switch (tsc_msg.cmd) {
        case SVC_CMD_MMAP_DEQUEUE:
                mutex_lock(&gdix_thp_core->frame_mutex);
                if (gdix_thp_core->frame_mmap_list.head != gdix_thp_core->frame_mmap_list.tail) {
                        gdix_thp_core->frame_mmap_list.head =
                                (gdix_thp_core->frame_mmap_list.head + 1) % GOODIX_THP_MAX_FRAME_BUF_COUNT;
                }
                mutex_unlock(&gdix_thp_core->frame_mutex);
                break;
        case SVC_CMD_BLE_MAC:
                memcpy(ble_mac, &tsc_msg.value[0], sizeof(ble_mac));
                memcpy(stylus_id, &tsc_msg.value[6], sizeof(stylus_id));
                ts_info("recv ble mac:%*ph, stylusID:%*ph", 6, ble_mac, 2, stylus_id);
                break;
        case SVC_CMD_GAME_FILTER:
                ts_info("recv game filter:%*ph", tsc_msg.len, tsc_msg.value);
                break;
        case SVC_CMD_UPDATE_VERSION:
                memcpy(gdix_thp_core->ts_dev->board_data.thp_ver, tsc_msg.value, tsc_msg.len);
                ts_info("thp_ver:%s", tsc_msg.value);
                break;
        default:
                ts_err("not support svc msg:0x%02x", tsc_msg.cmd);
                hal_is_prepared = true;
                break;
        }

        return 0;
}

static long goodix_thp_ioctl_get_chip_type(unsigned long arg)
{
        struct goodix_thp_core *ts = gdix_thp_core;
        u32 __user *user_val = (u32 *)arg;

        if (user_val == NULL) {
                ts_err("input parameter null");
                return -EINVAL;
        }

        if(copy_to_user(user_val, &ts->ts_dev->board_data.chip_type, sizeof(u32))) {
                ts_err("copy driver_state failed");
                return -EFAULT;
        }

        return 0;
}

/* enable or disable tsd debug socket */
static int goodix_thp_ioctl_set_tsd_state(int tsd_enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2];

        /* resume: touch power on is after display to avoid display disturb */
        ts_info("%s IN, set tsd state %d", __func__, tsd_enable);

        val[0] = NOTIFY_TYPE_TSD_CTRL;
        val[1] = tsd_enable;
        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
	return 0;
}

/* enable or disable tsd debug socket */
static int goodix_thp_ioctl_set_stylus_state(int stylus_enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2];

        /* resume: touch power on is after display to avoid display disturb */
        ts_info("%s IN, set stylus state %d", __func__, stylus_enable);

        val[0] = NOTIFY_TYPE_STYLUS_CTRL;
        val[1] = stylus_enable;
        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
	return 0;
}

/* charge state */
int goodix_thp_ioctl_set_charge_state(int charege_enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2];

        ts_info("%s IN, set charge state %d", __func__, charege_enable);

        val[0] = NOTIFY_TYPE_CHARGE;
        val[1] = charege_enable;
        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
	return 0;
}

/* onekey state */
static int goodix_thp_ioctl_set_onekey_state(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        ts_info("%s IN, set one_key state %d", __func__, enable ? 0 : 1);
        temp_cmd[0] = 0x00;
        temp_cmd[1] = 0x00;
        temp_cmd[2] = 0x05;
        temp_cmd[3] = 0xC3;

        if (enable) {
                temp_cmd[4] = 0x01;
        } else {
                temp_cmd[4] = 0x00;
        }

        for (i = 0; i < 5; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[5] = (u8)checksum;
        temp_cmd[6] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 7);
	return 0;
}

/* display_rotation */
int zte_set_display_rotation(int mrotation)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int level = cd->ztec.rotation_limit_level;
        int i;

        ts_info("%s IN, set display_rotation state %d", __func__, mrotation);
        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x06;
	temp_cmd[3] = 0x17;

	switch (mrotation) {
		case mRotatin_0:
                        temp_cmd[4] = 0x00;
                        temp_cmd[5] = 0x00;
			break;
		case mRotatin_90:
			if (level == rotation_limit_level_0) {
                                temp_cmd[4] = 0x40;
                                temp_cmd[5] = 0x00;
				ts_info("success in rotation_limit_level_0");
			} else if (level == rotation_limit_level_1) {
                                temp_cmd[4] = 0x40;
                                temp_cmd[5] = 0x40;
				ts_info("success in rotation_limit_level_1");
			} else if (level == rotation_limit_level_2) {
                                temp_cmd[4] = 0x40;
                                temp_cmd[5] = 0x80;
				ts_info("success in rotation_limit_level_2");
			} else if (level == rotation_limit_level_3) {
                                temp_cmd[4] = 0x40;
                                temp_cmd[5] = 0xc0;
				ts_info("success in rotation_limit_level_3");
			} else {
				ts_err("level is %d error!", level);
			}
			break;
		case mRotatin_270:
			if (level == rotation_limit_level_0) {
                                temp_cmd[4] = 0x80;
                                temp_cmd[5] = 0x00;
				ts_info("success in rotation_limit_level_0");
			} else if (level == rotation_limit_level_1) {
                                temp_cmd[4] = 0x80;
                                temp_cmd[5] = 0x40;
				ts_info("success in rotation_limit_level_1");
			} else if (level == rotation_limit_level_2) {
                                temp_cmd[4] = 0x80;
                                temp_cmd[5] = 0x80;
				ts_info("success in rotation_limit_level_2");
			} else if (level == rotation_limit_level_3) {
                                temp_cmd[4] = 0x80;
                                temp_cmd[5] = 0xc0;
				ts_info("success in rotation_limit_level_3");
			} else {
				ts_err("level is %d error!", level);
			}
			break;
		default:
			ts_err("mrotation is %d error!", mrotation);
			break;
	}
        for (i = 0; i < 6; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[6] = (u8)checksum;
        temp_cmd[7] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);
	return 0;
}

/* game sensibility */
int zte_sensibility_level(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        ts_info("%s IN, set sensibility state %d", __func__, enable);
        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x06;
	temp_cmd[3] = 0x27;

	switch (enable) {
	case sensibility_level_0:
		ts_info("%s success in sensibility_level_0", __func__);
		temp_cmd[4] = 0x00;
		temp_cmd[5] = 0x00;
		break;
	case sensibility_level_1:
		ts_info("%s success in sensibility_level_1", __func__);
		temp_cmd[4] = 0x01;
		temp_cmd[5] = 0x00;
		break;
	case sensibility_level_2:
		ts_info("%s success in sensibility_level_2", __func__);
		temp_cmd[4] = 0x02;
		temp_cmd[5] = 0x00;
		break;
	case sensibility_level_3:
		ts_info("%s success in sensibility_level_3", __func__);
		temp_cmd[4] = 0x03;
		temp_cmd[5] = 0x00;
		break;
	case sensibility_level_4:
		ts_info("%s success in sensibility_level_4", __func__);
		temp_cmd[4] = 0x04;
		temp_cmd[5] = 0x00;
		break;
	default:
		ts_err("%s: enable not support", __func__);
		return 0;
	}

        for (i = 0; i < 6; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[6] = (u8)checksum;
        temp_cmd[7] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

	return 0;
}

/* game follow_hand */
int zte_follow_hand_level(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        ts_info("%s IN, set follow_hand state %d", __func__, enable);
        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x06;
	temp_cmd[3] = 0x28;

	switch (enable) {
	case follow_hand_level_0:
		ts_info("%s success in follow_hand_level_0", __func__);
		temp_cmd[4] = 0x00;
		temp_cmd[5] = 0x00;
		break;
	case follow_hand_level_1:
		ts_info("%s success in follow_hand_level_1", __func__);
		temp_cmd[4] = 0x01;
		temp_cmd[5] = 0x00;
		break;
	case follow_hand_level_2:
		ts_info("%s success in follow_hand_level_2", __func__);
		temp_cmd[4] = 0x02;
		temp_cmd[5] = 0x00;
		break;
	case follow_hand_level_3:
		ts_info("%s success in follow_hand_level_3", __func__);
		temp_cmd[4] = 0x03;
		temp_cmd[5] = 0x00;
		break;
	case follow_hand_level_4:
		ts_info("%s success in follow_hand_level_4", __func__);
		temp_cmd[4] = 0x04;
		temp_cmd[5] = 0x00;
		break;
	default:
		ts_err("%s: enable not support", __func__);
		return 0;
	}

        for (i = 0; i < 6; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[6] = (u8)checksum;
        temp_cmd[7] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

	return 0;
}

/* game stability */
int zte_stability_level(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        ts_info("%s IN, set stability state %d", __func__, enable);
        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x06;
	temp_cmd[3] = 0x29;

	switch (enable) {
	case stability_level_0:
		ts_info("%s success in stability_level_0", __func__);
		temp_cmd[4] = 0x00;
		temp_cmd[5] = 0x00;
		break;
	case stability_level_1:
		ts_info("%s success in stability_level_1", __func__);
		temp_cmd[4] = 0x01;
		temp_cmd[5] = 0x00;
		break;
	case stability_level_2:
		ts_info("%s success in stability_level_2", __func__);
		temp_cmd[4] = 0x02;
		temp_cmd[5] = 0x00;
		break;
	case stability_level_3:
		ts_info("%s success in stability_level_3", __func__);
		temp_cmd[4] = 0x03;
		temp_cmd[5] = 0x00;
		break;
	case stability_level_4:
		ts_info("%s success in stability_level_4", __func__);
		temp_cmd[4] = 0x04;
		temp_cmd[5] = 0x00;
		break;
	default:
		ts_err("%s: enable not support", __func__);
		return 0;
	}

        for (i = 0; i < 6; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[6] = (u8)checksum;
        temp_cmd[7] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

	return 0;
}

/* report_rate start*/
int report_rate_120HZ(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x05;
	temp_cmd[3] = 0x9d;

	if (enable == 1) {
	        ts_info("%s success in 120Hz", __func__); //temp NX789 actually 180HZ
		temp_cmd[4] = 0x00;
	}

        for (i = 0; i < 5; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[5] = (u8)checksum;
        temp_cmd[6] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 7);

	return 0;
}

int report_rate_240HZ(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x05;
	temp_cmd[3] = 0x9d;

	if (enable == 1) {
	        ts_info("%s success in 240Hz", __func__);
		temp_cmd[4] = 0x01;
	}

        for (i = 0; i < 5; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[5] = (u8)checksum;
        temp_cmd[6] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 7);

	return 0;
}

int report_rate_480HZ(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x05;
	temp_cmd[3] = 0x9d;

	if (enable == 1) {
		ts_info("%s success in 480Hz", __func__);
		temp_cmd[4] = 0x02;
                temp_cmd[5] = 0x00;
	}
        for (i = 0; i < 6; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[6] = (u8)checksum;
        temp_cmd[7] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

	return 0;
}
int report_interpolate_mode(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x06;
	temp_cmd[3] = 0xC1;

	if (enable == 1) {
		ts_info("%s enter success", __func__);
		temp_cmd[4] = 0x01;
                temp_cmd[5] = 0x00;
	} else {
		ts_info("%s exit success", __func__);
		temp_cmd[4] = 0x00;
                temp_cmd[5] = 0x00;
	}
        for (i = 0; i < 6; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[6] = (u8)checksum;
        temp_cmd[7] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

	return 0;
}

int zte_tp_set_report_rate(int enable)
{
        static int current_report_mode = tp_freq_240Hz;
        int ret = 0;

        ts_info("%s IN, set report_rate state %d", __func__, enable);

	switch (enable) {
	case tp_freq_120Hz:
		if (current_report_mode == tp_freq_960Hz || current_report_mode == tp_freq_480Hz) {
			ret = report_interpolate_mode(0);
			if (ret < 0)
			        return ret;
		}
		ret = report_rate_120HZ(1);
		if (ret < 0)
		        return ret;
		current_report_mode = tp_freq_120Hz;
		break;
	case tp_freq_240Hz:
                if (current_report_mode == tp_freq_960Hz || current_report_mode == tp_freq_480Hz) {
                        ret = report_interpolate_mode(0);
                        if (ret < 0)
			         return ret;
                }
		ret = report_rate_240HZ(1);
		if (ret < 0)
		        return ret;
                current_report_mode = tp_freq_240Hz;
		break;
	case tp_freq_480Hz:
		if (current_report_mode == tp_freq_960Hz) {
			ret = report_interpolate_mode(0);
			if (ret < 0)
				return ret;
		}
		ret = report_rate_240HZ(1);
		if (ret < 0)
		         return ret;
		ret = report_interpolate_mode(1);
		if (ret < 0)
			return ret;
		current_report_mode = tp_freq_480Hz;
		break;
	case tp_freq_960Hz:
		if (current_report_mode == tp_freq_480Hz) {
			ret = report_interpolate_mode(0);
			if (ret < 0)
				return ret;
		}
		ret = report_rate_480HZ(1);
		if (ret < 0)
			return ret;
		ret = report_interpolate_mode(1);
		if (ret < 0)
			return ret;
		current_report_mode = tp_freq_960Hz;
		break;
	default:
		ts_err("%s: enable not support", __func__);
		return 0;
	}

	return 0;
}
/* report_rate end*/

/* game mode */
int zte_play_game(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int ret = 0;
        int i;

        ts_info("%s IN, set game state %d", __func__, enable);
        temp_cmd[0] = 0x00;
	temp_cmd[1] = 0x00;
	temp_cmd[2] = 0x06;
	temp_cmd[3] = 0xC2;

	if (enable) {
		temp_cmd[4] = 0x01;
                temp_cmd[5] = 0x00;
                for (i = 0; i < 6; i++) {
                        checksum += temp_cmd[i];
                }
                temp_cmd[6] = (u8)checksum;
                temp_cmd[7] = (u8)(checksum >> 8);
                put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

		ret = zte_sensibility_level(2);
		if (ret < 0)
			return ret;
		ret = zte_follow_hand_level(2);
		if (ret < 0)
			return ret;
		ret = zte_tp_set_report_rate(cd->ztec.tp_report_rate);
		if (ret < 0)
			return ret;
		ret = zte_stability_level(2);
		if (ret < 0)
			return ret;

		ts_info("send enter game cmd success");
	} else {
		temp_cmd[4] = 0x00;
                temp_cmd[5] = 0x00;
                for (i = 0; i < 6; i++) {
                        checksum += temp_cmd[i];
                }
                temp_cmd[6] = (u8)checksum;
                temp_cmd[7] = (u8)(checksum >> 8);
                put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 8);

		ret = zte_sensibility_level(2);
		if (ret < 0)
			return ret;
		ret = zte_follow_hand_level(2);
		if (ret < 0)
			return ret;
		ret = zte_tp_set_report_rate(1);
		if (ret < 0)
			return ret;
		ret = zte_stability_level(2);
		if (ret < 0)
			return ret;
		cd->ztec.rotation_limit_level = 2;

		ts_info("send exit game cmd success");
	}

	return 0;
}

int goodix_thp_save_debug(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2];

        ts_info("%s IN, default open", __func__);
        cd->logtofile_on = enable;
        val[0] = NOTIFY_TYPE_LOGTOFILE;
        val[1] = enable;

        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, 2);
        return 0;
}

int goodix_thp_reset_basline(int enable)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 temp_cmd[16];
        u16 checksum = 0;
        int i;

        ts_info("%s IN", __func__);
        temp_cmd[0] = 0x00;
        temp_cmd[1] = 0x00;
        temp_cmd[2] = 0x05;
        temp_cmd[3] = 0x90;

        if (enable) {
                temp_cmd[4] = 0x01;
        } else {
                temp_cmd[4] = 0x00;
        }

        for (i = 0; i < 5; i++) {
                checksum += temp_cmd[i];
        }
        temp_cmd[5] = (u8)checksum;
        temp_cmd[6] = (u8)(checksum >> 8);
        put_frame_list(cd, REQUEST_TYPE_CMD, temp_cmd, 7);
	return 0;
}

int goodix_thp_save_dump_log(void) /*for debug ghost point*/
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[1] = {NOTIFY_TYPE_DUMP_REP};

        ts_info("%s IN, default open", __func__);
        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, 1);

        return 0;
}

int recovery_game_mode_after_reset(void)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        int ret = 0;

        if(cd->ztec.is_play_game) {
                ret = zte_play_game(cd->ztec.is_play_game);
                if (ret < 0)
                        return ret;

                ret = zte_sensibility_level(cd->ztec.sensibility_level);
                if (ret < 0)
                        return ret;

                ret = zte_follow_hand_level(cd->ztec.follow_hand_level);
                if (ret < 0)
                        return ret;

                ret = zte_stability_level(cd->ztec.stability_level);
                if (ret < 0)
                        return ret;
                ret = zte_set_display_rotation(cd->ztec.display_rotation);
                if (ret < 0)
                        return ret;

                ts_info("recovery game mode success");
        } else {
                ts_info("no need to recovery game mode");
        }
        return 0;
}

static long goodix_thp_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg)
{
        long ret;

        switch (cmd) {
        case IOCTL_CMD_GET_FRAME:
                ret = goodix_thp_ioctl_get_frame(arg);
                break;
        case IOCTL_CMD_SET_RESET_VALUE:
                ret = goodix_thp_ioctl_set_reset_value(arg);
                break;
        case IOCTL_CMD_SET_WAIT_TIME:
                ret = goodix_thp_ioctl_set_wait_time(arg);
                break;
        case IOCTL_CMD_SPI_TRANS:
                ret = goodix_thp_ioctl_spi_trans((void __user *)arg);
                break;
        case IOCTL_CMD_NOTIFY_UPDATE:
                ret = goodix_thp_ioctl_notify_update((void __user *)arg);
                break;
        case IOCTL_CMD_SET_WAIT_MODE:
                ret = goodix_thp_ioctl_set_wait_mode(arg);
                break;
        case IOCTL_CMD_IRQ_ENABLE:
                ret = goodix_thp_ioctl_irq_enable(arg);
                break;
        case IOCTL_CMD_GET_FRAME_BUF_NUM:
                ret = goodix_thp_ioctl_get_frame_buf_num(arg);
                break;
        case IOCTL_CMD_RESET_FRAME_LIST:
                ret = goodix_thp_ioctl_reset_frame_list();
                break;
        case IOCTL_CMD_GET_DRIVER_STATE:
                ret = goodix_thp_ioctl_get_driver_state(arg);
                break;
        case IOCTL_CMD_GET_STATE_CHANGE_FLAG:
                ret = goodix_thp_ioctl_get_state_change_flag(arg);
                break;
        case IOCTL_CMD_SET_STATE_CHANGE_FLAG:
                ret = goodix_thp_ioctl_set_state_change_flag(arg);
                break;
        case IOCTL_CMD_SET_SPI_SPEED:
                ret = goodix_thp_ioctl_set_spi_speed(arg);
                break;
        case IOCTL_CMD_MUILT_SPI_TRANS:
                ret = goodix_thp_ioctl_multi_spi_trans((void __user *)arg);
                break;
        case IOCTL_CMD_ENTER_SUSPEND:
                ret = goodix_thp_ioctl_enter_suspend(arg);
                break;
        case IOCTL_CMD_ENTER_RESUME:
                ret = goodix_thp_ioctl_enter_resume();
                break;
        case IOCTL_CMD_RECV_TSC_MSG:
                ret = goodix_thp_ioctl_recv_tsc_msg(arg);
                break;
        case IOCTL_CMD_GET_CHIP_TYPE:
                ret = goodix_thp_ioctl_get_chip_type(arg);
                break;
        case IOCTL_CMD_SET_TOOL_OPS:
                ret = goodix_thp_ioctl_set_tsd_state(arg);
                break;
        default:
                ts_err("cmd unknown.");
                ret = 0;
        }

        return ret;
}

static int goodix_thp_mmap(struct file *filp, struct vm_area_struct *vma)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        void *sh_mem = (void *)cd->frame_mmap_list.buf;
        size_t size = vma->vm_end - vma->vm_start;
        struct page *page = NULL;

        if (size > MMAP_BUFFER_SIZE) {
                ts_err("vm_size[%d] > mmap_size[%d]",
                        (int)size, MMAP_BUFFER_SIZE);
                return -EINVAL;
        }

        page = virt_to_page((unsigned long)sh_mem + (vma->vm_pgoff << PAGE_SHIFT));
        if (remap_pfn_range(vma, vma->vm_start, page_to_pfn(page),
                        size, vma->vm_page_prot)) {
                return -EAGAIN;
        }

        return 0;
}

static const struct file_operations g_thp_fops = {
        .owner = THIS_MODULE,
        .open = goodix_thp_open,
        .release = goodix_thp_release,
        .unlocked_ioctl = goodix_thp_ioctl,
        .mmap = goodix_thp_mmap,
};

static struct miscdevice g_thp_misc_device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = GOODIX_THP_MISC_DEVICE_NAME,
        .fops = &g_thp_fops,
};

/**
 * goodix_thp_power_init- Get regulator for touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_thp_power_init(struct goodix_thp_core *core_data)
{
        struct goodix_thp_board_data *ts_bdata;
        struct device *dev = NULL;
        int ret = 0;

        ts_info("Power init");
        /* dev:i2c client device or spi slave device*/
        dev =  core_data->ts_dev->dev;
        ts_bdata = board_data(core_data);

	/*core_data->iovdd_enable_gpio = ts_bdata->iovdd_enable_gpio;
	ret = devm_gpio_request_one(dev, core_data->iovdd_enable_gpio,
						GPIOF_OUT_INIT_LOW, "TP_IOVDD_GPIO");
	if (ret) {
		ts_err("failed to request iovdd_enable_gpio");
		return ret;
	}*/

	/*if (strlen(ts_bdata->iovdd_name)) {
		core_data->vdd = devm_regulator_get(dev,
				 ts_bdata->iovdd_name);
		if (IS_ERR_OR_NULL(core_data->vdd)) {
			ret = PTR_ERR(core_data->vdd);
			ts_err("Failed to get regulator vdd:%d", ret);
			core_data->vdd = NULL;
		}
		if (ret < 0) {
			ts_err("set vdd voltage failed");
			return ret;
		}
	} else {
		ts_info("vdd name is NULL");
	}*/

	if (strlen(ts_bdata->avdd_name)) {
		core_data->avdd = devm_regulator_get(dev,
				 ts_bdata->avdd_name);
		if (IS_ERR_OR_NULL(core_data->avdd)) {
			ret = PTR_ERR(core_data->avdd);
			ts_err("Failed to get regulator avdd:%d", ret);
			core_data->avdd = NULL;
		}
		if (ret < 0) {
			ts_err("set avdd voltage failed");
			return ret;
		}
	} else {
		ts_info("avdd name is NULL");
	}

        if (core_data->avdd) {
                regulator_set_voltage(core_data->avdd, 
                        ts_bdata->avdd_voltage, ts_bdata->avdd_voltage);
        }

        return ret;
}

int goodix_thp_reset_after(struct goodix_thp_core *cd);

/**
 * goodix_thp_power_on- Turn on power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_thp_power_on(struct goodix_thp_core *core_data)
{
        struct goodix_thp_board_data *ts_bdata = board_data(core_data);
        int r;

        ts_info("Device power on");
        if (core_data->power_on) {
                ts_info("device has already power on");
                return 0;
        }

        /*r = regulator_enable(core_data->vdd);
        if (r) {
                ts_err("Failed to enable iovdd:%d", r);
                return r;
        }
        usleep_range(3000, 3100);*/

        if (core_data->avdd) {
            r = regulator_enable(core_data->avdd);
            if (r) {
                ts_err("Failed to enable avdd:%d", r);
                return r;
            }
            ts_info("regulator enable SUCCESS");
        }

        usleep_range(15000, 15100);
        gpio_direction_output(ts_bdata->reset_gpio, 1);
        if (ts_bdata->chip_type == CHIP_TYPE_9897) {
                r = goodix_thp_reset_after(core_data);
                if (r < 0) {
                        ts_err("reset_after process failed,r=%d", r);
                        goto power_off;
                }
        }

        core_data->power_on = 1;
        return 0;

power_off:
	gpio_direction_output(ts_bdata->reset_gpio, 0);
	/*regulator_disable(core_data->vdd);*/
	if (core_data->avdd) {
	        regulator_disable(core_data->avdd);
	}
        return r;
}

static void goodix_thp_power_off(struct goodix_thp_core *core_data)
{
        struct goodix_thp_board_data *ts_bdata = board_data(core_data);

        ts_info("Device power off");
        if (core_data->power_on == 0) {
                ts_info("device has already power off");
                return;
        }

	gpio_direction_output(ts_bdata->reset_gpio, 0);
	/*regulator_disable(core_data->vdd);*/
        if (core_data->avdd) {
                regulator_disable(core_data->avdd);
        }
        core_data->power_on = 0;
}

static int goodix_thp_gpio_setup(struct goodix_thp_core *core_data)
{
        struct goodix_thp_board_data *ts_bdata = board_data(core_data);
        int r = 0;

        ts_info("GPIO setup,reset-gpio:%d, irq-gpio:%d",
                ts_bdata->reset_gpio, ts_bdata->irq_gpio);

        /*
         * after kenerl3.13, gpio_ api is deprecated, new
         * driver should use gpiod_ api.
         */
        r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->reset_gpio,
                                  GPIOF_OUT_INIT_HIGH, "ts_reset_gpio");
        if (r < 0) {
                ts_err("Failed to request reset gpio, r:%d", r);
                return r;
        }

        r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->irq_gpio,
                                  GPIOF_IN, "ts_irq_gpio");
        if (r < 0) {
                ts_err("Failed to request irq gpio, r:%d", r);
                return r;
        }

        return 0;
}

static u16 zte_gesture_data(int status)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[3];
        u16 gsx_data;

	switch (status) {
	case (ZTE_GOODIX_DOU_TAP | ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_FP):
	case (ZTE_GOODIX_DOU_TAP | ZTE_GOODIX_FP):
		gsx_data = 0x0000;
                val[0] = NOTIFY_TYPE_GESTURE;
                val[1] = 0xff;
                val[2] = 0xff;
		ts_info("single_double_finger");
		break;
	case (ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_FP):
	case (ZTE_GOODIX_SIN_TAP):
	case (ZTE_GOODIX_FP):
                gsx_data = 0x0080;
                val[0] = NOTIFY_TYPE_GESTURE;
                val[1] = 0x00;
                val[2] = 0x80;
		ts_info("single_finger");
		break;
         case (ZTE_GOODIX_SIN_TAP | ZTE_GOODIX_DOU_TAP):
                gsx_data = 0x2000;
                val[0] = NOTIFY_TYPE_GESTURE;
                val[1] = 0x20;
                val[2] = 0x00;
		ts_info("single_double");
		break;
	case (ZTE_GOODIX_DOU_TAP):
                gsx_data = 0x3000;
                val[0] = NOTIFY_TYPE_GESTURE;
                val[1] = 0x30;
                val[2] = 0x00;
		ts_info("double");
		break;
	default:
		ts_err("status is %d error!", status);
		return 0;
	}

        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
        
	/*status = (cd->ztec.is_wakeup_gesture << 1) | cd->ztec.is_single_tap;
	ts_info("status is %d", status);*/
	return gsx_data;
}

static int goodix_thp_gesture_irq_handler(struct goodix_thp_core *core_data)
{
        int r = 0;
        int i;
        u8 ges_num = 0;
        u8 clean_data = 0;
        u8 temp_data[GESTURE_KEY_DATA_LEN] = {0};
        u32 ges_addr = core_data->ts_dev->board_data.ges_addr;
        //u16 gsx_data = ~core_data->gesture_enable;
        int coor_x, coor_y;
        struct thp_ts_device *ts_dev =  core_data->ts_dev;
        struct input_dev *input_dev = core_data->input_dev;
        int status;
        u8 ges_data[50] = {0};
        char buffer[256];
        int buffer_index = 0;

        status = (core_data->ztec.is_wakeup_gesture << 1) | core_data->ztec.is_single_tap;
        if (ges_addr == 0) {
                ts_err("gesture addr has not been assigned");
                goto exit;
        }

        /* get gesture data */
        r = ts_dev->hw_ops->read(ts_dev, ges_addr, temp_data, sizeof(temp_data));
        if (r < 0 || ((temp_data[0] & GESTURE_DATA_TYPE) == 0)) {
                ts_err("Read gesture data failed, r=%d, data[0]=0x%x",
                                r, temp_data[0]);
                goto re_send_ges_cmd;
        }

        r = ts_dev->hw_ops->read(ts_dev, 0x1033a, ges_data, sizeof(ges_data));

        for (i = 0; i < sizeof(ges_data); i++) {
                sprintf(buffer + buffer_index, "%d,", ges_data[i]);
                buffer_index += strlen(buffer + buffer_index);
        }
        buffer[buffer_index - 1] = '\0';
        ts_info("%s", buffer);

        /* check gesture data */
        if (checksum16_cmp(temp_data, GESTURE_DATA_HEAD_LEN, GOODIX_LE_MODE)) {
                ts_err("gesture data head check failed");
                ts_err("%*ph", GESTURE_DATA_HEAD_LEN, temp_data);
                goto re_send_ges_cmd;
        } else if (checksum16_cmp(&temp_data[GESTURE_DATA_HEAD_LEN],
                GESTURE_KEY_DATA_LEN - GESTURE_DATA_HEAD_LEN, GOODIX_LE_MODE)) {
                ts_err("Gesture data checksum error!");
                ts_info("Gesture data %*ph", (int)sizeof(temp_data), temp_data);
                goto re_send_ges_cmd;
        }

        /* save gesture data */
        memcpy(gdix_thp_core->gesture_data, temp_data, sizeof(temp_data));

        switch (temp_data[4]) {
        case 0xCC: //double tap
                ts_info("get gesture event: Double tap");
                ufp_report_gesture_uevent(DOUBLE_TAP_GESTURE);
                // ges_num = 1;
                break;
        case 0x63: // C
                ts_info("get gesture event: C");
		ges_num = 6;
                break;
        case 0x65: // E
                ts_info("get gesture event: E");
		ges_num = 6;
                break;
        case 0x6D: // M
                ts_info("get gesture event: M");
                break;
        case 0x77: // W
                ts_info("get gesture event: W");
		ges_num = 5;
                break;
        case 0x40: // A
                ts_info("get gesture event: A");
		ges_num = 6;
                break;
        case 0x66: // F
                ts_info("get gesture event: F");
		ges_num = 6;
                break;
        case 0x6F: // O
                ts_info("get gesture event: O");
		ges_num = 6;
                break;
        case 0xAA: // R2L
                ts_info("get gesture event: right to left");
                break;
        case 0xBB: // L2R
                ts_info("get gesture event: left to right");
                break;
        case 0xBA: // UP
                ts_info("get gesture event: up");
                ges_num = 2;
                break;
        case 0xAB: // DOWN
                ts_info("get gesture event: down");
                ges_num = 2;
                break;
        case 0x46: // FP_DOWN
        	core_data->ztec.finger_lock_flag = 0;
		ts_info("%s:finger_lock_flag = %d", __func__, core_data->ztec.finger_lock_flag);
                ts_info("get gesture event: finger print down");
		report_ufp_uevent(UFP_FP_DOWN);
                // goodix_thp_set_fp_int_pin(gdix_thp_core->ts_dev, 1);
                break;
        case 0x55: // FP_UP
                ts_info("get gesture event: finger print up");
		report_ufp_uevent(UFP_FP_UP);
                // goodix_thp_set_fp_int_pin(gdix_thp_core->ts_dev, 0);
            break;
        case 0x4C: // single tap
                ts_info("get gesture event: single tap");
		ufp_report_gesture_uevent(SINGLE_TAP_GESTURE);
                break;
        default:
                ts_err("not support gesture type %x", temp_data[4]);
                break;
        }

	for (i = 0; i < ges_num; i++) {
		coor_x = le16_to_cpup((__le16 *)&temp_data[8 + i * 4]);
		coor_y = le16_to_cpup((__le16 *)&temp_data[10 + i * 4]);
		ts_info("ges_coor_x:%d ges_coor_y:%d", coor_x, coor_y);
		input_mt_slot(input_dev, 0);
		input_mt_report_slot_state(input_dev, 0, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, coor_x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, coor_y);
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_sync(input_dev);
	}

	if (ges_num > 0) {
		input_mt_slot(input_dev, 0);
		input_mt_report_slot_state(input_dev, 0, 0);
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_sync(input_dev);
	}

        goto exit;

re_send_ges_cmd:
        /* resend gesture cmd */
        if(ts_dev->hw_ops->send_cmd(ts_dev, CMD_GESTURE, zte_gesture_data(status)))
                ts_info("warning: failed re_send gesture cmd");
exit:
        clean_data = 0;
        ts_dev->hw_ops->write(ts_dev, ges_addr, &clean_data, 1);
        return 0;
}

/**
 * goodix_thp_threadirq_func - Bottom half of interrupt
 * This functions is excuted in thread context,
 * sleep in this function is permit.
 *
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static irqreturn_t goodix_thp_threadirq_func(int irq, void *data)
{
        struct goodix_thp_core *core_data = data;
        struct thp_ts_device *ts_dev =  core_data->ts_dev;
        u8 *read_data = (u8 *)core_data->frame_read_data;
        int r;

#if IS_ENABLED(CONFIG_PM) && GTP_PATCH_COMERR_PM
	if (atomic_read(&core_data->suspended) && (core_data->pm_suspend)) {
		r = wait_for_completion_timeout(
					&core_data->pm_completion,
					msecs_to_jiffies(GTP_TIMEOUT_COMERR_PM));
		if (!r) {
			ts_err("Bus don't resume from pm(deep),timeout,skip irq");
			return IRQ_HANDLED;
		}
	}
#endif
        if (!hal_is_prepared) {
                ts_err("hal is not prepared,skip irq");
                return IRQ_HANDLED;
        }

        if (tpd_cdev->bbat_test_enter) {
                if (tpd_cdev->bbat_int_test == false) {
                        tpd_cdev->bbat_int_test = true;
                        complete(&tpd_cdev->bbat_test_completion);
                        ts_info("%s tpd int BBAT test success", __func__);
                }
                return IRQ_HANDLED;
        }

        if (core_data->reset_state) {
                ts_err("%s: ignore this irq.", __func__);
                return IRQ_HANDLED;
        }

        /* suspend irq handler */
        if (atomic_read(&core_data->suspended) && core_data->gesture_enable) {
                goodix_thp_gesture_irq_handler(core_data);
                return IRQ_HANDLED;
        }

        disable_irq_nosync(core_data->irq);

        /* get frame */
        r = ts_dev->hw_ops->get_frame(ts_dev, read_data);
        if (r < 0) {
                ts_err("failed to read frame, r %d", r);
                goto exit;
        }

        /* copy frame to frame list */
        put_frame_list(core_data, REQUEST_TYPE_FRAME, read_data, r);
exit:
        enable_irq(core_data->irq);
        return IRQ_HANDLED;
}

/**
 * goodix_thp_irq_setup- Requset interrput line from system
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_thp_irq_setup(struct goodix_thp_core *core_data)
{
        const struct goodix_thp_board_data *ts_bdata = board_data(core_data);
        int r;

        /* if ts_bdata-> irq is invalid */
        if (ts_bdata->irq <= 0)
                core_data->irq = gpio_to_irq(ts_bdata->irq_gpio);
        else
                core_data->irq = ts_bdata->irq;

        ts_info("IRQ:%u,flags:%d", core_data->irq, (int)ts_bdata->irq_flags);
        r = devm_request_threaded_irq(&core_data->pdev->dev,
                                      core_data->irq, NULL,
                                      goodix_thp_threadirq_func,
                                      ts_bdata->irq_flags | IRQF_ONESHOT,
                                      GOODIX_CORE_DRIVER_NAME,
                                      core_data);

        if (r < 0) {
                ts_err("Failed to requeset threaded irq:%d", r);
                return r;
        }

        mutex_lock(&core_data->irq_mutex);
        disable_irq(core_data->irq);
        core_data->irq_state = false;
        mutex_unlock(&core_data->irq_mutex);
        ts_info("%s: disable irq", __func__);

        return 0;
}

#if 0
static int goodix_thp_pen_input_dev_init(struct goodix_thp_core *core_data)
{
        struct input_dev *pen_dev = NULL;
        int r;

        /* alloc input_dev */
        pen_dev = input_allocate_device();
        if (!pen_dev) {
                ts_err("Failed to alloc suspend input dev");
                return -ENOMEM;
        }
        core_data->pen_dev = pen_dev;

        /* init input_dev */
        pen_dev->name = GOODIX_THP_STYLUS_INPUT_DEVICE_NAME;
        pen_dev->id.bustype = BUS_SPI;
        pen_dev->id.product = 0x0200;
        pen_dev->id.vendor = 0x27C6;
        pen_dev->id.version = 0x0001;

        /* set input_dev properties */
        set_bit(EV_SYN, pen_dev->evbit);
        set_bit(EV_KEY, pen_dev->evbit);
        set_bit(EV_ABS, pen_dev->evbit);
        set_bit(ABS_X, pen_dev->absbit);
		set_bit(ABS_Y, pen_dev->absbit);
		set_bit(ABS_TILT_X, pen_dev->absbit);
		set_bit(ABS_TILT_Y, pen_dev->absbit);
		set_bit(BTN_STYLUS, pen_dev->keybit);
		set_bit(BTN_STYLUS2, pen_dev->keybit);
		set_bit(BTN_TOUCH, pen_dev->keybit);
		set_bit(BTN_TOOL_PEN, pen_dev->keybit);
		set_bit(INPUT_PROP_DIRECT, pen_dev->propbit);

	input_set_abs_params(pen_dev, ABS_X, 0,
                        core_data->ts_dev->board_data.panel_max_x - 1, 0, 0);
	input_set_abs_params(pen_dev, ABS_Y, 0,
                        core_data->ts_dev->board_data.panel_max_y - 1, 0, 0);
	input_set_abs_params(pen_dev, ABS_PRESSURE, 0,
			core_data->ts_dev->board_data.panel_max_p - 1, 0, 0);
	input_set_abs_params(pen_dev, ABS_TILT_X,
			-GOODIX_PEN_MAX_TILT, GOODIX_PEN_MAX_TILT, 0, 0);
	input_set_abs_params(pen_dev, ABS_TILT_Y,
			-GOODIX_PEN_MAX_TILT, GOODIX_PEN_MAX_TILT, 0, 0);

        /* register input_dev */
        r = input_register_device(pen_dev);
        if (r) {
                ts_err("failed to register suspend input device");
                return r;
        }

        return 0;
}


void goodix_thp_pen_input_dev_exit(struct goodix_thp_core *core_data)
{
        input_unregister_device(core_data->pen_dev);
        input_free_device(core_data->pen_dev);
}
#endif

static void goodix_thp_force_release_all(void)
{
        struct input_dev *input_dev = gdix_thp_core->input_dev;
        /*struct input_dev *pen_dev = gdix_thp_core->pen_dev;*/
        int i;

        // release fingers
        for (i = 0; i < INPUT_AGENT_MAX_FINGERS; i++) {
                input_mt_slot(input_dev, i);
                input_mt_report_slot_state(input_dev, 0, 0);
                tpd_touch_release(input_dev, i);
#if defined(CONFIG_TOUCHSCREEN_UFP_MAC) && defined(ZTE_ONE_KEY)
                one_key_report(false, -1, -1, i);
#endif
        }
        report_ufp_uevent(UFP_FP_UP);
        input_report_key(input_dev, BTN_TOUCH, 0);
        input_report_key(input_dev, BTN_TOOL_FINGER, 0);
        input_sync(input_dev);

        //release stylus
        /*input_report_key(pen_dev, BTN_TOUCH, 0);
        input_report_key(pen_dev, BTN_TOOL_PEN, 0);
        input_sync(pen_dev);*/
}

static int large_area_uevent_count = 0;
static int large_area_ignore_count = 10;

static inline void __report_large_area_uevent(char *str)
{
        char *envp[2];

        if (!ufp_tp_ops.uevent_pdev) {
                ts_err("large uevent pdev is null!\n");
                return;
        }

        if (large_area_ignore_count >= 0) {
                large_area_ignore_count--;
                UFP_INFO("tp ignore large suppression area count = %d", large_area_ignore_count);
                return;
        }

        if (large_area_uevent_count >= 3) {
                return;
        }

        envp[0] = str;
        envp[1] = NULL;
        kobject_uevent_env(&(ufp_tp_ops.uevent_pdev->dev.kobj), KOBJ_CHANGE, envp);
        UFP_INFO("tp enter large suppression area");

        large_area_uevent_count++;
}

static long goodix_thp_input_agent_ioctl_set_coordinate(unsigned long arg)
{
        long ret = 0;
        void __user *argp = (void __user *)arg;
        struct input_dev *input_dev = gdix_thp_core->input_dev;
        /*struct input_dev *pen_dev = gdix_thp_core->pen_dev;*/
        struct thp_input_agent_ioctl_coor_data data;
        struct input_agent_coor_data *stylus_data = NULL;
        u8 i;

        if (arg == 0) {
                ts_err("%s:arg is null.", __func__);
                return -EINVAL;
        }

        /* copy data from hal */
        if (copy_from_user(&data, argp,
                        sizeof(struct thp_input_agent_ioctl_coor_data))) {
                ts_err("Failed to copy_from_user().");
                return -EFAULT;
        }

        if (data.touch[STYLUS_TRACK_ID].touch_valid == 1) {
                stylus_data = &data.touch[STYLUS_TRACK_ID];
                // release all fingers
                for (i = 0; i < INPUT_AGENT_MAX_FINGERS; i++) {
                        input_mt_slot(input_dev, i);
                        input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
                }
                input_report_key(input_dev, BTN_TOUCH, 0);
                input_report_key(input_dev, BTN_TOOL_FINGER, 0);
                input_sync(input_dev);
                // report stylus
                /*input_report_abs(pen_dev, ABS_X, stylus_data->x);
                input_report_abs(pen_dev, ABS_Y, stylus_data->y);
                input_report_abs(pen_dev, ABS_TILT_X, stylus_data->tilt_x);
                input_report_abs(pen_dev, ABS_TILT_Y, stylus_data->tilt_y);
                input_report_abs(pen_dev, ABS_PRESSURE, stylus_data->p);
                input_report_key(pen_dev, BTN_TOUCH, data.hover_stat ? 0 : 1);
                input_report_key(pen_dev, BTN_TOOL_PEN, 1);
                input_sync(pen_dev);*/
        } else {
                // release stylus
                /*input_report_key(pen_dev, BTN_TOUCH, 0);
                input_report_key(pen_dev, BTN_TOOL_PEN, 0);
                input_sync(pen_dev);*/
                // report fingers
                for (i = 0; i < INPUT_AGENT_MAX_FINGERS; i++) {
                        if (data.touch[i].touch_valid != 0) {
                                tpd_touch_press(input_dev, data.touch[i].x, data.touch[i].y, i, data.touch[i].major, 0);
#if defined(CONFIG_TOUCHSCREEN_UFP_MAC) && defined(ZTE_ONE_KEY)
                                one_key_report(true, data.touch[i].x, data.touch[i].y, i);
                        } else {
                                one_key_report(false, -1, -1, i);
#endif
                                tpd_touch_release(input_dev, i);
                        }
                }
                input_report_key(input_dev, BTN_TOUCH, (data.touch_num > 0) ? 1 : 0);
                input_report_key(input_dev, BTN_TOOL_FINGER, (data.touch_num > 0) ? 1 : 0);
                input_sync(input_dev);
        }
        /* large touch flag */
        if (data.large_touch_stat && (gdix_thp_core->ztec.is_palm_mode)) {
                __report_large_area_uevent(LAGRE_SUPPRESSION_AREA);
        } else {
                //TODO
        }

        /* fp touch flag */
        if (data.fp_mode) {
                report_ufp_uevent(UFP_FP_DOWN);
                //goodix_thp_set_fp_int_pin(gdix_thp_core->ts_dev, 1);
        } else {
                report_ufp_uevent(UFP_FP_UP);
                //goodix_thp_set_fp_int_pin(gdix_thp_core->ts_dev, 0);
        }

        return ret;
}

static int goodix_thp_input_agent_ioctl_read_status(unsigned long arg)
{
        return 0;
}

static int goodix_thp_input_agent_ioctl_get_custom_info(unsigned long arg)
{
        char __user *custom_info = (char *)arg;
        struct goodix_thp_core *cd = gdix_thp_core;

        if (!cd || !custom_info) {
                ts_err("%s:args error", __func__);
                return -EINVAL;
        }

        ts_info("%s:custom info:%s", __func__, cd->custom_info);

        if(copy_to_user(custom_info, cd->custom_info, sizeof(cd->custom_info))) {
                ts_err("%s:copy window_info failed", __func__);
                return -EFAULT;
        }

        return 0;
}

static long goodix_thp_input_agent_ioctl_set_events(unsigned long arg)
{
        long ret = 0;

        return ret;
}

int goodix_thp_input_agent_ioctl_get_events(unsigned long arg)
{
        return 0;
}

static int goodix_thp_input_agent_ioctl_get_driver_state(unsigned long arg)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u32 __user *driver_state = (u32 *)arg;
        int suspend_status;

        //ts_info("%s:driver state = %d", __func__, cd->suspended);

        if (driver_state == NULL) {
                ts_err("%s: input parameter null", __func__);
                return -EINVAL;
        }
        
        suspend_status = atomic_read(&cd->suspended);
        if(copy_to_user(driver_state, &suspend_status, sizeof(u32))) {
                ts_err("%s:copy driver_state failed", __func__);
                return -EFAULT;
        }

        return 0;
}

static int goodix_thp_input_agent_open(struct inode *inode, struct file *filp)
{
        return 0;
}

static int goodix_thp_input_agent_release(struct inode *inode,
                                                struct file *filp)
{
        return 0;
}

static long goodix_thp_input_agent_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg)
{
        long ret;

        switch (cmd) {
        case INPUT_AGENT_IOCTL_CMD_SET_COOR:
                ret = goodix_thp_input_agent_ioctl_set_coordinate(arg);
                break;
        case INPUT_AGENT_IOCTL_READ_STATUS:
                ret = goodix_thp_input_agent_ioctl_read_status(arg);
                break;
        case INPUT_AGENT_IOCTL_GET_CUSTOM_INFO:
                ret = goodix_thp_input_agent_ioctl_get_custom_info(arg);
                break;
        case INPUT_AGENT_IOCTL_CMD_SET_EVENTS:
                ret = goodix_thp_input_agent_ioctl_set_events(arg);
                break;
        case INPUT_AGENT_IOCTL_CMD_GET_EVENTS:
                ret = goodix_thp_input_agent_ioctl_get_events(arg);
                break;
        case INPUT_AGENT_IOCTL_GET_DRIVER_STATE:
                ret = goodix_thp_input_agent_ioctl_get_driver_state(arg);
                break;
        default:
                ts_err("cmd unkown.");
                ret = -EINVAL;
        }

        return ret;
}

static const struct file_operations g_thp_input_agent_fops = {
        .owner = THIS_MODULE,
        .open = goodix_thp_input_agent_open,
        .release = goodix_thp_input_agent_release,
        .unlocked_ioctl = goodix_thp_input_agent_ioctl,
};

static struct miscdevice g_thp_input_agent_misc_device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = DEVICE_NAME,
        .fops = &g_thp_input_agent_fops,
};

static int goodix_thp_input_agent_init(struct goodix_thp_core *core_data)
{
        struct input_dev *input_dev;
        int r;

        /* alloc input_dev */
        input_dev = input_allocate_device();
        if (!input_dev) {
                ts_err("%s:Unable to allocated input device", __func__);
                return	-ENODEV;
        }
        core_data->input_dev = input_dev;

        /* init input_dev */
        input_dev->name = GOODIX_THP_INPUT_DEVICE_NAME;
        input_dev->id.bustype = BUS_SPI;
        input_dev->id.product = 0x0201;
        input_dev->id.vendor = 0x27C6;
        input_dev->id.version = 0x0001;

        /* set input_dev properties */
        set_bit(EV_SYN, input_dev->evbit);
        set_bit(EV_KEY, input_dev->evbit);
        set_bit(EV_ABS, input_dev->evbit);
        set_bit(BTN_TOUCH, input_dev->keybit);
        set_bit(BTN_TOOL_FINGER, input_dev->keybit);
        set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

        input_set_abs_params(input_dev, ABS_MT_POSITION_X,
                        0, core_data->ts_dev->board_data.panel_max_x - 1, 0, 0);
        input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
                        0, core_data->ts_dev->board_data.panel_max_y - 1, 0, 0);
        input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
                        0, core_data->ts_dev->board_data.panel_max_w - 1, 0, 0);
        input_mt_init_slots(input_dev, INPUT_AGENT_MAX_FINGERS, INPUT_MT_DIRECT);

        // gesture
        input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
        input_set_capability(input_dev, EV_KEY, KEY_GOTO);

        /* register input_dev */
        r = input_register_device(input_dev);
        if (r) {
                ts_err("%s:failed to register input device", __func__);
                goto input_dev_reg_err;
        }

        r = misc_register(&g_thp_input_agent_misc_device);
        if (r) {
                ts_err("%s:failed to register misc device", __func__);
                goto misc_dev_reg_err;
        }

        return 0;

misc_dev_reg_err:
        input_unregister_device(input_dev);
input_dev_reg_err:
        return r;
}

static void goodix_thp_input_agent_exit(void)
{
        input_unregister_device(gdix_thp_core->input_dev);
        misc_deregister(&g_thp_input_agent_misc_device);
}

/* Description:switch scan_rate
 * @buf: 0/1/2/3/4 represent 300/240/180/120/60hz
 */
static ssize_t goodix_thp_scan_rate_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        struct goodix_thp_core *core_data = gdix_thp_core;
        struct thp_ts_device *tdev = core_data->ts_dev;
        int index = 0;

        if (sscanf(buf, "%d", &index) != 1)
                return -EINVAL;

        if (tdev->hw_ops->send_cmd(tdev, CMD_ACTIVE_SCAN_RATE, index))
                ts_err("goodix switch scan rate failed, index %d", index);

        return count;
}

/* Description: read driver version
 */
static ssize_t goodix_thp_driver_info_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{

        return snprintf(buf, PAGE_SIZE, "DriverVersion:%s\n",
                        GOODIX_THP_DRIVER_VERSION);

}

/* Description: debug read
 */
static ssize_t goodix_thp_debug_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        //struct goodix_thp_core *core_data = gdix_thp_core;
        size_t offset = 0;


        return offset;
}

/* Description: debug write
 */
static ssize_t goodix_thp_debug_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        //struct goodix_thp_core *core_data = gdix_thp_core;
        int value = 0;

        if (sscanf(buf, "%d", &value) != 1)
                return -EINVAL;

        return count;
}

static ssize_t goodix_thp_screen_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        size_t offset;
        int suspend_status;

        suspend_status = atomic_read(&cd->suspended);
        offset = sprintf(buf, "%s\n", suspend_status ? "off" : "on");
        return offset;
}

static ssize_t goodix_thp_screen_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2];

        val[0] = NOTIFY_TYPE_SCREEN;

        if (buf[0] == '0' || buf[0] == 0)
                val[1] = 0;
        else
                val[1] = 1;
        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
        return count;
}

static ssize_t goodix_thp_gesture_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[3];

        if (count < 2) {
                ts_err("invalid input param len:%zu", count);
                return count;
        }

        val[0] = NOTIFY_TYPE_GESTURE;
        val[1] = buf[0];
        val[2] = buf[1];
        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
        return count;
}

static ssize_t goodix_thp_tsd_ctrl_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        goodix_thp_ioctl_set_tsd_state(buf[0] != '0');
        return count;
}

static ssize_t goodix_thp_dump_rep_log_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[1] = {NOTIFY_TYPE_DUMP_REP};

        if (buf[0] == '1' || buf[0] == 1) {
                ts_info("dump rep log");
                put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, 1);
        }
        return count;
}

static ssize_t goodix_thp_stylus_ctrl_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        goodix_thp_ioctl_set_stylus_state(buf[0] != '0');
        return count;
}

static ssize_t goodix_thp_rawdata_ctrl_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2] = {NOTIFY_TYPE_RAWDATA, 0};

        if (buf[0] == 1 || buf[0] == '1')
                val[1] = 1;

        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, 2);
        return count;
}

static ssize_t goodix_thp_version_info(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        struct goodix_thp_core *cd = gdix_thp_core;

        return snprintf(buf, PAGE_SIZE, "%s\n", cd->ts_dev->board_data.thp_ver);
}

static ssize_t goodix_thp_logtofile_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        struct goodix_thp_core *cd = gdix_thp_core;

        return sprintf(buf, "%s\n", cd->logtofile_on ? "on" : "off");
}

static ssize_t goodix_thp_logtofile_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
        struct goodix_thp_core *cd = gdix_thp_core;
        u8 val[2] = {NOTIFY_TYPE_LOGTOFILE, 0};

        cd->logtofile_on = 0;
        if (buf[0] == 1 || buf[0] == '1') {
                val[1] = 1;
                cd->logtofile_on = 1;
        }

        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, 2);
        return count;
}

static DEVICE_ATTR(scan_rate, S_IWUSR | S_IWGRP, NULL,
                                goodix_thp_scan_rate_store);
static DEVICE_ATTR(driver_info, S_IRUGO, goodix_thp_driver_info_show, NULL);
static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR | S_IWGRP,
                                goodix_thp_debug_show, goodix_thp_debug_store);
static DEVICE_ATTR(screen_state, S_IRUGO | S_IWUSR | S_IWGRP,
                                goodix_thp_screen_show, goodix_thp_screen_store);
static DEVICE_ATTR(gesture_enable, S_IWUSR | S_IWGRP, NULL,
                                goodix_thp_gesture_store);
static DEVICE_ATTR(tsd_ctrl, S_IWUSR | S_IWGRP, NULL,
                                goodix_thp_tsd_ctrl_store);
static DEVICE_ATTR(dump_rep_log, S_IWUSR | S_IWGRP, NULL,
                                goodix_thp_dump_rep_log_store);
static DEVICE_ATTR(stylus_ctrl, S_IWUSR | S_IWGRP, NULL,
                                goodix_thp_stylus_ctrl_store);
static DEVICE_ATTR(rawdata_ctrl, S_IWUSR | S_IWGRP, NULL,
                                goodix_thp_rawdata_ctrl_store);
static DEVICE_ATTR(version_info, S_IRUGO, goodix_thp_version_info, NULL);
static DEVICE_ATTR(logtofile, S_IRUGO | S_IWUSR | S_IWGRP,
                                goodix_thp_logtofile_show, goodix_thp_logtofile_store);
static struct attribute *sysfs_attrs[] = {
        &dev_attr_scan_rate.attr,
        &dev_attr_driver_info.attr,
        &dev_attr_debug.attr,
        &dev_attr_screen_state.attr,
        &dev_attr_gesture_enable.attr,
        &dev_attr_tsd_ctrl.attr,
        &dev_attr_dump_rep_log.attr,
        &dev_attr_stylus_ctrl.attr,
        &dev_attr_rawdata_ctrl.attr,
        &dev_attr_version_info.attr,
        &dev_attr_logtofile.attr,
        NULL,
};

static const struct attribute_group sysfs_group = {
        .attrs = sysfs_attrs,
};

static int goodix_thp_sysfs_init(struct goodix_thp_core *core_data)
{
        int ret;

        ret = sysfs_create_group(&core_data->pdev->dev.kobj, &sysfs_group);
        if (ret) {
                ts_err("failed create core sysfs group");
                return ret;
        }

        return ret;
}

static void goodix_thp_sysfs_exit(struct goodix_thp_core *core_data)
{
        sysfs_remove_group(&core_data->pdev->dev.kobj, &sysfs_group);
}

static int goodix_thp_suspend(struct goodix_thp_core *core_data)
{
        int r = 0;
        struct thp_ts_device *ts_dev = core_data->ts_dev;
        //u16 gsx_data = ~core_data->gesture_enable;
        int status;

        ts_info("Suspend start");

        if (atomic_read(&core_data->suspended)) {
		ts_info("Already in suspend mode, exit.");
		goto exit;
	}
	ts_info("core_data->ztec.is_wakeup_gesture=%d", core_data->ztec.is_wakeup_gesture);
	ts_info("core_data->ztec.is_single_tap=%d", core_data->ztec.is_single_tap);
	status = (core_data->ztec.is_wakeup_gesture << 1) | core_data->ztec.is_single_tap;
	ts_info("status is %d", status);
	core_data->gesture_enable = status;

        goodix_thp_set_irq_enable(core_data, IRQ_DISABLE_FLAG);
        atomic_set(&core_data->suspended, 1);
        core_data->state_change_flag = 0;

        if (core_data->gesture_enable == 0) {
                /* power off */
                goodix_thp_power_off(core_data);
        } else {
                ts_info("enter gesture mode!");
                /* send enter gesture cmd */
                r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_GESTURE, zte_gesture_data(status));
                if (r) {
                        ts_err("send enter gesture cmd failed, r %d", r);
                        goto exit;
                }
#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	        if (core_data->ztec.is_one_key)
		        r = goodix_thp_ioctl_set_onekey_state(0);
	        else
		        r = goodix_thp_ioctl_set_onekey_state(1);
#endif
                goodix_thp_set_irq_enable(core_data, IRQ_ENABLE_FLAG);
                enable_irq_wake(core_data->irq);
        }
exit:
        goodix_thp_save_debug(0);
        goodix_thp_force_release_all();
        ts_info("Suspend end");
        return r;
}

static int goodix_thp_resume(struct goodix_thp_core *core_data)
{
        struct thp_ts_device *ts_dev = core_data->ts_dev;
        struct thp_input_agent_ioctl_coor_data data;
        int ret;

 #ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	if (aod_down_flag) {
		while (finger_retry-- && aod_down_flag) {
			if (core_data->ztec.finger_lock_flag) {
				core_data->ztec.finger_lock_flag = 0;
				ts_info("fingerprint unlock time consuming is %d ms", (100 - finger_retry)*16);
				break;
			}
			msleep(10);
		}
		aod_down_flag = false;
	}
#endif

        ts_info("Resume start");

        large_area_uevent_count = 0;

        if (atomic_read(&core_data->suspended) == 0) {
                ts_info("Already in normal mode,exit.");
                goto exit;
        }

        goodix_thp_set_irq_enable(core_data, IRQ_DISABLE_FLAG);

        if (core_data->gesture_enable == 0) {
                /* power on */
                goodix_thp_power_on(core_data);
                msleep(100);
        } else {
                disable_irq_wake(core_data->irq);
                goodix_thp_reset(ts_dev, 100);
        }

#ifdef GOODIX_USB_DETECT_GLOBAL
	if (GOODIX_USB_detect_flag) {
		goodix_thp_ioctl_set_charge_state(1);
	}
#endif
	if (core_data->ztec.is_play_game) {
		ret = zte_play_game(1);
		if (ret)
		        ts_err("set game mode failed!");
		ret = zte_sensibility_level(core_data->ztec.sensibility_level);
		ret = zte_follow_hand_level(core_data->ztec.follow_hand_level);
		ret = zte_stability_level(core_data->ztec.stability_level);
		ret = zte_set_display_rotation(core_data->ztec.display_rotation);
	}

	atomic_set(&core_data->suspended, 0);
	core_data->state_change_flag = 1;
exit:
	data.large_touch_stat = 0;
	goodix_thp_set_irq_enable(core_data, IRQ_ENABLE_FLAG);
	//goodix_thp_save_debug(1);

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp | core_data->ztec.is_single_game;
	core_data->ztec.is_wakeup_gesture = core_data->ztec.is_set_wakeup_in_suspend;
	core_data->ztec.is_one_key = core_data->ztec.is_set_onekey_in_suspend;
	core_data->ztec.is_fake_sleep = core_data->ztec.is_fake_sleep_in_suspend;
        core_data->ztec.is_screen_off_awake = core_data->ztec.is_screen_off_awake_in_suspend;
#else
	core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp | core_data->ztec.is_single_game;
	core_data->ztec.is_wakeup_gesture = core_data->ztec.is_set_wakeup_in_suspend;
#endif
	ts_info("core_data->ztec.is_single_tap is %d", core_data->ztec.is_single_tap);
	ts_info("core_data->ztec.is_set_wakeup_in_suspend is %d", core_data->ztec.is_set_wakeup_in_suspend);
	ts_info("core_data->ztec.is_wakeup_gesture is %d", core_data->ztec.is_wakeup_gesture);
	ts_info("core_data->ztec.is_one_key is %d", core_data->ztec.is_one_key);
	ts_info("core_data->ztec.is_fake_sleep is %d", core_data->ztec.is_fake_sleep);
	ts_info("core_data->ztec.is_screen_off_awake is %d", core_data->ztec.is_screen_off_awake);

        ts_info("Resume end");
        return 0;
}

#ifdef CONFIG_TOUCHSCREEN_DRM_PANEL_NOTIFIER
static void goodix_ts_register_for_panel_events(struct goodix_thp_core *core_data)
{
	void *cookie = NULL;

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH, active_panel,
			&goodix_ts_panel_notifier_callback, core_data);
	if (!cookie) {
		ts_err("Failed to register for panel events\n");
		return ;
	}

	ts_info("registered for panel notifications panel: 0x%p\n",
			active_panel);

	core_data->notifier_cookie = cookie;
}

static void goodix_ts_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification, void *client_data)
{
	if (!notification) {
		UFP_ERR("Invalid notification\n");
		return ;
	}

	/*UFP_INFO("Notification type:%d, early_trigger:%d",
			notification->notif_type,
			notification->notif_data.early_trigger);*/
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (panel_enter_low_power) {
			panel_enter_low_power = false;
			ufp_notifier_cb(false);
		}
		if (notification->notif_data.early_trigger)
			UFP_ERR("resume notification pre commit\n");
		else{
			change_tp_state(ON);
		}
		break;
	case DRM_PANEL_EVENT_BLANK:
		if (panel_enter_low_power) {
			panel_enter_low_power = false;
			ufp_notifier_cb(false);
			pr_info("ufp exit lp1\n");
		}
		if (notification->notif_data.early_trigger) {
			change_tp_state(OFF);
		} else {
			UFP_ERR("suspend notification post commit\n");
		}
		break;
	case DRM_PANEL_EVENT_BLANK_LP:
		panel_enter_low_power = true;
		ufp_notifier_cb(true);
		ufp_report_lcd_state();
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		/*UFP_ERR("shashank:Received fps change old fps:%d new fps:%d\n",
				notification->notif_data.old_fps,
				notification->notif_data.new_fps);*/
		break;
	default:
		UFP_ERR("notification serviced :%d\n",
				notification->notif_type);
		break;
	}
}

#elif IS_ENABLED(CONFIG_DRM_MEDIATEK)
static int goodix_thp_drm_notifier_callback(struct notifier_block *nb,
	unsigned long value, void *v)
{
	struct goodix_thp_core *cd =
                container_of(nb, struct goodix_thp_core, pm_notif);
	int *data = (int *)v;
        u8 val[2];

        if (!cd || !v) {
                ts_err("%s invalid parameters", __func__);
                return -1;
        }

        if (value == MTK_DISP_EVENT_BLANK) {
                /* resume: touch power on is after display to avoid display disturb */
                ts_info("%s IN, MTK_DISP_EVENT_BLANK", __func__);
                if (*data == MTK_DISP_BLANK_UNBLANK) {
                        val[0] = NOTIFY_TYPE_SCREEN;
                        val[1] = 1;
                        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
                }
                ts_info("%s OUT", __func__);
        } else if (value == MTK_DISP_EARLY_EVENT_BLANK) {
                /**
                 * suspend: touch power off is before display to avoid touch report event
                 * after screen is off
                 */
                ts_info("%s IN, MTK_DISP_EARLY_EVENT_BLANK", __func__);
                if (*data == MTK_DISP_BLANK_POWERDOWN) {
                        val[0] = NOTIFY_TYPE_SCREEN;
                        val[1] = 0;
                        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
                }
                ts_info("%s OUT", __func__);
        } else {
                ts_info("%s ignore disp value %d, data %d", __func__, value, *data);
        }

	return 0;
}
#elif IS_ENABLED(CONFIG_FB)
static int goodix_thp_fb_notifier_callback(struct notifier_block *self,
                 unsigned long event, void *data)
{
        struct fb_event *evdata = data;
        int *blank = NULL;
        struct goodix_thp_core *cd = container_of(self, struct goodix_thp_core,
                pm_notif);
        u8 val[2];

        blank = evdata->data;
        ts_info("FB event:%lu,blank:%d", event, *blank);
	if (event == FB_EVENT_BLANK) {
		if (*blank == FB_BLANK_UNBLANK) {
                        val[0] = NOTIFY_TYPE_SCREEN;
                        val[1] = 1;
                        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
		} else if (*blank == FB_BLANK_POWERDOWN) {
                        val[0] = NOTIFY_TYPE_SCREEN;
                        val[1] = 0;
                        put_frame_list(cd, REQUEST_TYPE_NOTIFY, val, sizeof(val));
		}
	}

        return 0;
}
#endif

int goodix_thp_enter_tui(void)
{
        int r = 0;
        struct goodix_thp_core *core_data = gdix_thp_core;
        struct thp_ts_device *ts_dev = core_data->ts_dev;

        ts_info("enter tui mode!");
        atomic_set(&core_data->suspended, 1);

        /* stop frame data report */
        r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_RAWDATA, RAWDATA_DISABLE);
        if (r)
                ts_err("send rawdata disable cmd failed, r %d", r);

        /* start touch data report */
        r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_TOUCH_REPORT, TOUCH_DATA_ENABLE);
        if (r)
                ts_err("send touch_data enable cmd failed, r %d", r);

        /* switch to 60hz scan rate */
        r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_ACTIVE_SCAN_RATE, SCAN_RATE_60);
        if (r)
                ts_err("send switch 60hz cmd failed, r %d", r);

        return 0;
}
EXPORT_SYMBOL_GPL(goodix_thp_enter_tui);

int goodix_thp_exit_tui(void)
{
        int r = 0;
        struct goodix_thp_core *core_data = gdix_thp_core;
        struct thp_ts_device *ts_dev = core_data->ts_dev;

        ts_info("enter tui mode!");
        atomic_set(&core_data->suspended, 0);

        /* stop touch data report */
        r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_TOUCH_REPORT, TOUCH_DATA_DISABLE);
        if (r)
                ts_err("send touch_data disable cmd failed, r %d", r);

        /* start frame data report */
        r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_RAWDATA, RAWDATA_ENABLE);
        if (r)
                ts_err("send rawdata enable cmd failed, r %d", r);

        /* switch to 120hz scan rate */
        r = ts_dev->hw_ops->send_cmd(ts_dev, CMD_ACTIVE_SCAN_RATE, SCAN_RATE_120);
        if (r)
                ts_err("send switch 120hz cmd failed, r %d", r);

        return 0;
}
EXPORT_SYMBOL_GPL(goodix_thp_exit_tui);

static int tpd_goodix_ts_resume(void *_core_data)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)_core_data;
        u8 val[2];

        ts_info("%s enter resume workqueue", __func__);

        val[0] = NOTIFY_TYPE_SCREEN;
        val[1] = 1;
        put_frame_list(core_data, REQUEST_TYPE_NOTIFY, val, sizeof(val));

	return 0;
}

static int tpd_goodix_ts_suspend(void *_core_data)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)_core_data;
        u8 val[2];

        ts_info("%s enter suspend workqueue", __func__);

        val[0] = NOTIFY_TYPE_SCREEN;
        val[1] = 0;
        put_frame_list(core_data, REQUEST_TYPE_NOTIFY, val, sizeof(val));

	return 0;
}

static void tpd_resume_suspend_register(struct goodix_thp_core *core_data)
{
	ts_info("tpd_resume_suspend_register IN");
	tpd_cdev->tp_data = core_data;
	tpd_cdev->tp_resume_func = tpd_goodix_ts_resume;
	tpd_cdev->tp_suspend_func = tpd_goodix_ts_suspend;
}

#ifdef CONFIG_TOUCHSCREEN_DRM_PANEL_NOTIFIER
static int goodix_ts_check_dt(struct device *dev)
{
	int retval = -1;
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(dev->of_node, "panel", NULL);
	if (count <= 0)
		return -ENODEV;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(dev->of_node, "panel", i);
		if (node != NULL)
			ts_info("%s: node = %s", __func__, node->name);

		panel = of_drm_find_panel(node);
		if (!IS_ERR(panel)) {
			strcpy(DEVICE_NODE_NAME, node->name);
			of_node_put(node);
			active_panel = panel;
			return 0;
		}

		if (PTR_ERR(panel) == -ENODEV) {
			ts_info("%s: no device!", __func__);
			retval = -ENODEV;
		} else if (PTR_ERR(panel) == -EPROBE_DEFER) {
			ts_info("%s: device has not been probed yet", __func__);
			retval = -EPROBE_DEFER;
		}

		of_node_put(node);
	}

	return retval;
}
#endif

/**
 * goodix_thp_probe - called by kernel when a Goodix touch
 *  platform driver is added.
 */
static int goodix_thp_probe(struct platform_device *pdev)
{
        struct goodix_thp_core *core_data = NULL;
        struct thp_ts_device *tdev;
        int r;

        ts_info("goodix_thp_probe IN");

        /*init thp core data */
        tdev = pdev->dev.platform_data;
        if (!tdev || !tdev->hw_ops) {
                ts_err("Invalid touch device");
                return -ENODEV;
        }

        core_data = devm_kzalloc(&pdev->dev, sizeof(struct goodix_thp_core),
                                 GFP_KERNEL);
        if (!core_data) {
                ts_err("Failed to allocate memory for core data");
                return -ENOMEM;
        }

        core_data->frame_mmap_list.buf = kmalloc(MMAP_BUFFER_SIZE, GFP_KERNEL);
        core_data->pdev = pdev;
        core_data->ts_dev = tdev;
        mutex_init(&core_data->frame_mutex);
        mutex_init(&core_data->ts_mutex);
        mutex_init(&core_data->irq_mutex);
        init_waitqueue_head(&(core_data->frame_wq));
        /* gesture init */
        memset(core_data->gesture_type, 0xff, GESTURE_TYPE_LEN);
        memset(core_data->gesture_data, 0xff, GESTURE_KEY_DATA_LEN);
        memset(core_data->gesture_buffer_data, 0xff, GESTURE_BUFFER_DATA_LEN);
        platform_set_drvdata(pdev, core_data);

        gdix_thp_core = core_data;
        gdix_thp_core->sdev = tdev->spi_dev;

#ifdef CONFIG_TOUCHSCREEN_DRM_PANEL_NOTIFIER
        /* get and register panel from lcd */
        r = goodix_ts_check_dt(tdev->dev);
		goodix_ts_register_for_panel_events(core_data);
#endif

        /* get GPIO resource*/
        r = goodix_thp_gpio_setup(core_data);
        if (r < 0) {
                ts_err("setup gpio failed, r %d", r);
                goto out;
        }

        /* power init & power on */
        r = goodix_thp_power_init(core_data);
        if (r < 0) {
                ts_err("power init failed, r %d", r);
                goto out;
        }

        r = goodix_thp_power_on(core_data);
        if (r < 0) {
                ts_err("power on failed, r %d", r);
                goto out;
        }

        /* board init */
        r = tdev->hw_ops->board_init(tdev);
        if (r) {
                ts_err("goodix device chip detect failed, r %d", r);
                goto out;
        }

        /* get custom info */
        r = tdev->hw_ops->get_custom_info(tdev, core_data->custom_info,
                                        GOODIX_THP_CUSTOM_INFO_LEN);
        if (r) {
                ts_err("goodix get custom info failed, r %d", r);
                goto out;
        }
        core_data->custom_info[GOODIX_THP_CUSTOM_INFO_LEN] = '\0';

        /* register misc dev */
        r = misc_register(&g_thp_misc_device);
        if (r) {
                ts_err("failed to register misc device '/dev/thp', r %d", r);
                goto out;
        }

        /*r = goodix_thp_pen_input_dev_init(core_data);
        if (r) {
                ts_err("failed to init suspend input dev, r %d", r);
                goto err_init_pen_dev;
        }*/

        /* init input_agent */
        r = goodix_thp_input_agent_init(core_data);
        if (r) {
                ts_err("failed to init gdix_input_agent, r %d", r);
                goto err_init_wrapper;
        }

        /* init sysfs */
        r = goodix_thp_sysfs_init(core_data);
        if (r) {
                ts_err("failed to create sysfs, r %d", r);
                goto err_sysfs_init;
        }

        /* request irq */
        r = goodix_thp_irq_setup(core_data);
        if (r) {
                ts_err("goodix setup irq failed, r %d", r);
                goto err_irq_setup;
        }

        tpd_resume_suspend_register(core_data);
        goodix_thp_tpd_register_fw_class(core_data);

#if IS_ENABLED(CONFIG_DRM_MEDIATEK)
        core_data->pm_notif.notifier_call = goodix_thp_drm_notifier_callback;
	if (mtk_disp_notifier_register("Touch", &core_data->pm_notif))
		ts_err("Failed to register disp notifier client:%d", ret);
#elif IS_ENABLED(CONFIG_FB)
        core_data->pm_notif.notifier_call = goodix_thp_fb_notifier_callback;
        r = fb_register_client(&core_data->pm_notif);
        if (r < 0)
                ts_err("[FB]Unable to register fb_notifier, ret:%d", r);
#endif

 #if defined(CONFIG_PM) && GTP_PATCH_COMERR_PM
	init_completion(&core_data->pm_completion);
	core_data->pm_suspend = false;
#endif

	core_data->ztec.is_single_tap = 0;
	core_data->ztec.is_single_aod = 0;
	core_data->ztec.is_single_fp = 0;
	core_data->ztec.is_wakeup_gesture = 0;
	core_data->ztec.is_one_key = 0;
	core_data->ztec.finger_lock_flag = 0;
	core_data->ztec.is_set_onekey_in_suspend = 0;
	core_data->ztec.is_play_game = 0;
	core_data->ztec.is_palm_mode = 0;
	core_data->ztec.sensibility_level = 2;
	core_data->ztec.follow_hand_level = 2;
	core_data->ztec.stability_level = 2;
	core_data->ztec.rotation_limit_level = 2;
	core_data->ztec.is_fake_sleep = 0;
	core_data->ztec.is_fake_sleep_in_suspend = 0;
	core_data->ztec.is_screen_off_awake = 0;
	core_data->ztec.is_screen_off_awake_in_suspend = 0;
#ifdef FOR_ZTE_CELL
	core_data->ztec.tp_report_rate = 0;
	ts_info("tp_report_rate set 120HZ");
#else
	core_data->ztec.tp_report_rate = 1;
	ts_info("tp_report_rate set 240HZ");
#endif

        return 0;

err_irq_setup:
        goodix_thp_sysfs_exit(core_data);
err_sysfs_init:
        goodix_thp_input_agent_exit();
err_init_wrapper:
        /*goodix_thp_pen_input_dev_exit(core_data);*/
        misc_deregister(&g_thp_misc_device);
out:
        ts_info("goodix_thp_probe OUT, r:%d", r);
        return r;
}

static int goodix_thp_remove(struct platform_device *pdev)
{
        struct goodix_thp_core *core_data = gdix_thp_core;

        ts_info("IN");
        goodix_thp_power_off(core_data);
        goodix_thp_sysfs_exit(core_data);
        goodix_thp_input_agent_exit();
        /*goodix_thp_pen_input_dev_exit(core_data);*/
        misc_deregister(&g_thp_misc_device);
#if IS_ENABLED(CONFIG_DRM_MEDIATEK)
        if (mtk_disp_notifier_unregister(&core_data->pm_notif))
                ts_info("Error occurred when unregister disp_notifier");
#elif IS_ENABLED(CONFIG_FB)
        fb_unregister_client(&core_data->pm_notif);
#endif
        kfree(core_data->frame_mmap_list.buf);
        return 0;
}

#if defined(CONFIG_PM) && GTP_PATCH_COMERR_PM
static int goodix_pm_suspend(struct device *dev)
{
	struct goodix_thp_core *core_data = dev_get_drvdata(dev);

        ts_info("system enters into pm_suspend");
	core_data->pm_suspend = true;
	reinit_completion(&core_data->pm_completion);
	return 0;
}

static int goodix_pm_resume(struct device *dev)
{
	 struct goodix_thp_core *core_data = dev_get_drvdata(dev);

	ts_info("system resumes from pm_suspend");
	core_data->pm_suspend = false;
	complete(&core_data->pm_completion);
	return 0;
}

static const struct dev_pm_ops goodix_dev_pm_ops = {
	.suspend = goodix_pm_suspend,
	.resume = goodix_pm_resume,
};
#endif

static const struct platform_device_id ts_core_ids[] = {
        {.name = GOODIX_CORE_DRIVER_NAME},
        {}
};
MODULE_DEVICE_TABLE(platform, ts_core_ids);

static struct platform_driver thp_core_driver = {
        .driver = {
                .name = GOODIX_CORE_DRIVER_NAME,
                .owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_PM)
		.pm = &goodix_dev_pm_ops,
#endif
        },
        .probe = goodix_thp_probe,
        .remove = goodix_thp_remove,
        .id_table = ts_core_ids,
};

int goodix_thp_core_init(void)
{
        ts_info("IN 2025-3-29");
        ts_info("goodix thp driver v%s", GOODIX_THP_DRIVER_VERSION);

        return platform_driver_register(&thp_core_driver);
}

int goodix_thp_core_deinit(void)
{
        ts_info("IN");

        platform_driver_unregister(&thp_core_driver);
        return 0;
}
