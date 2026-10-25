/* Started by AICoder, pid:04becm57efe39f9147880b2413403b7ca486a081 */
/*
 * ESWIN EPH861X series Touchscreen driver
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

// Uncomment to enable the dev_dbg prints to dmesg
#define DEBUG
// uncomment to test with input forced open
//#define INPUT_DEVICE_ALWAYS_OPEN
#include <linux/types.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/sysfs.h>

#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <uapi/linux/stat.h>
#include <linux/jiffies.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_tlv.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_eswin.h"
#include "eswin_eph861x.h"

extern char eswin_touch_fw_name[];
extern char eswin_touch_device_settings_name[];
extern int get_eswin_touch_module_info_from_lcd(void);

int eph_check_firmware_format(struct device *dev, const struct firmware *fw)
{
    unsigned int pos = 0;
    char c;

    while (pos < fw->size)
    {
        c = *(fw->data + pos);

        if (c < '0' || (c > '9' && c < 'A') || c > 'F')
        {
            return 0;
        }

        pos++;
    }

    eswin_log(ERR_LOG, "Aborting: firmware file must be in binary format");
    eswin_log(ERR_LOG, "xxd -r -p EPH861x_XXXXX_XXXX.enc > EPH861x_XXXXX_XXXX.bin");

    return -EINVAL;
}

int eph_update_file_name(struct device *dev,
                         char **out_file_name,
                         const char *in_file_name,
                         size_t in_str_len)
{
    char *file_name_tmp;

    /* Simple sanity check */
    if (in_str_len > 64)
    {
        eswin_log(WARN_LOG, "File name too long");
        return -EINVAL;
    }

    file_name_tmp = (char *)krealloc(*out_file_name, in_str_len + 1, GFP_KERNEL);
    if (!file_name_tmp)
    {
        return -ENOMEM;
    }

    *out_file_name = file_name_tmp;
    memcpy(*out_file_name, in_file_name, in_str_len);

    /* Echo into the sysfs entry may append newline at the end of buf */
    if (in_file_name[in_str_len - 1] == '\n')
    {
        (*out_file_name)[in_str_len - 1] = '\0';
    }
    else
    {
        (*out_file_name)[in_str_len] = '\0';
    }

    return 0;
}

#ifdef CONFIG_OF // Open Firmware (Device Tree)
struct eph_platform_data *eph_platform_data_get_from_device_tree(struct comms_device *commsdevice)
{
    struct eph_platform_data *ephplatform;
    struct device_node *devnode = commsdevice->dev.of_node;
    int ret_val;

    eswin_log(DEBUG_LOG, "enter, using device tree");

    if (!devnode)
    {
        return (struct eph_platform_data *)ERR_PTR(-ENOENT); /* no device tree device */
    }

    ephplatform = (struct eph_platform_data *)devm_kzalloc(&commsdevice->dev, sizeof(struct eph_platform_data), GFP_KERNEL);
    if (!ephplatform)
    {
        return (struct eph_platform_data *)ERR_PTR(-ENOMEM);
    }

    ephplatform->gpio_reset = of_get_named_gpio(devnode, "eswin,reset-gpio", 0);
    if (ephplatform->gpio_reset < 0) {
        ephplatform->gpio_reset = 0;
    }
    ephplatform->gpio_irq = of_get_named_gpio(devnode, "eswin,irq-gpio", 0);
    if (ephplatform->gpio_irq < 0) {
        ephplatform->gpio_irq = 0;
    }
    eswin_log(INFO_LOG, "gpio_reset=%d, gpio_irq=%d", ephplatform->gpio_reset, ephplatform->gpio_irq);

/* Started by AICoder, pid:y85fetc95au0ac2141c80b7c4027a43a88952ee4 */
    ephplatform->use_gpio_power = of_property_read_bool(devnode, "eswin,use-power-gpio");
    if (ephplatform->use_gpio_power) {
        eswin_log(INFO_LOG, "use_gpio_power");
        ephplatform->gpio_power = of_get_named_gpio(devnode, "eswin,power-gpio", 0);
        if (ephplatform->gpio_power < 0) {
            ephplatform->gpio_power = 0;
        }
        eswin_log(INFO_LOG, "gpio_power=%d", ephplatform->gpio_power);
    }

    ephplatform->use_regulator_dvdd = of_property_read_bool(devnode, "eswin,use-dvdd-supply");
    if (ephplatform->use_regulator_dvdd) {
        eswin_log(INFO_LOG, "use_regulator_dvdd");
        ret_val = of_property_read_string(devnode, "eswin,regulator_dvdd", &ephplatform->regulator_dvdd);
        if (ret_val) {
            eswin_log(ERR_LOG, "Couldn't read eswin,regulator_dvdd ret=%d", ret_val);
            ephplatform->regulator_dvdd = NULL;
        } else {
            eswin_log(INFO_LOG, "read eswin,regulator_dvdd success");
        }
        of_property_read_u32(devnode, "eswin,dvdd-voltage", &ephplatform->dvdd_voltage);
        eswin_log(INFO_LOG, "dvdd_voltage(%d)", ephplatform->dvdd_voltage);
    }

    ephplatform->use_regulator_avdd = of_property_read_bool(devnode, "eswin,use-avdd-supply");
    if (ephplatform->use_regulator_avdd) {
        eswin_log(INFO_LOG, "use_regulator_avdd");
        ret_val = of_property_read_string(devnode, "eswin,regulator_avdd", &ephplatform->regulator_avdd);
        if (ret_val) {
            eswin_log(ERR_LOG, "Couldn't read eswin,regulator_avdd ret=%d", ret_val);
            ephplatform->regulator_avdd = NULL;
        } else {
            eswin_log(INFO_LOG, "read eswin,regulator_avdd success");
        }
        of_property_read_u32(devnode, "eswin,avdd-voltage", &ephplatform->avdd_voltage);
        eswin_log(INFO_LOG, "avdd_voltage(%d)", ephplatform->avdd_voltage);
    }
/* Ended by AICoder, pid:y85fetc95au0ac2141c80b7c4027a43a88952ee4 */

    get_eswin_touch_module_info_from_lcd();
    ret_val = of_property_read_string(devnode, "eswin,device_settings_name", &ephplatform->device_settings_name);
    if (ret_val) {
        eswin_log(ERR_LOG, "Couldn't read eswin,device_settings_name from dts: %d, so use lcd name", ret_val);
        ephplatform->device_settings_name = eswin_touch_device_settings_name;
    }
    ret_val = of_property_read_string(devnode, "eswin,fw_name", &ephplatform->fw_name);
    if (ret_val) {
        eswin_log(ERR_LOG, "Couldn't read eswin,fw_name from dts: %d, so use lcd name", ret_val);
        ephplatform->fw_name = eswin_touch_fw_name;
    }
    eswin_log(INFO_LOG, "ephplatform->fw_name:%s, ephplatform->device_settings_name:%s",
        ephplatform->fw_name, ephplatform->device_settings_name);

    of_property_read_string(devnode, "eswin,input_name", &ephplatform->input_name);

    of_property_read_u32(devnode, "eswin,suspend-mode", &ephplatform->suspend_mode);
    eswin_log(INFO_LOG, "eswin,suspend-mode %d", ephplatform->suspend_mode);

    of_property_read_u32(devnode, "eswin,panel-invert-x", &ephplatform->panel_invert_x);
    of_property_read_u32(devnode, "eswin,panel-invert-y", &ephplatform->panel_invert_y);
    eswin_log(INFO_LOG, "panel-invert x%d y%d", ephplatform->panel_invert_x, ephplatform->panel_invert_y);

    of_property_read_u32(devnode, "eswin,panel-max-x", &ephplatform->panel_max_x);
    of_property_read_u32(devnode, "eswin,panel-max-y", &ephplatform->panel_max_y);
    eswin_log(INFO_LOG, "panel-max X%d Y%d", ephplatform->panel_max_x, ephplatform->panel_max_y);

    eswin_log(DEBUG_LOG, "exit, using device tree");

    return ephplatform;
}
#else // CONFIG_OF
struct eph_platform_data *eph_platform_data_get_from_device_tree(struct comms_device *commsdevice)
{
    return (struct eph_platform_data *)ERR_PTR(-ENOENT);
}
#endif // CONFIG_OF

struct eph_platform_data *eph_platform_data_get_default(struct comms_device *commsdevice)
{
    struct eph_platform_data *ephplatform = (struct eph_platform_data *)devm_kzalloc(&commsdevice->dev, sizeof(struct eph_platform_data), GFP_KERNEL);

    eswin_log(INFO_LOG, "enter");

    if (!ephplatform)
    {
        return (struct eph_platform_data *)ERR_PTR(-ENOMEM);
    }

    /* Set default parameters */

    eswin_log(INFO_LOG, "exit");

    return ephplatform;
}

struct eph_platform_data *eph_platform_data_get(struct comms_device *commsdevice)
{
    struct eph_platform_data *ephplatform;

    eswin_log(DEBUG_LOG, "enter");

    ephplatform = (struct eph_platform_data *)dev_get_platdata(&commsdevice->dev);
    if (ephplatform)
    {
        eswin_log(INFO_LOG, "dev_get_platdata success, return");
        return ephplatform;
    }

    ephplatform = eph_platform_data_get_from_device_tree(commsdevice);
    if (!IS_ERR(ephplatform) || PTR_ERR(ephplatform) != -ENOENT)
    {
        eswin_log(INFO_LOG, "eph_platform_data_get_from_device_tree success, return");
        return ephplatform;
    }

    ephplatform = eph_platform_data_get_default(commsdevice);
    if (!IS_ERR(ephplatform))
    {
        eswin_log(INFO_LOG, "eph_platform_data_get_default success, return");
        return ephplatform;
    }

    eswin_log(ERR_LOG, "abnormal, No platform data specified");
    return (struct eph_platform_data *)ERR_PTR(-EINVAL);
}

/* Started by AICoder, pid:w837es63aad687f14b3f0b7fd0a2ec4a8727f870 */
int eph_gpio_setup(struct eph_data *ephdata)
{
    int ret_val = 0;
    eswin_log(DEBUG_LOG, "enter");

    if (ephdata->ephplatform->gpio_irq) {
        ret_val = gpio_request(ephdata->ephplatform->gpio_irq, "irq-gpio");
        if (ret_val) {
            eswin_log(ERR_LOG, "irq_gpio_request %lu failed (%d)", ephdata->ephplatform->gpio_irq, ret_val);
            goto err_exit1;
        }
        gpio_direction_input(ephdata->ephplatform->gpio_irq);
        eswin_log(INFO_LOG, "gpio_irq %lu IN %d", ephdata->ephplatform->gpio_irq, gpio_get_value(ephdata->ephplatform->gpio_irq));
    }

    if (ephdata->ephplatform->gpio_reset) {
        ret_val = gpio_request(ephdata->ephplatform->gpio_reset, "reset-gpio");
        if (ret_val) {
            eswin_log(ERR_LOG, "gpio_request %lu failed (%d)", ephdata->ephplatform->gpio_reset, ret_val);
            goto err_exit2;
        }
        gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
        eswin_log(INFO_LOG, "gpio_reset %lu OUT %d", ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if (ephdata->ephplatform->gpio_power) {
            ret_val = gpio_request(ephdata->ephplatform->gpio_power, "power-gpio");
            if (ret_val) {
                eswin_log(ERR_LOG, "gpio_request %lu failed (%d)", ephdata->ephplatform->gpio_power, ret_val);
                goto err_exit3;
            }
            gpio_direction_output(ephdata->ephplatform->gpio_power, GPIO_RESET_YES_LOW);
            eswin_log(INFO_LOG, "gpio_power %lu OUT %d", ephdata->ephplatform->gpio_power, GPIO_RESET_YES_LOW);
        }
    }

    eswin_log(DEBUG_LOG, "exit");
    return ret_val;

err_exit3:
    if (ephdata->ephplatform->use_gpio_power) {
        if (ephdata->ephplatform->gpio_power) {
            ephdata->ephplatform->gpio_power = 0;
        }
    }
    if (ephdata->ephplatform->gpio_reset) {
        gpio_free(ephdata->ephplatform->gpio_reset);
    }
err_exit2:
    if (ephdata->ephplatform->gpio_reset) {
        ephdata->ephplatform->gpio_reset = 0;
    }
    if (ephdata->ephplatform->gpio_irq) {
        gpio_free(ephdata->ephplatform->gpio_irq);
    }
err_exit1:
    if (ephdata->ephplatform->gpio_irq) {
        ephdata->ephplatform->gpio_irq = 0;
    }
    return ret_val;
}
/* Ended by AICoder, pid:w837es63aad687f14b3f0b7fd0a2ec4a8727f870 */

int eph_wait_for_completion(struct eph_data *ephdata,
                            struct completion *comp,
                            unsigned int timeout_ms,
                            const char *dbg_str)
{
    unsigned long timeout = msecs_to_jiffies(timeout_ms);
    long ret_val;

    eswin_log(INFO_LOG, "enter, %s", dbg_str);

    ret_val = wait_for_completion_interruptible_timeout(comp, timeout);
    if (ret_val < 0)
    {
        return ret_val;
    }
    if (ret_val == 0)
    {
        eswin_log(ERR_LOG, "wait_for_completion timeout %s", dbg_str);
        return -ETIMEDOUT;
    }
    return 0;
}

void eph_regulator_enable(struct eph_data *ephdata)
{
    int ret_val = 0;

    eswin_log(DEBUG_LOG, "enter");

    if (ephdata->ephplatform->gpio_reset) {
        gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
    }

/* Started by AICoder, pid:782a4lcbe9q8834148ff096a10d7e928ebc26030 */
    if (ephdata->ephplatform->use_regulator_dvdd) {
        if (ephdata->reg_dvdd) {
            ret_val = regulator_enable(ephdata->reg_dvdd);
            if (ret_val) {
                eswin_log(ERR_LOG, "regulator_enable reg_dvdd failed(%d)", ret_val);
            } else {
                eswin_log(INFO_LOG, "regulator_enable reg_dvdd success");
            }
        }
    }
    msleep(EPH_REGULATOR_DELAY);
    if (ephdata->ephplatform->use_gpio_power) {
        if (gpio_is_valid(ephdata->ephplatform->gpio_power)) {
            gpio_direction_output(ephdata->ephplatform->gpio_power, 1);
            eswin_log(INFO_LOG, "set power gpio value to 1");
        }
    }
    msleep(EPH_REGULATOR_DELAY);
    if (ephdata->ephplatform->use_regulator_avdd) {
        if (ephdata->reg_avdd) {
            ret_val = regulator_enable(ephdata->reg_avdd);
            if (ret_val) {
                eswin_log(ERR_LOG, "regulator_enable reg_avdd failed(%d)", ret_val);
            } else {
                eswin_log(INFO_LOG, "regulator_enable reg_avdd success");
            }
        }
    }
/* Ended by AICoder, pid:782a4lcbe9q8834148ff096a10d7e928ebc26030 */
    /* According to power sequencing specification, RESET line must be kept 
     * low until some time after regulators come up to voltage */
    msleep(EPH_REGULATOR_DELAY);
    if (ephdata->ephplatform->gpio_reset) {
        gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_NO_HIGH);
    }
    //TODO this additional delay should not be needed
    /* Delay to prevent poor signals after power up. This will allow device time to "settle" before baseline */
    msleep(EPH_POWERON_DELAY);
retry_wait:
    reinit_completion(&ephdata->chg_completion);
    ephdata->in_bootloader = true;
    ret_val = eph_wait_for_completion(ephdata, &ephdata->chg_completion, EPH_POWERON_DELAY, "CHG");
    if (ret_val == -EINTR)
    {
        goto retry_wait;
    }
    ephdata->in_bootloader = false;
    eswin_log(DEBUG_LOG, "exit");
}

/* Started by AICoder, pid:b0d8cf9033s8704143540bb0107e892fdff128c0 */
void eph_regulator_disable(struct eph_data *ephdata)
{
    eswin_log(DEBUG_LOG, "enter");

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if (ephdata->reg_dvdd) {
            regulator_disable(ephdata->reg_dvdd);
            eswin_log(INFO_LOG, "regulator_disable reg_dvdd");
        }
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if (gpio_is_valid(ephdata->ephplatform->gpio_power)) {
            gpio_direction_output(ephdata->ephplatform->gpio_power, 0);
            eswin_log(INFO_LOG, "set power gpio value to 0");
        }
    }

    if (ephdata->ephplatform->use_regulator_avdd) {
        if (ephdata->reg_avdd) {
            regulator_disable(ephdata->reg_avdd);
            eswin_log(INFO_LOG, "regulator_disable reg_avdd");
        }
    }

    eswin_log(DEBUG_LOG, "exit");
}
/* Ended by AICoder, pid:b0d8cf9033s8704143540bb0107e892fdff128c0 */

#ifdef ESWIN_ESD_CHECK
/* Started by AICoder, pid:yde8dxadedj8a9c14602092930148b5c1c0296a2 */
void eph_recovery_device(struct eph_data *ephdata)
{
    int ret_val;

    if (!ephdata) {
        eswin_log(ERR_LOG, "ephdata is null");
        return;
    }

    disable_irq(ephdata->chg_irq);
    eswin_log(INFO_LOG, "disable_irq");

    eswin_log(INFO_LOG, "gpio is %ld", ephdata->ephplatform->gpio_reset);
    gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if (ephdata->reg_dvdd && regulator_is_enabled(ephdata->reg_dvdd)) {
            regulator_disable(ephdata->reg_dvdd);
            eswin_log(INFO_LOG, "regulator_disable reg_dvdd");
        }
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if (gpio_is_valid(ephdata->ephplatform->gpio_power)) {
            gpio_direction_output(ephdata->ephplatform->gpio_power, 0);
            eswin_log(INFO_LOG, "gpio_direction_output pwr(%d) low", ephdata->ephplatform->gpio_power);
        }
    }

    if (ephdata->ephplatform->use_regulator_avdd) {
        if (ephdata->reg_avdd && regulator_is_enabled(ephdata->reg_avdd)) {
            regulator_disable(ephdata->reg_avdd);
            eswin_log(INFO_LOG, "regulator_disable reg_avdd");
        }
    }

    msleep(500);
    eswin_log(DEBUG_LOG, "msleep(500)");

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if (ephdata->reg_dvdd) {
            ret_val = regulator_enable(ephdata->reg_dvdd);
            eswin_log(INFO_LOG, "regulator_enable reg_dvdd");
        }
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if (gpio_is_valid(ephdata->ephplatform->gpio_power)) {
            gpio_direction_output(ephdata->ephplatform->gpio_power, 1);
            eswin_log(INFO_LOG, "gpio_direction_output pwr(%d) high", ephdata->ephplatform->gpio_power);
        }
    }

    if (ephdata->ephplatform->use_regulator_avdd) {
        if (ephdata->reg_avdd) {
            ret_val = regulator_enable(ephdata->reg_avdd);
            eswin_log(INFO_LOG, "regulator_enable reg_avdd");
        }
    }

    msleep(10);
    gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_NO_HIGH);

    // wait ic stable
    msleep(200);

    enable_irq(ephdata->chg_irq);
    eswin_log(INFO_LOG, "enable_irq");
}
/* Ended by AICoder, pid:yde8dxadedj8a9c14602092930148b5c1c0296a2 */
#endif

void eph_reset_device(struct eph_data *ephdata)
{
    eswin_log(INFO_LOG, "gpio is %ld", ephdata->ephplatform->gpio_reset);
    gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_YES_LOW);
    msleep(1);
    gpio_direction_output(ephdata->ephplatform->gpio_reset, GPIO_RESET_NO_HIGH);
    msleep(EPH_POWERON_DELAY);

    return;
}
/* Ended by AICoder, pid:04becm57efe39f9147880b2413403b7ca486a081 */