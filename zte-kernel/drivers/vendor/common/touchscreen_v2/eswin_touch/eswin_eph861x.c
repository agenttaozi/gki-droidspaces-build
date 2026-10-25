/* Started by AICoder, pid:s4b3ace8dewbce8147e128ddf02b083d4bd80d8b */
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
// Uncomment to test with input forced open
//#define INPUT_DEVICE_ALWAYS_OPEN

#include <linux/types.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/sysfs.h>

#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <uapi/linux/input-event-codes.h>
#include <uapi/linux/stat.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hardirq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_comms.h"
#include "eswin_eph861x_eswin.h"
#include "eswin_eph861x_bootloader.h"
#include "eswin_eph861x_tlv_command.h"
#include "eswin_eph861x_tlv_report.h"
#include "eswin_eph861x.h"
#include "eswin_eph861x_testing.h"

/* device settings file format version expected*/
#define EPH_DEVICE_SETTINGS_FORMAT       "<product>EPH8610</product>"

/* Touchscreen absolute values */
#define EPH_MAX_HEIGHT_WIDTH      255u

#define ESWIN_VDD_MIN_VOL         3300000
#define ESWIN_VDD_MAX_VOL         3300000
#define ESWIN_AVDD_MIN_VOL        1800000
#define ESWIN_AVDD_MAX_VOL        1800000

struct eph_data *zte_ephdata;
extern void eswin_tpd_register_fw_class(struct eph_data *ephdata);

static int eph_sysfs_mem_access_init(struct eph_data *ephdata);
static void eph_sysfs_mem_access_remove(struct eph_data *ephdata);
static int eph_configure_components(struct eph_data *ephdata, const struct firmware *device_settings);

static int eph_check_mem_access_params(struct eph_data *ephdata,
                                       loff_t off,
                                       size_t *count)
{
    if (off >= PAGE_SIZE)
    {
        return -EIO;
    }

    if (off + *count > PAGE_SIZE)
    {
        return -EIO;
    }

    return 0;
}

int eph_probe_bootloader(struct eph_data *ephdata)
{
    int ret_val;

    eswin_log(INFO_LOG, "enter");

    ret_val = eph_comms_specific_bootloader_checks(ephdata);
    if (ret_val)
    {
        return ret_val;
    }

    /* Check bootloader status and version information */
    ret_val = eph_read_bootloader_information(ephdata);

    eswin_log(INFO_LOG, "product_id = %x, variant_id = %x, bootloader_version = %x, error = %d",
            ephdata->ephdeviceinfo.product_id,
            ephdata->ephdeviceinfo.variant_id,
            ephdata->ephdeviceinfo.bootloader_version,
            ret_val);

    if (ret_val)
    {
        /* Force device into bootloader mode using chg line held low while toggle reset */
        ret_val = eph_chg_force_bootloader(ephdata);

        /* now forced into bootloader check whether we get a valid read */
        ret_val = eph_read_bootloader_information(ephdata);

        eswin_log(INFO_LOG, "product_id = %x, variant_id = %x, bootloader_version = %x, error = %d",
                ephdata->ephdeviceinfo.product_id,
                ephdata->ephdeviceinfo.variant_id,
                ephdata->ephdeviceinfo.bootloader_version,
                ret_val);
        
        /* Release CHG line as no more bootloader reads required - Can return to app (reset to app not strictly required */
        (void)eph_bootloader_release_chg(ephdata);

        if (ret_val)
        {
            return ret_val;
        }
    }

    return 0;
}

/* Started by AICoder, pid:7e5a5qb338jfe7a1477508f380f50431c1c04aa9 */
static int eph_read_and_process_messages(struct eph_data *ephdata)
{
    int ret_val;
    int retry = 3;

    eswin_log(DEBUG_LOG, "enter");

    do {
        mutex_lock(&ephdata->comms_mutex);
        /* Read report */
        ret_val = eph_read_report(ephdata, ephdata->report_buf);
        if (ret_val < 0) {
            eswin_log(ERR_LOG, "Failed to read message (%d)", ret_val);
            mutex_unlock(&ephdata->comms_mutex);
            return ret_val;
        }

        ret_val = eph_handle_report(ephdata, ephdata->report_buf);
        mutex_unlock(&ephdata->comms_mutex);

        if (ret_val == 0) {
            retry--;
        } else {
            retry = 0;
        }
    } while (retry != 0);

    memset(ephdata->report_buf, 0, COMMS_BUF_SIZE);
    return ret_val;
}
/* Ended by AICoder, pid:7e5a5qb338jfe7a1477508f380f50431c1c04aa9 */

static irqreturn_t eph_interrupt(int irq, void *dev_id)
{
    struct eph_data *ephdata = (struct eph_data *)dev_id;
    struct device *dev = &ephdata->commsdevice->dev;
/* Started by AICoder, pid:25a9cg70e1n341114f4e0aa6106f460ec2e3ba59 */
#ifdef CONFIG_PM
    int ret;
#endif
/* Ended by AICoder, pid:25a9cg70e1n341114f4e0aa6106f460ec2e3ba59 */

    if (dev == NULL)
        return IRQ_HANDLED;

/* Started by AICoder, pid:n1c16kdf0fj448414dee0a19c0a22e008c99d188 */
#ifdef CONFIG_PM
    if (ephdata->suspended && ephdata->zte_ctrl.pm_suspend) {
        ret = wait_for_completion_timeout(&ephdata->zte_ctrl.pm_completion, msecs_to_jiffies(500));
        if (!ret) {
            eswin_log(ERR_LOG, "wait pm resume timeout 500ms ret=(%d)", ret);
            return IRQ_HANDLED;
        }
    }
#endif
/* Ended by AICoder, pid:n1c16kdf0fj448414dee0a19c0a22e008c99d188 */

    pm_stay_awake(dev);

    eph_read_and_process_messages(ephdata);

    /* will not unblock any other threads until message has been read */
    complete(&ephdata->chg_completion);

    pm_relax(dev);

    return IRQ_HANDLED;
}

int eph_trigger_baseline(struct eph_data *ephdata)
{
    int ret_val;
    u8 cfg[8] =    {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0xF0, 0x00, 0x00, 0x00,0x01};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));
    eswin_log(INFO_LOG, "enter");
    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;

}
#if defined(CONFIG_BOARD_FLORAL)
static void eph_trigger_baseline_work(struct work_struct *work)
{
    int ret_val = 0;
    struct eph_data *ephdata = container_of(work, struct eph_data, force_baseline_work);
    struct backlight_device *bd = ephdata->bl;
    int brightness = 0;

    eswin_log(DEBUG_LOG, "enter");

    if (ephdata->last_brightness == 0) {
        do {
            if (bd->ops && bd->ops->get_brightness)
                brightness = bd->ops->get_brightness(bd);
            else
                brightness = bd->props.brightness;

            if (brightness)
                break;

            eswin_log(INFO_LOG, "eph wait 5 msec");
            msleep(5);

        } while (brightness == 0);

        ret_val = eph_trigger_baseline(ephdata);

        if (ret_val)
            eswin_log(ERR_LOG, "eph set baseline fail %d", ret_val);

        ephdata->last_brightness = brightness;

        eswin_log(INFO_LOG, "brightness %d", brightness);
    } else {
        eswin_log(INFO_LOG, "no need force baseline");
    }
}
#endif
static int eph_trigger_backup_to_nvm(struct eph_data *ephdata)
{
    int ret_val;
    u8 cfg[8] =    {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,0x02};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));
    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;

}

// enable: 0 mean disable
//         1 mean enable
static int eph_dev_enable_eng_mode(struct eph_data *ephdata, int enable)
{
    int ret_val;
    u8 cfg[8] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));

    if (enable == 0)
        cfg[7] = 0;
    else if (enable == 1)
        cfg[7] = 0x80;
    else {
        eswin_log(ERR_LOG, "unexpected param %d", enable);
        return -1;
    };

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;
}

/* eph_update_device_settings - download device settings to chip
 * The file consists of repeating patterns of the following:
 *   <TYPE> - 1-byte type
 *   <LENGTH> - 2-byte length
 *   <DATA> - length-bytes of data
 */
static int eph_update_device_settings(struct eph_data *ephdata, const struct firmware *device_settings_image)
{
    struct eph_device_settings device_settings;
    int ret_val = 0;
    u8* cfg_ptr;
    u16 component;
    u16 offset;
    u16 msg_count = 0u;
    u8 retry = 0u;
    u16 pos = 0u;
    struct tlv_header tlvheader;

    eswin_log(INFO_LOG, "enter");

    /* Allocate space for zero terminated copy of the device settings file */
    device_settings.raw = (u8 *)kzalloc(device_settings_image->size + 1, GFP_KERNEL);
    if (!device_settings.raw)
    {
        eswin_log(ERR_LOG, "ESWIN Couldnt allocate memory for device settings file %d", -ENOMEM);
        return -ENOMEM;
    }

    // enable eng mode
    ret_val = eph_dev_enable_eng_mode(ephdata, 1);
    eswin_log(INFO_LOG, "eng enable %d", ret_val);

    /* Copy from the firmware image into local buffer */
    memcpy(device_settings.raw, device_settings_image->data, device_settings_image->size);
    /* Pad last entry as a zero. We are about to loop through all config and control tlvs */
    /* The 0 will not match an expected T and the configuration loading will terminate */
    device_settings.raw[device_settings_image->size] = 0;
    device_settings.raw_size = device_settings_image->size;
    cfg_ptr = &device_settings.raw[0];

    //TODO add format checking - Currently no version information in settings file.
    while (pos < device_settings_image->size)
    {
        tlvheader = eph_get_tl_header_info(ephdata, cfg_ptr);
        /* if item next in the data stream is not configuration or control break out */
        if((TLV_CONFIG_DATA_WRITE == tlvheader.type) || (TLV_CONTROL_DATA_WRITE == tlvheader.type))
        {
            if (pos > device_settings_image->size)
            {
                /* Have reached the end of the file but did not find the terminating character */
                ret_val = -ENOMEM;
                eswin_log(ERR_LOG, "ESWIN went out of bounds of device settings file %d", ret_val);
                /* Ensure memory released before returning */
                goto fail_release;
            }
            component = *(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_COMPONENT_FIELD) | (*(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_COMPONENT_FIELD + 1) << 8);
            offset = *(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_OFFSET_FIELD) | (*(cfg_ptr + TLV_HEADER_SIZE + TLV_WRITE_OFFSET_FIELD + 1) << 8);
            eswin_log(DEBUG_LOG, "TYPE: %d LEGNTH: %d COMPONENT_ID: %d OFFSET: %d", tlvheader.type, tlvheader.length, component, offset);
            msg_count++;
            eswin_log(DEBUG_LOG, "Configuration/control write: BLOCK NUMBER: %d", msg_count);

            mutex_lock(&ephdata->comms_mutex);
            ret_val = eph_write_control_config(ephdata, tlvheader.length, cfg_ptr);
            mutex_unlock(&ephdata->comms_mutex);

            retry = 0u;
            while (ret_val)
            {
                eswin_log(INFO_LOG,
                         "Retry control config write, It failed for some reason. msg_count: %u",
                         msg_count);

                mutex_lock(&ephdata->comms_mutex);
                ret_val = eph_write_control_config(ephdata, tlvheader.length, cfg_ptr);
                mutex_unlock(&ephdata->comms_mutex);

                retry++;
                if (10 < retry)
                {
                    ret_val = -EIO;
                    eswin_log(ERR_LOG, "ESWIN failed to send settings to TIC %d", ret_val);
                    /* Ensure memory released before returning */
                    goto fail_release;

                }
            }

            cfg_ptr = cfg_ptr + tlvheader.length;
            pos = pos + tlvheader.length;

            if(ret_val)
            {
                eswin_log(INFO_LOG, "Configuration/Contol write failure on: BLOCK NUMBER: %d", msg_count);
                /* Ensure memory released before returning */
                goto fail_release;
            }
        }
        else
        {
            break;
        }
    }

    ret_val = eph_trigger_backup_to_nvm(ephdata);
    if(ret_val)
    {
        eswin_log(ERR_LOG, "Failed to trigger_backup_to_nvm %d", ret_val);
    }

    msleep(200);

    // disable eng mode
    ret_val = eph_dev_enable_eng_mode(ephdata, 0);
    eswin_log(INFO_LOG, "eng enable %d", ret_val);

    ret_val = eph_trigger_baseline(ephdata);
    if(ret_val)
    {
        eswin_log(ERR_LOG, "Failed to trigger_baseline %d", ret_val);
    }
    eph_reset_device(ephdata);

    eswin_log(INFO_LOG, "Config successfully updated");

fail_release:
    kfree(device_settings.raw);

    return ret_val;
}

static int eph_acquire_irq(struct eph_data *ephdata)
{
    int ret_val;
    char *commsdevice_name;

    eswin_log(DEBUG_LOG, "enter");

    if (!ephdata->chg_irq) {
        ephdata->chg_irq = gpio_to_irq(ephdata->ephplatform->gpio_irq);

        commsdevice_name = eph_comms_devicename_get(ephdata);

        /* configure a thread that is triggered on the interrupt */
        /* This specific IRQ remains disabled while handler/thread is active */
        /* Thread happens outside of interrupt handler and so frees up the general interupt hardware */
        ret_val = request_threaded_irq(ephdata->chg_irq,
                                       NULL,
                                       eph_interrupt,
                                       IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                                       commsdevice_name,
                                       ephdata);
        eswin_log(INFO_LOG, "request_threaded_irq:IRQF_TRIGGER_LOW | IRQF_ONESHOT");
        if (ret_val) {
            eswin_log(ERR_LOG, "request_threaded_irq failed(%d)", ret_val);
            ephdata->chg_irq = 0;
            return ret_val;
        }
        enable_irq_wake(ephdata->chg_irq);
        /* Presence of ephdata->chg_irq means IRQ initialised */
        eswin_log(INFO_LOG, "gpio_to_irq %lu -> %d", ephdata->ephplatform->gpio_irq, ephdata->chg_irq);
    } else {
        enable_irq(ephdata->chg_irq);
        eswin_log(DEBUG_LOG, "enable_irq");
    }

    eswin_log(DEBUG_LOG, "exit");

    return 0;
}

static int eph_input_open(struct input_dev *inputdev);
static void eph_input_close(struct input_dev *inputdev);

static void eph_unregister_input_device(struct eph_data *ephdata)
{
    eswin_log(INFO_LOG, "enter");

    if (ephdata->inputdev)
    {
#ifdef INPUT_DEVICE_ALWAYS_OPEN
        eph_input_close(ephdata->inputdev);
#endif //INPUT_DEVICE_ALWAYS_OPEN

        input_unregister_device(ephdata->inputdev);
    }
}

/* Started by AICoder, pid:geaee2c45eb820f14f2d08832081b64605d74f7a */
static int eph_probe_regulators(struct eph_data *ephdata)
{
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val = 0;

    eswin_log(DEBUG_LOG, "enter");

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if (ephdata->ephplatform->regulator_dvdd) {
            ephdata->reg_dvdd = regulator_get(dev, ephdata->ephplatform->regulator_dvdd);
            if (IS_ERR(ephdata->reg_dvdd)) {
                ret_val = PTR_ERR(ephdata->reg_dvdd);
                eswin_log(ERR_LOG, "Error %d getting dvdd regulator", ret_val);
                goto err_exit1;
            }
            eswin_log(INFO_LOG, "success getting dvdd regulator");
            regulator_set_voltage(ephdata->reg_dvdd, ephdata->ephplatform->dvdd_voltage, ephdata->ephplatform->dvdd_voltage);
            eswin_log(INFO_LOG, "regulator_set_voltage reg_dvdd %d %d", ephdata->ephplatform->dvdd_voltage, ephdata->ephplatform->dvdd_voltage);
        }
    }

    if (ephdata->ephplatform->use_regulator_avdd) {
        if (ephdata->ephplatform->regulator_avdd) {
            ephdata->reg_avdd = regulator_get(dev, ephdata->ephplatform->regulator_avdd);
            if (IS_ERR(ephdata->reg_avdd)) {
                ret_val = PTR_ERR(ephdata->reg_avdd);
                eswin_log(ERR_LOG, "Error %d getting avdd regulator", ret_val);
                goto err_exit2;
            }
            eswin_log(INFO_LOG, "success getting avdd regulator");
            regulator_set_voltage(ephdata->reg_avdd, ephdata->ephplatform->avdd_voltage, ephdata->ephplatform->avdd_voltage);
            eswin_log(INFO_LOG, "regulator_set_voltage reg_avdd %d %d", ephdata->ephplatform->avdd_voltage, ephdata->ephplatform->avdd_voltage);
        }
    }

    eph_regulator_enable(ephdata);
    eswin_log(DEBUG_LOG, "Initialised regulators end");

    return ret_val;

err_exit2:
    if (ephdata->ephplatform->use_regulator_avdd) {
        ephdata->reg_avdd = NULL;
    }
    if (ephdata->ephplatform->use_regulator_dvdd) {
        regulator_put(ephdata->reg_dvdd);
    }
err_exit1:
    if (ephdata->ephplatform->use_regulator_dvdd) {
        ephdata->reg_dvdd = NULL;
    }
    return ret_val;
}
/* Ended by AICoder, pid:geaee2c45eb820f14f2d08832081b64605d74f7a */

static int eph_input_device_initialize(struct eph_data *ephdata)
{
    const struct eph_platform_data *ephplatform = ephdata->ephplatform;
    struct device *dev = &ephdata->commsdevice->dev;
    int ret_val;
    unsigned int mt_flags = 0;
    u16 max_resoultion;

    eswin_log(DEBUG_LOG, "enter");

    eswin_log(INFO_LOG, "Touchscreen resolution X%uY%u", ephplatform->panel_max_x, ephplatform->panel_max_y);

    eswin_log(INFO_LOG, "ESWIN device not allocated, allocate device %d", 0);
    /* allocate memory for new managed input device 
     * (no need to free as is freed when last reference to the device is dropped) 
     * use input_unregister_device() and memory will be freed (after last reference) */
    ephdata->inputdev = devm_input_allocate_device(dev);
    if (!ephdata->inputdev)
    {
        eswin_log(ERR_LOG, "allocate device failed %d", -ENOMEM);
        return -ENOMEM;
    }

    if (ephplatform->input_name)
    {
        ephdata->inputdev->name = ephplatform->input_name;
    }
    else
    {
        ephdata->inputdev->name = "ESWIN EPH861X Touchscreen";
    }

    ephdata->inputdev->phys = ephdata->phys;
    ephdata->inputdev->id.bustype = EPH_COMMS_BUS_TYPE;
    ephdata->inputdev->dev.parent = dev;

#ifndef INPUT_DEVICE_ALWAYS_OPEN
    ephdata->inputdev->open = eph_input_open;
    ephdata->inputdev->close = eph_input_close;
#endif //INPUT_DEVICE_ALWAYS_OPEN

    /* this will confuse android into thinking we support a feature we dont - IDEALLY should no be set */
    /* Cannot remove EV_KEY from get event so appears to be required for pixel at least */
    input_set_capability(ephdata->inputdev, EV_KEY/*event type*/, BTN_TOUCH/*event code*/);

    input_set_capability(ephdata->inputdev, EV_ABS, ABS_MT_POSITION_X);
    input_set_capability(ephdata->inputdev, EV_ABS, ABS_MT_POSITION_Y);

    /* direct device, e.g. touchscreen */
    mt_flags |= INPUT_MT_DIRECT;

    /* multi touch */
    ret_val = input_mt_init_slots(ephdata->inputdev, CONFIG_SUPPORTED_TOUCHES, mt_flags);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "Error %d initialising slots", ret_val);
        return ret_val;
    }

    /* reports co-ordinates of the tool */
    /* Height appears to always be Y */
    input_set_abs_params(ephdata->inputdev, ABS_MT_POSITION_X, 0, ephplatform->panel_max_x, 0, 0);
    input_set_abs_params(ephdata->inputdev, ABS_MT_POSITION_Y, 0, ephplatform->panel_max_y, 0, 0);

    max_resoultion = (ephplatform->panel_max_y > ephplatform->panel_max_x) ? ephplatform->panel_max_y :
        ephplatform->panel_max_x;

    /* The minor and major has no particular orientation just the longest/shortest axis */
    input_set_abs_params(ephdata->inputdev, ABS_MT_TOUCH_MAJOR, 0, (EPH_MAX_HEIGHT_WIDTH*max_resoultion), 0, 0);
    input_set_abs_params(ephdata->inputdev, ABS_MT_TOUCH_MINOR, 0, (EPH_MAX_HEIGHT_WIDTH*max_resoultion), 0, 0);
    input_set_abs_params(ephdata->inputdev, ABS_MT_PRESSURE, 0, 255, 0, 0);

    input_set_drvdata(ephdata->inputdev, ephdata);

    eswin_log(INFO_LOG, "input_register_device");

    input_set_capability(ephdata->inputdev, EV_KEY, KEY_WAKEUP);

    ret_val = input_register_device(ephdata->inputdev);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "Error %d registering input device", ret_val);
        return ret_val;
    }

#ifdef INPUT_DEVICE_ALWAYS_OPEN
    eph_input_open(ephdata->inputdev);
#endif //INPUT_DEVICE_ALWAYS_OPEN
    tpd_cdev->input = ephdata->inputdev;
    eswin_log(DEBUG_LOG, "exit");

    return 0;
}

void eph_request_fw_device_settings_nowait_cb(const struct firmware *device_settings, void *ctx)
{
    (void)eph_configure_components((struct eph_data *)ctx, device_settings);
    (void)eph_input_device_initialize((struct eph_data *)ctx);
    release_firmware(device_settings);
}

static int eph_initialize(struct eph_data *ephdata)
{
    int comms_attempts = 0;
    int ret_val;
    int device_info_read_retry = 0;

    eswin_log(DEBUG_LOG, "enter");

    while (5 > comms_attempts)
    {

        mutex_lock(&ephdata->comms_mutex);
        ret_val = eph_read_device_information(ephdata);
        while(ret_val && (device_info_read_retry <= COMMS_READ_RETRY_NUM))
        {
            eswin_log(INFO_LOG, "device_info_read_retry");
            /* retry we didnt get a device information response */
            ret_val = eph_read_device_information(ephdata);
            device_info_read_retry++;

        }
        mutex_unlock(&ephdata->comms_mutex);

        eswin_log(INFO_LOG,
            "Product ID: %u Variant ID: %u Application_version_major %u Application_version_minor: %u Bootloader_version: %u Protocol_version: %u CRC:%u",
            ephdata->ephdeviceinfo.product_id, ephdata->ephdeviceinfo.variant_id,
            ephdata->ephdeviceinfo.application_version_major, ephdata->ephdeviceinfo.application_version_minor,
            ephdata->ephdeviceinfo.bootloader_version, ephdata->ephdeviceinfo.protocol_version, ephdata->ephdeviceinfo.crc);
        if ((0 == ret_val) && ((ephdata->ephdeviceinfo.application_version_major != 0) || (ephdata->ephdeviceinfo.application_version_minor != 0)))
        {
            eswin_log(INFO_LOG, "sucessfully read from device info and confirmed in application mode");
            /* sucessfully read from device info and confirmed in application mode */
            ephdata->in_bootloader = false;
            break;
        }
#if 0
        eswin_log(INFO_LOG, "Failed to read the device information - try bootloader");
        /* Failed to read the device information - try bootloader */
        ret_val = eph_chg_force_bootloader(ephdata);
        eswin_log(INFO_LOG, "Check bootloader state");
        /* Check bootloader state */
        ret_val = eph_probe_bootloader(ephdata);
        eswin_log(INFO_LOG, "release chg as bootloader actions complete");
        /* release chg as bootloader actions complete  */
        (void)eph_bootloader_release_chg(ephdata);

        if (ret_val)
        {
            eswin_log(INFO_LOG, "Chip is not in appmode or bootloader mode");
            /* Chip is not in appmode or bootloader mode */
            return ret_val;
        }

        /* OK, we are in bootloader, see if we can recover */
        if (comms_attempts > 1)
        {
            eswin_log(ERR_LOG, "Could not recover from bootloader mode");
            /*
             * We can reflash from this state, so do not
             * abort initialization.
             */
            ephdata->in_bootloader = true;
            return 0;
        }
#else
        comms_attempts++;
#endif
        eswin_log(INFO_LOG, "Attempt to exit bootloader into app mode");
        /* Attempt to exit bootloader into app mode */
        eph_reset_device(ephdata);
        msleep(200);
    }

    if (comms_attempts > 5) {
        eswin_log(INFO_LOG, "in_bootloader %d", ephdata->in_bootloader);
    }

    ret_val = eph_acquire_irq(ephdata);
    if (ret_val)
    {
        return ret_val;
    } else {
        eswin_log(INFO_LOG, "eph_acquire_irq success");
    }

    ret_val = eph_sysfs_mem_access_init(ephdata);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "eph_sysfs_mem_access_init failed(%d)", ret_val);
        return ret_val;
    } else {
        eswin_log(INFO_LOG, "eph_sysfs_mem_access_init success");
    }

#if 0
    eswin_log(INFO_LOG, "device_settings_name: %s", ephdata->device_settings_name);
    if (ephdata->device_settings_name)
    {
        ret_val = request_firmware_nowait(THIS_MODULE,
                                          true,
                                          ephdata->device_settings_name,
                                          &ephdata->commsdevice->dev,
                                          GFP_KERNEL,
                                          ephdata,
                                          eph_request_fw_device_settings_nowait_cb);
        if (ret_val)
        {
            eswin_log(ERR_LOG, "Failed to invoke settings firmware load: %d", ret_val);
            return ret_val;
        } else {
            eswin_log(INFO_LOG, "success to invoke settings firmware load");
        }
    }
    else
    {
        ret_val = eph_configure_components(ephdata, NULL);
        if (ret_val)
        {
            /* Do not return on configuration loading failure as will still want to initialise device */
            eswin_log(ERR_LOG, "Failed to eph_configure_components %d", ret_val);
        } else {
            eswin_log(INFO_LOG, "success to eph_configure_components");
        }

        ret_val = eph_input_device_initialize(ephdata);
        if (ret_val)
        {
            return ret_val;
        } else {
            eswin_log(INFO_LOG, "eph_input_device_initialize success");
        }
    }
#endif

    eswin_log(DEBUG_LOG, "exit");

    return ret_val;
}

static int eph_configure_components(struct eph_data *ephdata, const struct firmware *device_settings)
{
    int ret_val = 0;

    eswin_log(INFO_LOG, "%s", device_settings ? "device_settings":"-");

    if (device_settings)
    {
        ret_val = eph_update_device_settings(ephdata, device_settings);
        if (ret_val)
        {
            eswin_log(ERR_LOG, "Error %d updating device_settings", ret_val);
        }
    }

    eswin_log(INFO_LOG, "exit");
    return ret_val;
}

/* Firmware Version is reported as Major.Minor.bootloaderversion */
static ssize_t eph_devattr_fw_version_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
                     ephdata->ephdeviceinfo.application_version_major, ephdata->ephdeviceinfo.application_version_minor,
                     ephdata->ephdeviceinfo.bootloader_version);
}

/* Hardware Version is reported as DevicefamilyID.DevicevariantID */
static ssize_t eph_devattr_hw_version_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%u.%u\n",
                     ephdata->ephdeviceinfo.product_id, ephdata->ephdeviceinfo.variant_id);
}


static int eph_enter_bootloader(struct eph_data *ephdata)
{
    int ret_val;

    eswin_log(INFO_LOG, "enter");


    if (!ephdata->in_bootloader)
    {
        if (ephdata->suspended)
        {
            if (ephdata->ephplatform->suspend_mode == EPH_SUSPEND_REGULATOR)
            {
                eph_regulator_enable(ephdata);
            }

            ephdata->suspended = false;
        }

        /* only disable interrupt and unregister if we have not done so before */
        disable_irq(ephdata->chg_irq);

    }

    /* force bootloader regardless whether we are in bootloader already - Always in defined TIC state */
    /* Force device into bootloader mode using chg line held low while toggle reset */
    ret_val = eph_chg_force_bootloader(ephdata);

    if (ret_val)
    {
        /* failed to enter bootloader correctly - restore CHG */
        (void)eph_bootloader_release_chg(ephdata);
        return ret_val;
    }


    ret_val = eph_comms_specific_bootloader_checks(ephdata);
    if (ret_val)
    {
        /* failed to enter bootloader correctly - restore CHG */
        (void)eph_bootloader_release_chg(ephdata);
        return ret_val;
    }

    if (!ephdata->in_bootloader)
    {
        ephdata->in_bootloader = true;
        /* Need in_bootloader to be true otherwise the regulators get disabled */
        eph_sysfs_mem_access_remove(ephdata);
        eph_unregister_input_device(ephdata);
    }

    eswin_log(INFO_LOG, "Entered bootloader");

    return 0;
}

int eph_load_device_settings(struct device *dev)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    int ret_val;

    eswin_log(DEBUG_LOG, "enter");

    ephdata->ephflash = (struct eph_flash*)devm_kzalloc(dev, sizeof(struct eph_flash), GFP_KERNEL);
    if (!ephdata->ephflash)
    {
        return -ENOMEM;
    }

    ephdata->ephflash->ephdata = ephdata;

    eswin_log(INFO_LOG, "request settings name: %s", ephdata->device_settings_name);

    /* Finds settings under the requested name */
    ret_val = request_firmware(&ephdata->ephflash->fw, ephdata->device_settings_name, dev);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "request settings failed(%d)", ret_val);
        goto free;
    } else {
        eswin_log(INFO_LOG, "request settings success");
    }

    ret_val = eph_configure_components(ephdata, ephdata->ephflash->fw);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "eph_configure_components device_settings failed(%d)", ret_val);
        goto release_firmware;
    } else {
        eswin_log(INFO_LOG, "eph_configure_components device_settings success");
    }

release_firmware:
    release_firmware(ephdata->ephflash->fw);
free:
    devm_kfree(dev, ephdata->ephflash);
    eswin_log(DEBUG_LOG, "exit");
    return ret_val;
}

int eph_load_fw(struct device *dev)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    int ret_val;

    eswin_log(DEBUG_LOG, "enter");

    ephdata->ephflash = (struct eph_flash*)devm_kzalloc(dev, sizeof(struct eph_flash), GFP_KERNEL);
    if (!ephdata->ephflash)
    {
        return -ENOMEM;
    }

    ephdata->ephflash->ephdata = ephdata;

    eswin_log(INFO_LOG, "request fw name:%s", ephdata->fw_name);
    
    /* Finds fw under the requested name */
    ret_val = request_firmware(&ephdata->ephflash->fw, ephdata->fw_name, dev);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "request fw failed(%d)", ret_val);
        goto free;
    } else {
        eswin_log(INFO_LOG, "request fw success");
    }

    eswin_log(INFO_LOG, "eph_check_firmware_format");
    /* Check for incorrect enc file */
    ret_val = eph_check_firmware_format(dev, ephdata->ephflash->fw);
    if (ret_val)
    {
        goto release_firmware;
    }

    eswin_log(INFO_LOG, "eph_enter_bootloader");
    ret_val = eph_enter_bootloader(ephdata);
    if (ret_val)
    {
        goto release_firmware;
    }

    eswin_log(INFO_LOG, "eph_send_frames");
    ret_val = eph_send_frames(ephdata);
    if (ret_val)
    {
        goto release_firmware;
    }

release_firmware:
    release_firmware(ephdata->ephflash->fw);
free:
    devm_kfree(dev, ephdata->ephflash);
    eswin_log(DEBUG_LOG, "exit");
    return ret_val;
}

/* gesture_mode: BIT0 enable/disable
 *               BIT1 tap
 *               BIT2 doubel tap
 *               BIT4 swipe
 */
int eph_gesture_mode_set(struct eph_data *ephdata, u8 gesture_mode)
{
    int ret_val;

    u8 type = TLV_CONFIG_DATA_WRITE;
    u8 prepayload_len = 4;
    // gesture extraction 450
    u8 comp_id_low = (u8)450;
    u8 comp_id_high = (u8)(450 > 8);
    // param config flags
    u8 offset = 24;
    // config length
    u8 data_len = 1;
    volatile u8 data[] = { gesture_mode & 0xf };
    volatile u8 tlv[] = {type, data_len + prepayload_len, 0x00, comp_id_low, comp_id_high, offset, 0x00, data[0]};
    u16 length = (sizeof(tlv)/sizeof(tlv[0]));
    eswin_log(DEBUG_LOG, "enter, gesture_mode=%d", gesture_mode);
    if (gesture_mode & 0xf0) {
        eswin_log(ERR_LOG, "Invaild gesture mode");
        return -EINVAL;
    }

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, (u8 *)&tlv[0]);
    mutex_unlock(&ephdata->comms_mutex);
    eswin_log(INFO_LOG, "gesture_mode=%d, eph_write_control_config ret_val=%d", gesture_mode, ret_val);

    if (likely(!ret_val))
        ephdata->gesture_mode = gesture_mode;
    else
        eswin_log(ERR_LOG, "Set/Clr gesture mode fail");

    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);
    return ret_val;
}

static ssize_t eph_devattr_update_fw_store(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    int ret_val;

    /* Pass in file name and length of name and if accepted put into fw_name */
    ret_val = eph_update_file_name(dev, &ephdata->fw_name, buf, count);
    if (ret_val)
    {
        return ret_val;
    }

    ret_val = eph_load_fw(dev);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "The firmware update failed(%d)", ret_val);
        count = ret_val;
    }
    else
    {
        eswin_log(INFO_LOG, "The firmware update succeeded");

        /* TODO re-check - initial bootloader appears to require an extended delay before moving to communicating with App */
        msleep(EPH_RESET_TIME*2);
        ephdata->suspended = false;

        ret_val = eph_initialize(ephdata);
        if (ret_val)
        {
            return ret_val;
        }
    }

    return count;
}

static ssize_t eph_devattr_update_device_settings_store(struct device *dev,
                                            struct device_attribute *attr,
                                            const char *buf,
                                            size_t count)
{
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    const struct eph_platform_data *ephplatform = ephdata->ephplatform;
    const struct firmware *device_settings;
    int ret_val;

    ret_val = eph_update_file_name(dev, &ephdata->device_settings_name, buf, count);
    if (ret_val)
    {
        return ret_val;
    }

    /* find the device settings file under the following name */
    ret_val = request_firmware(&device_settings, ephdata->device_settings_name, dev);
    if (ret_val)
    {
        eswin_log(ERR_LOG, "request_firmware failed(%d) %s", ret_val, ephdata->device_settings_name);
        goto out;
    }

    ephdata->updating_device_settings = true;


    if (ephdata->suspended)
    {
        eswin_log(INFO_LOG, "ESWIN device was in suspend %d", ret_val);
        if (ephplatform->suspend_mode == EPH_SUSPEND_REGULATOR)
        {
            enable_irq(ephdata->chg_irq);
            eph_regulator_enable(ephdata);
        }
        else if (ephplatform->suspend_mode == EPH_SUSPEND_DEEP_SLEEP)
        {
            /* do nothing as TIC does not currently support sleep */
        }

        ephdata->suspended = false;
    }

    ret_val = eph_configure_components(ephdata, device_settings);
    if (!ret_val)
    {
        /* no error so return count */
        ret_val = count;
    }

    release_firmware(device_settings);
out:
    ephdata->updating_device_settings = false;
    return ret_val;
}


static ssize_t eph_devattr_comms_read(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    int ret_val;
    size_t count = TLV_HEADER_SIZE;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);


    /* Wait for any pending IRQ handler to complete - Gives priority for interrupt handler */
    synchronize_irq(ephdata->chg_irq);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_comms_two_stage_read(ephdata, (u8*)buf);
    mutex_unlock(&ephdata->comms_mutex);


    if(ret_val)
    {
        /* Return NULL message if there is no message or an error occured */
        /* clear 3 bytes to mimic null read */
        memset(buf, 0, TLV_HEADER_SIZE); 
    }
    else
    {
        /* decode and return the legth of message */
        count = (size_t)((buf[TLV_LENGTH_FIELD] | ((u16)buf[TLV_LENGTH_FIELD + 1u] << 8u)) + TLV_HEADER_SIZE);
    }

    eswin_log(INFO_LOG, "ESWIN sysfs read the following %d, %d , %d", buf[0], buf[1], buf[2]);

    return ret_val == 0 ? count : ret_val;
}


static ssize_t eph_devattr_comms_write(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    
    struct eph_data *ephdata = (struct eph_data*)dev_get_drvdata(dev);
    ssize_t ret_val;

    ret_val = eph_check_mem_access_params(ephdata, 0, &count);
    if (0 < ret_val)
    {
        return ret_val;
    }

    if (0 < count)
    {

        /* Wait for any pending IRQ handler to complete - Gives priority for interrupt handler */
        synchronize_irq(ephdata->chg_irq);

        mutex_lock(&ephdata->comms_mutex);
        ret_val = eph_comms_write(ephdata, count, (u8 *)buf);
        mutex_unlock(&ephdata->comms_mutex);

        eswin_log(INFO_LOG, "ESWIN sysfs: Write the following %d, %d, %d", buf[0], buf[1], buf[2]);        
    }
    

    return ret_val == 0 ? count : ret_val;


  }


static ssize_t eph_devattr_device_report_read(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    struct tlv_header tlvheader;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);



    mutex_lock(&ephdata->sysfs_report_buffer_lock);
    tlvheader = eph_get_tl_header_info(ephdata, sysfs_report_buf);


    if (TLV_HEADER_SIZE>tlvheader.length)
    {
        /* if length was 0 for any reason generate NULL message - Ensures null message copied to the buffer */
        tlvheader.length = TLV_HEADER_SIZE;
        memset(&sysfs_report_buf[0], 0, tlvheader.length); 
    }

    /* copy message into page buffer */
    memcpy(buf,&sysfs_report_buf[0],tlvheader.length);

    /* Flush buffer once consumed - to prevent same message being read twice */
    /* relies on message being smaller than a page and the tooling processing the header in that single read */
    memset(&sysfs_report_buf[0], 0, tlvheader.length); 

    /* Only generate a message if valid message is being returned. */
    if (0!=tlvheader.type)
    {
        eswin_log(INFO_LOG, "ESWIN sysfs read buffer. Type: %d, length: %d", tlvheader.type, tlvheader.length);
    }
    mutex_unlock(&ephdata->sysfs_report_buffer_lock);


    return (size_t)tlvheader.length;

  }


static ssize_t eph_devattr_reset_device(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    int ret_val;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    eswin_log(INFO_LOG, "start gpio_reset");
    eph_reset_device(ephdata);

    eswin_log(INFO_LOG, "start trigger_baseline");
    ret_val = eph_trigger_baseline(ephdata);
    eph_clear_all_host_touch_slots(ephdata);

    eswin_log(INFO_LOG, "end");
    return ret_val;

  }

static ssize_t eph_devattr_gesture_wakeup_read(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    struct eph_data *ephdata;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (!ephdata)
        return -EIO;

    return sprintf(buf, "%s mode[%x]\n", ephdata->gesture_wakeup_enable ? "enable" : "disable",
            ephdata->gesture_mode);
}

static ssize_t eph_devattr_gesture_wakeup_store(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    struct eph_data *ephdata;
    int ret_val = -1;
    int input = 0;
    u8 gesture_mode = 0;
    u8 set_mode = 0;
    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (!ephdata)
        return -EIO;

    if (kstrtoint(buf, 10, &input))
        return -EINVAL;

    switch (input) {
    case 1:
        /* enable tap */
        if (ephdata->gesture_mode & BIT(1)) {
            eswin_log(INFO_LOG, "tap already set");
            goto exit;
        }
        gesture_mode = BIT(1);
        break;
    case 2:
        /* enable double tap */
        if (ephdata->gesture_mode & BIT(2)) {
            eswin_log(INFO_LOG, "double tap already set");
            goto exit;
        }
        gesture_mode = BIT(2);
        break;
    case 3:
        /* enable swipe */
        if (ephdata->gesture_mode & BIT(3)) {
            eswin_log(INFO_LOG, "swipe already set");
            goto exit;
        }
        gesture_mode = BIT(3);
        break;
    case -1:
        /* disable tap */
        if ((ephdata->gesture_mode & BIT(1)) == 0) {
            eswin_log(INFO_LOG, "tap not set");
            goto exit;
        }
        gesture_mode |= ~BIT(1);
        break;
    case -2:
        /* disable double tap */
        if ((ephdata->gesture_mode & BIT(2)) == 0) {
            eswin_log(INFO_LOG, "double tap not set");
            goto exit;
        }
        gesture_mode |= ~BIT(2);
        break;
    case -3:
        /* disable swipe */
        if ((ephdata->gesture_mode & BIT(3)) == 0) {
            eswin_log(INFO_LOG, "swipe not set");
            goto exit;
        }
        gesture_mode |= ~BIT(3);
        break;
    case 0:
    default:
        eswin_log(ERR_LOG, "Invaild Para set");
        return -EINVAL;
    }

    set_mode = (input > 0) ? (ephdata->gesture_mode | gesture_mode) :
            (ephdata->gesture_mode & gesture_mode);

    ret_val = eph_gesture_mode_set(ephdata, set_mode);

    if (ret_val) {
        eswin_log(ERR_LOG, "mode set/clr fail %x", ret_val);
        goto exit;
    }

    if (ephdata->gesture_mode & 0xE) {
        ephdata->gesture_wakeup_enable = true;
        eswin_log(INFO_LOG, "mode %x enabled", ephdata->gesture_mode);
    } else {
        ephdata->gesture_wakeup_enable = false;
        eswin_log(INFO_LOG, "gesture disabled");
    }

exit:
    return count;
}


/* .attr.name, .attr.mode, .show, .store  */
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, eph_devattr_update_fw_store);

static DEVICE_ATTR(pt01, (S_IRUGO | S_IWUSR), eph_devattr_test_pt01_show, eph_devattr_test_pt01_set);
static DEVICE_ATTR(pt02, (S_IRUGO | S_IWUSR), eph_devattr_test_pt02_show, eph_devattr_test_pt02_set);
static DEVICE_ATTR(pt03, (S_IRUGO | S_IWUSR), eph_devattr_test_pt03_show, eph_devattr_test_pt03_set);
static DEVICE_ATTR(pt04, (S_IRUGO | S_IWUSR), eph_devattr_test_pt04_show, eph_devattr_test_pt04_set);

/* S_IRUGO - read-only attributes  */
static DEVICE_ATTR(fw_version, S_IRUGO, eph_devattr_fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, eph_devattr_hw_version_show, NULL);
/* S_IWUSR - write access to root only */
static DEVICE_ATTR(update_device_settings, S_IWUSR, NULL, eph_devattr_update_device_settings_store);
static DEVICE_ATTR(write_device_message, S_IWUSR, NULL, eph_devattr_comms_write);
static DEVICE_ATTR(read_device_message, S_IRUGO, eph_devattr_comms_read, NULL);
static DEVICE_ATTR(read_device_report, S_IRUGO, eph_devattr_device_report_read, NULL);
static DEVICE_ATTR(reset_device, S_IRUGO, eph_devattr_reset_device, NULL);
static DEVICE_ATTR(gesture_wakeup, (S_IWUSR|S_IRUGO), eph_devattr_gesture_wakeup_read, eph_devattr_gesture_wakeup_store);


static struct attribute *eph_fw_attrs[] =
{
    /* update_fw */
    &dev_attr_update_fw.attr,
    NULL
};

static const struct attribute_group eph_fw_attr_group =
{
    .attrs = eph_fw_attrs,
};

static struct attribute *eph_test_pt_attrs[] =
{
    &dev_attr_pt01.attr,
    &dev_attr_pt02.attr,
    &dev_attr_pt03.attr,
    &dev_attr_pt04.attr,
    NULL,
};

static const struct attribute_group eph_test_attr_group =
{
    .attrs = eph_test_pt_attrs,
};

static struct attribute *eph_attrs[] =
{
    /* fw_version */
    &dev_attr_fw_version.attr,
    /* hw_version */    
    &dev_attr_hw_version.attr,
    /* update_device_settings */
    &dev_attr_update_device_settings.attr,
    /* write message to device */    
    &dev_attr_write_device_message.attr,
    /* read message from device */    
    &dev_attr_read_device_message.attr,
    /* read buffered report from device */    
    &dev_attr_read_device_report.attr,

    &dev_attr_reset_device.attr,
    &dev_attr_gesture_wakeup.attr,
    NULL
};

static const struct attribute_group eph_attr_group =
{
    .attrs = eph_attrs,
};

static int eph_sysfs_mem_access_init(struct eph_data *ephdata)
{
    struct comms_device *commsdevice = ephdata->commsdevice;
    int ret_val;

    eswin_log(DEBUG_LOG, "enter");

    ret_val = sysfs_create_group(&commsdevice->dev.kobj, &eph_attr_group);
    if (ret_val)
    {
        eswin_log(INFO_LOG, "Failure %d creating sysfs group", ret_val);
        sysfs_remove_group(&commsdevice->dev.kobj, &eph_attr_group);
        return ret_val;
    }

    ret_val = sysfs_create_group(&commsdevice->dev.kobj, &eph_test_attr_group);
    if (ret_val)
    {
        eswin_log(INFO_LOG, "Failure %d creating test sysfs group", ret_val);
        sysfs_remove_group(&commsdevice->dev.kobj, &eph_test_attr_group);
        return ret_val;
    }

    eswin_log(DEBUG_LOG, "exit");
    return ret_val;
}

static void eph_sysfs_mem_access_remove(struct eph_data *ephdata)
{
    struct comms_device *commsdevice = ephdata->commsdevice;

    eswin_log(INFO_LOG, "enter");

    sysfs_remove_group(&commsdevice->dev.kobj, &eph_attr_group);
}


static int eph_start(struct eph_data *ephdata)
{
    eswin_log(INFO_LOG, "suspend_mode %d", ephdata->ephplatform->suspend_mode);

    if (!ephdata->suspended || ephdata->in_bootloader)
    {
        eswin_log(INFO_LOG, "suspended %d, in_bootloader = %d", ephdata->suspended, ephdata->in_bootloader);
        return 0;
    }

    switch (ephdata->ephplatform->suspend_mode)
    {
        case EPH_SUSPEND_REGULATOR:
            enable_irq(ephdata->chg_irq);
            eswin_log(DEBUG_LOG, "enable_irq");
            #if (ESWIN_EPH861X_I2C)
            /* TODO Pixel4XL specific - Temporary comment out diabling/enable of regulator due to demo handset specific issues */
            eph_regulator_enable(ephdata);
            #endif
            break;

        case EPH_SUSPEND_DEEP_SLEEP:
        default:
            /* do nothing for the moment as no tic sleep */

            break;
    }

    ephdata->suspended = false;

    eswin_log(INFO_LOG, "exit");

    return 0;
}

static int eph_stop(struct eph_data *ephdata)
{

    eswin_log(INFO_LOG, "suspend mode %d", ephdata->ephplatform->suspend_mode);

    if (ephdata->suspended || ephdata->in_bootloader || ephdata->updating_device_settings)
    {
        eswin_log(INFO_LOG, "suspended %d, in_bootloader %d, updating_device_settings %d", ephdata->suspended, ephdata->in_bootloader, ephdata->updating_device_settings);
        return 0;
    }

    switch (ephdata->ephplatform->suspend_mode)
    {
        case EPH_SUSPEND_REGULATOR:
            disable_irq(ephdata->chg_irq);
            eswin_log(DEBUG_LOG, "disable_irq");
            #if (ESWIN_EPH861X_I2C)
            /* TODO Pixel4XL specific - Temporary comment out diabling of regulator due to demo handset specific issues */
            eph_regulator_disable(ephdata);
            #endif
            eph_clear_all_host_touch_slots(ephdata);
            break;

        case EPH_SUSPEND_DEEP_SLEEP:
        default:

            /* For now does nothing as sleep not implemented in TIC */

            /* Clear all the touch slots on UI */
            eph_clear_all_host_touch_slots(ephdata);
    }

    ephdata->suspended = true;

    eswin_log(INFO_LOG, "exit");

    return 0;
}

static int eph_input_open(struct input_dev *inputdev)
{
    struct eph_data *ephdata = (struct eph_data *)input_get_drvdata(inputdev);
    int ret_val;

    eswin_log(INFO_LOG, "enter");

    ret_val = eph_start(ephdata);

    if (ret_val)
    {
        eswin_log(ERR_LOG, "failed rc=%d", ret_val);
    }

    return ret_val;
}

static void eph_input_close(struct input_dev *inputdev)
{
    struct eph_data *ephdata = (struct eph_data *)input_get_drvdata(inputdev);
    int ret_val;

    eswin_log(INFO_LOG, "enter");

    ret_val = eph_stop(ephdata);

    if (ret_val)
    {
        eswin_log(ERR_LOG, "failed rc=%d", ret_val);
    }
}

int eph_dev_enter_lp_mode(struct eph_data *ephdata);
int eph_dev_enter_normal_mode(struct eph_data *ephdata);

#if defined(CONFIG_BOARD_FLORAL) //pixel4XL platform
static int eph_notifier_callback(struct notifier_block *nb, unsigned long event,
        void *data)
{
    struct eph_data *ephdata = container_of(nb, struct eph_data, notifier);
    struct msm_drm_notifier *evdata = data;
    struct comms_device *commsdevice;
    struct backlight_device *bd = ephdata->bl;
    unsigned int blank;
    int brightness = 0;

    if (!evdata || evdata->id != 0)
        return 0;

    commsdevice = ephdata->commsdevice;

    eswin_log(DEBUG_LOG, "enter, event %x", event);

    if (event != MSM_DRM_EVENT_BLANK)
        return 0;

    if (evdata->data && event == MSM_DRM_EVENT_BLANK && ephdata) {
        blank = *(int *)(evdata->data);

        switch (blank) {
        case MSM_DRM_BLANK_POWERDOWN:
            eph_dev_enter_lp_mode(ephdata);
            if (bd->ops && bd->ops->get_brightness)
                brightness = bd->ops->get_brightness(bd);
            else
                brightness = bd->props.brightness;
            ephdata->last_brightness = brightness;
            eswin_log(INFO_LOG, "brightness %d", brightness);
            break;
        case MSM_DRM_BLANK_UNBLANK:
            eph_dev_enter_normal_mode(ephdata);
            schedule_work(&ephdata->force_baseline_work);
            break;
        default:
            break;
        }
    }
    eswin_log(DEBUG_LOG, "exit");
    return NOTIFY_OK;
}
#elif defined(CONFIG_BOARD_CLOUDRIPPER) // pixel7Pro platform
struct drm_connector *eph_get_bridge_connector(struct drm_bridge *bridge)
{
    struct drm_connector *connector;
    struct drm_connector_list_iter conn_iter;
    eswin_log(DEBUG_LOG, "enter");
    drm_connector_list_iter_begin(bridge->dev, &conn_iter);
    drm_for_each_connector_iter(connector, &conn_iter) {
        if (connector->encoder == bridge->encoder)
            break;
    }
    drm_connector_list_iter_end(&conn_iter);
    eswin_log(DEBUG_LOG, "exit");
    return connector;
}

static bool eph_bridge_is_lp_mode(struct drm_connector *connector)
{
    eswin_log(DEBUG_LOG, "enter");
    if (connector && connector->state) {
        struct exynos_drm_connector_state *s =
            to_exynos_connector_state(connector->state);
        return s->exynos_mode.is_lp_mode;
    }
    return false;
}

static void eph_panel_bridge_enable(struct drm_bridge *bridge)
{
    struct eph_data *ephdata =
                container_of(bridge, struct eph_data, panel_bridge);
    struct device *dev = &ephdata->commsdevice->dev;

    eswin_log(DEBUG_LOG, "enter");
    ephdata->is_panel_lp_mode = eph_bridge_is_lp_mode(ephdata->connector);
    if (!ephdata->is_panel_lp_mode) {
        eph_dev_enter_normal_mode(ephdata);
        schedule_work(&ephdata->force_baseline_work);
    }
    eswin_log(DEBUG_LOG, "exit");
}

static void eph_panel_bridge_disable(struct drm_bridge *bridge)
{
    struct eph_data *ephdata =
            container_of(bridge, struct eph_data, panel_bridge);
    struct device *dev = &ephdata->commsdevice->dev;
    eswin_log(DEBUG_LOG, "enter");
    if (bridge->encoder && bridge->encoder->crtc) {
        const struct drm_crtc_state *crtc_state = bridge->encoder->crtc->state;

        if (drm_atomic_crtc_effectively_active(crtc_state))
            return;
    }

    eph_dev_enter_lp_mode(ephdata);
    ephdata->last_brightness = backlight_get_brightness(ephdata->bl);
    eswin_log(DEBUG_LOG, "exit");
}

static void eph_panel_bridge_mode_set(struct drm_bridge *bridge,
                        const struct drm_display_mode *mode,
                        const struct drm_display_mode *adjusted_mode)
{
    struct eph_data *ephdata = container_of(bridge, struct eph_data, panel_bridge);
    struct device *dev = &ephdata->commsdevice->dev;

    eswin_log(DEBUG_LOG, "enter");

    if (!ephdata->connector || !ephdata->connector->state) {
        ephdata->connector = eph_get_bridge_connector(bridge);
        eswin_log(ERR_LOG, "Get bridge connector");
    }
    eswin_log(DEBUG_LOG, "exit");
}

static const struct drm_bridge_funcs panel_bridge_funcs = {
    .enable = eph_panel_bridge_enable,
    .disable = eph_panel_bridge_disable,
    .mode_set = eph_panel_bridge_mode_set,
};

static int eph_register_panel_bridge(struct eph_data *ephdata)
{
    struct device *dev = &ephdata->commsdevice->dev;
    eswin_log(DEBUG_LOG, "enter");
#ifdef CONFIG_OF
    ephdata->panel_bridge.of_node = dev->of_node;
#endif
    ephdata->panel_bridge.funcs = &panel_bridge_funcs;
    drm_bridge_add(&ephdata->panel_bridge);

    eswin_log(DEBUG_LOG, "exit");

    return 0;
}

static void eph_unregister_panel_bridge(struct eph_data *ephdata)
{
    struct drm_bridge *node;
    struct drm_bridge *bridge = &ephdata->panel_bridge;
    eswin_log(DEBUG_LOG, "enter");
    drm_bridge_remove(bridge);

    if (!bridge->dev) /* not attached */
        return;

    drm_modeset_lock(&bridge->dev->mode_config.connection_mutex, NULL);
    list_for_each_entry(node, &bridge->encoder->bridge_chain, chain_node)
        if (node == bridge) {
            if (bridge->funcs->detach)
                bridge->funcs->detach(bridge);
            list_del(&bridge->chain_node);
            break;
        }
    drm_modeset_unlock(&bridge->dev->mode_config.connection_mutex);
    bridge->dev = NULL;
    eswin_log(DEBUG_LOG, "exit");
}
#endif //USE_DRM_BRIDGE

#ifdef ESWIN_USE_PINCTRL
static int eph_pinctrl_configure(struct eph_data *ephdata, bool enable)
{
    struct pinctrl_state *state;

    if (IS_ERR_OR_NULL(ephdata->pinctrl)) {
        eswin_log(ERR_LOG, "Invalid pinctrl");
        return -EINVAL;
    }

    eswin_log(DEBUG_LOG, "enable %d", enable);

    if (enable) {
        state = pinctrl_lookup_state(ephdata->pinctrl, "ts_active");
        if (IS_ERR(state))
            eswin_log(ERR_LOG, "Could not get ts_active pinstate!");
    } else {
        state = pinctrl_lookup_state(ephdata->pinctrl, "ts_suspend");
        if (IS_ERR(state))
            eswin_log(ERR_LOG, "Could not get ts_suspend pinstate!");
    }
    if (!IS_ERR_OR_NULL(state))
        return pinctrl_select_state(ephdata->pinctrl, state);

    return 0;
}
#endif

#ifdef ESWIN_ESD_CHECK
/* Started by AICoder, pid:pe36128581p8bd41420a088e202971149348a7e7 */
int heartbeat_work_start(struct eph_data *ephdata)
{
    if (atomic_read(&ephdata->heartbeat_on)) {
        eswin_log(WARN_LOG, "heartbeat already on");
        return 0;
    }

    atomic_set(&ephdata->heartbeat_on, 1);
    schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(300));
    return 0;
}

int heartbeat_work_stop(struct eph_data *ephdata)
{
    atomic_set(&ephdata->heartbeat_on, 0);
    cancel_delayed_work_sync(&ephdata->heartbeat_work);
    return 0;
}
/* Ended by AICoder, pid:pe36128581p8bd41420a088e202971149348a7e7 */

/* Started by AICoder, pid:sad73rde7b7ac5614dc30862f016b77b8723ac87 */
static int send_heartbeat(struct eph_data *ephdata)
{
    int ret;
    mutex_lock(&ephdata->comms_mutex);
    ret = eph_read_device_information(ephdata);
    mutex_unlock(&ephdata->comms_mutex);
    return ret;
}

static void heartbeat_work_handler(struct work_struct *work)
{
    int ret = 0;
    struct eph_data *ephdata = container_of(work, struct eph_data, heartbeat_work.work);

    eswin_log(INFO_LOG, "ic heartbeat work...");

    if (!atomic_read(&ephdata->heartbeat_on)) {
        eswin_log(WARN_LOG, "ic heartbeat off");
        return;
    }

    // check power
    if (ephdata->ephplatform->use_regulator_avdd) {
        if ((ephdata->reg_avdd) && (!regulator_is_enabled(ephdata->reg_avdd)))
            eswin_log(ERR_LOG, "ic avdd is not enable");
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if ((gpio_is_valid(ephdata->ephplatform->gpio_power)) && (!gpio_get_value(ephdata->ephplatform->gpio_power)))
            eswin_log(ERR_LOG, "pwr gpio value is not high");
    }

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if ((ephdata->reg_dvdd) && (!regulator_is_enabled(ephdata->reg_dvdd)))
            eswin_log(ERR_LOG, "ic dvdd is not enable");
        }

    if (ephdata->suspended) {
#if 1
        if (ephdata->in_bootloader) {
            eswin_log(WARN_LOG, "system suspend, ic in boot mode!");
            goto ic_reset;
        } else {
            // ic in gesture mode or deep mode or idle
            // it's possible clk is low when suspend, first read/write data
            // will error
            // send the heartbeat two times to avoid this issue

            //NOTE: deep mode process
            //if (deep_mode) {
            // send resume cmd to ic
            //}
            ret = send_heartbeat(ephdata);
            // for zte need double send
            //ret = send_heartbeat(ephdata);
            if (ret) {
                eswin_log(ERR_LOG, "suspend, heartbeat fail %d", ret);
                goto ic_recovery;
            }
            //if (deep_mode) {
            // send suspend cmd to ic
            //}
        }
#endif
        eswin_log(WARN_LOG, "system suspend");
    } else {
        if (ephdata->in_bootloader) {
            /* may loading firmware or setting */
            eswin_log(WARN_LOG, "system active, ic in boot mode!");
        } else {
            // ic in active or idle mode
            ret = send_heartbeat(ephdata);
            if (ret) {
                eswin_log(ERR_LOG, "suspend, heartbeat fail %d", ret);
                goto ic_recovery;
            }
        }
    }

    if (atomic_read(&ephdata->heartbeat_on))
        schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(5000));
    return;

ic_recovery:
    eph_recovery_device(ephdata);
    if (atomic_read(&ephdata->heartbeat_on))
        schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(3000));
    return;
#if 1
ic_reset:
    eph_reset_device(ephdata);
    if (atomic_read(&ephdata->heartbeat_on))
        schedule_delayed_work(&ephdata->heartbeat_work, msecs_to_jiffies(3000));
    return;
#endif
}
/* Ended by AICoder, pid:sad73rde7b7ac5614dc30862f016b77b8723ac87 */
#endif

#if (ESWIN_EPH861X_SPI)
static int eph_probe(struct comms_device *commsdevice)
#endif
#if (ESWIN_EPH861X_I2C)
static int eph_probe(struct comms_device *commsdevice, const struct comms_device_id *id)
#endif
{
    struct eph_data *ephdata;
    struct eph_platform_data *ephplatform;
    int ret_val;

    eswin_log(DEBUG_LOG, "enter");
#ifdef CONFIG_NOT_USE_LCD_INFO
    tpd_cdev->ztp_probe_fail_chip_id = TS_CHIP_MAX;
#endif
    ret_val = eph_comms_specific_checks(commsdevice);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_comms_specific_checks failed, ret=%d", ret_val);
        return -EINVAL;
    } else {
        eswin_log(INFO_LOG, "eph_comms_specific_checks success");
    }

    ephplatform = eph_platform_data_get(commsdevice);
    if (IS_ERR(ephplatform)) {
        eswin_log(ERR_LOG, "eph_platform_data_get failed");
        return PTR_ERR(ephplatform);
    } else {
        eswin_log(INFO_LOG, "eph_platform_data_get success");
    }

    ephdata = (struct eph_data *)kzalloc(sizeof(struct eph_data), GFP_KERNEL);
    if (!ephdata) {
        return -ENOMEM;
    }

    zte_ephdata = ephdata;
#if defined(CONFIG_BOARD_FLORAL)
    ephdata->bl = backlight_device_get_by_type(BACKLIGHT_RAW);

    if (ephdata->bl) {
        eswin_log(INFO_LOG, "backlight brightness %x", ephdata->bl->props.brightness);
        eswin_log(INFO_LOG, "backlight max brightness %x", ephdata->bl->props.max_brightness);
    } else {
        eswin_log(ERR_LOG, "backlight not ready");
        ret_val = -EPROBE_DEFER;
        goto err_free_mem;
    }

    INIT_WORK(&ephdata->force_baseline_work, eph_trigger_baseline_work);
#endif
#ifdef ESWIN_ESD_CHECK
    INIT_DELAYED_WORK(&ephdata->heartbeat_work, heartbeat_work_handler);
#endif
    ephdata->commsdevice = commsdevice;
    ephdata->ephplatform = ephplatform;
    eph_comms_driver_data_set(commsdevice, ephdata);

#ifdef ESWIN_USE_PINCTRL
    ephdata->pinctrl = devm_pinctrl_get(&commsdevice->dev);
    if (IS_ERR_OR_NULL(ephdata->pinctrl)) {
        eswin_log(WARN_LOG, "Could not get pinctrl");
    } else {
        eph_pinctrl_configure(ephdata, true);
    }
#endif

    ret_val = eph_allocate_comms_memory(commsdevice, ephdata);

    if (ephdata->ephplatform->device_settings_name) {
        eph_update_file_name(&ephdata->commsdevice->dev,
                             &ephdata->device_settings_name,
                             ephdata->ephplatform->device_settings_name,
                             strlen(ephdata->ephplatform->device_settings_name));
    }
    if (ephdata->ephplatform->fw_name) {
        eph_update_file_name(&ephdata->commsdevice->dev,
                             &ephdata->fw_name,
                             ephdata->ephplatform->fw_name,
                             strlen(ephdata->ephplatform->fw_name));
    }
    eswin_log(INFO_LOG, "ephdata->fw_name: %s, ephdata->device_settings_name: %s", ephdata->fw_name, ephdata->device_settings_name);

    init_completion(&ephdata->chg_completion);
    init_completion(&ephdata->reset_completion);


    mutex_init(&ephdata->comms_mutex);
    mutex_init(&ephdata->sysfs_report_buffer_lock);

    device_init_wakeup(&commsdevice->dev, true);

    ret_val = eph_gpio_setup(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_gpio_setup failed(%d)", ret_val);
        goto err_free_mem;
    } else {
        eswin_log(INFO_LOG, "eph_gpio_setup success");
    }

    ret_val = eph_acquire_irq(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_acquire_irq failed(%d)", ret_val);
        goto err_free_irq;
    } else {
        eswin_log(INFO_LOG, "eph_acquire_irq success");
    }

    ret_val = eph_probe_regulators(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_probe_regulators failed(%d)", ret_val);
        goto err_free_irq;
    } else {
        eswin_log(INFO_LOG, "eph_probe_regulators success");
    }

    /* Need to have IRQ disabled before calling eph_initialize() as it re-enables it */
    disable_irq(ephdata->chg_irq);

    ret_val = sysfs_create_group(&commsdevice->dev.kobj, &eph_fw_attr_group);
    if (ret_val) {
        eswin_log(ERR_LOG, "Failure %d creating fw sysfs group", ret_val);
        goto err_free_irq;
    } else {
        eswin_log(INFO_LOG, "sysfs_create_group success");
    }

    ret_val = eph_read_device_information(ephdata);
    eswin_log(INFO_LOG,
        "Product ID: %u Variant ID: %u Application_version_major %u Application_version_minor: %u Bootloader_version: %u Protocol_version: %u CRC:%u",
        ephdata->ephdeviceinfo.product_id, ephdata->ephdeviceinfo.variant_id,
        ephdata->ephdeviceinfo.application_version_major, ephdata->ephdeviceinfo.application_version_minor,
        ephdata->ephdeviceinfo.bootloader_version, ephdata->ephdeviceinfo.protocol_version, ephdata->ephdeviceinfo.crc);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_read_device_information failed(%d)", ret_val);
        goto err_free_irq;
    }

    eswin_log(INFO_LOG, "fw_name: %s", ephdata->fw_name);
    if (ephdata->fw_name) {
        ret_val = eph_load_fw(&ephdata->commsdevice->dev);
        if (ret_val) {
            eswin_log(ERR_LOG, "eph_load_fw failed(%d)", ret_val);
            goto err_free_irq;
        } else {
            eswin_log(INFO_LOG, "eph_load_fw success");
            //delay for load stabel
            msleep(200);
            //reset tic for into normal mode
            eph_reset_device(ephdata);
            //delay for tic ready
            msleep(10);
        }
    }

    ret_val = eph_initialize(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_initialize failed(%d)", ret_val);
        goto err_free_irq;
    } else {
        eswin_log(INFO_LOG, "eph_initialize success");
    }

#if defined(CONFIG_BOARD_FLORAL) // pixel4XL plat
    ephdata->notifier.notifier_call = eph_notifier_callback;
    ret_val = msm_drm_register_client(&ephdata->notifier);
    if (ret_val < 0)
        eswin_log(ERR_LOG, "Failure %d register msm drm client", ret_val);
    else
        eswin_log(INFO_LOG, "success register norifier");
#elif defined(CONFIG_BOARD_CLOUDRIPPER) // pixel7Pro plat
    ret_val = eph_register_panel_bridge(ephdata);
    if (ret_val < 0)
        eswin_log(ERR_LOG, "Failure %d panel bridge", ret_val);
    else
        eswin_log(INFO_LOG, "success register panel bridge");
#endif

    eswin_log(INFO_LOG, "device_settings_name: %s", ephdata->device_settings_name);
    if (ephdata->device_settings_name) {
        ret_val = eph_load_device_settings(&ephdata->commsdevice->dev);
        if (ret_val) {
            eswin_log(ERR_LOG, "eph_load_device_settings failed(%d)", ret_val);
            goto err_free_irq;
        } else {
            eswin_log(INFO_LOG, "eph_load_device_settings success");
            tpd_cdev->fw_ready = true;
        }
    }

    ret_val = eph_input_device_initialize(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_input_device_initialize failed(%d)", ret_val);
        goto err_free_irq;
    } else {
        eswin_log(INFO_LOG, "eph_input_device_initialize success");
    }

    enable_irq(ephdata->chg_irq);

    msleep(100); // wait tic stable
    ret_val = eph_read_device_setting_version(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_read_device_setting_version failed(%d)", ret_val);
    } else {
        eswin_log(INFO_LOG, "eph_read_device_setting_version success");
    }

    /* default disable gesture */
    ephdata->gesture_mode = 0x0;
    ephdata->gesture_wakeup_enable = false;

    ephdata->lp = false;
    ephdata->irq_wake = false;
#ifdef ESWIN_ESD_CHECK
    heartbeat_work_start(zte_ephdata);
#endif

#ifdef CONFIG_PM
	init_completion(&ephdata->zte_ctrl.pm_completion);
	ephdata->zte_ctrl.pm_suspend = false;
#endif

    eswin_tpd_register_fw_class(ephdata);
    tpd_cdev->TP_have_registered = true;
    tpd_cdev->tp_chip_id = TS_CHIP_ESWIN;
    eswin_log(DEBUG_LOG, "exit");
    return 0;

err_free_irq:
    if (ephdata->chg_irq) {
        free_irq(ephdata->chg_irq, ephdata);
        ephdata->chg_irq = 0;
    }

    if (ephdata->ephplatform->gpio_reset) {
        gpio_direction_output(ephdata->ephplatform->gpio_reset, 0);
        gpio_free(ephdata->ephplatform->gpio_reset);
        ephdata->ephplatform->gpio_reset = 0;
    }

    if (ephdata->ephplatform->gpio_irq) {
        gpio_free(ephdata->ephplatform->gpio_irq);
        ephdata->ephplatform->gpio_irq = 0;
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if (ephdata->ephplatform->gpio_power) {
            gpio_direction_output(ephdata->ephplatform->gpio_power, 0);
            gpio_free(ephdata->ephplatform->gpio_power);
            ephdata->ephplatform->gpio_power = 0;
        }
    }

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if(ephdata->reg_dvdd) {
            if (regulator_is_enabled(ephdata->reg_dvdd)) {
                regulator_disable(ephdata->reg_dvdd);
            }
            regulator_put(ephdata->reg_dvdd);
            ephdata->reg_dvdd = NULL;
        }
    }

    if (ephdata->ephplatform->use_regulator_avdd) {
        if(ephdata->reg_avdd) {
            if (regulator_is_enabled(ephdata->reg_avdd)) {
                regulator_disable(ephdata->reg_avdd);
            }
            regulator_put(ephdata->reg_avdd);
            ephdata->reg_avdd = NULL;
        }
    }

err_free_mem:
    kfree(ephdata);
    tpd_cdev->ztp_probe_fail_chip_id = TS_CHIP_ESWIN;
    eswin_log(ERR_LOG, "error");
    return ret_val;
}

static int eph_remove(struct comms_device *commsdevice)
{
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);
    eswin_log(INFO_LOG, "enter");
#ifdef ESWIN_ESD_CHECK
    cancel_delayed_work_sync(&ephdata->heartbeat_work);
#endif
    sysfs_remove_group(&commsdevice->dev.kobj, &eph_fw_attr_group);
    eph_sysfs_mem_access_remove(ephdata);

#if defined(CONFIG_BOARD_FLORAL)
    msm_drm_unregister_client(&ephdata->notifier);
#elif defined(CONFIG_BOARD_CLOUDRIPPER)
    eph_unregister_panel_bridge(ephdata);
#endif

    if (ephdata->chg_irq) {
        free_irq(ephdata->chg_irq, ephdata);
        ephdata->chg_irq = 0;
    }

    if (ephdata->ephplatform->gpio_reset) {
        gpio_free(ephdata->ephplatform->gpio_reset);
        ephdata->ephplatform->gpio_reset = 0;
    }

    if (ephdata->ephplatform->gpio_irq) {
        gpio_free(ephdata->ephplatform->gpio_irq);
        ephdata->ephplatform->gpio_irq = 0;
    }

    if (ephdata->ephplatform->use_gpio_power) {
        if (ephdata->ephplatform->gpio_power) {
            gpio_free(ephdata->ephplatform->gpio_power);
            ephdata->ephplatform->gpio_power = 0;
        }
    }

    if (ephdata->ephplatform->use_regulator_avdd) {
        if(ephdata->reg_avdd) {
            if (regulator_is_enabled(ephdata->reg_avdd)) {
                regulator_disable(ephdata->reg_avdd);
            }
            regulator_put(ephdata->reg_avdd);
            ephdata->reg_avdd = NULL;
        }
    }

    if (ephdata->ephplatform->use_regulator_dvdd) {
        if(ephdata->reg_dvdd) {
            if (regulator_is_enabled(ephdata->reg_dvdd)) {
                regulator_disable(ephdata->reg_dvdd);
            }
            regulator_put(ephdata->reg_dvdd);
            ephdata->reg_dvdd = NULL;
        }
    }

    eph_unregister_input_device(ephdata);

#if (ESWIN_EPH861X_SPI && ESWIN_EPH861X_SPI_USE_DMA)
    dma_pool_free(pool_rx, ephdata->comms_receive_buf, ephdata->comms_dma_handle_rx);
    dma_pool_free(pool_tx, ephdata->comms_send_buf, ephdata->comms_dma_handle_tx);

    dma_pool_destroy(pool_rx);
    dma_pool_destroy(pool_tx);
#elif (ESWIN_EPH861X_SPI)
    kfree(ephdata->comms_receive_buf);
    kfree(ephdata->comms_send_buf);
#endif

    kfree(ephdata->comms_send_crc_buf);
    kfree(ephdata->report_buf);
    kfree(ephdata);

    return 0;
}

/* Started by AICoder, pid:r2b8cb12f7b9b0d14dd80b3570499241c9b2e36b */
int eph_read_report_touch_enable_status(struct eph_data *ephdata, u8 *buffer)
{
    int ret_val = 0;
    struct eph_device_control_config_read_command r_cmd;

    // Check if ephdata is null
    if (!ephdata) {
        return -1;
    }

    eswin_log(DEBUG_LOG, "enter");

    // Initialize read command structure
    r_cmd.header.type = TLV_CONFIG_DATA_READ;
    r_cmd.header.length = 0x6;
    r_cmd.command_id = 0x1F4;
    r_cmd.offset = 0x00;
    r_cmd.read_length = 0x01;

    // Lock the mutex for communication
    mutex_lock(&ephdata->comms_mutex);

    // Read control configuration
    ret_val = eph_read_control_config(ephdata, &r_cmd, buffer);

    // Unlock the mutex
    mutex_unlock(&ephdata->comms_mutex);

    // Handle error case
    if (ret_val) {
        eswin_log(ERR_LOG, "fail %d", ret_val);
        goto err;
    }

    // Debug information
    eswin_log(INFO_LOG, "0x%x 0x%x 0x%x 0x%x 0x%x",
              buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);

err:
    eswin_log(DEBUG_LOG, "exit, ret=%d", ret_val);
    return ret_val;
}
/* Ended by AICoder, pid:r2b8cb12f7b9b0d14dd80b3570499241c9b2e36b */

/* Started by AICoder, pid:11deebb428f16e914ccd0ad6f08abf1c698701bc */
int eph_report_touch_enable(struct eph_data *ephdata, int enable)
{
    u8 cfg_enable[] = {TLV_CONFIG_DATA_WRITE, 0x05, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x03};
    u8 cfg_disable[] = {TLV_CONFIG_DATA_WRITE, 0x05, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x02};

    u16 length = sizeof(cfg_enable) / sizeof(cfg_enable[0]);
    int ret_val = 0;

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = (enable) ? eph_write_control_config(ephdata, length, cfg_enable)
                       : eph_write_control_config(ephdata, length, cfg_disable);
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);
    return ret_val;
}
/* Ended by AICoder, pid:11deebb428f16e914ccd0ad6f08abf1c698701bc */

int eph_deep_mode_enable(struct eph_data *ephdata, int enable)
{
    int ret_val = 0;
    u8 cfg[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00};
    u16 length = (sizeof(cfg)/sizeof(cfg[0]));

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    if (enable == 1)
        cfg[7] = 0x02;
    else if (enable == 0)
        cfg[7] = 0x00;

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, length, &cfg[0]);
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);
    return ret_val;
}

/* Started by AICoder, pid:x3112tf41a2c776142e9082d30d6a515d4c8cfdd */
int eph_ufp_report_enable(struct eph_data *ephdata, int enable)
{
    int ret_val = 0;
    u8 cfg_disable[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0B, 0x00, 0x03, 0x00, 0x00};
    u8 cfg_enable[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0B, 0x00, 0x03, 0x00, 0x01};
    u16 length = sizeof(cfg_disable) / sizeof(cfg_disable[0]);

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = (enable) ? eph_write_control_config(ephdata, length, cfg_enable)
                       : eph_write_control_config(ephdata, length, cfg_disable);
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);

    return ret_val;
}
/* Ended by AICoder, pid:x3112tf41a2c776142e9082d30d6a515d4c8cfdd */

int eph_charger_status_enable(struct eph_data *ephdata, int enable)
{
    int ret_val = 0;
    u8 cfg_plug[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x02, 0x00, 0x01};
    u8 cfg_unplug[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x02, 0x00, 0x00};
    u16 length = sizeof(cfg_plug) / sizeof(cfg_plug[0]);

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = (enable) ? eph_write_control_config(ephdata, length, cfg_plug)
                       : eph_write_control_config(ephdata, length, cfg_unplug);
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);

    return ret_val;
}

int eph_headset_status_enable(struct eph_data *ephdata, int enable)
{
    int ret_val = 0;
    u8 cfg_plug[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x01};
    u8 cfg_unplug[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x00};
    u16 length = sizeof(cfg_plug) / sizeof(cfg_plug[0]);

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = (enable) ? eph_write_control_config(ephdata, length, cfg_plug)
                       : eph_write_control_config(ephdata, length, cfg_unplug);
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);

    return ret_val;
}

int eph_sensibility_status_enable(struct eph_data *ephdata, int enable)
{
    int ret_val = 0;
    u8 cfg_normal[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x07, 0x00, 0x00};
    u8 cfg_higher[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x07, 0x00, 0x01};
    u8 cfg_highest[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x07, 0x00, 0x02};
    u16 length = sizeof(cfg_normal) / sizeof(cfg_normal[0]);

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    switch (enable) {
        case normal_sensibility:
            ret_val = eph_write_control_config(ephdata, length, cfg_normal);
        break;
        case higher_sensibility:
            ret_val = eph_write_control_config(ephdata, length, cfg_higher);
        break;
        case highest_sensibility:
            ret_val = eph_write_control_config(ephdata, length, cfg_highest);
        break;
        default:
            eswin_log(ERR_LOG, "Unsupport tp sensibility level (%d)", enable);
        break;
    }
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);

    return ret_val;
}

int eph_mRotatin_status_enable(struct eph_data *ephdata, int enable)
{
    int ret_val = 0;
    u8 cfg_mRotatin_0[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x00};
    u8 cfg_mRotatin_90[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x01};
    u8 cfg_mRotatin_180[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x02};
    u8 cfg_mRotatin_270[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x03};
    u16 length = sizeof(cfg_mRotatin_0) / sizeof(cfg_mRotatin_0[0]);

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    switch (enable) {
        case mRotatin_0:
            ret_val = eph_write_control_config(ephdata, length, cfg_mRotatin_0);
        break;
        case mRotatin_90:
            ret_val = eph_write_control_config(ephdata, length, cfg_mRotatin_90);
        break;
        case mRotatin_180:
            ret_val = eph_write_control_config(ephdata, length, cfg_mRotatin_180);
        break;
        case mRotatin_270:
            ret_val = eph_write_control_config(ephdata, length, cfg_mRotatin_270);
        break;
        default:
            eswin_log(ERR_LOG, "Unsupport tp mRotatin (%d)", enable);
        break;
    }
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);

    return ret_val;
}

/* Started by AICoder, pid:u63a51fc34p4ab7148810abc80faef16d569e0f3 */
int eph_fod_status_enable(struct eph_data *ephdata, int enable)
{
    u8 fod_enable[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x08};
    u8 fod_disable[] = {TLV_CONTROL_DATA_WRITE, 0x05, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00};
    u16 length = sizeof(fod_enable) / sizeof(fod_enable[0]);
    int ret_val = 0;

    eswin_log(DEBUG_LOG, "enter, enable=%d", enable);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = enable ? eph_write_control_config(ephdata, length, fod_enable)
                     : eph_write_control_config(ephdata, length, fod_disable);
    mutex_unlock(&ephdata->comms_mutex);

    eswin_log(INFO_LOG, "enable=%d, eph_write_control_config ret_val=%d", enable, ret_val);
    eswin_log(DEBUG_LOG, "exit, ret_val=%d", ret_val);

    return ret_val;
}
/* Ended by AICoder, pid:u63a51fc34p4ab7148810abc80faef16d569e0f3 */

int eph_dev_enter_lp_mode(struct eph_data *ephdata)
{
    int ret_val = 0;
    eswin_log(DEBUG_LOG, "enter");
    if (ephdata->lp)
        return 0;

    if (!ephdata->lp) {
        if (!ephdata->irq_wake) {
            enable_irq_wake(ephdata->chg_irq);
            ephdata->irq_wake = true;
        }
        if (ephdata->gesture_wakeup_enable)
            ret_val = eph_gesture_mode_set(ephdata, ephdata->gesture_mode | BIT(0));

        // tic deep mode
        if ((ephdata->irq_wake == false) && (!ephdata->gesture_wakeup_enable))
            ret_val = eph_deep_mode_enable(ephdata, 1);

        ephdata->lp = true;
    }

    if (ret_val)
        eswin_log(ERR_LOG, "Failed to enter lp mode (%d)", ret_val);

    eswin_log(DEBUG_LOG, "exit");
    return ret_val;
}

int eph_dev_enter_normal_mode(struct eph_data *ephdata)
{
    int ret_val = 0;
    eswin_log(DEBUG_LOG, "enter");
    if (!ephdata->lp)
        return 0;

    if (ephdata->lp) {
        if ((ephdata->irq_wake == false) && (!ephdata->gesture_wakeup_enable))
            ret_val = eph_deep_mode_enable(ephdata, 0);

        if (ephdata->irq_wake) {
            disable_irq_wake(ephdata->chg_irq);
            ephdata->irq_wake = false;
        }

        if (ephdata->gesture_wakeup_enable)
            ret_val = eph_gesture_mode_set(ephdata, ephdata->gesture_mode & (~BIT(0)));

        ephdata->lp = false;
    }

    if (ret_val)
        eswin_log(ERR_LOG, "Failed to enter normal mode (%d)", ret_val);

    eswin_log(DEBUG_LOG, "exit");
    return ret_val;
}

int eph_suspend(struct device *dev)
{
    struct comms_device *commsdevice = eph_comms_device_get(dev);
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

    eswin_log(DEBUG_LOG, "enter");

    if (!ephdata->inputdev)
    {
        return 0;
    }

    if (ephdata->suspended == true)
        return 0;

#ifdef ESWIN_ESD_CHECK
    heartbeat_work_stop(ephdata);
#endif

    cancel_work_sync(&ephdata->force_baseline_work);

    mutex_lock(&ephdata->inputdev->mutex);

    if (ephdata->inputdev->users)
    {
        (void)eph_stop(ephdata);
    }

    mutex_unlock(&ephdata->inputdev->mutex);

#ifdef ESWIN_USE_PINCTRL
    eph_pinctrl_configure(ephdata, false);
#endif

    eswin_log(DEBUG_LOG, "exit");
    return 0;
}

int eph_resume(struct device *dev)
{
    struct comms_device *commsdevice = eph_comms_device_get(dev);
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

    eswin_log(DEBUG_LOG, "enter");

    if (!ephdata->inputdev)
    {
        return 0;
    }

    if (ephdata->suspended == false)
        return 0;

#ifdef ESWIN_USE_PINCTRL
    eph_pinctrl_configure(ephdata, true);
#endif

    mutex_lock(&ephdata->inputdev->mutex);

    if (ephdata->inputdev->users)
    {
        (void)eph_start(ephdata);
    }

    mutex_unlock(&ephdata->inputdev->mutex);

    /* TIC report gesture event need 150 ~ 170ms delay 200ms for irq
     * process gesture event
    */
    mdelay(200);

    {
        eph_clear_all_host_touch_slots(ephdata);
    }

#ifdef ESWIN_ESD_CHECK
    heartbeat_work_start(ephdata);
#endif

    eswin_log(DEBUG_LOG, "exit");
    return 0;
}

#ifdef CONFIG_PM
static int eph_pm_suspend(struct device *dev)
{
    struct comms_device *commsdevice = eph_comms_device_get(dev);
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

	eswin_log(INFO_LOG, "enter!");
	ephdata->zte_ctrl.pm_suspend = true;
	reinit_completion(&ephdata->zte_ctrl.pm_completion);
	return 0;
}

static int eph_pm_resume(struct device *dev)
{
    struct comms_device *commsdevice = eph_comms_device_get(dev);
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

	eswin_log(INFO_LOG, "enter!");
	ephdata->zte_ctrl.pm_suspend = false;
	complete(&ephdata->zte_ctrl.pm_completion);
	return 0;
}

static const struct dev_pm_ops eph_pm_ops = {
	.suspend = eph_pm_suspend,
	.resume = eph_pm_resume,
};

//static SIMPLE_DEV_PM_OPS(eph_pm_ops, eph_pm_suspend, eph_pm_resume);
#endif

#ifdef CONFIG_OF // Open Firmware (Device Tree)
static const struct of_device_id eph_of_match[] =
{
    { .compatible = "eswin,eph861x", },
    {},
};
MODULE_DEVICE_TABLE(of, eph_of_match);
#endif // CONFIG_OF

static const struct comms_device_id eph_id[] =
{
    { "eswin_eph861x", 0 },
    { }
};
MODULE_DEVICE_TABLE(comms_mode_type, eph_id);

static struct comms_driver eph_driver =
{
    .id_table   = eph_id,
    .probe      = eph_probe,
    .remove     = eph_remove,
    .driver = {
        .name   = "eswin_eph861x",
        .owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(eph_of_match),
#ifdef CONFIG_PM
        .pm = &eph_pm_ops,
#endif
    },

};

int eswin_touch_init(void)
{
    int ret = 0;
    eswin_log(INFO_LOG, "driver_time:2025-3-21");

#ifndef CONFIG_NOT_USE_LCD_INFO
#ifdef CONFIG_TP_GET_LCD_ID
	ret = get_lcd_panel_id();
	if (ret == 0) {
        if (strnstr(lcd_id, "ID23", strlen(lcd_id))) {
            eswin_log(ERR_LOG, "lcd_id is not match");
            return -EPERM;
        } else {
            eswin_log(INFO_LOG, "lcd_id is match");
        }
	} else {
        eswin_log(ERR_LOG, "lcd_id not find");
        return -EPERM;
    }
#else
	ret = get_tp_chip_id();
	if (ret == 0) {
        if ((tpd_cdev->tp_chip_id != TS_CHIP_MAX) && (tpd_cdev->tp_chip_id != TS_CHIP_ESWIN)) {
            eswin_log(WARN_LOG, "this tp is not used, return");
            return -EPERM;
        }
    }
#endif
#endif

    if (tpd_cdev->TP_have_registered) {
        eswin_log(ERR_LOG, "TP have registered by other TP");
        return -EPERM;
    }

#if (ESWIN_EPH861X_SPI)
    ret = spi_register_driver(&eph_driver);
    if (ret != 0) {
        eswin_log(ERR_LOG, "eswin spi_register_driver failed, ret=%d", ret);
    }
#endif

#if (ESWIN_EPH861X_I2C)
    ret = i2c_add_driver(&eph_driver);
    if (ret != 0) {
        eswin_log(ERR_LOG, "eswin i2c_add_driver failed, ret=%d", ret);
    }
#endif

    return ret;
}

void eswin_touch_exit(void)
{
#if (ESWIN_EPH861X_SPI)
    spi_unregister_driver(&eph_driver);
#endif

#if (ESWIN_EPH861X_I2C)
    i2c_del_driver(&eph_driver);
#endif
}

//module_comms_driver(eph_driver);

/* Module information */
MODULE_AUTHOR("chris.ollerenshaw@eswin.com>");
MODULE_DESCRIPTION("ESWIN EPH861 series Touchscreen driver");
MODULE_LICENSE("GPL");
/* Ended by AICoder, pid:s4b3ace8dewbce8147e128ddf02b083d4bd80d8b */