# Makefile for the common kernel device drivers.

obj-$(CONFIG_ZTE_LEDS_AW210XX)	+= aw210xx/
obj-$(CONFIG_ZTE_LEDS_AW2016)	+= aw2016/
obj-$(CONFIG_ZTE_LEDS_AW21024)	+= aw21024/

ifeq ($(CONFIG_VENDOR_ZTE_MISC),m)
	subdir-ccflags-y += -DCONFIG_VENDOR_ZTE_MISC_COMMON
endif