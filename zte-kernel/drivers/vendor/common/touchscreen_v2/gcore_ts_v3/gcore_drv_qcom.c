/*
 * GalaxyCore touchscreen driver
 *
 * Copyright (C) 2021 GalaxyCore Incorporated
 *
 * Copyright (C) 2021 Neo Chen <neo_chen@gcoreinc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "gcore_drv_common.h"

#ifdef CONFIG_DRM_PANEL_NOTIFIER
#include <drm/drm_panel.h>
#endif

void gcore_suspend(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (gdev->tp_suspend)  {
		GTP_INFO("tp  already in suspend, return");
		return;
	}
	GTP_DEBUG("enter gcore suspend");

#ifdef	HUB_TP_PS_ENABLE
	if(tpd_cdev->tpd_proximity_enable && !tpd_cdev->tpd_proximity_state) {
		fn_data.gdev->ts_stat = TS_SUSPEND;
		gcore_touch_release_all_point(fn_data.gdev->input_device);
		gdev->tp_suspend = true;
		tpd_cdev->tp_resume_before_lcd_cmd = false;
		tpd_cdev->tpd_proximity_suspended = true;
		GTP_DEBUG("enable tp proximity, end tp suspend.");
		return;
	}
#endif

#if GCORE_WDT_RECOVERY_ENABLE
	cancel_delayed_work_sync(&fn_data.gdev->wdt_work);
#endif
#ifdef CONFIG_TOUCHSCREEN_POINT_REPORT_CHECK
	cancel_delayed_work_sync(&tpd_cdev->point_report_check_work);
#endif
	cancel_delayed_work_sync(&fn_data.gdev->fwu_work);
	cancel_delayed_work_sync(&tpd_cdev->send_cmd_work);

#ifdef CONFIG_SAVE_CB_CHECK_VALUE
	fn_data.gdev->CB_ckstat = false;
#endif
#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (gdev->gesture_wakeup_en) {
		enable_irq_wake(gdev->touch_irq);
		GTP_DEBUG("gesture wakeup enable");
		msleep(20);
		gdev->gesture_irq_en = true;
		gdev->ts_stat = TS_SUSPEND;
		gcore_touch_release_all_point(fn_data.gdev->input_device);
		gdev->tp_suspend = true;
		return;
	}
#endif
#ifdef HUB_TP_PS_ENABLE
	tpd_cdev->tpd_proximity_state = 1;
#endif
	fn_data.gdev->ts_stat = TS_SUSPEND;
	gcore_touch_release_all_point(fn_data.gdev->input_device);
	gdev->irq_disable(gdev);
	gdev->tp_suspend = true;
	GTP_DEBUG("gcore suspend end");

}

void gcore_resume(void)
{
	struct gcore_dev *gdev = fn_data.gdev;

	if (!gdev->tp_suspend)  {
		GTP_INFO("tp  already resume, return");
		return;
	}
#ifdef GCORE_RESUME_DELAY
	GTP_DEBUG("gcore resume delay 20ms");
	msleep(20);
#endif
	GTP_DEBUG("enter gcore resume");

#ifdef HUB_TP_PS_ENABLE
	if(!tpd_cdev->tpd_proximity_state){
		gdev->ts_stat = TS_NORMAL;
		gdev->tp_suspend = false;
		tpd_enable_ps(tpd_cdev->tpd_proximity_enable);
		tpd_cdev->tp_resume_before_lcd_cmd = true;
		tpd_cdev->tpd_proximity_state = 1;
		GTP_DEBUG("enable tp proximity, end tp resume.");
		return;
	}
#endif

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (gdev->gesture_irq_en) {
		gdev->gesture_irq_en = false;
		GTP_INFO("disable irq wake");
		disable_irq_wake(gdev->touch_irq);
	} else {
		gdev->irq_enable(gdev);
	}
#else
	gdev->irq_enable(gdev);
#endif

#ifdef CONFIG_GCORE_AUTO_UPDATE_FW_HOSTDOWNLOAD
	gcore_request_firmware_update_work(NULL);
#else
#if CONFIG_GCORE_RESUME_EVENT_NOTIFY
	queue_delayed_work(fn_data.gdev->gtp_workqueue, &fn_data.gdev->resume_notify_work, msecs_to_jiffies(200));
#else
	mod_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->send_cmd_work, msecs_to_jiffies(300));
#endif
	gcore_touch_release_all_point(fn_data.gdev->input_device);
#endif
	gdev->ts_stat = TS_NORMAL;
	gdev->tp_suspend = false;
	GTP_DEBUG("gcore resume end");
}

#ifndef CONFIG_TOUCHSCREEN_LCD_NOTIFY
#ifdef CONFIG_DRM_PANEL_NOTIFIER
int gcore_ts_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	unsigned int blank;

	struct drm_panel_notifier *evdata = data;

	if (!evdata)
		return 0;

	blank = *(int *)(evdata->data);
	GTP_DEBUG("event = %d, blank = %d", event, blank);

	if (!(event == DRM_PANEL_EARLY_EVENT_BLANK || event == DRM_PANEL_EVENT_BLANK)) {
		GTP_DEBUG("event(%lu) do not need process", event);
		return 0;
	}

	switch (blank) {
	case DRM_PANEL_BLANK_POWERDOWN:
/* feiyu.zhu modify for tp suspend/resume */
		if (event == DRM_PANEL_EARLY_EVENT_BLANK) {
			gcore_suspend();
		}
		break;

	case DRM_PANEL_BLANK_UNBLANK:
		if (event == DRM_PANEL_EVENT_BLANK) {
			gcore_resume();
		}
		break;

	default:
		break;
	}
	return 0;
}

#elif defined(CONFIG_FB)
int gcore_ts_fb_notifier_callback(struct notifier_block *self, \
			unsigned long event, void *data)
{
	unsigned int blank;
	struct fb_event *evdata = data;

	if (!evdata)
		return 0;

	blank = *(int *)(evdata->data);
	GTP_DEBUG("event = %d, blank = %d", event, blank);

	if (!(event == FB_EARLY_EVENT_BLANK || event == FB_EVENT_BLANK)) {
		GTP_DEBUG("event(%lu) do not need process", event);
		return 0;
	}

	switch (blank) {
	case FB_BLANK_POWERDOWN:
		if (event == FB_EARLY_EVENT_BLANK) {
			change_tp_state(LCD_OFF);
		}
		break;

	case FB_BLANK_UNBLANK:
		if (event == FB_EVENT_BLANK) {
			change_tp_state(LCD_ON);
		}
		break;

	default:
		break;
	}
	return 0;

}
#endif
#endif

int gcore_touch_driver_init(void)
{
	GTP_DEBUG("touch driver init. 2025-1-21");

	time_after_fw_upgrade = 0;
	if (get_tp_chip_id() == 0) {
		if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_GCORE)) {
			GTP_ERROR("this tp is not used,return.");
			return -EPERM;
		}
	}
	if (tpd_cdev->TP_have_registered) {
		GTP_ERROR("TP have registered by other TP.");
		return -EPERM;
	}

	if (gcore_touch_bus_init()) {
		GTP_ERROR("bus init fail!");
		return -EPERM;
	}

	return 0;
}

/* should never be called */
void  gcore_touch_driver_exit(void)
{
	gcore_touch_bus_exit();
}

