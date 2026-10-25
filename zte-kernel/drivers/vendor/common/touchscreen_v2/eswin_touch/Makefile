#
# Makefile for the ESWIN_TOUCH touchscreen driver.
#
# Each configuration option enables a list of files.

ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/eswin_touch/firmware_config/$(CONFIG_TS_FIRMWARE)/

obj-$(CONFIG_TOUCHSCREEN_ESWIN_TOUCH) += eswin_touch.o

eswin_touch-objs := eswin_common_interface.o
eswin_touch-objs += eswin_eph861x.o
eswin_touch-objs += eswin_eph861x_bootloader.o
eswin_touch-objs += eswin_eph861x_comms.o
eswin_touch-objs += eswin_eph861x_eswin.o
eswin_touch-objs += eswin_eph861x_tlv_command.o
eswin_touch-objs += eswin_eph861x_tlv_report.o
eswin_touch-objs += eswin_eph861x_testing.o

obj-$(CONFIG_TOUCHSCREEN_ESWIN_TOUCH_I2C) += eswin_eph861x_i2c.o
obj-$(CONFIG_TOUCHSCREEN_ESWIN_TOUCH_SPI) += eswin_eph861x_spi.o
