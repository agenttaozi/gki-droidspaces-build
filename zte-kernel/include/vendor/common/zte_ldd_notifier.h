#ifndef __ZTE_LDD_NOTIFIER_H__
#define __ZTE_LDD_NOTIFIER_H__

enum lcd_state {
	LCD_POWER_ON = 0,
	LCD_RESET,
	LCD_CMD_ON,
	LCD_CMD_OFF,
	LCD_CMD_OFF_END,
	LCD_POWER_OFF,
	LCD_POWER_OFF_RESET_LOW,
	LCD_SHUTDOWN,
	LCD_ENTER_AOD,
	LCD_EXIT_AOD,

};

enum tp_notify_event {
	LCD_SUSPEND_POWER_ON,
	LCD_SUSPEND_POWER_OFF,
	PSENSOR_NOTIFY_LCD_SUSPEND,
	PSENSOR_NOTIFY_LCD_RESUME,
	TP_ESD_CHECK_ERROR,

};

enum zte_ldd_notify_event {
	HEADPHONE_PLUGIN,
	HEADPHONE_UPPLUG,
	HALL_SWITCH_ON,
	HALL_SWITCH_OFF,
};

extern int lcd_notifier_register_client(struct notifier_block *nb);
extern int lcd_notifier_unregister_client(struct notifier_block *nb);
extern int lcd_notifier_call_chain(unsigned long val);
extern int tpd_notifier_register_client(struct notifier_block *nb);
extern int tpd_notifier_unregister_client(struct notifier_block *nb);
extern int tpd_notifier_call_chain(unsigned long val);
extern int zte_ldd_notifier_register_client(struct notifier_block *nb);
extern int zte_ldd_notifier_unregister_client(struct notifier_block *nb);
extern int zte_ldd_notifier_call_chain(unsigned long val);
#endif