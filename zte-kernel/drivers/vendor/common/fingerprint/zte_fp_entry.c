/****************************

       zte_fp_entry.c

****************************/

#include "zte_fp_entry.h"

#define ZTE_FP_DEV_NAME     "zte,fingerprint"

struct zte_fp_data {
	struct platform_device *fp_dev;
	struct pinctrl *fp_pinctrl;
	struct pinctrl_state *mode_spi;
	struct pinctrl_state *mode_gpio;
};

struct zte_fp_data fp_data;
int fp_new_boardid_rst_gpio = 0;

static const struct of_device_id zte_fp_match_table[] = {
	{ .compatible = ZTE_FP_DEV_NAME },
	{},
};

int zte_fp_pinctrl_select_spi(bool is_spi_mode)
{
	struct pinctrl_state *mode_tmp = NULL;
	int ret = 0;

	if (IS_ERR_OR_NULL(fp_data.fp_pinctrl)
		|| IS_ERR_OR_NULL(fp_data.mode_spi)
		|| IS_ERR_OR_NULL(fp_data.mode_gpio)) {
		zte_fp_log(INFO_LOG, "pinctrl not config");
		return 0;
	}

	mode_tmp = (is_spi_mode) ? fp_data.mode_spi : fp_data.mode_gpio;

	ret = pinctrl_select_state(fp_data.fp_pinctrl, mode_tmp);
	if (ret < 0) {
		zte_fp_log(INFO_LOG, "failed to select pin to %s state", (is_spi_mode) ? "spi" : "gpio");
	} else {
		zte_fp_log(INFO_LOG, "success to select pin to %s state", (is_spi_mode) ? "spi" : "gpio");
	}

	return 0;
}

int zte_fp_get_new_boardid_gpio(void)
{
	int ret = 0;
	struct device_node *dev_node = NULL;
	int boardid_gpio = 0;

	zte_fp_log(INFO_LOG, "enter");

	/* Find device tree node. */
	dev_node = of_find_compatible_node(NULL, NULL, ZTE_FP_COMPATIBLE);
	if (!dev_node) {
		zte_fp_log(ERR_LOG, "of_find_compatible_node(%s) failed", ZTE_FP_COMPATIBLE);
		return (-ENODEV);
	} else {
		zte_fp_log(INFO_LOG, "of_find_compatible_node(%s) success", ZTE_FP_COMPATIBLE);
	}
	if (of_property_read_bool(dev_node, "zte_fp,pcb_boardid_check")) {
		boardid_gpio = of_get_named_gpio(dev_node, "zte_fp,pcb_boardid-gpio", 0);
		zte_fp_log(INFO_LOG, "boardid_gpio = %d", boardid_gpio);
		if (gpio_is_valid(boardid_gpio)) {
			gpio_direction_input(boardid_gpio);
			if(gpio_get_value(boardid_gpio)) {
				fp_new_boardid_rst_gpio = of_get_named_gpio(dev_node, "zte_fp,new_boardid_rst-gpio", 0);
				if (gpio_is_valid(fp_new_boardid_rst_gpio)) {
					zte_fp_log(INFO_LOG, "fp_new_boardid_rst_gpio = %d", fp_new_boardid_rst_gpio);
					return 0;
				}
			}
		}
	}
	fp_new_boardid_rst_gpio = 0;
	zte_fp_log(INFO_LOG, "fp_new_boardid_rst_gpio = %d", fp_new_boardid_rst_gpio);
	return ret;
}

static int zte_fp_probe(struct platform_device *fp_dev)
{
	fp_data.fp_pinctrl = devm_pinctrl_get(&(fp_dev->dev));
	if (IS_ERR_OR_NULL(fp_data.fp_pinctrl)) {
		zte_fp_log(INFO_LOG, "devm_pinctrl_get failed");
		goto Failed_loop;
	}

	zte_fp_log(INFO_LOG, "devm_pinctrl_get success");

	fp_data.mode_spi = pinctrl_lookup_state(fp_data.fp_pinctrl, "zte_fp_spi");
	if (IS_ERR_OR_NULL(fp_data.mode_spi)) {
		zte_fp_log(INFO_LOG, "pinctrl_lookup_state spi failed");
		goto Failed_loop;
	}

	zte_fp_log(INFO_LOG, "pinctrl_lookup_state spi success");

	fp_data.mode_gpio = pinctrl_lookup_state(fp_data.fp_pinctrl, "zte_fp_gpio");
	if (IS_ERR_OR_NULL(fp_data.mode_gpio)) {
		zte_fp_log(INFO_LOG, "pinctrl_lookup_state gpio failed");
	} else {
		zte_fp_log(INFO_LOG, "pinctrl_lookup_state gpio success");
	}

	zte_fp_get_new_boardid_gpio();
	zte_fp_log(INFO_LOG, "success to select pin to spi state");

	return 0;

Failed_loop:
	zte_fp_log(INFO_LOG, "failed to probe");

	return 0;
}

static int zte_fp_remove(struct platform_device *fp_dev)
{
	devm_pinctrl_put(fp_data.fp_pinctrl);

	return 0;
}

static struct platform_driver zte_fp_driver = {
	.driver = {
		.name = ZTE_FP_DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = zte_fp_match_table,
	},
	.probe = zte_fp_probe,
	.remove = zte_fp_remove,
};

static int __init zte_fp_init(void)
{
	int ret = 0;
	zte_fp_log(INFO_LOG, "enter! driver_time:2024-04-20");

	platform_driver_register(&zte_fp_driver);

#ifdef CONFIG_PLATFORM_FINGERPRINT_CDFINGER
	zte_fp_log(INFO_LOG, "cdfinger_fp_init enter");
	cdfinger_fp_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_CHIPONE
	zte_fp_log(INFO_LOG, "fpsensor_init enter");
	fpsensor_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SUNWAVE
	zte_fp_log(INFO_LOG, "sf_ctl_driver_init enter");
	sf_ctl_driver_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_GOODIX
	zte_fp_log(INFO_LOG, "goodix_driver_init enter");
	gf_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC
	zte_fp_log(INFO_LOG, "fpc_sensor_init enter");
	fpc_sensor_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC1020
	zte_fp_log(INFO_LOG, "fpc_init enter");
	fpc_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SILEAD
	zte_fp_log(INFO_LOG, "silfp_dev_init enter");
	silfp_dev_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH
	zte_fp_log(INFO_LOG, "focaltech_fp_driver_init enter");
	focaltech_fp_driver_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH_V2
	zte_fp_log(INFO_LOG, "focaltech_fp_driver_v2_init enter");
	focaltech_fp_driver_v2_init();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_MICROARRAY
	zte_fp_log(INFO_LOG, "microarray mas_init enter");
	mas_init();
#endif

	ret = zte_fp_debug_proc_init();
	if (ret) {
		zte_fp_log(ERR_LOG, "zte_fp_debug_proc_init failed");
		//return ret;
	} else {
		zte_fp_log(INFO_LOG, "zte_fp_debug_proc_init success");
	}

	ret = zte_fp_debug_gpio_init();
	if (ret) {
		zte_fp_log(ERR_LOG, "zte_fp_gpio_init failed");
		//return ret;
	} else {
		zte_fp_log(INFO_LOG, "zte_fp_gpio_init success");
	}

	zte_fp_log(INFO_LOG, "exit");
	return 0;
}


static void __exit zte_fp_exit(void)
{
	int ret = 0;
	zte_fp_log(INFO_LOG, "enter");

#ifdef CONFIG_PLATFORM_FINGERPRINT_CDFINGER
	zte_fp_log(INFO_LOG, "cdfinger_fp_exit enter");
	cdfinger_fp_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_CHIPONE
	zte_fp_log(INFO_LOG, "fpsensor_exit enter");
	fpsensor_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SUNWAVE
	zte_fp_log(INFO_LOG, "sf_ctl_driver_exit enter");
	sf_ctl_driver_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_GOODIX
	zte_fp_log(INFO_LOG, "goodix_driver_exit enter");
	gf_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC
	zte_fp_log(INFO_LOG, "fpc_sensor_exit enter");
	fpc_sensor_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FPC1020
	zte_fp_log(INFO_LOG, "fpc_exit enter");
	fpc_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_SILEAD
	zte_fp_log(INFO_LOG, "silfp_dev_exit enter");
	silfp_dev_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH
	zte_fp_log(INFO_LOG, "focaltech_fp_driver_exit enter");
	focaltech_fp_driver_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_FOCALTECH_V2
	zte_fp_log(INFO_LOG, "focaltech_fp_driver_v2_exit enter");
	focaltech_fp_driver_v2_exit();
#endif

#ifdef CONFIG_PLATFORM_FINGERPRINT_MICROARRAY
	zte_fp_log(INFO_LOG, "microarray mas_exit enter");
	mas_exit();
#endif

	ret = zte_fp_debug_proc_deinit();
	if (ret) {
		zte_fp_log(ERR_LOG, "zte_fp_debug_proc_deinit failed");
	} else {
		zte_fp_log(INFO_LOG, "zte_fp_debug_proc_deinit success");
	}

	zte_fp_debug_gpio_deinit();

	platform_driver_unregister(&zte_fp_driver);

	zte_fp_log(INFO_LOG, "exit");
}

module_init(zte_fp_init);
module_exit(zte_fp_exit);

MODULE_DESCRIPTION("ZTE Fp Driver Entry");
MODULE_AUTHOR("***@zte.com");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ZTE");


