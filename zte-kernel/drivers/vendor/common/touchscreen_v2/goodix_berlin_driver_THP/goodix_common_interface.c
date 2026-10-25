#include "goodix_thp.h"
#include <linux/spi/spi.h>

#ifdef GOODIX_USB_DETECT_GLOBAL
#include <linux/power_supply.h>
extern bool GOODIX_USB_detect_flag;
extern int goodix_thp_ioctl_set_charge_state(int charege_enable);
#endif
extern int goodix_thp_reset_after(struct goodix_thp_core *cd);
extern void goodix_thp_reset(struct thp_ts_device *ts_dev, int delay_ms);
extern int zte_set_display_rotation(int mrotation);
extern int zte_stability_level(int enable);
extern int zte_follow_hand_level(int enable);
extern int zte_sensibility_level(int enable);
extern int zte_play_game(int enable);
extern int zte_tp_set_report_rate(int enable);
extern int goodix_thp_reset_basline(int enable);
extern int recovery_game_mode_after_reset(void);
extern int is_fake_sleep_mode;
extern int is_screen_off_awake_mode;

static atomic_t ato_ver = ATOMIC_INIT(0);

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
    struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
    int ret = 0;

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, tp in suspend!", __func__);
		return -EIO;
	}

	if (atomic_cmpxchg(&ato_ver, 0, 1)) {
		ts_err("busy, wait!");
		return -EIO;
	}

	ts_info("%s: enter!", __func__);

	snprintf(cdev->ic_tpinfo.tp_name, 100, "thp_version   IC type:goodix GT9916\n%s",
				core_data->ts_dev->board_data.thp_ver);

	ts_info("%s: end!", __func__);

	atomic_cmpxchg(&ato_ver, 1, 0);

    return ret;
}

static int tpd_get_singleaodgesture(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->b_single_aod_enable = core_data->ztec.is_single_aod;
	ts_info("%s: enter!, core_data->ztec.is_single_aod=%d", __func__, core_data->ztec.is_single_aod);
	ts_info("%s: enter!, cdev->b_single_aod_enable=%d", __func__, cdev->b_single_aod_enable);
	return 0;
}

static int tpd_set_singleaodgesture(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int mark = 0;

	if(enable){
		mark = 1;
		core_data->ztec.is_single_aod = mark;
	} else {
		core_data->ztec.is_single_aod = mark;
	}

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_aod = mark;
		core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp | core_data->ztec.is_single_game;
	}
	ts_info("core_data->ztec.is_single_fp=%d", core_data->ztec.is_single_fp);
	ts_info("core_data->ztec.is_single_aod=%d", core_data->ztec.is_single_aod);
	ts_info("core_data->ztec.is_single_tap=%d", core_data->ztec.is_single_tap);
	return 0;
}

static int tpd_get_singlegamegesture(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->b_single_game_enable = core_data->ztec.is_single_game;
	ts_info("%s: enter!, core_data->ztec.is_single_game=%d", __func__, core_data->ztec.is_single_game);
	ts_info("%s: enter!, cdev->b_single_game_enable=%d", __func__, cdev->b_single_game_enable);
	return 0;
}

static int tpd_set_singlegamegesture(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_game = enable;
		core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp | core_data->ztec.is_single_game;
	}
	ts_info("core_data->ztec.is_single_fp=%d", core_data->ztec.is_single_fp);
	ts_info("core_data->ztec.is_single_aod=%d", core_data->ztec.is_single_aod);
	ts_info("core_data->ztec.is_single_tap=%d", core_data->ztec.is_single_tap);
	ts_info("core_data->ztec.is_single_game=%d", core_data->ztec.is_single_game);
	return 0;
}

static int tpd_get_singlefpgesture(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->b_single_tap_enable = core_data->ztec.is_single_fp;
	ts_info("%s: enter!, core_data->ztec.is_single_fp=%d", __func__, core_data->ztec.is_single_fp);
	ts_info("%s: enter!, cdev->b_single_tap_enable=%d", __func__, cdev->b_single_tap_enable);
	return 0;
}

static int tpd_set_singlefpgesture(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int mark = 0;

	if(enable){
		mark = 4;
		core_data->ztec.is_single_fp = mark;
	} else {
		core_data->ztec.is_single_fp = mark;
	}
	
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_single_fp = mark;
		core_data->ztec.is_single_tap = core_data->ztec.is_single_aod | core_data->ztec.is_single_fp | core_data->ztec.is_single_game;
	}
	ts_info("core_data->ztec.is_single_fp=%d", core_data->ztec.is_single_fp);
	ts_info("core_data->ztec.is_single_aod=%d", core_data->ztec.is_single_aod);
	ts_info("core_data->ztec.is_single_tap=%d", core_data->ztec.is_single_tap);
	return 0;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->b_gesture_enable = core_data->ztec.is_wakeup_gesture;

	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	core_data->ztec.is_set_wakeup_in_suspend = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_wakeup_gesture = enable;
	}
	ts_info("core_data->ztec.is_wakeup_gesture=%d", core_data->ztec.is_wakeup_gesture);
	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_set_one_key(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	core_data->ztec.is_set_onekey_in_suspend = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_one_key = enable;
	}

	return 0;
}

static int tpd_get_one_key(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->one_key_enable = core_data->ztec.is_one_key;

	return 0;
}
#endif

static int tpd_set_finger_lock_flag(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	core_data->ztec.finger_lock_flag = enable;

	return 0;
}

static int tpd_get_finger_lock_flag(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->finger_lock_flag = core_data->ztec.finger_lock_flag;

	return 0;
}

#ifdef GOODIX_USB_DETECT_GLOBAL
static bool goodix_get_charger_status(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	bool status = false;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = true;
	} else {
		status = false;
	}
	ts_info("charger status:%d", status);
	return status;
}

static void goodix_work_charger_detect_work(struct work_struct *work)
{
	int ret = -EINVAL;
	struct delayed_work *charger_work_delay = container_of(work, struct delayed_work, work);
	struct goodix_thp_core *core_data = container_of(charger_work_delay, struct goodix_thp_core, charger_work);
	static int status = 0;

	ts_info("into charger detect");
	GOODIX_USB_detect_flag = goodix_get_charger_status();
	if (GOODIX_USB_detect_flag && !atomic_read(&core_data->suspended) && !status) {
		ret = goodix_thp_ioctl_set_charge_state(1);
		status = 1;
	} else if (!GOODIX_USB_detect_flag && !atomic_read(&core_data->suspended) && status) {
		ret = goodix_thp_ioctl_set_charge_state(0);
		status = 0;
	} else if (!GOODIX_USB_detect_flag && atomic_read(&core_data->suspended) && status) {
		status = 0;
	} else if (GOODIX_USB_detect_flag && atomic_read(&core_data->suspended) && !status) {
		status = 1;
	}

}

static int goodix_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct goodix_thp_core *core_data = container_of(nb, struct goodix_thp_core, charger_notifier);

	/*ts_info("into charger notify");*/
	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(core_data->charger_wq, &core_data->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int goodix_init_charger_notifier(struct goodix_thp_core *core_data)
{
	int ret = 0;

	ts_info("Init Charger notifier");

	core_data->charger_notifier.notifier_call = goodix_charger_notify_call;
	ret = power_supply_reg_notifier(&core_data->charger_notifier);
	return ret;
}
#endif/*GOODIX_USB_DETECT_GLOBAL*/

static int tpd_set_rotation_limit_level(struct ztp_device *cdev, int level)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int ret = 0;

	if (level > 3)
		level = 3;
	core_data->ztec.rotation_limit_level = level;

	ret = zte_set_display_rotation(cdev->display_rotation);
	if (ret) {
		ts_err("Write display rotation failed!");
	}

	return 0;
}

static int tpd_get_rotation_limit_level(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->rotation_limit_level = core_data->ztec.rotation_limit_level;

	return 0;
}

static int tpd_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = -1;
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	if (atomic_read(&core_data->suspended)) {
		ts_err("%s:error, change set in suspend!", __func__);
		return 0;
	}
	cdev->display_rotation = mrotation;
	core_data->ztec.display_rotation = mrotation;
	ts_info("%s:display_rotation=%d", __func__, cdev->display_rotation);

	ret = zte_set_display_rotation(cdev->display_rotation);

	if (ret) {
		ts_err("Write display rotation failed!");
	}
	return cdev->display_rotation;
}

static int tpd_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int ret = 0;

	if (tp_report_rate_level > 4)
		tp_report_rate_level = 4;
	core_data->ztec.tp_report_rate = tp_report_rate_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ret = zte_tp_set_report_rate(tp_report_rate_level);
		if (ret)
			ts_err("set report rate mode failed!");
	}

	return 0;
}

static int tpd_get_tp_report_rate(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->tp_report_rate = core_data->ztec.tp_report_rate;

	return 0;
}

static int tpd_set_sensibility_level(struct ztp_device *cdev, u8 tp_sensibility_level)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int ret = 0;

	if (tp_sensibility_level > 4)
		tp_sensibility_level = 4;
	core_data->ztec.sensibility_level = tp_sensibility_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ret = zte_sensibility_level(tp_sensibility_level);
		if (ret)
			ts_err("set sensibility_level mode failed!");
	}

	return 0;
}

static int tpd_get_sensibility_level(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->sensibility_level = core_data->ztec.sensibility_level;

	return 0;
}

static int tpd_set_follow_hand_level(struct ztp_device *cdev, int tp_follow_hand_level)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int ret = 0;

	if (tp_follow_hand_level > 4)
		tp_follow_hand_level = 4;
	core_data->ztec.follow_hand_level = tp_follow_hand_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ret = zte_follow_hand_level(tp_follow_hand_level);
		if (ret)
			ts_err("set follow_hand_level mode failed!");
	}

	return 0;
}

static int tpd_get_follow_hand_level(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->follow_hand_level = core_data->ztec.follow_hand_level;

	return 0;
}

static int tpd_set_stability_level(struct ztp_device *cdev, int tp_stability_level)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int ret = 0;

	if (tp_stability_level > 4)
		tp_stability_level = 4;
	core_data->ztec.stability_level = tp_stability_level;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		ret = zte_stability_level(tp_stability_level);
		if (ret)
			ts_err("set stability_level mode failed!");
	}

	return 0;
}

static int tpd_get_stability_level(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->stability_level = core_data->ztec.stability_level;

	return 0;
}

static int tpd_set_play_game(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;
	int ret;

	if (atomic_read(&core_data->suspended)) {
		/* we can not play game in black screen */
		core_data->ztec.is_play_game = enable;
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		if (core_data->ztec.is_play_game == enable) {
			ts_info("play no need reset");
		} else {
			core_data->ztec.is_play_game = enable;
			ret = zte_play_game(enable);
			ts_info("enter_play_game %d\n", enable);
		}
	}

	return 0;
}

static int tpd_get_play_game(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->play_game_enable = core_data->ztec.is_play_game;

	return 0;
}

static int tpd_set_palm_mode(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	core_data->ztec.is_palm_mode = enable;
	ts_info("palm_mode is %d", enable);

	return 0;
}

static int tpd_get_palm_mode(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->palm_mode_en = core_data->ztec.is_palm_mode;

	return 0;
}

static int goodix_ghost_check_reset(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	goodix_thp_reset_basline(1);
	goodix_thp_reset(core_data->ts_dev, 100);
	recovery_game_mode_after_reset();
	ts_info("goodix_ghost_reset success");

	return 0;
}

static int tpd_set_fake_sleep(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	core_data->ztec.is_fake_sleep_in_suspend = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_fake_sleep = enable;
		is_fake_sleep_mode = enable;
	}

	return 0;
}

static int tpd_get_fake_sleep(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->fake_sleep_enable = core_data->ztec.is_fake_sleep;

	return 0;
}

static int tpd_set_screen_off_awake(struct ztp_device *cdev, int enable)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	core_data->ztec.is_screen_off_awake_in_suspend = enable;
	if (atomic_read(&core_data->suspended)) {
		ts_err("%s: error, change set in suspend!", __func__);
	} else {
		core_data->ztec.is_screen_off_awake = enable;
		is_screen_off_awake_mode = enable;
	}

	return 0;
}

static int tpd_get_screen_off_awake(struct ztp_device *cdev)
{
	struct goodix_thp_core *core_data = (struct goodix_thp_core *)cdev->private;

	cdev->screen_off_awake_enable = core_data->ztec.is_screen_off_awake;

	return 0;
}

void goodix_thp_tpd_register_fw_class(struct goodix_thp_core *core_data)
{
	struct goodix_thp_board_data *ts_bdata = board_data(core_data);

	ts_info("%s: entry", __func__);

#ifdef GOODIX_USB_DETECT_GLOBAL
	core_data->charger_wq = create_singlethread_workqueue("GOODIX_charger_detect");
	if (!core_data->charger_wq) {
		ts_err("allocate charger_wq failed");
	} else  {
		GOODIX_USB_detect_flag = goodix_get_charger_status();
		INIT_DELAYED_WORK(&core_data->charger_work, goodix_work_charger_detect_work);
		goodix_init_charger_notifier(core_data);
	}
#endif

	tpd_cdev->private = (void *)core_data;

	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
	tpd_cdev->max_x = ts_bdata->panel_max_x;
	tpd_cdev->max_y = ts_bdata->panel_max_y;

	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;

	tpd_cdev->get_singleaod = tpd_get_singleaodgesture;
	tpd_cdev->set_singleaod = tpd_set_singleaodgesture;
	tpd_cdev->get_singlegame = tpd_get_singlegamegesture;
	tpd_cdev->set_singlegame = tpd_set_singlegamegesture;

	tpd_cdev->get_singletap = tpd_get_singlefpgesture;
	tpd_cdev->set_singletap = tpd_set_singlefpgesture;

	tpd_cdev->get_rotation_limit_level = tpd_get_rotation_limit_level;
	tpd_cdev->set_rotation_limit_level = tpd_set_rotation_limit_level;
	tpd_cdev->set_display_rotation = tpd_set_display_rotation;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	tpd_cdev->set_one_key = tpd_set_one_key;
	tpd_cdev->get_one_key = tpd_get_one_key;
#endif

	tpd_cdev->get_finger_lock_flag = tpd_get_finger_lock_flag;
	tpd_cdev->set_finger_lock_flag = tpd_set_finger_lock_flag;

	tpd_cdev->get_tp_report_rate = tpd_get_tp_report_rate;
	tpd_cdev->set_tp_report_rate = tpd_set_tp_report_rate;

	tpd_cdev->get_sensibility = tpd_get_sensibility_level;
	tpd_cdev->set_sensibility = tpd_set_sensibility_level;

	tpd_cdev->get_follow_hand_level = tpd_get_follow_hand_level;
	tpd_cdev->set_follow_hand_level = tpd_set_follow_hand_level;

	tpd_cdev->get_stability_level = tpd_get_stability_level;
	tpd_cdev->set_stability_level = tpd_set_stability_level;

	tpd_cdev->get_play_game = tpd_get_play_game;
	tpd_cdev->set_play_game = tpd_set_play_game;

	tpd_cdev->tp_palm_mode_read = tpd_get_palm_mode;
	tpd_cdev->tp_palm_mode_write = tpd_set_palm_mode;

	tpd_cdev->ghost_check_reset = goodix_ghost_check_reset;

	tpd_cdev->set_fake_sleep = tpd_set_fake_sleep;
	tpd_cdev->get_fake_sleep = tpd_get_fake_sleep;

	tpd_cdev->set_screen_off_awake = tpd_set_screen_off_awake;
	tpd_cdev->get_screen_off_awake = tpd_get_screen_off_awake;

	ts_info("%s: end", __func__);
}
