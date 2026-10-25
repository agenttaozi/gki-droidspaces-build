// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * leds-aw21024.c   aw21024 led module
 *
 * Copyright (c) 2020 Shanghai Awinic Technology Co., Ltd. All Rights Reserved
 *
 *  Author: Awinic
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/leds.h>
#include "leds-aw21024.h"
#include "leds-aw21024-reg.h"
#include <linux/random.h>

/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW21024_I2C_NAME "aw21024_led"
#define AW21024_DRIVER_VERSION "V1.2.0"
#define AW_I2C_RETRIES 5
#define AW_I2C_RETRY_DELAY 1
#define AW_READ_CHIPID_RETRIES 2
#define AW_READ_CHIPID_RETRY_DELAY 1

static void cancel_all_works(struct aw21024 *aw21024);
static void aw21024_miniled2_light_on(struct aw21024 *aw21024, uint8_t brightness);
static void aw21024_miniled3_light_on(struct aw21024 *aw21024, uint8_t brightness, int mdelay);
static void aw21024_miniled3_random_light_on(struct aw21024 *aw21024);
/* add zte boardtest interface start */
static uint8_t is_breath_led_exist = 0;
#ifdef CONFIG_VENDOR_ZTE_MISC_COMMON
#include <vendor/common/zte_misc.h>

int is_breath_led_exist_get(char *val, const void *arg)
{
	return snprintf(val, PAGE_SIZE, "%d", is_breath_led_exist);
}

static struct zte_misc_ops is_breath_led_exist_node = {
	.node_name = "is_breath_led_exist",
	.set = NULL,
	.get = is_breath_led_exist_get,
	.free = NULL,
	.arg = NULL,
};
#endif
/* zte boardtest interface end */
/******************************************************
 *
 * led effect
 *
 ******************************************************/
#define AW21024_CFG_NAME_MAX		64

struct aw21024_cfg aw21024_cfg_array[] = {
	{aw21024_cfg_led_off, sizeof(aw21024_cfg_led_off)},
	{aw21024_all_leds_on, sizeof(aw21024_all_leds_on)},
	{aw21024_red_leds_on, sizeof(aw21024_red_leds_on)},
	{aw21024_green_leds_on, sizeof(aw21024_green_leds_on)},
	{aw21024_blue_leds_on, sizeof(aw21024_blue_leds_on)},
	{aw21024_breath_leds_on, sizeof(aw21024_breath_leds_on)}
};

/******************************************************
 *
 * aw21024 i2c write/read
 *
 ******************************************************/

static int
aw21024_i2c_write(struct aw21024 *aw21024, unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw21024->i2c, reg_addr, reg_data);
		if (ret < 0)
			AW_ERROR("%s: i2c_write cnt=%d error=%d\n", __func__, cnt, ret);
		else
			break;

		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

static int
aw21024_i2c_read(struct aw21024 *aw21024, unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw21024->i2c, reg_addr);
		if (ret < 0) {
			AW_ERROR("%s: i2c_read cnt=%d error=%d\n", __func__, cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

static int aw21024_i2c_write_bits(struct aw21024 *aw21024,
				unsigned char reg_addr, unsigned int mask,
				unsigned char reg_data)
{
	unsigned char reg_val = 0;

	aw21024_i2c_read(aw21024, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= (reg_data & (~mask));
	aw21024_i2c_write(aw21024, reg_addr, reg_val);

	return 0;
}

/*****************************************************
 *
 * aw21024 led effect cfg
 *
 *****************************************************/
static void aw21024_update_cfg_array(struct aw21024 *aw21024,
				     unsigned char *p_cfg_data,
				     unsigned int cfg_size)
{
	unsigned int i = 0;

	for (i = 0; i < cfg_size; i += 2)
		aw21024_i2c_write(aw21024, p_cfg_data[i], p_cfg_data[i + 1]);
}

static int aw21024_cfg_update_array(struct aw21024 *aw21024)
{
	aw21024_update_cfg_array(aw21024, aw21024_cfg_array[aw21024->effect].p,
				 aw21024_cfg_array[aw21024->effect].count);
	return 0;
}

/*****************************************************
 *
 * aw21024 led init
 *
 *****************************************************/
static int aw21024_current_conversion(struct aw21024 *aw21024, unsigned int current_data)
{
	if (aw21024->cdev.max_brightness == 255) {
		aw21024->conversion_led_current = current_data;
	} else if (aw21024->cdev.max_brightness == 1023) {
		if ((current_data >= 1) && (current_data <= 4)) {
			aw21024->conversion_led_current = 1;
		} else {
			aw21024->conversion_led_current = (current_data * 255)
						/ aw21024->cdev.max_brightness;
		}
	} else {
		if ((current_data >= 1) && (current_data <= 8)) {
			aw21024->conversion_led_current = 1;
		} else {
			aw21024->conversion_led_current = (current_data * 255)
						/ aw21024->cdev.max_brightness;
		}
	}
	return 0;
}

static int aw21024_brightness_conversion(struct aw21024 *aw21024, unsigned int brightness_data)
{
	if (aw21024->cdev.max_brightness == 255) {
		aw21024->cdev.brightness = brightness_data;
	} else if (aw21024->cdev.max_brightness == 1023) {
		if ((brightness_data >= 1) && (brightness_data <= 4)) {
			aw21024->cdev.brightness = 1;
		} else {
			aw21024->cdev.brightness = (brightness_data * 255)
						/ aw21024->cdev.max_brightness;
		}
	} else {
		if ((brightness_data >= 1) && (brightness_data <= 8)) {
			aw21024->cdev.brightness = 1;
		} else {
			aw21024->cdev.brightness = (brightness_data * 255)
						/ aw21024->cdev.max_brightness;
		}
	}
	return 0;
}

static int aw21024_chip_enable(struct aw21024 *aw21024, bool flag)
{
	if (flag) {
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CHIP_EN_CLOSE_MASK,
				       AW21024_BIT_CHIP_EN);
	} else {
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CHIP_EN_CLOSE_MASK,
				       AW21024_BIT_CHIP_CLOSE);
	}
	return 0;
}

static int aw21024_pwm_set(struct aw21024 *aw21024, unsigned int mode)
{
	switch (mode) {
	case AW21024_CLKPRQ_16MH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_16MH);
		break;
	case AW21024_CLKPRQ_8MH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_8MH);
		break;
	case AW21024_CLKPRQ_1MH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_1MH);
		break;
	case AW21024_CLKPRQ_512KH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_512KH);
		break;
	case AW21024_CLKPRQ_256KH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_256KH);
		break;
	case AW21024_CLKPRQ_125KH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_125KH);
		break;
	case AW21024_CLKPRQ_62KH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_62KH);
		break;
	case AW21024_CLKPRQ_31KH:
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_CLKPRQ_MASK,
				       AW21024_BIT_CLKPRQ_31KH);
		break;
	default:
		break;
	}
	return 0;
}

static int aw21024_apse_set(struct aw21024 *aw21024, bool mode)
{
	if (mode) {
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_APSE_MASK,
				       AW21024_BIT_APSE_ENABLE);
	} else {
		aw21024_i2c_write_bits(aw21024,
				       AW21024_REG_GCR,
				       AW21024_BIT_APSE_MASK,
				       AW21024_BIT_APSE_DISABLE);
	}

	return 0;
}

static int aw21024_br_update(struct aw21024 *aw21024)
{
	aw21024_i2c_write(aw21024, AW21024_REG_UPDATE, 0x00);
	return 0;
}

static int aw21024_led_init(struct aw21024 *aw21024)
{
	aw21024_chip_enable(aw21024, true);
	usleep_range(200, 300);
	aw21024_pwm_set(aw21024, aw21024->clk_pwm);
	aw21024_apse_set(aw21024, aw21024->apse_mode);
	AW_INFO("before aw21024_current_conversion aw21024->dts_led_current is %d\n",
						aw21024->dts_led_current);
	aw21024_current_conversion(aw21024, aw21024->dts_led_current);
	AW_INFO("after aw21024_current_conversion aw21024->conversion_led_current is %d\n",
					aw21024->conversion_led_current);
	aw21024_i2c_write(aw21024, AW21024_REG_GCCR,
				aw21024->conversion_led_current);

	return 0;
}
static void aw21024_soft_reset(struct aw21024 *aw21024)
{
    aw21024_i2c_write(aw21024, AW21024_REG_RESET, 0x00);
    usleep_range(5000, 5100);
    aw21024_led_init(aw21024);
}
/*****************************************************
 *
 * aw21024 led hw reset
 *
 *****************************************************/
/* Started by AICoder, pid:18eccdbf64md66114d900bb3d0b1a01741025e67 */
static int aw21024_hw_reset_only(struct aw21024 *aw21024)
{
    if (aw21024 && gpio_is_valid(aw21024->reset_gpio)) {
        gpio_set_value_cansleep(aw21024->reset_gpio, 0);
        usleep_range(2000, 4000);
        gpio_set_value_cansleep(aw21024->reset_gpio, 1);
        usleep_range(2000, 4000);
    } else {
        dev_err(aw21024->dev, "%s: reset failed\n", __func__);
    }
    return 0;
}
/* Ended by AICoder, pid:18eccdbf64md66114d900bb3d0b1a01741025e67 */
static int aw21024_hw_reset(struct aw21024 *aw21024)
{
	if (aw21024 && gpio_is_valid(aw21024->reset_gpio)) {
		gpio_set_value_cansleep(aw21024->reset_gpio, 0);
		usleep_range(2000, 4000);
		gpio_set_value_cansleep(aw21024->reset_gpio, 1);
		usleep_range(2000, 4000);
	} else {
		dev_err(aw21024->dev, "%s:  failed\n", __func__);
	}
	aw21024_led_init(aw21024);
	return 0;
}

static int aw21024_hw_off(struct aw21024 *aw21024)
{
	if (aw21024 && gpio_is_valid(aw21024->reset_gpio)) {
		gpio_set_value_cansleep(aw21024->reset_gpio, 0);
		usleep_range(200, 400);
	} else {
		dev_err(aw21024->dev, "%s:  failed\n", __func__);
	}
	return 0;
}

/*****************************************************
 *
 * aw21024 led brightness
 *
 *****************************************************/
static void aw21024_brightness_work(struct work_struct *work)
{
	struct aw21024 *aw21024 = container_of(work, struct aw21024, brightness_work);

    if(aw21024->mini_led) {
        msleep(50);
        if (!aw21024->rgbcdev[RED_CDEV].brightness && !aw21024->rgbcdev[GREEN_CDEV].brightness && !aw21024->rgbcdev[BLUE_CDEV].brightness) {
            aw21024_i2c_write(aw21024, AW21024_REG_GCCR, 0);
            aw21024_miniled3_light_on(aw21024, 0, 0);
            aw21024_miniled2_light_on(aw21024, 0);
            AW_INFO("mini led test off .\n");
            return;
        }
        aw21024->cdev.brightness = 40;
        aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
        // Configure the group settings
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);
        aw21024_miniled3_light_on(aw21024, aw21024->cdev.brightness, 0);
        aw21024_miniled2_light_on(aw21024,  aw21024->cdev.brightness / 10 * 12);
        AW_INFO("mini led test on .\n");
        return;
    } 
    if (aw21024->cdev.brightness > 0) {
		AW_INFO("aw21024->cdev.brightness high 0\n");
		if (aw21024->cdev.brightness > aw21024->cdev.max_brightness)
			aw21024->cdev.brightness = aw21024->cdev.max_brightness;

		AW_INFO("before aw21024_brightness_conversion aw21024->cdev.brightness is %d\n",
						aw21024->cdev.brightness);
		aw21024_brightness_conversion(aw21024,
						aw21024->cdev.brightness);
		AW_INFO("after aw21024_brightness_conversion aw21024->cdev.brightness is %d\n",
						aw21024->cdev.brightness);
		aw21024_i2c_write(aw21024, AW21024_REG_GCCR,
						aw21024->cdev.brightness);
	} else {
		AW_INFO("aw21024->cdev.brightness low 0\n");
		aw21024_i2c_write(aw21024, AW21024_REG_GCCR, 0);
	}
}

static void aw21024_set_brightness(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct aw21024 *aw21024 = container_of(cdev, struct aw21024, cdev);

	aw21024->cdev.brightness = brightness;
	AW_INFO("awled led brightness:%d\n", brightness);
	schedule_work(&aw21024->brightness_work);
}

/* Started by AICoder, pid:gc5d6f87a0a91af146a7094ac0947e8d72076b02 */
static void aw21024_set_color_brightness(struct aw21024 *aw21024, int color_cdev, enum led_brightness brightness)
{
    if (aw21024->mini_led) {
        AW_INFO("mini led color_cdev:%d\n", color_cdev);
        aw21024->rgbcdev[color_cdev].brightness = brightness;
        schedule_work(&aw21024->brightness_work);
    } else {
        aw21024->rgbcdev[color_cdev].brightness = brightness;
        aw21024->color_cdev = color_cdev;
        schedule_work(&aw21024->rgbbrightness_work);
    }
}

static void aw21024_color_brightness_work(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, rgbbrightness_work);
    int color_cdev = -1;

    aw21024_soft_reset(aw21024);
    color_cdev = aw21024->color_cdev;
    if (aw21024->rgbcdev[color_cdev].brightness > 0) {
        const char *color_name = NULL;
        switch (color_cdev) {
            case RED_CDEV: color_name = "red"; break;
            case GREEN_CDEV: color_name = "green"; break;
            case BLUE_CDEV: color_name = "blue"; break;
        }
        AW_INFO("%s brightness high 0\n", color_name);
        if (aw21024->rgbcdev[color_cdev].brightness > aw21024->rgbcdev[color_cdev].max_brightness)
            aw21024->rgbcdev[color_cdev].brightness = aw21024->rgbcdev[color_cdev].max_brightness;

        AW_INFO("before aw21024_brightness_conversion %s brightness is %d\n",
                color_name, aw21024->rgbcdev[color_cdev].brightness);
        aw21024_brightness_conversion(aw21024, aw21024->rgbcdev[color_cdev].brightness);
        AW_INFO("after aw21024_brightness_conversion %s brightness is %d\n",
                color_name, aw21024->rgbcdev[color_cdev].brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->rgbcdev[color_cdev].brightness);
        aw21024->effect = color_cdev + 2; // Assuming effect values are 2 for red, 3 for green, 4 for blue
        aw21024_cfg_update_array(aw21024);
    } else {
        const char *color_name = NULL;
        switch (color_cdev) {
            case RED_CDEV: color_name = "red"; break;
            case GREEN_CDEV: color_name = "green"; break;
            case BLUE_CDEV: color_name = "blue"; break;
        }
        AW_INFO("%s cdev.brightness low 0\n", color_name);
        aw21024_i2c_write(aw21024, AW21024_REG_GCCR, 0);
    }
}

static void aw21024_set_red_brightness(struct led_classdev *cdev, enum led_brightness brightness)
{
    struct aw21024 *aw21024 = container_of(cdev, struct aw21024, rgbcdev[RED_CDEV]);
    aw21024_set_color_brightness(aw21024, RED_CDEV, brightness);
}

static void aw21024_set_green_brightness(struct led_classdev *cdev, enum led_brightness brightness)
{
    struct aw21024 *aw21024 = container_of(cdev, struct aw21024, rgbcdev[GREEN_CDEV]);
    aw21024_set_color_brightness(aw21024, GREEN_CDEV, brightness);
}

static void aw21024_set_blue_brightness(struct led_classdev *cdev, enum led_brightness brightness)
{
    struct aw21024 *aw21024 = container_of(cdev, struct aw21024, rgbcdev[BLUE_CDEV]);
    aw21024_set_color_brightness(aw21024, BLUE_CDEV, brightness);
}
/* Ended by AICoder, pid:gc5d6f87a0a91af146a7094ac0947e8d72076b02 */

/*****************************************************
 *
 * check chip id
 *
 *****************************************************/
static int aw21024_read_chip_id(struct aw21024 *aw21024)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char reg_val = 0;

	/* hardware reset */
	aw21024_hw_reset_only(aw21024);
	usleep_range(200, 400);

	while (cnt++ < AW_READ_CHIPID_RETRIES) {
		ret = aw21024_i2c_read(aw21024, AW21024_REG_RESET, &reg_val);
		if (ret < 0) {
			dev_err(aw21024->dev,
				"%s: failed to read AW21024_CHIP_ID : %d\n",
				__func__, ret);
		} else {
			if (reg_val == AW21024_CHIP_ID) {
				AW_INFO("AW21024_CHIP_ID is : 0x%x\n", reg_val);
				is_breath_led_exist = 1;  /* zte boardtest interface */
				return 0;
			}
		}
		msleep(AW_READ_CHIPID_RETRY_DELAY);
	}
	AW_INFO("This chipid is not AW21024 and reg_data is : 0x%x\n", reg_val);

	return -EINVAL;
}

static int aw21024_read_chip_version(struct aw21024 *aw21024)
{
	int ret = -1;
	unsigned char reg_val = 0;

	ret = aw21024_i2c_read(aw21024, AW21024_REG_VER, &reg_val);
	if (ret < 0)
		dev_err(aw21024->dev, "%s: failed to read VERSION : %d\n", __func__, ret);
	else
		AW_INFO("THE CHIP_VERSION: 0x%x\n", reg_val);

	return ret;
}

/******************************************************
 *
 * sys group attribute
 *
 ******************************************************/

 /* Started by AICoder, pid:h0187uaf6a4679814dd10847a0eb2b8a94d918bc */

static int aw21024_init_and_write(struct aw21024 *aw21024, unsigned int index)
{
    int ret = 0;

    ret = aw21024_i2c_write(aw21024, AW21024_REG_GCCR, 0x80);
    if (ret) {
        AW_ERROR("Failed to write to GCCR register\n");
        return ret;
    }

    ret = aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
    if (ret) {
        AW_ERROR("Failed to write to GCFG0 register\n");
        return ret;
    }

    ret = aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);
    if (ret) {
        AW_ERROR("Failed to write to GCFG1 register\n");
        return ret;
    }

    ret = aw21024_i2c_write(aw21024, AW21024_REG_BR0 + index - 1, 0x80);
    if (ret) {
        AW_ERROR("Failed to write to BR0 register\n");
        return ret;
    }

    ret = aw21024_i2c_write(aw21024, AW21024_REG_COL0 + index - 1, 0x80);
    if (ret) {
        AW_ERROR("Failed to write to COL0 register\n");
        return ret;
    }

    ret = aw21024_i2c_write(aw21024, AW21024_REG_UPDATE, 0x00);
    if (ret) {
        AW_ERROR("Failed to write to UPDATE register\n");
        return ret;
    }

    return 0;
}

/* Started by AICoder, pid:a588an6ccaf88db1477d08c6504c2b0d78b5c204 */
void set_rgb_color(struct aw21024 *aw21024, uint32_t rgb_data) {
    aw21024->red = (rgb_data & 0xff0000) >> 16;
    aw21024->green = (rgb_data & 0x00ff00) >> 8;
    aw21024->blue = (rgb_data & 0x0000ff);
}
/* Ended by AICoder, pid:a588an6ccaf88db1477d08c6504c2b0d78b5c204 */

/* Started by AICoder, pid:6d99e6c4bdm5105144250bd7d337445b5bb99997 */
static void aw21024_set_rgb_br(struct aw21024 *aw21024, cdev_type_t led_color, uint8_t brightness)
{
	unsigned int i = 0;

	switch (led_color) {
        case RED_CDEV:
            for (i = 0; i < sizeof(aw21024_red_leds_br); i++)
                aw21024_i2c_write(aw21024, aw21024_red_leds_br[i], brightness);
            break;
        case GREEN_CDEV:
            for (i = 0; i < sizeof(aw21024_green_leds_br); i++)
                aw21024_i2c_write(aw21024, aw21024_green_leds_br[i], brightness);
            break;
        case BLUE_CDEV:
            for (i = 0; i < sizeof(aw21024_blue_leds_br); i++)
                aw21024_i2c_write(aw21024, aw21024_blue_leds_br[i], brightness);
            break;
        default:
            break; // Invalid color
    }
}

static void aw21024_set_rgb_col(struct aw21024 *aw21024, cdev_type_t led_color, uint8_t regcol)
{
	unsigned int i = 0;

	switch (led_color) {
        case RED_CDEV:
            for (i = 0; i < sizeof(aw21024_red_leds_col); i++)
                aw21024_i2c_write(aw21024, aw21024_red_leds_col[i], regcol);
            break;
        case GREEN_CDEV:
            for (i = 0; i < sizeof(aw21024_green_leds_col); i++)
                aw21024_i2c_write(aw21024, aw21024_green_leds_col[i], regcol);
            break;
        case BLUE_CDEV:
            for (i = 0; i < sizeof(aw21024_blue_leds_col); i++)
                aw21024_i2c_write(aw21024, aw21024_blue_leds_col[i], regcol);
            break;
        default:
            break; // Invalid color
    }
}

static void cancel_all_works(struct aw21024 *aw21024)
{
    if (aw21024->running_led_state != WORK_STOP) {
        aw21024->running_led_state = WORK_STOP;
        cancel_work_sync(&aw21024->running_led_work);
    }
    if (aw21024->boot_running_led_state != WORK_STOP) {
        aw21024->boot_running_led_state = WORK_STOP;
        cancel_work_sync(&aw21024->boot_running_led_work);
    }
	if (aw21024->fast_flashing_led_state != WORK_STOP) {
        aw21024->fast_flashing_led_state = WORK_STOP;
        cancel_work_sync(&aw21024->fast_flashing_led_work);
    }
	if (aw21024->alternate_flashing_state != WORK_STOP) {
        aw21024->alternate_flashing_state = WORK_STOP;
        cancel_work_sync(&aw21024->alternate_flashing_led_work);
    }
    if (aw21024->boot_running_miniled_state != WORK_STOP) {
        aw21024->boot_running_miniled_state = WORK_STOP;
        cancel_work_sync(&aw21024->boot_running_miniled_work);
    }
    if (aw21024->miniled_charging_state != WORK_STOP) {
        aw21024->miniled_charging_state = WORK_STOP;
        cancel_work_sync(&aw21024->miniled_charging_work);
    }
    AW_INFO("%s works canceled.\n", __func__);
}

static void aw21024_update(struct aw21024 *aw21024)
{
    aw21024_i2c_write(aw21024, AW21024_REG_UPDATE, 0x00);
}

static void aw21024_set_rgb_ledoff(struct aw21024 *aw21024)
{
    aw21024_i2c_write(aw21024, AW21024_REG_BR9, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_BR10, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_BR11, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_COL9, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_COL10, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_COL11, 0x00);

    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, 0);
	/* update */
    aw21024_update(aw21024);
}

static void aw21024_light_on(struct aw21024 *aw21024, uint8_t brightness)
{
    unsigned int led_brightness = 0;

    if (brightness == 0) {
        aw21024_set_rgb_ledoff(aw21024);
        return;
    }

    led_brightness =  brightness * 12 / 10;
    brightness = led_brightness > 255 ? 255 : led_brightness;
    AW_INFO("color = [%d %d %d] brightness = %d\n",
            aw21024->red, aw21024->green, aw21024->blue, brightness);

    aw21024_i2c_write(aw21024, AW21024_REG_BR9, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR10, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR11, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL9, aw21024->blue);
    aw21024_i2c_write(aw21024, AW21024_REG_COL10, aw21024->red);
    aw21024_i2c_write(aw21024, AW21024_REG_COL11, aw21024->green);
    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, brightness);

    /* update */
    aw21024_update(aw21024);
}

/* Started by AICoder, pid:c00696c7ba8b94b14f750986a2543d79033532e8 */
static void fast_flashing_led_work_func(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, fast_flashing_led_work);
    int i = 0;

    AW_INFO("aw21024->red:%d,aw21024->green:%d,aw21024->blue:%d,aw21024->cdev.brightness:%d.",
            aw21024->red, aw21024->green, aw21024->blue, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
    /* group config */
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);

    while (aw21024->fast_flashing_led_state == WORK_RUNNING) {
        if (aw21024->mini_led) {
             for (i = 0; i < 4; i++) {
                aw21024_miniled2_light_on(aw21024, 0);
                aw21024_miniled3_light_on(aw21024, 0, 0);
                usleep_range(50000, 51000);
                aw21024_i2c_write(aw21024, AW21024_REG_BR20 + i, aw21024->cdev.brightness);
                aw21024_i2c_write(aw21024, AW21024_REG_COL20 + i, aw21024->cdev.brightness);
                aw21024_miniled3_random_light_on(aw21024);
             }
        } else {
            for (i = 0; i < 5; i++) {
                aw21024_light_on(aw21024, aw21024->cdev.brightness);
                usleep_range(50000, 51000);
                aw21024_light_on(aw21024, 0);
                usleep_range(50000, 51000);
            }
            msleep(150);
        }
    }
    if (aw21024->mini_led) {
        aw21024_miniled2_light_on(aw21024, 0);
        aw21024_miniled3_light_on(aw21024, 0, 0);
    } else {
        aw21024_light_on(aw21024, 0);
    }
    aw21024->fast_flashing_led_state = WORK_STOP;
}

static void aw21024_led1_light_on(struct aw21024 *aw21024, uint8_t brightness)
{
    int i = 0;

    if (brightness > 255)
        brightness = 255;
    for (i = 0; i < 3; i++) {
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[RED_CDEV][i] - 1, brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[RED_CDEV][i] - 1, aw21024->red);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[GREEN_CDEV][i] - 1, brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[GREEN_CDEV][i] - 1, aw21024->green);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[BLUE_CDEV][i] - 1, brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[BLUE_CDEV][i] - 1, aw21024->blue);
    }
    aw21024_update(aw21024);
}

static void aw21024_led2_light_on(struct aw21024 *aw21024, uint8_t brightness)
{
    unsigned int led_brightness = 0;

    led_brightness =  brightness * 12 / 10;
    brightness = led_brightness > 255 ? 255 : led_brightness;
    aw21024_i2c_write(aw21024, AW21024_REG_BR9, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR10, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR11, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL9, aw21024->blue);
    aw21024_i2c_write(aw21024, AW21024_REG_COL10, aw21024->red);
    aw21024_i2c_write(aw21024, AW21024_REG_COL11, aw21024->green);
    /* update */
    aw21024_update(aw21024);
}

static void aw21024_led3_light_on(struct aw21024 *aw21024, uint8_t brightness)
{
    int i = 0;

    if (brightness > 255)
        brightness = 255;
    for (i = 0; i < 3; i++) {
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[RED_CDEV][i] - 1, brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[RED_CDEV][i] - 1, aw21024->red);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[GREEN_CDEV][i] - 1, brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[GREEN_CDEV][i] - 1, aw21024->green);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[BLUE_CDEV][i] - 1, brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[BLUE_CDEV][i] - 1, aw21024->blue);
    }
    aw21024_update(aw21024);
}

/* Started by AICoder, pid:845db7f9382d48b1428b08b3a1159a0a06b8ad69 */
static void aw21024_miniled2_light_on(struct aw21024 *aw21024, uint8_t brightness)
{
    if (brightness > 255)
        brightness = 255;
    aw21024_i2c_write(aw21024, AW21024_REG_BR20, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR21, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR22, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_BR23, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL20, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL21, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL22, brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL23, brightness);
    aw21024_update(aw21024);
}

static void aw21024_miniled3_charging_set(struct aw21024 *aw21024, uint8_t led_num, enum miniled_charging_state state)
{
    int i = 0;
    uint8_t brightness = aw21024->cdev.brightness;
    unsigned int led_brightness = 0;

    switch (state) {
    case MINI_LED_OFF:
        for (i = led_num; i < 20; i++) {
            aw21024_i2c_write(aw21024, AW21024_REG_BR0 + i, 0);
            aw21024_i2c_write(aw21024, AW21024_REG_COL0 + i, 0);
        }
        aw21024_update(aw21024);
        break;
    case MINI_LED_ON:
        for (i = 0; i < led_num; i++) {    
            led_brightness =  brightness * mini_leds_brightness[i] / 10;
            led_brightness = led_brightness > 255 ? 255 : led_brightness;
            aw21024_i2c_write(aw21024, AW21024_REG_BR0 + i, (unsigned char)led_brightness);
            aw21024_i2c_write(aw21024, AW21024_REG_COL0 + i, (unsigned char)led_brightness);
        }
        aw21024_update(aw21024);
        break;
    case MINI_LED_RUNNING:
        for (i = led_num; i < 20; i++) {
            if (aw21024->miniled_charging_state == WORK_STOP)
                break;
            led_brightness =  brightness * mini_leds_brightness[i] / 10;
            led_brightness = led_brightness > 255 ? 255 : led_brightness;
            aw21024_i2c_write(aw21024, AW21024_REG_BR0 + i, (unsigned char)led_brightness);
            aw21024_i2c_write(aw21024, AW21024_REG_COL0 + i, (unsigned char)led_brightness);
            aw21024_update(aw21024);
            msleep(200);
        }
        break;
    default:
        break;
    }
}

static void aw21024_miniled3_light_on(struct aw21024 *aw21024, uint8_t brightness, int mdelay)
{
    int i = 0;
    unsigned int led_brightness = 0;

    if (brightness > 255)
        brightness = 255;

    for (i = 0; i < 20; i++) {
        led_brightness =  brightness * mini_leds_brightness[i] / 10;
        led_brightness = led_brightness > 255 ? 255 : led_brightness;
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + i, (unsigned char)led_brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + i, (unsigned char)led_brightness);
        if (mdelay) {
            aw21024_update(aw21024);
            msleep(mdelay);
        }
    }
    if (!mdelay)
        aw21024_update(aw21024);
}

static void aw21024_miniled3_light_on_right_to_left(struct aw21024 *aw21024, uint8_t brightness, int mdelay)
{
    int i = 0;
    unsigned int led_brightness = 0;

    if (brightness > 255)
        brightness = 255;
    for (i = 19; i >= 0; i--) {
        led_brightness =  brightness * mini_leds_brightness[i] / 10;
        led_brightness = led_brightness > 255 ? 255 : led_brightness;
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 +  i, (unsigned char)led_brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 +  i, (unsigned char)led_brightness);
        aw21024_update(aw21024);
        msleep(mdelay);
    }
}

static void aw21024_miniled23_color_brightness(struct aw21024 *aw21024, uint8_t brightness)
{
    int i = 0;
    unsigned int led_brightness = 0;

    if (brightness > 255)
        brightness = 255;
    for (i = 0; i < 24; i++) {
        led_brightness =  brightness * mini_leds_brightness[i] / 10;
        led_brightness = led_brightness > 255 ? 255 : led_brightness;
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + i, (unsigned char)led_brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + i, (unsigned char)led_brightness);
    }
}

static void aw21024_miniled3_random_light_on(struct aw21024 *aw21024)
{
    u32 random = 0;
    u32 random_number = 0;

    random = prandom_u32();
    random_number = random % 20;
    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + random_number, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + random_number, aw21024->cdev.brightness);
    aw21024_update(aw21024);
    usleep_range(2000, 2100);
}
/* Ended by AICoder, pid:845db7f9382d48b1428b08b3a1159a0a06b8ad69 */

static void alternate_flashing_led_work_func(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, alternate_flashing_led_work);

    AW_INFO("aw21024->red:%d,aw21024->green:%d,aw21024->blue:%d,aw21024->cdev.brightness:%d.",
            aw21024->red, aw21024->green, aw21024->blue, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
    /* group config */
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);

    while (aw21024->alternate_flashing_state == WORK_RUNNING) {
        if (aw21024->mini_led) {
            aw21024_miniled3_light_on(aw21024, aw21024->cdev.brightness, 25);
            aw21024_miniled3_light_on(aw21024, 0, 0);
            msleep(25);
            aw21024_miniled3_light_on_right_to_left(aw21024, aw21024->cdev.brightness, 25);
            aw21024_miniled3_light_on(aw21024, 0, 0);
        } else {
            aw21024_led1_light_on(aw21024, aw21024->cdev.brightness);
            aw21024_led3_light_on(aw21024, aw21024->cdev.brightness);
            msleep(300);
            aw21024_led1_light_on(aw21024, 0);
            aw21024_led3_light_on(aw21024, 0);
            msleep(300);
            aw21024_led2_light_on(aw21024, aw21024->cdev.brightness);
            msleep(300);
            aw21024_led2_light_on(aw21024, 0);
            msleep(300);
            aw21024_led1_light_on(aw21024, aw21024->cdev.brightness);
            aw21024_led2_light_on(aw21024, aw21024->cdev.brightness);
            aw21024_led3_light_on(aw21024, aw21024->cdev.brightness);
            msleep(1000);
            aw21024_led1_light_on(aw21024, 0);
            aw21024_led2_light_on(aw21024, 0);
            aw21024_led3_light_on(aw21024, 0);
            msleep(300);
        }
    }
    if (aw21024->mini_led) {
        aw21024_miniled3_light_on(aw21024, 0, 0);
    } else {        
        aw21024_set_rgb_br(aw21024, RED_CDEV, 0);
        aw21024_set_rgb_br(aw21024, GREEN_CDEV, 0);
        aw21024_set_rgb_br(aw21024, BLUE_CDEV, 0);
        aw21024_update(aw21024);
    }
    aw21024->alternate_flashing_state = WORK_STOP;
}

static void running_led_ring(struct aw21024 *aw21024, int current_led)
{
    int last1 = 0, last2 = 0;

    if ((current_led >= RUNNING_LED_MAX) || (current_led < 0))
        current_led = 0;
    last1 = current_led - 1;
    last2 = current_led - 2;
    if (last1 < 0)
        last1 = last1 + RUNNING_LED_MAX;
    if (last2 < 0)
        last2 = last2 + RUNNING_LED_MAX;

    if (aw21024->red) {
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_red_running_leds[current_led] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_red_running_leds[current_led] - 1, aw21024->red);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_red_running_leds[last1] - 1, aw21024->cdev.brightness / 2);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_red_running_leds[last1] - 1, aw21024->red);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_red_running_leds[last2] - 1, 0);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_red_running_leds[last2] - 1, 0);
    }
    if (aw21024->green) {
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_green_running_leds[current_led] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_green_running_leds[current_led] - 1, aw21024->green);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_green_running_leds[last1] - 1, aw21024->cdev.brightness / 2);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_green_running_leds[last1] - 1, aw21024->green);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_green_running_leds[last2] - 1, 0);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_green_running_leds[last2] - 1, 0);
    }
    if (aw21024->blue) {
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_blue_running_leds[current_led] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_blue_running_leds[current_led] - 1, aw21024->blue);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_blue_running_leds[last1] - 1, aw21024->cdev.brightness / 2);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_blue_running_leds[last1] - 1, aw21024->blue);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_blue_running_leds[last2] - 1, 0);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_blue_running_leds[last2] - 1, 0);
    }
    /* update */
    aw21024_update(aw21024);
}

static void running_led_one_ring(struct aw21024 *aw21024)
{
    int i = 0;

    for (i = 0; i < RUNNING_LED_MAX; i++) {
        if (aw21024->red) {
            aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_red_running_leds[i] - 1, aw21024->cdev.brightness);
            aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_red_running_leds[i] - 1, aw21024->red);
        }
        if (aw21024->green) {
            aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_green_running_leds[i] - 1, aw21024->cdev.brightness);
            aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_green_running_leds[i] - 1, aw21024->green);
        }
        if (aw21024->blue) {
            aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_blue_running_leds[i] - 1, aw21024->cdev.brightness);
            aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_blue_running_leds[i] - 1, aw21024->blue);
        }
        /* update */
        aw21024_update(aw21024);
        msleep(50);
        aw21024_led1_light_on(aw21024, 0);
        aw21024_led3_light_on(aw21024, 0);
    }
}

static void running_led_lefttoright(struct aw21024 *aw21024)
{
    int i = 0;

    for (i = 0; i < 3; i++) {
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[RED_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[RED_CDEV][i] - 1, aw21024->red);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[GREEN_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[GREEN_CDEV][i] - 1, aw21024->green);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[BLUE_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[BLUE_CDEV][i] - 1, aw21024->blue);

        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[RED_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[RED_CDEV][i] - 1, aw21024->red);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[GREEN_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[GREEN_CDEV][i] - 1, aw21024->green);
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[BLUE_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[BLUE_CDEV][i] - 1, aw21024->blue);
        aw21024_update(aw21024);
        msleep(100);
    }
}

/* Started by AICoder, pid:mc0060e84b1ca3c146910a3c8040063167323af8 */
static void set_led13_light(struct aw21024 *aw21024, uint8_t level)
{
    // Ensure the level is within the valid range (1 to 3)
    if (level > 3) {
        level = 3;
    } else if (level <= 0) {
        level = 1;
    }

    // LED1 Configuration
    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[RED_CDEV][level - 1] - 1, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[RED_CDEV][level - 1] - 1, aw21024->red);

    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[GREEN_CDEV][level - 1] - 1, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[GREEN_CDEV][level - 1] - 1, aw21024->green);

    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[BLUE_CDEV][level - 1] - 1, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[BLUE_CDEV][level - 1] - 1, aw21024->blue);

    // LED3 Configuration
    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[RED_CDEV][level - 1] - 1, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[RED_CDEV][level - 1] - 1, aw21024->red);

    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[GREEN_CDEV][level - 1] - 1, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[GREEN_CDEV][level - 1] - 1, aw21024->green);

    aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[BLUE_CDEV][level - 1] - 1, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[BLUE_CDEV][level - 1] - 1, aw21024->blue);

    aw21024_update(aw21024);
}
/* Ended by AICoder, pid:mc0060e84b1ca3c146910a3c8040063167323af8 */

static void running_led_work_func(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, running_led_work);
    int current_led = 0;
    int color_count = 0;

    AW_INFO("aw21024->red:%d,aw21024->green:%d,aw21024->blue:%d,aw21024->cdev.brightness:%d.",
            aw21024->red, aw21024->green, aw21024->blue, aw21024->cdev.brightness);

    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
    /* group config */
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, AW21024_GCFG1_SET);

    while (aw21024->running_led_state == WORK_RUNNING) {
        if ((aw21024->effect_mode == RUNNING_LED_7COLOR) || (aw21024->effect_mode == RUNNING_LED)) {
            if (aw21024->effect_mode == RUNNING_LED_7COLOR) {
                if (color_count >= 7)
                    color_count = 0;
                aw21024->red = (aw21024_7color[color_count] & 0xff0000) >> 16;
                aw21024->green = (aw21024_7color[color_count] & 0x00ff00) >> 8;
                aw21024->blue = (aw21024_7color[color_count] & 0x0000ff);
                color_count++;
            }
            if (current_led >= RUNNING_LED_MAX)
                current_led = 0;
            running_led_ring(aw21024, current_led);
            msleep(100);
            current_led++;
        } else if (aw21024->effect_mode == RUNNING_LED_LEFTTORIGHT) {
            running_led_lefttoright(aw21024);
            aw21024_led1_light_on(aw21024, 0);
            aw21024_led3_light_on(aw21024, 0);
            msleep(400);
        } else if (aw21024->effect_mode == LED1_3_CHARGING) {
            running_led_one_ring(aw21024);
            msleep(100);
            aw21024_led1_light_on(aw21024, 0);
            aw21024_led3_light_on(aw21024, 0);
            msleep(3000);
        }
    }

    aw21024->running_led_state = WORK_STOP;
    aw21024_set_rgb_br(aw21024, RED_CDEV, 0);
    aw21024_set_rgb_br(aw21024, GREEN_CDEV, 0);
    aw21024_set_rgb_br(aw21024, BLUE_CDEV, 0);
    /* update */
    aw21024_update(aw21024);
}

/* Started by AICoder, pid:o3e60pebc41f5ab14eb20ab51084ae660839f736 */
static void boot_running_led_work_func(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, boot_running_led_work);
    uint8_t brightness = 0;
    int i = 0;

    // Initialize the brightness of the LED
   aw21024->cdev.brightness = 200;
    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);

    // Configure the group settings
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);

    set_rgb_color(aw21024, COLOR_YELLOW);
    // Loop to turn on LEDs
    for (i = 0; i < 3; i++) {
        if (aw21024->boot_running_led_state == WORK_STOP)
            goto end;

#if 0
        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_1[BLUE_CDEV][i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_1[BLUE_CDEV][i] - 1, aw21024->cdev.brightness);

        aw21024_i2c_write(aw21024, AW21024_REG_BR0 + aw21024_leds_3[BLUE_CDEV][2 - i] - 1, aw21024->cdev.brightness);
        aw21024_i2c_write(aw21024, AW21024_REG_COL0 + aw21024_leds_3[BLUE_CDEV][2 - i] - 1, aw21024->cdev.brightness);

        aw21024_update(aw21024);
        msleep(1000);
#else
        set_led13_light(aw21024, i + 1);
        msleep(1000);
#endif
    }

    // Turn off LED1 and LED3
    aw21024_led1_light_on(aw21024, 0);
    aw21024_led3_light_on(aw21024, 0);
    msleep(1000);

    set_rgb_color(aw21024, COLOR_YELLOW);
    for (i = 0; i < 100; i++) {
        if (aw21024->boot_running_led_state == WORK_STOP)
            goto end;

        brightness = 2 * (i + 1);
        aw21024_led1_light_on(aw21024, brightness);
        aw21024_led3_light_on(aw21024, brightness);
        msleep(50);
    }

    set_rgb_color(aw21024, COLOR_YELLOW);
    for (i = 0; i < 100; i++) {
        if (aw21024->boot_running_led_state == WORK_STOP)
            goto end;

        brightness = 2 * (i + 1);
        aw21024_led2_light_on(aw21024, brightness);
        msleep(50);
    }

    msleep(2000);

end:
    // Turn off all LEDs
    aw21024_led1_light_on(aw21024, 0);
    aw21024_led2_light_on(aw21024, 0);
    aw21024_led3_light_on(aw21024, 0);
}
/* Ended by AICoder, pid:o3e60pebc41f5ab14eb20ab51084ae660839f736 */

static void boot_running_miniled_work_func(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, boot_running_miniled_work);
    int i = 0;
    int mini_brightness = 0;

    aw21024->cdev.brightness = 100; 
    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
    // Configure the group settings
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);

    switch (aw21024->step) {
    case 1:
        mini_brightness = 80;
        aw21024_miniled3_light_on(aw21024, mini_brightness, 75);
        aw21024_hw_reset(aw21024);
        msleep(150);
        aw21024_miniled3_light_on_right_to_left(aw21024, mini_brightness, 75);
        aw21024->step = 2;
        break;
    case 2:
        aw21024_hw_reset(aw21024);
        usleep_range(200, 400);
        aw21024->step = 3;
        break;
    case 3:
        for (i = 1; i <= 100; i++) {
            mini_brightness = i;
            aw21024_miniled3_light_on(aw21024, mini_brightness, 0);
            msleep(30);
        }
        aw21024->step = 4;
        break;
    case 4:
        for (i = 1; i <= 120; i++) {
            mini_brightness = i;
            aw21024_miniled2_light_on(aw21024, mini_brightness);
            msleep(25);
        }
        aw21024->step = 5;
        break;
    case 5:
        /* hardware reset */
        aw21024_hw_reset(aw21024);
        usleep_range(200, 400);
        aw21024->step = 1;
        break;
    default:
        break;
    }
}

static void miniled_charging_work_func(struct work_struct *work)
{
    struct aw21024 *aw21024 = container_of(work, struct aw21024, miniled_charging_work);
    int led_num = 0;

    led_num = aw21024->effect_brightness * 20 / 100;
    if (led_num <= 0) led_num = 1;
    if (led_num >= 20) led_num = 19;
    AW_INFO("aw21024->cdev.brightness:%d, led_num:%d.", aw21024->cdev.brightness, led_num);

    aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);
    aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
    aw21024_miniled2_light_on(aw21024, aw21024->cdev.brightness / 10 * 12);
    aw21024_miniled3_charging_set(aw21024, led_num, MINI_LED_ON);
    aw21024_miniled3_charging_set(aw21024, led_num, MINI_LED_OFF);
    while (aw21024->miniled_charging_state == WORK_RUNNING) {
        led_num = aw21024->effect_brightness * 20 / 100;
        if (led_num <= 0) led_num = 1;
        if (led_num >= 20) led_num = 19;
        aw21024_miniled3_charging_set(aw21024, led_num, MINI_LED_ON);
        aw21024_miniled3_charging_set(aw21024, led_num, MINI_LED_RUNNING);
        msleep(200);
        aw21024_miniled3_charging_set(aw21024, led_num, MINI_LED_OFF);
        msleep(200);

    }

    aw21024->miniled_charging_state = WORK_STOP;
    aw21024_miniled3_light_on(aw21024, 0, 0);
}

static uint32_t aw21024_get_pat_time_cfg(int16_t ms)
{
    int32_t i = 0;
    int32_t len = sizeof(pat_time_cfg_group) / sizeof(struct pat_time);

    for (i = 0; i < len; i++) {
        if (pat_time_cfg_group[i].ms >= ms)
            return pat_time_cfg_group[i].reg_val;
    }

    return -1;
}

static int32_t aw21024_patcfg_set(struct aw21024 *aw21024,
                                  bool flag, uint8_t times)
{
    uint8_t t0_cfg, t1_cfg, t2_cfg, t3_cfg;

    t0_cfg = aw21024_get_pat_time_cfg(aw21024->pat_time_cfg.t0);
    t1_cfg = aw21024_get_pat_time_cfg(aw21024->pat_time_cfg.t1);
    t2_cfg = aw21024_get_pat_time_cfg(aw21024->pat_time_cfg.t2);
    t3_cfg = aw21024_get_pat_time_cfg(aw21024->pat_time_cfg.t3);

    if (!flag) {
        AW_INFO("pat off.\n");

        /* group config */
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);

        aw21024_i2c_write(aw21024, AW21024_REG_PATT0, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_PATT1, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_PATT2, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_PATT3, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_PATCFG, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_PATGO, 0x00);
        return 0;
    }

    aw21024_i2c_write(aw21024, AW21024_REG_PATT0, ((t0_cfg << 4) | t1_cfg));
    aw21024_i2c_write(aw21024, AW21024_REG_PATT1, ((t2_cfg << 4) | t3_cfg));
    aw21024_i2c_write(aw21024, AW21024_REG_PATT2, AW21024_PATT2_SET);
    aw21024_i2c_write(aw21024, AW21024_REG_PATT3, times);
    aw21024_i2c_write(aw21024, AW21024_REG_FADEH, aw21024->cdev.brightness);
    aw21024_i2c_write(aw21024, AW21024_REG_FADEL, 0x00);
    if (aw21024->mini_led)
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0xff);
    else
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, AW21024_GCFG0_SET);
    aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, AW21024_GCFG1_SET);
    aw21024_i2c_write(aw21024, AW21024_REG_PATCFG, AW21024_PATCFG_SET);
    return 0;
}

/* Started by AICoder, pid:y9882l43a95e6fa1451d0992a020104e59e56d05 */
static ssize_t led_test_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    unsigned int databuf[2] = {0, 0};
    unsigned int color_cdev = -1;
    uint8_t brightness = 0;
    uint8_t led_num = 0;
    int ret = 0;

    if (sscanf(buf, "%d %d", &databuf[0], &databuf[1]) == 2) {
        cancel_all_works(aw21024);
        aw21024_hw_reset(aw21024);
        msleep(100);

        if (databuf[0] == 0) {
            if (databuf[1] != 0) {
                aw21024->led_test = true;
                led_num = databuf[1];
                ret = aw21024_init_and_write(aw21024, led_num);
                if (ret) {
                    AW_ERROR("Failed to initialize and write registers\n");
                    return ret;
                }
            } else {
                aw21024->led_test = false;
                return count;
            }
        } else {
            aw21024->led_test = true;
            color_cdev = databuf[0] - 1;

            if (databuf[1] > 255)
                databuf[1] = 255;

            brightness = databuf[1];
            aw21024_i2c_write(aw21024, AW21024_REG_GCCR, brightness);
            if(!aw21024->mini_led) {
                aw21024_set_rgb_br(aw21024, color_cdev, brightness);
                aw21024_set_rgb_col(aw21024, color_cdev, 0xff);
                aw21024_update(aw21024);
            } else {
                if (color_cdev == 1) {
                    aw21024_miniled2_light_on(aw21024, brightness);
                } else if (color_cdev == 2){
                    aw21024_miniled3_light_on(aw21024, brightness, 0);
                } else {
                    aw21024_miniled2_light_on(aw21024, brightness + 20);
                    aw21024_miniled3_light_on(aw21024, brightness, 0);
                }
            }
        }
    }

    return count;
}
/* Ended by AICoder, pid:y9882l43a95e6fa1451d0992a020104e59e56d05 */
/* Ended by AICoder, pid:h0187uaf6a4679814dd10847a0eb2b8a94d918bc */

static ssize_t led_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	AW_INFO("no used");
	return 0;
}

/* Started by AICoder, pid:l5c100855czb898144420a3c108d6b91a75850ab */
static ssize_t groupcolor_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    uint32_t rgb_data = 0;

    if (aw21024->led_test) {
        AW_INFO("enter, led testing\n");
        return len;
    }

    aw21024->breath_mode = false;
    aw21024_soft_reset(aw21024);
    if (sscanf(buf, "%x", &rgb_data) == 1) {
        AW_INFO("enter, rgb_data=0x%x\n", rgb_data);
        cancel_all_works(aw21024);
        aw21024->effect_brightness = (rgb_data & 0xff000000) >> 24;
        aw21024->red = (rgb_data & 0xff0000) >> 16;
        aw21024->green = (rgb_data & 0x00ff00) >> 8;
        aw21024->blue = (rgb_data & 0x0000ff);
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x00);
        if (rgb_data == 0) {
            if (aw21024->mini_led) {
                aw21024_miniled2_light_on(aw21024,  0);
                aw21024_miniled3_light_on(aw21024,  0, 0);
            } else {
                aw21024_led1_light_on(aw21024, 0);
                aw21024_led2_light_on(aw21024, 0);
                aw21024_led3_light_on(aw21024, 0);
            }
            return len;
        }
        if (aw21024->mini_led) {
            AW_INFO("mini_led groupcolor set\n");
            aw21024->cdev.brightness = 80;
            aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
            aw21024_miniled2_light_on(aw21024,  aw21024->cdev.brightness / 10 * 12);
            aw21024_miniled3_light_on(aw21024,  aw21024->cdev.brightness, 0);
        } else {
           aw21024->cdev.brightness = 200;
           aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
           aw21024_light_on(aw21024, aw21024->cdev.brightness);
        }
    }

    return len;
}

static ssize_t groupcolor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    AW_INFO("no used");
    return 0;
}

static ssize_t breathcolor_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    uint32_t rgb_data = 0, on_ms = 0, off_ms = 0;

    if (aw21024->led_test) {
        AW_INFO("enter, led testing\n");
        return len;
    }

    if (sscanf(buf, "%x %x %x", &rgb_data, &on_ms, &off_ms) == 3) {
        AW_INFO("enter, rgb_data=0x%06x on_ms=0x%x off_ms=0x%x\n", rgb_data, on_ms, off_ms);

        if (aw21024->miniled_charging_state == WORK_RUNNING) {
            aw21024->effect_brightness = (rgb_data & 0xff000000) >> 24;
            return len;
        } else {
            aw21024_soft_reset(aw21024);
            cancel_all_works(aw21024);
        }
        if (rgb_data == 0) {
            if (aw21024->mini_led) {
                aw21024_miniled2_light_on(aw21024,  0);
                aw21024_miniled3_light_on(aw21024,  0, 0);
            } else {
                aw21024_light_on(aw21024, 0);
            }
            return len;
        }

        aw21024->effect_brightness = (rgb_data & 0xff000000) >> 24;
        aw21024->flash_times = 0;

        /* set sl */
        aw21024->red = (rgb_data & 0xff0000) >> 16;
        aw21024->green = (rgb_data & 0x00ff00) >> 8;
        aw21024->blue = (rgb_data & 0x0000ff);

        /* pat config */
        aw21024->pat_time_cfg.t0 = (on_ms >> 16) & 0xffff;
        aw21024->pat_time_cfg.t1 = on_ms & 0xffff;
        aw21024->pat_time_cfg.t2 = (off_ms >> 16) & 0xffff;
        aw21024->pat_time_cfg.t3 = off_ms & 0xffff;
        aw21024->breath_mode = true;
        if ((on_ms == 1) && (off_ms == 1)) {
            AW_INFO("enter, fast flashing mode\n");
            //aw21024->fast_flashing_led_state = WORK_RUNNING;
            //schedule_work(&aw21024->fast_flashing_led_work);
            return len;
        }

        AW_INFO("color = [%d %d %d] effect_brightness = %d brightness = %d\n",
                aw21024->red, aw21024->green, aw21024->blue, aw21024->effect_brightness, aw21024->cdev.brightness);

        if (aw21024->mini_led) {
            aw21024->cdev.brightness = 100;
            aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
            aw21024_miniled23_color_brightness(aw21024, aw21024->cdev.brightness);
        } else {
           aw21024->cdev.brightness = 200;
           aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
           aw21024_led2_light_on(aw21024, aw21024->cdev.brightness);
        }
        aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);

        aw21024_patcfg_set(aw21024, true, aw21024->flash_times);

        /* PAT run */
        aw21024_i2c_write(aw21024, AW21024_REG_PATGO, AW21024_PATGO_SET);
    }

    return len;
}

static ssize_t breathcolor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    AW_INFO("no used");
    return 0;
}
/* Ended by AICoder, pid:l5c100855czb898144420a3c108d6b91a75850ab */

static ssize_t dancecolor_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    unsigned int rgb_data;
    unsigned int mode;

    if (aw21024->led_test) {
        AW_INFO("enter,led testing\n");
         return len;
    }
    if (aw21024->mini_led) {
        AW_INFO("enter, mini_led return\n");
        return len;
    }
    if (sscanf(buf, "%x %x", &rgb_data, &mode)) {
        aw21024->effect_mode = mode;
        AW_INFO("rgb_data=0x%x, mode=0x%x", rgb_data, mode);
        cancel_all_works(aw21024);
        aw21024->red = (rgb_data & 0xff0000) >> 16;
        aw21024->green = (rgb_data & 0x00ff00) >> 8;
        aw21024->blue = (rgb_data & 0x0000ff);
        if (aw21024->red || aw21024->green || aw21024->blue) {
            if (mode == ALTERNATE_FLASHING) {
                aw21024->alternate_flashing_state = WORK_RUNNING;
                schedule_work(&aw21024->alternate_flashing_led_work);
            } else if (mode == BOOT_RUNNING_LED) {
                aw21024->boot_running_led_state = WORK_RUNNING;
                schedule_work(&aw21024->boot_running_led_work);
            } else {
                aw21024->running_led_state = WORK_RUNNING;
                schedule_work(&aw21024->running_led_work);
            }
        } else {
            aw21024->running_led_state = WORK_STOP;
            aw21024->alternate_flashing_state = WORK_STOP;
            aw21024->boot_running_led_state = WORK_STOP;
            aw21024->fast_flashing_led_state = WORK_STOP;
        }
    }
    return len;
}
/* Ended by AICoder, pid:c00696c7ba8b94b14f750986a2543d79033532e8 */

static ssize_t dancecolor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    AW_INFO("no used");
    return 0;
}

static ssize_t effect_color_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "effect_color=0x%x(%d)\n",
                    aw21024->effect_color, aw21024->effect_color);
    return len;
}

static ssize_t effect_color_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    int rc;
    unsigned int val = 0;

    rc = sscanf(buf, "%x", &val);
    if (rc < 0)
        return rc;

    aw21024->effect_color = val;

    return len;
}

static ssize_t effect_mode_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    ssize_t len = 0;

    len += snprintf(buf + len, PAGE_SIZE - len, "work_mode=%x, scene=%x\n",
                    aw21024->work_mode, aw21024->scene);
    return len;
}

/* Started by AICoder, pid:6733bk16e5bc7c414a050aeb90a4c35a7048c336 */
static ssize_t effect_mode_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    int rc;
    unsigned int val = 0;

    rc = sscanf(buf, "%x", &val);
    if (rc < 0)
        return rc;
    aw21024->effect_mode = 0;
    aw21024->scene = val & 0x0F;
    aw21024->work_mode = val & 0xF0;
    AW_INFO("work_mode=0x%x, scene=0x%x\n", aw21024->work_mode, aw21024->scene);
    if (!aw21024->red && !aw21024->green && !aw21024->blue) {
        AW_ERROR("LED COLORis 0!");
        return len;
    }
 
    switch (aw21024->work_mode) {
        case MUSIC_DANCE:
            if (aw21024->mini_led)
                return len;
            aw21024->effect_mode = 0;
            if (aw21024->effect_brightness <= 100) {
                aw21024_led1_light_on(aw21024, 0);
                aw21024_led3_light_on(aw21024, 0);
                set_led13_light(aw21024, 1);
            } else if(aw21024->effect_brightness <= 150) {
                aw21024_led1_light_on(aw21024, 0);
                aw21024_led3_light_on(aw21024, 0);
                set_led13_light(aw21024, 1);
                set_led13_light(aw21024, 2);
            } else {
                set_led13_light(aw21024, 1);
                set_led13_light(aw21024, 2);
                set_led13_light(aw21024, 3);
            }
            return len;
        case LIGHT_LONG:
            if (!aw21024->mini_led) {
                aw21024_led1_light_on(aw21024, aw21024->cdev.brightness);
                aw21024_led3_light_on(aw21024, aw21024->cdev.brightness);
                return len;
            }
            break;
        case BREATH:
            if (!aw21024->mini_led) {
                aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0xff);
                aw21024_led1_light_on(aw21024, aw21024->cdev.brightness);
                aw21024_led3_light_on(aw21024, aw21024->cdev.brightness);
                return len;
            }
            break;
        case FLASH:
            aw21024->effect_mode = RUNNING_LED;
            break;
        case ALTER_FLASH:
            aw21024->effect_mode = ALTERNATE_FLASHING;
            break;
        case FAST_FLASH:
            aw21024->effect_mode = 0;
            break;
        default:
            return -EINVAL; // Invalid color_cdev
    }
    if ((aw21024->scene == MESSAGE) && (!aw21024->mini_led)) {
        AW_INFO("MESSAGE effect!");
        aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
        aw21024_patcfg_set(aw21024, true, 0);
        aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0xff);
        aw21024_led1_light_on(aw21024, aw21024->cdev.brightness);
        aw21024_led2_light_on(aw21024, aw21024->cdev.brightness);
        aw21024_led3_light_on(aw21024, aw21024->cdev.brightness);
        /* PAT run */
        aw21024_i2c_write(aw21024, AW21024_REG_PATGO, AW21024_PATGO_SET);
        return len;
    }
    if (aw21024->effect_mode == ALTERNATE_FLASHING) {
        aw21024->alternate_flashing_state = WORK_RUNNING;
        schedule_work(&aw21024->alternate_flashing_led_work);
    } else if (aw21024->work_mode == FAST_FLASH) {
        aw21024->fast_flashing_led_state = WORK_RUNNING;
        schedule_work(&aw21024->fast_flashing_led_work);
    } else if (aw21024->scene == CHARGING) {
        if (aw21024->breath_mode) {
            if (aw21024->mini_led) {
                aw21024->cdev.brightness = 50;
                aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
                aw21024->miniled_charging_state = WORK_RUNNING;
	            schedule_work(&aw21024->miniled_charging_work);
            }
        } else {
            if (aw21024->mini_led) {
                aw21024->cdev.brightness = 50;
                aw21024_i2c_write(aw21024, AW21024_REG_GCCR, aw21024->cdev.brightness);
                aw21024_miniled3_light_on(aw21024, aw21024->cdev.brightness, 0);
            }
        }
    } else if  (aw21024->effect_mode == RUNNING_LED){
        if (!aw21024->mini_led) {
            aw21024->running_led_state = WORK_RUNNING;
            schedule_work(&aw21024->running_led_work);
        }
    }
    return len;
}
/* Ended by AICoder, pid:6733bk16e5bc7c414a050aeb90a4c35a7048c336 */

/* Ended by AICoder, pid:6d99e6c4bdm5105144250bd7d337445b5bb99997 */
static ssize_t
reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);

	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw21024_i2c_write(aw21024, databuf[0], databuf[1]);
	return count;
}

static ssize_t reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;

	for (i = 0; i < AW21024_REG_MAX; i++) {
		if (!(aw21024_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw21024_i2c_read(aw21024, i, &reg_val);
		len +=
		    snprintf(buf + len, PAGE_SIZE - len, "reg:0x%02x=0x%02x\n",
			     i, reg_val);
	}
	return len;
}

static ssize_t
hwen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val == 1)
		aw21024_hw_reset(aw21024);
	else
		aw21024_hw_off(aw21024);

	return count;
}

static ssize_t
hwen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
	ssize_t len = 0;

	len +=
	snprintf(buf + len, PAGE_SIZE - len, "hwen=%d\n", gpio_get_value(aw21024->reset_gpio));

	return len;
}

static ssize_t
rgbcolor_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[2] = { 0, 0 };
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);


	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		/* GEn 0-7=0 */
		aw21024_i2c_write(aw21024, AW21024_REG_GCFG0, 0x00);
		/* GEn disable */
		aw21024_i2c_write(aw21024, AW21024_REG_GCFG1, 0x10);
		/* RGBMD=1 3=1 */
		aw21024_i2c_write(aw21024, AW21024_REG_GCR2, 0x01);
		/* brightness default */
		aw21024_i2c_write(aw21024, AW21024_REG_GCCR, 0x25);
		/* before trim */
		/*aw21024_i2c_write(aw21024, AW21024_REG_BR0 + databuf[0] * 2, 0xff);*/
		/* after trim */
		aw21024_i2c_write(aw21024, AW21024_REG_BR0 + databuf[0], 0xff);
		aw21024->rgbcolor = (databuf[1] & 0x00ff0000) >> 16;
		aw21024_i2c_write(aw21024, AW21024_REG_COL0 + databuf[0] * 3, aw21024->rgbcolor);
		aw21024->rgbcolor = (databuf[1] & 0x0000ff00) >> 8;
		aw21024_i2c_write(aw21024, AW21024_REG_COL0 + databuf[0] * 3 + 1,
				  aw21024->rgbcolor);
		aw21024->rgbcolor = (databuf[1] & 0x000000ff);
		aw21024_i2c_write(aw21024, AW21024_REG_COL0 + databuf[0] * 3 + 2,
				  aw21024->rgbcolor);
		aw21024_br_update(aw21024);
	}
	return len;
}

static ssize_t
effect_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;
	if (val < (sizeof(aw21024_cfg_array) / sizeof(struct aw21024_cfg))) {
		AW_INFO("%s: enter effect!!!\n", __func__);
		aw21024->effect = val;
		aw21024_cfg_update_array(aw21024);
	}

	return len;
}

static ssize_t
effect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);

	for (i = 0; i < sizeof(aw21024_cfg_array) / sizeof(struct aw21024_cfg); i++) {
		len +=
		snprintf(buf + len, PAGE_SIZE - len, "cfg[%x] = %ps\n", i, aw21024_cfg_array[i].p);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "current cfg = %ps\n",
		aw21024_cfg_array[aw21024->effect].p);

	return len;
}

static ssize_t mini_led_boot_step_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    ssize_t len = 0;

    len += snprintf(buf, PAGE_SIZE, "%d\n", aw21024->step);
    return len;
}

static ssize_t mini_led_boot_step_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw21024 *aw21024 = container_of(led_cdev, struct aw21024, cdev);
    int rc;
    unsigned int val = 0;

    rc = sscanf(buf, "%x", &val);
    if (rc < 0)
        return rc;
    aw21024->step = val;
    AW_INFO("aw21024->step:%d\n", aw21024->step);
#ifndef ZTE_FEATURE_PV_AR
    cancel_all_works(aw21024);
    schedule_work(&aw21024->boot_running_miniled_work);
#endif
    return len;
}

static DEVICE_ATTR_RW(led_test);
static DEVICE_ATTR_RW(groupcolor);
static DEVICE_ATTR_RW(breathcolor);
static DEVICE_ATTR_RW(dancecolor);
static DEVICE_ATTR_RW(effect_color);
static DEVICE_ATTR_RW(effect_mode);
static DEVICE_ATTR_RW(mini_led_boot_step);
static DEVICE_ATTR_RW(reg);
static DEVICE_ATTR_RW(hwen);
static DEVICE_ATTR_WO(rgbcolor);
static DEVICE_ATTR_RW(effect);

static struct attribute *aw21024_attributes[] = {
	&dev_attr_led_test.attr,
	&dev_attr_groupcolor.attr,
	&dev_attr_breathcolor.attr,
	&dev_attr_dancecolor.attr,
	&dev_attr_effect_color.attr,
	&dev_attr_effect_mode.attr,
    &dev_attr_mini_led_boot_step.attr,
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_rgbcolor.attr,
	&dev_attr_effect.attr,
	NULL
};

static struct attribute_group aw21024_attribute_group = {
	.attrs = aw21024_attributes
};

/* Started by AICoder, pid:7dc8419a7530aab1427908625199a073e8733617 */
static int aw21024_init_led_cdev(struct aw21024 *aw21024)
{
    int ret = -1, i = 0, j = 0;

    dev_info(aw21024->dev, "enter\n");

    for (i = 0; i < LED_CDEV; i++) {
        aw21024->rgbcdev[i].brightness = 255;
        aw21024->rgbcdev[i].max_brightness = 255;
        switch (i) {
            case RED_CDEV:
                aw21024->rgbcdev[i].name = "red";
                aw21024->rgbcdev[i].brightness_set = aw21024_set_red_brightness;
                break;
            case GREEN_CDEV:
                aw21024->rgbcdev[i].name = "green";
                aw21024->rgbcdev[i].brightness_set = aw21024_set_green_brightness;
                break;
            case BLUE_CDEV:
                aw21024->rgbcdev[i].name = "blue";
                aw21024->rgbcdev[i].brightness_set = aw21024_set_blue_brightness;
                break;
            default:
                return -EINVAL; // Invalid color_cdev
        }
        ret = led_classdev_register(aw21024->dev, &aw21024->rgbcdev[i]);
        if (ret) {
            dev_err(aw21024->dev, "unable to register led ret=%d\n", ret);
            goto free_class;
        }
    }
	aw21024->cdev.name = "aw_led";
    INIT_WORK(&aw21024->brightness_work, aw21024_brightness_work);
    aw21024->cdev.brightness_set = aw21024_set_brightness;
    ret = led_classdev_register(aw21024->dev, &aw21024->cdev);
    if (ret) {
        dev_err(aw21024->dev, "unable to register led ret=%d\n", ret);
        goto free_class;
    }
	INIT_WORK(&aw21024->rgbbrightness_work, aw21024_color_brightness_work);
    ret = sysfs_create_group(&aw21024->cdev.dev->kobj, &aw21024_attribute_group);
    if (ret) {
        dev_err(aw21024->dev, "led sysfs ret: %d\n", ret);
        goto free_class;
    }

    return 0;

free_class:
    for (j = 0; j < i; j++)
        led_classdev_unregister(&aw21024->rgbcdev[j]);

    return ret;
}
/* Ended by AICoder, pid:7dc8419a7530aab1427908625199a073e8733617 */

/******************************************************
 *
 * dts gpio
 *
 ******************************************************/

static int aw21024_parse_dts(struct device *dev, struct aw21024 *aw21024, struct device_node *np)
{
	int ret = 0;

	aw21024->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	ret = of_property_read_u32(np, "clk_pwm", &aw21024->clk_pwm);
	if (ret) {
		aw21024->clk_pwm = 1;
		dev_err(dev, "%s dts clk_pwm not found\n", __func__);
	} else {
		dev_info(dev, "%s read dts clk_pwm = %d\n", __func__, aw21024->clk_pwm);
	}
	aw21024->apse_mode = of_property_read_bool(np, "apse_mode");
	if (aw21024->apse_mode)
		dev_info(dev, "%s driver use apse_mode\n", __func__);
	else
		dev_info(dev, "%s driver use general_mode\n", __func__);

	aw21024->mini_led = of_property_read_bool(np, "mini_led");
	if (aw21024->mini_led)
		dev_info(dev, "%s led is mini_led\n", __func__);
	else
		dev_info(dev, "%s led is not mini_lede\n", __func__);
	ret = of_property_read_u32(np, "led_current", &aw21024->dts_led_current);
	if (ret) {
		aw21024->dts_led_current = 125;
		dev_err(dev, "%s dts led_current not found\n", __func__);
	} else {
		dev_info(dev, "%s read dts led_current = %d\n", __func__, aw21024->dts_led_current);
	}
	ret = of_property_read_u32(np, "brightness", &aw21024->cdev.brightness);
	if (ret) {
		aw21024->cdev.brightness = 200;
		dev_err(dev, "%s dts brightness not found\n", __func__);
	} else {
		dev_info(dev, "%s read dts brightness = %d\n", __func__, aw21024->cdev.brightness);
	}
	ret = of_property_read_u32(np, "max_brightness", &aw21024->cdev.max_brightness);
	if (ret) {
		aw21024->cdev.max_brightness = 255;
		dev_err(dev, "%s dts max_brightness not found\n", __func__);
	} else {
		dev_info(dev, "%s read dts max_brightness = %d\n", __func__,
			aw21024->cdev.max_brightness);
	}

	return 0;
}

/* Started by AICoder, pid:k5cabk656ff5ab9143580af9c014e5169a10056f */
void init_led_states(struct aw21024 *aw21024) {
    aw21024->running_led_state = WORK_STOP;
    aw21024->boot_running_led_state = WORK_STOP;
    aw21024->boot_running_miniled_state = WORK_STOP;
    aw21024->fast_flashing_led_state = WORK_STOP;
    aw21024->alternate_flashing_state = WORK_STOP;
    aw21024->miniled_charging_state = WORK_STOP;
    aw21024->boot_running_miniled_state = WORK_STOP;
    aw21024->led_test = false;
    aw21024->breath_mode = false;
}

/* Ended by AICoder, pid:k5cabk656ff5ab9143580af9c014e5169a10056f */
#define BOOT_MODE_MAX_LEN 20
static char boot_mode[BOOT_MODE_MAX_LEN] = "NONE";

static int zte_get_bootmode(void)
{
	struct device_node *np = NULL;
	const char *cmd_line = NULL, *s = NULL;
	int ret = 0;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		AW_ERROR("%s: find chosen failed\n", __func__);
		return 0;
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		AW_ERROR("%s: read bootargs failed\n", __func__);
		return 0;
	}

	s = strstr(cmd_line, "sprdboot.mode=");
	if (!s) {
		AW_ERROR("%s: find sprdboot.mode failed\n", __func__);
		return 0;
	}

	sscanf(s, "sprdboot.mode=%s", boot_mode);
	AW_INFO("%s: zte sprdboot.mode is %s\n", __func__, boot_mode);
	return 0;
}

/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw21024_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct aw21024 *aw21024;
	struct device_node *np = i2c->dev.of_node;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		dev_err(&i2c->dev, "check_functionality failed\n");
		return -ENODEV;
	}

	aw21024 = devm_kzalloc(&i2c->dev, sizeof(struct aw21024), GFP_KERNEL);
	if (aw21024 == NULL)
		return -ENOMEM;

	aw21024->dev = &i2c->dev;
	aw21024->i2c = i2c;

	i2c_set_clientdata(i2c, aw21024);
	dev_set_drvdata(&i2c->dev, aw21024);

	/* get dts info */
	if (np) {
		ret = aw21024_parse_dts(&i2c->dev, aw21024, np);
		if (ret) {
			dev_err(&i2c->dev, "%s: failed to parse device tree node\n", __func__);
			goto err_parse_dts;
		}
	} else {
		aw21024->reset_gpio = -1;
	}

	if (gpio_is_valid(aw21024->reset_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw21024->reset_gpio,
					    GPIOF_OUT_INIT_LOW, "aw21024_rst");
		if (ret) {
			dev_err(&i2c->dev, "%s: rst request failed\n", __func__);
			goto err_gpio_request;
		}
	}
    /* add boardtest interface start */
#ifdef CONFIG_VENDOR_ZTE_MISC_COMMON
	zte_misc_register_callback(&is_breath_led_exist_node, NULL);
#endif
    /* add boardtest interface end */
	/* aw21024 chip id */
	ret = aw21024_read_chip_id(aw21024);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s: aw21024_read_chip_id failed ret=%d\n", __func__, ret);
		goto err_id;
	}

	ret = aw21024_read_chip_version(aw21024);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s: aw21024_read_chip_version failed ret=%d\n", __func__, ret);
		goto err_id;
	}

	aw21024_led_init(aw21024);
	aw21024_init_led_cdev(aw21024);
	init_led_states(aw21024);
	INIT_WORK(&aw21024->running_led_work, running_led_work_func);
	INIT_WORK(&aw21024->boot_running_led_work, boot_running_led_work_func);
	INIT_WORK(&aw21024->boot_running_miniled_work, boot_running_miniled_work_func);
	INIT_WORK(&aw21024->fast_flashing_led_work, fast_flashing_led_work_func);
	INIT_WORK(&aw21024->alternate_flashing_led_work, alternate_flashing_led_work_func);
	INIT_WORK(&aw21024->miniled_charging_work, miniled_charging_work_func);

	zte_get_bootmode();
#ifndef ZTE_FEATURE_PV_AR
	if (aw21024->mini_led) {
		aw21024->step = 1;
	} else {
        if (strcmp(boot_mode, "charger")) {
	        aw21024->boot_running_led_state = WORK_RUNNING;
	        schedule_work(&aw21024->boot_running_led_work);
        }
	}
#endif
	AW_INFO("%s: probe successful!!!\n", __func__);
	return 0;

err_id:
	devm_gpio_free(&i2c->dev, aw21024->reset_gpio);
err_gpio_request:
err_parse_dts:
	devm_kfree(&i2c->dev, aw21024);
	aw21024 = NULL;
	return ret;
}

static int aw21024_i2c_remove(struct i2c_client *i2c)
{
	struct aw21024 *aw21024 = i2c_get_clientdata(i2c);
	int i = 0;

	sysfs_remove_group(&aw21024->cdev.dev->kobj, &aw21024_attribute_group);
	led_classdev_unregister(&aw21024->cdev);
	for (i = 0; i < LED_CDEV; i++)
		led_classdev_unregister(&aw21024->rgbcdev[i]);

	if (gpio_is_valid(aw21024->reset_gpio))
		devm_gpio_free(&i2c->dev, aw21024->reset_gpio);
    cancel_all_works(aw21024);
	devm_kfree(&i2c->dev, aw21024);
	aw21024 = NULL;

	return 0;
}

static const struct i2c_device_id aw21024_i2c_id[] = {
	{AW21024_I2C_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, aw21024_i2c_id);

static const struct of_device_id aw21024_dt_match[] = {
	{.compatible = "awinic,aw21024_led"},
	{},
};

static struct i2c_driver aw21024_i2c_driver = {
	.driver = {
		   .name = AW21024_I2C_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(aw21024_dt_match),
		   },
	.probe = aw21024_i2c_probe,
	.remove = aw21024_i2c_remove,
	.id_table = aw21024_i2c_id,
};

static int __init aw21024_i2c_init(void)
{
	int ret = 0;

	AW_INFO("aw21024 driver version %s\n", AW21024_DRIVER_VERSION);
	ret = i2c_add_driver(&aw21024_i2c_driver);
	if (ret) {
		AW_ERROR("fail to add aw21024 device into i2c\n");
		return ret;
	}
	return 0;
}

module_init(aw21024_i2c_init);

static void __exit aw21024_i2c_exit(void)
{
	i2c_del_driver(&aw21024_i2c_driver);
}

module_exit(aw21024_i2c_exit);

MODULE_DESCRIPTION("AW21024 LED Driver");
MODULE_LICENSE("GPL v2");
