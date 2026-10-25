#
# Makefile for zte touchscreen_v2 driver.
#
ifeq ($(CONFIG_VENDOR_ZTE_LOG_EXCEPTION),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
endif
ifeq ($(ZTE_EDGE_MISTOUCH_PREVENTION_NARROW),true)
	ccflags-y += -DCONFIG_EDGE_MISTOUCH_PREVENTION_NARROW
endif
ifeq ($(ZTE_FEATURE_F03_90HZ_TO_120HZ),true)
	ccflags-y += -DCONFIG_NOPEN
endif
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_v2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ifeq ($(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V3),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_THP/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chipone_tddi_v2_1/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chipone_tddi_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chipone_tddi_pad/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chsc5xxx/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/eswin_touch/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/ilitek_tddi_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/ilitek_tddi_v3_1/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ifeq ($(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_UFP_V4_1),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_ufp_v4_1/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_ufp_v4_1/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ifeq ($(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_1),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_v4_1/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_v4_1/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ifeq ($(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_2),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_v4_2/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_v4_2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ifeq ($(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_3),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_v4_3/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_v4_3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ifeq ($(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_PAD),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_pad/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_pad/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/tlsc6x_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/tlsc6x_v3/tlsc_chip3535/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/tlsc6x_v3/tlsc_chip3536/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/gcore_ts_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/gcore_gc7272_spi/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/betterlife_ts/factory_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/betterlife_ts/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/sitronix_incell/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/sitronix_incell_v2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/novatek_touch_spi/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/omnivision_tcm/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/hxchipset_v3_3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/hxchipset_pad/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/axs_touch/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/

zte_tpd-y := ztp_core.o ztp_report_algo.o ztp_state_change.o
zte_tpd-$(CONFIG_TOUCHSCREEN_UFP_MAC) +=  ztp_ufp.o

################### AXS_TOUCH ######################
zte_tpd-$(CONFIG_TOUCHSCREEN_AXS_TOUCH) += \
	axs_touch/axs_core.o \
	axs_touch/axs_i2c.o \
	axs_touch/axs_spi.o \
	axs_touch/axs_debug.o \
	axs_touch/axs_upgrade.o \
	axs_touch/axs_download.o \
	axs_touch/axs_gesture.o \
	axs_touch/axs_esd.o \
	axs_touch/axs_psensor.o \
	axs_touch/axs_common_interface.o \
	axs_touch/axs_test_ini.o \
	axs_touch/axs_test.o \
	axs_touch/axs_15260test.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_AXS_TOUCH)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_AXS_COMMON
endif
########################################################

################## BETTERLIFE_TS #######################
zte_tpd-$(CONFIG_TOUCHSCREEN_BETTERLIFE_TS) += \
	betterlife_ts/bl_ts.o \
	betterlife_ts/fw_update.o \
	betterlife_ts/bl_common_interface.o \
	betterlife_ts/factory_test/bl_test.o \
	betterlife_ts/factory_test/bl_test_ini.o \
	betterlife_ts/factory_test/bl_test_mcsc.o \
	betterlife_ts/factory_test/bl_test_sc.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_BETTERLIFE_TS)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_BETTERLIFE_COMMON
endif
########################################################

################## CHIPONE_TDDI_PAD ####################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHIPONE_TDDI_PAD) += \
	chipone_tddi_pad/cts_driver.o \
	chipone_tddi_pad/cts_core.o \
	chipone_tddi_pad/cts_sfctrlv2.o \
	chipone_tddi_pad/cts_spi_flash.o \
	chipone_tddi_pad/cts_firmware.o \
	chipone_tddi_pad/cts_test.o \
	chipone_tddi_pad/cts_tcs.o \
	chipone_tddi_pad/cts_platform.o \
	chipone_tddi_pad/cts_tool.o \
	chipone_tddi_pad/cts_sysfs.o \
	chipone_tddi_pad/cts_strerror.o \
	chipone_tddi_pad/cts_common_interface.o \
	chipone_tddi_pad/cts_selftest.o \
	chipone_tddi_pad/cts_selftest_ini.o \
	chipone_tddi_pad/cts_oem.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_CHIPONE_TDDI_PAD)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_CHIPONE_COMMON
endif
########################################################

################## CHIPONE_TDDI_V2_1 ###################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHIPONE_TDDI_V2_1) += \
	chipone_tddi_v2_1/cts_driver.o \
	chipone_tddi_v2_1/cts_core.o \
	chipone_tddi_v2_1/cts_sfctrlv2.o \
	chipone_tddi_v2_1/cts_spi_flash.o \
	chipone_tddi_v2_1/cts_firmware.o \
	chipone_tddi_v2_1/cts_test.o \
	chipone_tddi_v2_1/cts_tcs.o \
	chipone_tddi_v2_1/cts_platform.o \
	chipone_tddi_v2_1/cts_tool.o \
	chipone_tddi_v2_1/cts_sysfs.o \
	chipone_tddi_v2_1/cts_common_interface.o \
	chipone_tddi_v2_1/cts_selftest.o \
	chipone_tddi_v2_1/cts_selftest_ini.o \
	chipone_tddi_v2_1/cts_strerror.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_CHIPONE_TDDI_V2_1)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_CHIPONE_COMMON
endif
#######################################################

################## CHIPONE_TDDI_V3 ####################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHIPONE_TDDI_V3) += \
	chipone_tddi_v3/cts_driver.o \
	chipone_tddi_v3/cts_i2c_driver.o \
	chipone_tddi_v3/cts_spi_driver.o \
	chipone_tddi_v3/cts_core.o \
	chipone_tddi_v3/cts_sfctrlv2.o \
	chipone_tddi_v3/cts_spi_flash.o \
	chipone_tddi_v3/cts_firmware.o \
	chipone_tddi_v3/cts_test.o \
	chipone_tddi_v3/cts_strerror.o \
	chipone_tddi_v3/cts_charger_detect.o \
	chipone_tddi_v3/cts_earjack_detect.o \
	chipone_tddi_v3/cts_platform.o \
	chipone_tddi_v3/cts_tool.o \
	chipone_tddi_v3/cts_sysfs.o \
	chipone_tddi_v3/cts_ioctl.o \
	chipone_tddi_v3/cts_cdev.o \
	chipone_tddi_v3/cts_common_interface.o \
	chipone_tddi_v3/cts_selftest.o \
	chipone_tddi_v3/cts_selftest_ini.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_CHIPONE_TDDI_V3)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_CHIPONE_COMMON
endif
#######################################################

#################### CHSC5XXX #########################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHSC5XXX)	+= \
	chsc5xxx/basic_util.o \
	chsc5xxx/i2c_communication.o \
	chsc5xxx/qual_core.o \
	chsc5xxx/semi_touch_function.o \
	chsc5xxx/semi_touch_upgrade.o \
	chsc5xxx/semi_touch_apk.o \
	chsc5xxx/semi_touch_device.o \
	chsc5xxx/semi_touch_custom.o \
	chsc5xxx/semi_touch_test_5448.o \
	chsc5xxx/semi_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_CHSC5XXX)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_CHSC5XXX_COMMON
endif
#######################################################

#################### ESWIN_TOUCH #########################
zte_tpd-$(CONFIG_TOUCHSCREEN_ESWIN_TOUCH) += \
	eswin_touch/eswin_common_interface.o \
	eswin_touch/eswin_eph861x.o \
	eswin_touch/eswin_eph861x_bootloader.o \
	eswin_touch/eswin_eph861x_comms.o \
	eswin_touch/eswin_eph861x_eswin.o \
	eswin_touch/eswin_eph861x_tlv_command.o \
	eswin_touch/eswin_eph861x_tlv_report.o \
	eswin_touch/eswin_eph861x_spi.o \
	eswin_touch/eswin_eph861x_i2c.o \
	eswin_touch/eswin_eph861x_testing.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_ESWIN_TOUCH)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_ESWIN_COMMON
endif
#######################################################

############# FOCALTECH_TOUCH_UFP_V4_1 ################
zte_tpd-$(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_UFP_V4_1) += \
	focaltech_touch_ufp_v4_1/focaltech_core.o \
	focaltech_touch_ufp_v4_1/focaltech_ex_fun.o \
	focaltech_touch_ufp_v4_1/focaltech_ex_mode.o \
	focaltech_touch_ufp_v4_1/focaltech_gesture.o \
	focaltech_touch_ufp_v4_1/focaltech_esdcheck.o \
	focaltech_touch_ufp_v4_1/focaltech_point_report_check.o \
	focaltech_touch_ufp_v4_1/focaltech_flash.o \
	focaltech_touch_ufp_v4_1/focaltech_common_interface.o \
	focaltech_touch_ufp_v4_1/focaltech_spi_v2.o \
	focaltech_touch_ufp_v4_1/focaltech_test/focaltech_test.o \
	focaltech_touch_ufp_v4_1/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_ufp_v4_1/focaltech_flash/focaltech_upgrade_ft3681.o \
	focaltech_touch_ufp_v4_1/focaltech_flash/focaltech_upgrade_ft3683g.o \
	focaltech_touch_ufp_v4_1/focaltech_test/supported_ic/focaltech_test_ft3681.o\
	focaltech_touch_ufp_v4_1/focaltech_test/supported_ic/focaltech_test_ft3683g.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_UFP_V4_1)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_FOCALTECH_COMMON
endif
########################################################

################ FOCALTECH_TOUCH_V4_1 ##################
zte_tpd-$(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_1) += \
	focaltech_touch_v4_1/focaltech_core.o \
	focaltech_touch_v4_1/focaltech_ex_fun.o \
	focaltech_touch_v4_1/focaltech_ex_mode.o \
	focaltech_touch_v4_1/focaltech_gesture.o \
	focaltech_touch_v4_1/focaltech_esdcheck.o \
	focaltech_touch_v4_1/focaltech_point_report_check.o \
	focaltech_touch_v4_1/focaltech_flash_spi.o \
	focaltech_touch_v4_1/focaltech_common_interface.o \
	focaltech_touch_v4_1/focaltech_spi_v2.o \
	focaltech_touch_v4_1/focaltech_proximity.o \
	focaltech_touch_v4_1/focaltech_test/focaltech_test.o \
	focaltech_touch_v4_1/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_v4_1/focaltech_test/supported_ic/focaltech_test_ft8720.o\
	focaltech_touch_v4_1/focaltech_test/supported_ic/focaltech_test_ft8725.o \
	focaltech_touch_v4_1/focaltech_test/supported_ic/focaltech_test_ft8057s.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_1)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_FOCALTECH_COMMON
endif
########################################################

################ FOCALTECH_TOUCH_V4_2 ##################
zte_tpd-$(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_2) += \
	focaltech_touch_v4_2/focaltech_common_interface.o \
	focaltech_touch_v4_2/focaltech_core.o \
	focaltech_touch_v4_2/focaltech_debug.o \
	focaltech_touch_v4_2/focaltech_esdcheck.o \
	focaltech_touch_v4_2/focaltech_ex_fun.o \
	focaltech_touch_v4_2/focaltech_ex_mode.o \
	focaltech_touch_v4_2/focaltech_flash.o \
	focaltech_touch_v4_2/focaltech_flash/focaltech_upgrade_ft3683g.o \
	focaltech_touch_v4_2/focaltech_gesture.o \
	focaltech_touch_v4_2/focaltech_hp.o \
	focaltech_touch_v4_2/focaltech_point_report_check.o \
	focaltech_touch_v4_2/focaltech_proximity.o \
	focaltech_touch_v4_2/focaltech_spi_v2.o \
	focaltech_touch_v4_2/focaltech_test/focaltech_test.o \
	focaltech_touch_v4_2/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_v4_2/focaltech_test/supported_ic/focaltech_test_ft3683g.o \

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_2)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_FOCALTECH_COMMON
endif
########################################################

################ FOCALTECH_TOUCH_V4_3 ##################
zte_tpd-$(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_3) += \
	focaltech_touch_v4_3/focaltech_core.o \
	focaltech_touch_v4_3/focaltech_esdcheck.o \
	focaltech_touch_v4_3/focaltech_ex_fun.o \
	focaltech_touch_v4_3/focaltech_ex_mode.o \
	focaltech_touch_v4_3/focaltech_flash.o \
	focaltech_touch_v4_3/focaltech_flash/focaltech_upgrade_ft5b36u.o \
	focaltech_touch_v4_3/focaltech_gesture.o \
	focaltech_touch_v4_3/focaltech_i2c.o \
	focaltech_touch_v4_3/focaltech_point_report_check.o \
	focaltech_touch_v4_3/focaltech_proximity.o \
	focaltech_touch_v4_3/focaltech_test/focaltech_test.o \
	focaltech_touch_v4_3/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_v4_3/focaltech_test/supported_ic/focaltech_test_ft5b36u.o \
	focaltech_touch_v4_3/focaltech_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_V4_3)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_FOCALTECH_COMMON_I2C
endif
########################################################

###################FOCAL TOUCH PAD #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_PAD) += \
	focaltech_touch_pad/focaltech_core.o \
	focaltech_touch_pad/focaltech_ex_fun.o \
	focaltech_touch_pad/focaltech_ex_mode.o \
	focaltech_touch_pad/focaltech_gesture.o \
	focaltech_touch_pad/focaltech_esdcheck.o \
	focaltech_touch_pad/focaltech_point_report_check.o \
	focaltech_touch_pad/focaltech_flash_spi.o \
	focaltech_touch_pad/focaltech_spi_v2.o \
	focaltech_touch_pad/focaltech_debug.o \
	focaltech_touch_pad/focaltech_hp.o \
	focaltech_touch_pad/focaltech_proximity.o \
	focaltech_touch_pad/focaltech_common_interface.o \
	focaltech_touch_pad/focaltech_test/focaltech_test.o \
	focaltech_touch_pad/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_pad/focaltech_test/supported_ic/focaltech_test_ft8205p.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_FOCALTECH_TOUCH_PAD)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_FOCALTECH_COMMON
endif
######################################################

################## GCORE_GC7272_SPI ####################
zte_tpd-$(CONFIG_TOUCHSCREEN_GCORE_GC7272_SPI) += \
	gcore_gc7272_spi/gcore_drv_fw_update.o \
	gcore_gc7272_spi/gcore_drv_interface.o \
	gcore_gc7272_spi/gcore_drv_main.o \
	gcore_gc7272_spi/gcore_drv_mp_test.o \
	gcore_gc7272_spi/gcore_drv_sprd.o \
	gcore_gc7272_spi/gcore_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_GCORE_GC7272_SPI)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_GCORE_COMMON
endif
########################################################

#################### GCORE_TS_V3 #######################
zte_tpd-$(CONFIG_TOUCHSCREEN_GCORE_TS_V3) += \
	gcore_ts_v3/gcore_drv_main.o \
	gcore_ts_v3/gcore_drv_qcom.o \
	gcore_ts_v3/gcore_drv_fw_update.o \
	gcore_ts_v3/gcore_drv_interface.o \
	gcore_ts_v3/gcore_drv_mp_test.o \
	gcore_ts_v3/gcore_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_GCORE_TS_V3)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_GCORE_COMMON
endif
#########################################################

############# GOODIX_BERLIN_DRIVER_THP ###################
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_THP) += \
	goodix_berlin_driver_THP/goodix_thp.o \
	goodix_berlin_driver_THP/goodix_thp_spi.o \
	goodix_berlin_driver_THP/goodix_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_THP)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_GOODIX_THP_COMMON
endif
#########################################################

############# GOODIX_BERLIN_DRIVER_V2 ###################
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V2) += \
	goodix_berlin_driver_v2/goodix_ts_core.o \
	goodix_berlin_driver_v2/goodix_brl_hw.o \
	goodix_berlin_driver_v2/goodix_cfg_bin.o \
	goodix_berlin_driver_v2/goodix_ts_utils.o \
	goodix_berlin_driver_v2/goodix_brl_fwupdate.o \
	goodix_berlin_driver_v2/goodix_ts_gesture.o \
	goodix_berlin_driver_v2/goodix_ts_inspect.o \
	goodix_berlin_driver_v2/goodix_ts_tools.o \
	goodix_berlin_driver_v2/goodix_common_interface.o

zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V2_I2C) += \
	goodix_berlin_driver_v2/goodix_brl_i2c.o

zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V2_SPI) += \
	goodix_berlin_driver_v2/goodix_brl_spi.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V2)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_GOODIX_COMMON
endif
#########################################################

############# GOODIX_BERLIN_DRIVER_V3 ###################
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V3) += \
	goodix_berlin_driver_v3/goodix_brl_fwupdate.o \
	goodix_berlin_driver_v3/goodix_brl_hw.o \
	goodix_berlin_driver_v3/goodix_cfg_bin.o \
	goodix_berlin_driver_v3/goodix_ts_core.o \
	goodix_berlin_driver_v3/goodix_ts_dump.o \
	goodix_berlin_driver_v3/goodix_ts_gesture.o \
	goodix_berlin_driver_v3/goodix_ts_inspect.o \
	goodix_berlin_driver_v3/goodix_ts_tools.o \
	goodix_berlin_driver_v3/goodix_ts_utils.o \
	goodix_berlin_driver_v3/goodix_common_interface.o

zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V3_I2C) += \
	goodix_berlin_driver_v3/goodix_brl_i2c.o

zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V3_SPI) += \
	goodix_berlin_driver_v3/goodix_brl_spi.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_GOODIX_BERLIN_DRIVER_V3)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_GOODIX_COMMON
endif
#########################################################

################### HXCHIPSET_PAD #######################
zte_tpd-$(CONFIG_TOUCHSCREEN_HXCHIPSET_PAD) += \
	hxchipset_pad/himax_ic_core.o \
	hxchipset_pad/himax_common.o \
	hxchipset_pad/himax_inspection.o \
	hxchipset_pad/himax_debug.o \
	hxchipset_pad/himax_platform.o \
	hxchipset_pad/himax_common_interface.o

zte_tpd-$(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83102J) += hxchipset_pad/himax_ic_HX83102j.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_HXCHIPSET_PAD)),)
	ifneq ($(filter y, $(CONFIG_FB)),)
		ccflags-y += -DHX_CONFIG_FB
	endif
	ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_COMMON
	ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_DEBUG
	ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_INSPECT
	ccflags-y += -DCONFIG_HMX_DB
endif
#########################################################

################### HXCHIPSET_V3_3 ######################
zte_tpd-$(CONFIG_TOUCHSCREEN_HXCHIPSET_V3_3) += \
	hxchipset_v3_3/himax_ic_core.o \
	hxchipset_v3_3/himax_common.o \
	hxchipset_v3_3/himax_inspection.o \
	hxchipset_v3_3/himax_debug.o \
	hxchipset_v3_3/himax_platform.o \
	hxchipset_v3_3/himax_common_interface.o

zte_tpd-$(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83112F) += hxchipset_v3_3/himax_ic_HX83112f.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_HXCHIPSET_V3_3)),)
	ifneq ($(filter y, $(CONFIG_FB)),)
		ccflags-y += -DHX_CONFIG_FB
	endif
	ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_COMMON
	ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_DEBUG
	ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_INSPECT
	ccflags-y += -DCONFIG_HMX_DB
endif
#########################################################

################ ILITEK_TDDI_V3 #########################
zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3) += \
	ilitek_tddi_v3/ilitek_v3.o \
	ilitek_tddi_v3/ilitek_v3_qcom.o \
	ilitek_tddi_v3/ilitek_v3_ic.o \
	ilitek_tddi_v3/ilitek_v3_touch.o \
	ilitek_tddi_v3/ilitek_v3_mp.o \
	ilitek_tddi_v3/ilitek_v3_node.o \
	ilitek_tddi_v3/ilitek_v3_common_interface.o
zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_I2C) += \
	ilitek_tddi_v3/ilitek_v3_i2c.o \
	ilitek_tddi_v3/ilitek_v3_flash.o

zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_SPI) += \
	ilitek_tddi_v3/ilitek_v3_spi.o \
	ilitek_tddi_v3/ilitek_v3_hostdl.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_ILITEK_COMMON
endif
########################################################

################ ILITEK_TDDI_V3_1 #########################
zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_1) += \
	ilitek_tddi_v3_1/ilitek_v3.o \
	ilitek_tddi_v3_1/ilitek_v3_qcom.o \
	ilitek_tddi_v3_1/ilitek_v3_ic.o \
	ilitek_tddi_v3_1/ilitek_v3_touch.o \
	ilitek_tddi_v3_1/ilitek_v3_mp.o \
	ilitek_tddi_v3_1/ilitek_v3_node.o \
	ilitek_tddi_v3_1/ilitek_v3_common_interface.o

zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_1_I2C) += \
	ilitek_tddi_v3_1/ilitek_v3_i2c.o \
	ilitek_tddi_v3_1/ilitek_v3_flash.o

zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_1_SPI) += \
	ilitek_tddi_v3_1/ilitek_v3_spi.o \
	ilitek_tddi_v3_1/ilitek_v3_hostdl.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_1)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_ILITEK_COMMON
endif
########################################################

################### NVT_Touch_HostDL #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_NOVATEK_TOUCH_SPI) += \
	novatek_touch_spi/nt36xxx.o \
	novatek_touch_spi/nt36xxx_fw_update.o \
	novatek_touch_spi/nt36xxx_ext_proc.o \
	novatek_touch_spi/nt36xxx_mp_ctrlram.o \
	novatek_touch_spi/nt36xxx_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_NOVATEK_TOUCH_SPI)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_NOVATEK_TOUCH_SPI_COMMON
endif
########################################################

################### OMNIVISION_TCM #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM) += \
	omnivision_tcm/omnivision_common_interface.o \
	omnivision_tcm/omnivision_tcm_core.o \
	omnivision_tcm/omnivision_tcm_touch.o \
	omnivision_tcm/omnivision_tcm_device.o \
	omnivision_tcm/omnivision_tcm_testing.o \
	omnivision_tcm/omnivision_tcm_recovery.o \
	omnivision_tcm/omnivision_tcm_diagnostics.o \
	omnivision_tcm/omnivision_tcm_csv_parser.o

zte_tpd-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_SPI) += \
	omnivision_tcm/omnivision_tcm_spi.o \
	omnivision_tcm/omnivision_tcm_zeroflash.o

zte_tpd-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_I2C) += \
	omnivision_tcm/omnivision_tcm_i2c.o \
	omnivision_tcm/omnivision_tcm_reflash.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_OMNIVISION_TCM)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_OMNIVISION_COMMON
endif
########################################################

################## SITRONIX_INCELL #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_SITRONIX_INCELL) += \
	sitronix_incell/sitronix_ts.o \
	sitronix_incell/sitronix_st7123_i2c.o \
	sitronix_incell/sitronix_st7123_spi.o \
	sitronix_incell/sitronix_st7123_utility.o \
	sitronix_incell/sitronix_st7123_mt.o \
	sitronix_incell/sitronix_st7123_nodes.o \
	sitronix_incell/sitronix_st7123_upgrade.o \
	sitronix_incell/sitronix_common_interface.o \
	sitronix_incell/sitronix_st7123_test.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_SITRONIX_INCELL)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_SITRONIX_COMMON
endif
########################################################

################## SITRONIX_INCELL_V2 #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_SITRONIX_INCELL_V2) += \
	sitronix_incell_v2/sitronix_ts.o \
	sitronix_incell_v2/sitronix_ts_i2c.o \
	sitronix_incell_v2/sitronix_ts_spi.o \
	sitronix_incell_v2/sitronix_ts_utility.o \
	sitronix_incell_v2/sitronix_ts_mt.o \
	sitronix_incell_v2/sitronix_ts_nodes.o \
	sitronix_incell_v2/sitronix_ts_upgrade.o \
	sitronix_incell_v2/sitronix_common_interface.o \
	sitronix_incell_v2/sitronix_ts_test.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_SITRONIX_INCELL_V2)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_SITRONIX_COMMON
endif
########################################################

###################### TLSC6X_V3 #######################
zte_tpd-$(CONFIG_TOUCHSCREEN_TLSC6X_V3) += \
	tlsc6x_v3/tlsc6x_main.o \
	tlsc6x_v3/tlsc6x_comp.o \
	tlsc6x_v3/tlsc6x_common_interface.o

ifneq ($(filter y m, $(CONFIG_TOUCHSCREEN_TLSC6X_V3)),)
	ccflags-y += -DCONFIG_TOUCHSCREEN_TLSC6X_COMMON
endif
########################################################

obj-$(CONFIG_TOUCHSCREEN_VENDOR_V2) += lcd_state_notify.o
obj-$(CONFIG_TOUCHSCREEN_VENDOR_V2) += zte_tpd.o
