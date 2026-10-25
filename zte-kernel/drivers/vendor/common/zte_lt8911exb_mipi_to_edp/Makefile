#enable float point operation, the code below don't work
#ccflags-y += -mfloat-abi=softfp

EXTRA_CFLAGS += -Wno-error=date-time
EXTRA_CFLAGS += -Wno-date-time

ifneq ($(filter y m, $(CONFIG_ZTE_LCD_LT8911EXB_MIPI_TO_EDP)),)
	ccflags-y += -DCONFIG_ZTE_LCD_LT8911EXB_MIPI_TO_EDP
endif

zte_lt8911exb_mipi_to_edp-y := LontiumDrv.o
obj-$(CONFIG_ZTE_LCD_LT8911EXB_MIPI_TO_EDP) += zte_lt8911exb_mipi_to_edp.o