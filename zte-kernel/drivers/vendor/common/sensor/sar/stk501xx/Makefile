ccflags-y += -DSTK_SPREADTRUM
ccflags-y += -DSTK_RETRY_I2C

#ifneq ($(ZTE_BOARD_NAME),)
# Started by AICoder, pid:5716587e3flbdb21461a08b3a09531011551a1d8
UPPERCASE_ZTE_BOARD_NAME := $(shell echo $(ZTE_BOARD_NAME) | tr '[:lower:]' '[:upper:]')
# Ended by AICoder, pid:5716587e3flbdb21461a08b3a09531011551a1d8
$(warning ZTE_BOARD_NAME IN SAR  $(UPPERCASE_ZTE_BOARD_NAME) )
ccflags-y += -DCONFIG_STKSAR_$(UPPERCASE_ZTE_BOARD_NAME)
#endif

obj-$(CONFIG_SAR_SENSOR_STK501XX)   += zte_sar_stk.o
zte_sar_stk-src += stk501xx.c stk501xx_qualcomm.c common_i2c.c common_timer.c common_gpio_qualcomm.c common_storage.c
zte_sar_stk-objs := $(zte_sar_stk-src:.c=.o)

