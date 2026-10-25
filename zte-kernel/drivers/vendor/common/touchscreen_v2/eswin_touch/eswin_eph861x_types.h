/* Started by AICoder, pid:w0825r6552ybda0144f909f832f0205cc4c0441a */
#ifndef __LINUX_PLATFORM_DATA_ESWIN_EPH_TYPES_H
#define __LINUX_PLATFORM_DATA_ESWIN_EPH_TYPES_H

#include <linux/types.h>

#include <uapi/asm-generic/errno-base.h>
#include <linux/sysfs.h>

#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <uapi/linux/input-event-codes.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>

#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <generated/autoconf.h>

#if defined(CONFIG_BOARD_FLORAL) // pixel4XL plat
#include <linux/msm_drm_notify.h>
#elif defined(CONFIG_BOARD_CLOUDRIPPER) // pixel7Pro plat
#include "exynos_drm_connector.h"
#include "panel-samsung-drv.h"
#endif

#include <linux/backlight.h>
#include <linux/workqueue.h>

#include "eswin_config.h"

#if (ESWIN_EPH861X_SPI_USE_DMA)
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#endif

//TODO Make this a Kconfig option or read from device tree

#if (ESWIN_EPH861X_SPI)
#include <linux/spi/spi.h>
#endif

#if (ESWIN_EPH861X_I2C)
#include <linux/i2c.h>
#endif

/* Delay times */
#define EPH_RESET_TIME       200  /* msec */
#define EPH_FW_RESET_TIME    3000 /* msec */
#define EPH_WAKEUP_TIME      25   /* msec */
#define EPH_REGULATOR_DELAY  150  /* msec */
#define EPH_POWERON_DELAY    100  /* msec */
#define EPH_RESET_HOLD_TIME  2  /* msec */

#if ESWIN_EPH861X_SPI
/* Alias communication adapter to relevant adaptor */
#define comms_device          spi_device
#define EPH_COMMS_BUS_TYPE    BUS_SPI
#define comms_device_id       spi_device_id
#define comms_mode_type       spi
#define comms_driver          spi_driver

#define module_comms_driver   module_spi_driver
#endif
#if ESWIN_EPH861X_I2C
/* Alias communication adapter to relevant adaptor */
#define comms_device          i2c_client
#define EPH_COMMS_BUS_TYPE    BUS_I2C
#define comms_device_id       i2c_device_id
#define comms_mode_type       i2c
#define comms_driver          i2c_driver

#define module_comms_driver   module_i2c_driver
#endif

struct tlv_header {
    u8 type;
    u16 length;
} __packed;

struct eph_device_control_config_write_response
{
    struct tlv_header header;
    u16 bytes_written;
} __packed;

struct eph_device_eng_data_write_command
{
    struct tlv_header header;
    u16 component_id;
    u8 data_id;
    u8 crc;
} __packed;

struct eph_device_eng_data_write_response
{
    struct tlv_header header;
    u16 bytes_written;
} __packed;

struct eph_device_control_config_read_command
{
    struct tlv_header header;
    u16 command_id;
    u16 offset;
    u16 read_length;
} __packed;

enum eph_suspend_mode
{
    EPH_SUSPEND_DEEP_SLEEP = 0,
    EPH_SUSPEND_REGULATOR  = 2,
};

/* Platform Data (e.g. populated from Device Tree) */
struct eph_platform_data
{
    enum eph_suspend_mode suspend_mode;
    unsigned long gpio_reset;
    unsigned long gpio_irq;
    bool use_gpio_power;
    unsigned long gpio_power;
    bool use_regulator_dvdd;
    const char *regulator_dvdd;
    unsigned int dvdd_voltage;
    bool use_regulator_avdd;
    const char *regulator_avdd;
    unsigned int avdd_voltage;
    const char *device_settings_name;
    const char *fw_name;
    const char *input_name;
    /* panel info */
    unsigned int panel_invert_x;
    unsigned int panel_invert_y;
    unsigned int panel_max_x;
    unsigned int panel_max_y;
};

struct eph_device_info
{
    struct tlv_header device_header;
    u8 product_id;
    u8 variant_id;
    u8 application_version_major;
    u16 application_version_minor;
    u16 bootloader_version;
    u8 protocol_version;
    u8 crc;
};

/* Config update context */
struct eph_device_settings
{
    u8 *raw;
    size_t raw_size;
    off_t raw_pos;
};

/* Firmware update context */
struct eph_flash
{
    struct eph_data *ephdata;
    const struct firmware *fw;
    u8 *pframe;
    loff_t fw_pos;
    size_t frame_size;
    unsigned int frame_count;
};

/*zte_add*/
struct zte_common_interface_member {
	unsigned int rawdata_cols;
	unsigned int rawdata_rows;
	unsigned int fw_ver;
    unsigned int settings_ver;
	bool fw_ready;
	int sensibility_level;
	int display_rotation;
	int is_one_key;
	int is_single_tap;
	int is_single_aod;
	int is_single_fp;
	int is_set_onekey_in_suspend;
	bool headset_state;
	bool charger_state;
#ifdef CONFIG_PM
	struct completion pm_completion;
	bool pm_suspend;
#endif
};

/* Each client has this additional data */
struct eph_data
{
    struct mutex comms_mutex;
    struct mutex sysfs_report_buffer_lock;
    struct comms_device *commsdevice;
    struct input_dev *inputdev;
    char phys[64];      /* device physical location */
    struct eph_platform_data *ephplatform;
    struct eph_device_info ephdeviceinfo;
    unsigned int chg_irq;
    bool in_bootloader;
#ifdef ESWIN_USE_PINCTRL
    struct pinctrl *pinctrl;
#endif
    /*zte_add*/
    struct zte_common_interface_member zte_ctrl;

#if ESWIN_EPH861X_I2C
    u8 bootloader_addr;
#endif
    u8 *report_buf;
    u8 *comms_send_buf;
    u8 *comms_receive_buf;
    u8 *comms_send_crc_buf;
    dma_addr_t comms_dma_handle_tx;
    dma_addr_t comms_dma_handle_rx;

    struct regulator *reg_dvdd;
    struct regulator *reg_avdd;
    char *fw_name;
    char *device_settings_name;
    struct eph_flash *ephflash;

    /* for reset handling */
    struct completion reset_completion;

    /* for power up handling */
    struct completion chg_completion;

    /* Indicates whether device is in suspend */
    bool suspended;

    bool gesture_wakeup_enable;
    u8 gesture_mode;

    /* low power mode gesture */
    u8 lp;
    bool irq_wake;

    struct backlight_device *bl;
    unsigned int last_brightness;

    struct work_struct force_baseline_work;
#ifdef ESWIN_ESD_CHECK
    struct delayed_work heartbeat_work;
    atomic_t heartbeat_on;
#endif
    /* Indicates whether device is updating its device settings */
    bool updating_device_settings;

    /* PTS */
    int16_t pt_threshold_add;
    int16_t pt_flatness_max;
    int16_t pt_max_delta;
    int16_t pt_max_diff_delta;

#if defined(CONFIG_BOARD_FLORAL) // pixel4XL plat
    struct notifier_block notifier;
#elif defined(CONFIG_BOARD_CLOUDRIPPER) // pixel7Pro plat
    bool is_panel_lp_mode;
    struct drm_connector *connector;
    struct drm_bridge panel_bridge;
#endif
};

#endif /* __LINUX_PLATFORM_DATA_ESWIN_EPH_TYPES_ */
/* Ended by AICoder, pid:w0825r6552ybda0144f909f832f0205cc4c0441a */