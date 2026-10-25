mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))

ccflags-y += -I$(mkfile_dir)/aw9610x/aw9610x_reg_config/$(CONFIG_SAR_PARA)/

$(warning CONFIG_SAR_PARA  = $(CONFIG_SAR_PARA) )

obj-$(CONFIG_SAR_SENSOR_AW9610X) += zte_sar.o

zte_sar-objs := ./comm/aw_sar_comm_interface.o aw_sar.o ./aw9610x/aw9610x.o