 /*
 * Copyright (C) 2012-2022 Microarray Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __X86_SETTINGS_H_
#define __X86_SETTINGS_H_

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/spi/spi.h>
#include "madev.h"

/*macro settings*/
#define MA_DRV_NAME			 "madev"
#define MA_DTS_NAME			"microarray,afs121"
#define MA_EINT_DTS_NAME		"microarray,afs121"
#define MA_INT_PIN_LABEL	"finger_int_pin"

struct fprint_gpio {
	int int_gpio;
    int int_num;
	int reset_gpio;
	int en_gpio;
	struct platform_device *dev;
    /* 0 fingerprint use system gpio control  power, 1 pmic/vddcammot power */
    int power_type;
    struct regulator *fp_reg;
    /* get the voltage from dts file */
    int power_voltage;
};

/*call madev function*/
extern int mas_plat_probe(struct platform_device *pdev);
extern int mas_plat_remove(struct platform_device *pdev);

/* the interface called by madev*/
int mas_get_gpio(struct platform_device *pdev);
int mas_free_gpio(void);
int mas_enable_power(bool on);
void mas_enable_spi_clock(struct spi_device *spi);
void mas_disable_spi_clock(struct spi_device *spi);
int mas_get_platform(void);
int mas_remove_platform(void);
void ma_spi_change(struct spi_device *spi, unsigned int speed, int flag);
int mas_get_int_value(unsigned int index);
void mas_reset(int count);
int mas_get_rst_value(void);
#endif
