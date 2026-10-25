#
# Makefile for sensor hub drivers.
#
ifeq ($(CONFIG_VENDOR_ZTE_LOG_EXCEPTION),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
	ccflags-y += -I$(srctree)/include/vendor/comdef/
endif

ifeq ($(ZTE_SPRD_T2U_SENSOR_FLAG),true)
	ccflags-y += -DZTE_SPRD_T2U_SENSOR_FLAG
endif

obj-$(CONFIG_SPRD_SENSOR_HUB)    += sensorhub.o
sensorhub-objs := shub_core.o shub_protocol.o shub_comm.o
sensorhub-objs += shub_zte.o
