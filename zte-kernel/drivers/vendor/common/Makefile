
# Makefile for the common kernel device drivers.
obj-$(CONFIG_TOUCHSCREEN_VENDOR_V2)	+= touchscreen_v2/
obj-$(CONFIG_NXP_TFA9894_SMARTPA)	+= nxp_tfa9894/
obj-$(CONFIG_POWER_VENDOR_COMMON)	+= power/
obj-y   += vcfg/
#for AWINIC AW881XXSmart PA
obj-$(CONFIG_SND_SMARTPA_AW881XX) += awinic/
obj-$(CONFIG_SND_SOC_TFA98XX) += tfa9873/
obj-$(CONFIG_VENDOR_SQC_CHARGER)   += sqc/
obj-$(CONFIG_VENDOR_SQC_CHARGER_V2)   += sqc_v2/
obj-$(CONFIG_VENDOR_SQC_CHARGER_V8)   += sqc_v8/
obj-$(CONFIG_VENDOR_SQC_CHARGER_SPRD)   += sqc_sprd/
obj-$(CONFIG_VENDOR_SQC_CHARGER_BASE)   += sqc_base/
obj-$(CONFIG_VENDOR_SQC_CHARGER_V7)   += sqc_v7/
obj-$(CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM)   += zlog/
obj-y   += sound/

#for sensor
obj-y   += sensor/

#for crypto_chip
obj-$(CONFIG_ZTE_CRYPTO_CHIP) += crypto_chip/

#for breathing light leds
obj-$(CONFIG_ZTE_BREATHING_LIGHT_LEDS) += leds/

#for zcard_det
obj-y   += zcard_det/

#lcd
obj-$(CONFIG_ZTE_LCDBL_I2C_CTRL_VSP_VSN) += zte_lcdbl/
obj-$(CONFIG_ZTE_LCD_LT8911EXB_MIPI_TO_EDP) += zte_lt8911exb_mipi_to_edp/

##### Fingerprint #####
obj-$(CONFIG_ZTE_PLATFORM_FINGERPRINT)    += fingerprint/

obj-$(CONFIG_VENDOR_STATS_INFO) += stats_info/

#for pv gpio test
ifdef ZTE_FEATURE_PV_AR
obj-$(CONFIG_ZTE_PV_PIN_TEST)   += zte_pv_pintest/
endif

#ZTE_AUDIO
obj-$(CONFIG_ZTE_AW883XX_SMART_PA) += aw883xx/
obj-$(CONFIG_ZTE_AW883XX_SMART_PA_NEW) += aw883xx_new/
obj-$(CONFIG_ZTE_SND_SOC_AW87XXX) += aw87xxx/
obj-$(CONFIG_ZTE_SND_SOC_AW87XXX_AW87381_AW87565) += aw87xxx_87391_87565/
obj-$(CONFIG_ZTE_SND_SOC_FS1599) += fs1599/
obj-$(CONFIG_ZTE_SND_SOC_CODEC_FS1815N) += fs1815/
obj-$(CONFIG_ZTE_SND_SOC_TFA9894)    += tfa9894/
obj-$(CONFIG_ZTE_SND_SOC_TASDEVICE_2563) += tas2563/
obj-$(CONFIG_ZTE_SND_SOC_AW87394) += aw87394/
#ZTE_EMMC
obj-$(CONFIG_ZTE_EMMC_INFO)	+= zte_emmc_info/

#ZTE_SYS_F2FS
obj-$(CONFIG_ZTE_SYSFS_F2FS) += zte_sys_f2fs/

#USB
obj-$(CONFIG_REDRIVER_PIDPX1217)	+= pidpx1217/
obj-$(CONFIG_USB_WHS3844)	+= whs3844/

# aw haptic_hv
obj-$(CONFIG_AWINIC_HAPTIC_HV)	+= aw_haptic_hv/

# notebook power on with lid on
obj-y += notebook_poweron_with_lidon/

