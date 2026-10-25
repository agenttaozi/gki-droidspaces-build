/*
 * file: ztp_core.h
 *
 */
#ifndef  _ZTP_CORE_H_
#define _ZTP_CORE_H_

#ifdef CONFIG_TOUCHSCREEN_AXS_COMMON
int axs_ts_init(void);
void axs_ts_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_BETTERLIFE_COMMON
int btl_ts_init(void);
void  btl_ts_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHIPONE_COMMON
int  cts_driver_init(void);
void  cts_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_CHSC5XXX_COMMON
int  semi_i2c_device_init(void);
void  semi_i2c_device_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_ESWIN_COMMON
int eswin_touch_init(void);
void eswin_touch_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_FOCALTECH_COMMON
int  fts_ts_spi_init(void);
void  fts_ts_spi_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_FOCALTECH_COMMON_I2C
int  fts_ts_i2c_init(void);
void  fts_ts_i2c_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_GCORE_COMMON
int gcore_touch_driver_init(void);
void  gcore_touch_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_GOODIX_COMMON
int  goodix_ts_core_init(void);
void  goodix_ts_core_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_GOODIX_THP_COMMON
int  goodix_thp_spi_init(void);
void  goodix_thp_spi_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_HIMAX_COMMON
int himax_common_init(void);
void himax_common_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_ILITEK_COMMON
int  ilitek_plat_dev_init(void);
void  ilitek_plat_dev_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_NOVATEK_TOUCH_SPI_COMMON
int nvt_driver_init(void);
void nvt_driver_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_OMNIVISION_COMMON
int  ovt_tcm_module_init(void);
void  ovt_tcm_module_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_SITRONIX_COMMON
int  sitronix_ts_init(void);
void  sitronix_ts_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_TLSC6X_COMMON
int  tlsc6x_init(void);
void  tlsc6x_exit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_LCD_NOTIFY
void lcd_notify_register(void);
void lcd_notify_unregister(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
int ufp_mac_init(void);
void  ufp_mac_exit(void);
#endif
bool tp_ghost_check(void);
void ghost_check_reset(void);
void tpd_clean_all_event(void);
int tpd_report_work_init(void);
void tpd_report_work_deinit(void);
void tpd_resume_work_init(void);
void tpd_resume_work_deinit(void);

#endif

