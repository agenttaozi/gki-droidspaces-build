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

#include "x86-settings.h"

//static struct device_node *node;

struct fprint_gpio mafp_gpio;

/**
*  the platform struct start,for getting the platform device to set gpio state
*/

static struct of_device_id sof_match[] = {
	  { .compatible = MA_DTS_NAME, },		/*this name is used for matching the dts device for settings the gpios*/
	  { },
};
MODULE_DEVICE_TABLE(of, sof_match);

static struct platform_driver spdrv = {
	  .probe   = mas_plat_probe,
	  .remove  = mas_plat_remove,
	  .driver = {
		   .name  = MA_DRV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = sof_match,
	  }
};

/*
 *  set spi speed, often we must check whether the setting is efficient
 */

void ma_spi_change(struct spi_device *spi, unsigned int  speed, int flag)
{
	MALOG(INFO_LOG, "ma_spi_change start");
}

int mas_get_platform(void)
{
	int ret;
	MALOG(INFO_LOG, "start");

	ret = platform_driver_register(&spdrv);
	if (ret) {
		MALOG(ERR_LOG, "platform_driver_register failed, ret = %d", ret);
	} else {
		MALOG(INFO_LOG, "platform_driver_register success");
	}

	MALOG(INFO_LOG, "end");
	return ret;
}

int mas_remove_platform(void)
{
	MALOG(INFO_LOG, "start");

	platform_driver_unregister(&spdrv);

	MALOG(INFO_LOG, "end");
	return 0;
}

int mas_get_gpio(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	MALOG(INFO_LOG, "start");

	/***********POWER***********/
	ret = of_property_read_u32(np, "power-type", &mafp_gpio.power_type);
	if (ret < 0) {
		MALOG(ERR_LOG, "Power type get failed from dts, ret = %d", ret);
	}
	MALOG(INFO_LOG, "Power type[%d]", mafp_gpio.power_type);

	if (1 == mafp_gpio.power_type) {
		/* powered by ldo regulator */
		MALOG(INFO_LOG, "PWR_MODE_REGULATOR");
		ret = of_property_read_u32(np, "power-voltage", &mafp_gpio.power_voltage);
		if (ret < 0) {
			MALOG(ERR_LOG, "Power voltage get failed from dts, ret = %d", ret);
		}
		MALOG(INFO_LOG, "Power voltage[%d]", mafp_gpio.power_voltage);

		mafp_gpio.fp_reg = regulator_get(dev, "vdd");
		if (IS_ERR(mafp_gpio.fp_reg)) {
			MALOG(ERR_LOG, "regulator_get vdd failed from dts");
			return IS_ERR(mafp_gpio.fp_reg);
		} else {
			MALOG(INFO_LOG, "regulator_get vdd success from dts");
		}

		ret = regulator_set_voltage(mafp_gpio.fp_reg, mafp_gpio.power_voltage, mafp_gpio.power_voltage);
		MALOG(INFO_LOG, "after regulator_set_voltage power_voltage = %d", mafp_gpio.power_voltage);
		if (ret) {
			MALOG(ERR_LOG, "regulator_set_voltage failed use default 2.8v, ret = %d", ret);
			//goto err_power;
		} else {
			MALOG(INFO_LOG, "regulator_set_voltage success");
		}
	} else {
		MALOG(INFO_LOG, "PWR_MODE_GPIO");
		mafp_gpio.en_gpio = of_get_named_gpio(np, "fpen-gpios", 0);
		MALOG(INFO_LOG, "mafp_gpio.en_gpio : %d", mafp_gpio.en_gpio);
		if (mafp_gpio.en_gpio < 0) {
			MALOG(ERR_LOG, "of_get_named_gpio fpen-gpios failed");
			goto err_power;
		} else {
			MALOG(INFO_LOG, "of_get_named_gpio fpen-gpios success");
		}
		ret = gpio_request(mafp_gpio.en_gpio, "microarray_en");
		if (ret < 0) {
			MALOG(ERR_LOG, "gpio_request en_gpio failed, ret = %d", ret);
			goto err_power;
		} else {
			MALOG(INFO_LOG, "gpio_request en_gpio success");
		}
		gpio_direction_output(mafp_gpio.en_gpio, 0);
	}

	/***********RESET***********/
	mafp_gpio.reset_gpio = of_get_named_gpio(np, "fpreset-gpios", 0);
	MALOG(INFO_LOG, "mafp_gpio.reset_gpio : %d", mafp_gpio.reset_gpio);
	if (mafp_gpio.reset_gpio < 0) {
		MALOG(ERR_LOG, "of_get_named_gpio fpreset-gpios failed");
		goto err_rst;
	} else {
		MALOG(INFO_LOG, "of_get_named_gpio fpreset-gpios success");
	}
	ret = gpio_request(mafp_gpio.reset_gpio, "microarray_reset");
	if (ret < 0) {
		MALOG(ERR_LOG, "gpio_request reset_gpio failed, ret = %d", ret);
		goto err_rst;
	} else {
		MALOG(INFO_LOG, "gpio_request reset_gpio success");
	}
	gpio_direction_output(mafp_gpio.reset_gpio, 0);

	/***********INT***********/
	mafp_gpio.int_gpio = of_get_named_gpio(np, "fpint-gpios", 0);
	MALOG(INFO_LOG, "mafp_gpio.int_gpio : %d", mafp_gpio.int_gpio);
	if (mafp_gpio.int_gpio < 0) {
		MALOG(ERR_LOG, "of_get_named_gpio fpint-gpios failed");
		goto err_int;
	} else {
		MALOG(INFO_LOG, "of_get_named_gpio fpint-gpios success");
	}
	ret = gpio_request(mafp_gpio.int_gpio, "microarray_eint");
	if (ret < 0) {
		MALOG(ERR_LOG, "gpio_request int_gpio failed, ret = %d", ret);
		goto err_int;
	} else {
		MALOG(INFO_LOG, "gpio_request int_gpio success");
	}
	gpio_direction_input(mafp_gpio.int_gpio);

	mafp_gpio.int_num = gpio_to_irq(mafp_gpio.int_gpio);
	MALOG(INFO_LOG, "mafp_gpio.int_num : %d", mafp_gpio.int_num);
	if (mafp_gpio.int_num < 0) {
		MALOG(ERR_LOG, "gpio_to_irq failed");
		goto err_int2;
	}

	MALOG(INFO_LOG, "normal end");
	return ret;

err_int2:
	mafp_gpio.int_num = 0;

err_int:
	mafp_gpio.int_gpio = 0;
	gpio_free(mafp_gpio.reset_gpio);

err_rst:
	mafp_gpio.reset_gpio = 0;
	if (0 == mafp_gpio.power_type) {
		gpio_free(mafp_gpio.en_gpio);
	}

err_power:
	if (1 == mafp_gpio.power_type) {
		mafp_gpio.fp_reg = NULL;
	} else {
		mafp_gpio.en_gpio = 0;
	}

	MALOG(INFO_LOG, "abnormal end");
	return ret;
}

int mas_enable_power(bool on)
{
	int ret = 0;

	MALOG(INFO_LOG, "start");

	if (on) {
		if (1 == mafp_gpio.power_type) {
			if (mafp_gpio.fp_reg != NULL) {
				ret = regulator_enable(mafp_gpio.fp_reg);
				if (ret) {
					MALOG(ERR_LOG, "regulator_enable failed, ret = %d", ret);
					goto err;
				} else {
					MALOG(INFO_LOG, "regulator_enable success");
				}
			}
		} else {
			if (gpio_is_valid(mafp_gpio.en_gpio)) {
				gpio_set_value(mafp_gpio.en_gpio, 1);
			}
		}
	} else {
		if (1 == mafp_gpio.power_type) {
			if (mafp_gpio.fp_reg != NULL) {
				ret = regulator_disable(mafp_gpio.fp_reg);
				if (ret) {
					MALOG(ERR_LOG, "regulator_disable failed, ret = %d", ret);
					goto err;
				} else {
					MALOG(INFO_LOG, "regulator_disable success");
				}
			}
		} else {
			if (gpio_is_valid(mafp_gpio.en_gpio)) {
				gpio_set_value(mafp_gpio.en_gpio, 0);
			}
		}
	}

	MALOG(INFO_LOG, "normal end");
	return ret;

err:
	MALOG(INFO_LOG, "abnormal end");
	return ret;
}

int mas_free_gpio(void)
{
	int ret = 0;

	MALOG(INFO_LOG, "start");

	if (mafp_gpio.int_gpio != 0) {
		gpio_free(mafp_gpio.int_gpio);
		mafp_gpio.int_gpio = 0;
		if (mafp_gpio.int_num > 0) {
			free_irq(mafp_gpio.int_num, NULL);
			mafp_gpio.int_num = 0;
			MALOG(INFO_LOG, "free int_num success");
		}
		MALOG(INFO_LOG, "free int_gpio success");
	}

	if (mafp_gpio.reset_gpio != 0) {
		gpio_set_value(mafp_gpio.reset_gpio, 0);
		gpio_free(mafp_gpio.reset_gpio);
		mafp_gpio.reset_gpio = 0;
		MALOG(INFO_LOG, "set reset_gpio and free reset_gpio success");
	}

	if (1 == mafp_gpio.power_type) {
		/* powered by pmic regulator */
		if (mafp_gpio.fp_reg != NULL) {
			ret = regulator_disable(mafp_gpio.fp_reg);
			regulator_put(mafp_gpio.fp_reg);
			mafp_gpio.fp_reg = NULL;
			MALOG(INFO_LOG, "regulator_disable and regulator_put success");
		}
	} else {
		if (mafp_gpio.en_gpio != 0) {
			gpio_set_value(mafp_gpio.en_gpio, 0);
			gpio_free(mafp_gpio.en_gpio);
			mafp_gpio.en_gpio = 0;
			MALOG(INFO_LOG, "set en_gpio low and free pwr_gpio success");
		}
	}

	MALOG(INFO_LOG, "end");
	return ret;
}

void mas_enable_spi_clock(struct spi_device *spi)
{
/*  mt_spi_enable_clk(spi_master_get_devdata(spi->master));*/
}

void mas_disable_spi_clock(struct spi_device *spi)
{
/*  mt_spi_disable_clk(spi_master_get_devdata(spi->master));*/
}

/*
 * this function used for check the interrupt gpio state
 * @index 0 gpio level 1 gpio mode, often use 0
 * @return 0 gpio low 1 gpio high if index = 1,the return is the gpio mode
 * under 0 the of_property_read_u32_index return errno,check the dts as below:
 * last but not least use this function must checkt the label on dts file,
 * after is an example:
 * ma_finger: ma_finger{
 *   compatible = "mediatek,afs120x";
 *   finger_int_pin = <100 0>;
 * }
 */
int mas_get_int_value(unsigned int index)
{
	int ret;

	ret = gpio_get_value(mafp_gpio.int_gpio);
	MALOG(INFO_LOG, "MAFP guq int %d", ret);

	return ret;
}

void mas_reset(int count)
{
	gpio_set_value(mafp_gpio.reset_gpio, 0);
	usleep_range(count, count + 100);
	gpio_set_value(mafp_gpio.reset_gpio, 1);
}

int mas_get_rst_value(void)
{
	int ret;

	ret = gpio_get_value(mafp_gpio.reset_gpio);
	MALOG(INFO_LOG, "MAFP guq int %d", ret);

	return ret;
}
