/************************************************************************/
/* Copyright <2019-2020> GOODIX                                        */
/*                                                                      */
/* GOODIX Confidential. This software is owned or controlled by GOODIX  */
/* and may only be used strictly in accordance with the applicable      */
/* license terms.  By expressly accepting such terms or by downloading, */
/* installing, activating and/or otherwise using the software, you are  */
/* agreeing that you have read, and that you agree to comply with and   */
/* are bound by, such license terms.                                    */
/* If you do not agree to be bound by the applicable license terms,     */
/* then you may not retain, install, activate or otherwise use the      */
/* software.                                                            */
/*                                                                      */
/************************************************************************/

#ifndef _GOODIX_THP_H_
#define _GOODIX_THP_H_

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#endif
#if IS_ENABLED(CONFIG_DRM_MEDIATEK)
#include "mtk_disp_notify.h"
#elif IS_ENABLED(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include "../ztp_common.h"

/* macros definition */
#define GOODIX_THP_DRIVER_VERSION                       "1.1.1.7"
#define GOODIX_THP_DRIVER_NAME				"goodix_thp,gt9916"
#define GOODIX_CORE_DRIVER_NAME				"goodix_thp"
#define GOODIX_THP_STYLUS_INPUT_DEVICE_NAME	        "goodix_stylus_input"
#define GOODIX_THP_INPUT_DEVICE_NAME			"gdix_input_agent"

#define GOODIX_USB_DETECT_GLOBAL

#define GTP_PATCH_COMERR_PM                 1
#define GTP_TIMEOUT_COMERR_PM               700

/*chip_type*/
#define CHIP_TYPE_9897                                  1
#define CHIP_TYPE_9916                                  2
#define CHIP_TYPE_9966                                  3
#define CHIP_TYPE_9615                                  4

#define GOODIX_THP_MAX_FRAME_LEN			(10 * 1024)
#define GOODIX_THP_MAX_TRANS_DATA_LEN			(4096 * 32)
#define GOODIX_THP_MAX_FRAME_BUF_COUNT			20
#define GOODIX_THP_CUSTOM_INFO_LEN                      10
#define GOODIX_MAX_STR_LABLE_LEN                        32
#define GOODIX_THP_REQUEST_APP_SIZE                     12

#define GOODIX_THP_DEFATULT_WAIT_FRAME_TIME		2500

/* cmd definition */
//TODO:to confirm
#define CMD_SLEEP                    			0x84
#define CMD_GESTURE                  			0xA6
#define CMD_EXIT_GESTURE             			0xA7
#define CMD_RAWDATA                  			0x90
#define CMD_TOUCH_REPORT             			0x91
#define CMD_ACTIVE_SCAN_RATE         			0x9D

/* 9897 reg definition */
#define REG_INT_REPORT_TYPE_FLAG_9897                   0x101A0
#define REG_GESTURE_DATA_9897                           0x101A0
#define REG_GESTURE_BUFFER_DATA_9897                    0x101CA

/* 9916 reg definition */
//TODO:to confirm
#define REG_INT_REPORT_TYPE_FLAG_9916                   0x10308
#define REG_GESTURE_DATA_9916                           0x10308
#define REG_GESTURE_BUFFER_DATA_9916                    0x1029E

/*others*/
#define WAIT_STATE					0
#define WAKEUP_STATE					1
#define GET_FRAME_BLOCK_MODE			        1
#define GET_FRAME_NONBLOCK_MODE				0
#define IRQ_ENABLE_FLAG					1
#define IRQ_DISABLE_FLAG				0
#define GESTURE_DOUBLE_CLICK				0
#define GESTURE_SINGLE_CLICK				1

#define RAWDATA_DISABLE					0
#define RAWDATA_ENABLE					1
#define TOUCH_DATA_DISABLE				0
#define TOUCH_DATA_ENABLE				1
#define SLEEP_MODE					0
#define GOODIX_BE_MODE  				0
#define GOODIX_LE_MODE  				1

//TODO:to update
#define SCAN_RATE_240					1
#define SCAN_RATE_180					2
#define SCAN_RATE_120					3
#define SCAN_RATE_60					4

#define GESTURE_DATA_TYPE				0x20

#define INPUT_AGENT_MAX_FINGERS			        10
#define INPUT_AGENT_MAX_STYLUS			        1
#define INPUT_AGENT_MAX_POINTS  ((INPUT_AGENT_MAX_FINGERS) + (INPUT_AGENT_MAX_STYLUS))
#define STYLUS_TRACK_ID                                 10
#define GOODIX_THP_MAX_TIMEOUT				5000u
#define GOODIX_SPI_SPEED_WAKEUP				3500
#define GOODIX_PEN_MAX_TILT				90

#define GESTURE_DATA_HEAD_LEN				8
#define GESTURE_TYPE_LEN				32
#define GESTURE_KEY_DATA_LEN				42
#define GESTURE_BUFFER_DATA_LEN				514

#define GOODIX_THP_TYPE_B_PROTOCOL
#define EQUAL_ZERO(r)               			((r) == 0)

#define SCREEN_OFF                                      0
#define SCREEN_ON                                       1

/* ioctl cmd for afehal */
#define IO_TYPE	 (0xB8)
#define IOCTL_CMD_GET_FRAME \
                _IOWR(IO_TYPE, 0x01, struct thp_ioctl_frame)
#define IOCTL_CMD_SET_RESET_VALUE			_IOW(IO_TYPE, 0x02, u32)
#define IOCTL_CMD_SET_WAIT_TIME				_IOW(IO_TYPE, 0x03, u32)
#define IOCTL_CMD_SPI_TRANS \
                _IOWR(IO_TYPE, 0x04, struct thp_ioctl_spi_trans_data)
#define IOCTL_CMD_NOTIFY_UPDATE \
                _IOW(IO_TYPE, 0x05, struct thp_ioctl_update_info)
#define IOCTL_CMD_SET_WAIT_MODE				_IOW(IO_TYPE, 0x06, u32)

#define IOCTL_CMD_IRQ_ENABLE				_IOW(IO_TYPE, 0x07, u32)
#define IOCTL_CMD_GET_FRAME_BUF_NUM			_IOW(IO_TYPE, 0x08, u32)
#define IOCTL_CMD_RESET_FRAME_LIST			_IOW(IO_TYPE, 0x09, u32)
#define IOCTL_CMD_GET_DRIVER_STATE			_IOR(IO_TYPE, 0x0A, u32)
#define IOCTL_CMD_GET_STATE_CHANGE_FLAG		        _IOR(IO_TYPE, 0x0B, u32)
#define IOCTL_CMD_SET_STATE_CHANGE_FLAG		        _IOW(IO_TYPE, 0x0C, u32)
#define IOCTL_CMD_SET_SPI_SPEED				_IOW(IO_TYPE, 0x0D, u32)
#define IOCTL_CMD_MUILT_SPI_TRANS \
                _IOWR(IO_TYPE, 0x0E, struct thp_ioctl_multi_spi_trans_data)
#define IOCTL_CMD_ENTER_SUSPEND				_IOW(IO_TYPE, 0x0F, u32)
#define IOCTL_CMD_ENTER_RESUME				_IO(IO_TYPE, 0x10)
#define IOCTL_CMD_RECV_TSC_MSG \
                _IOW(IO_TYPE, 0x11, struct thp_ioctl_tsc_msg)
#define IOCTL_CMD_GET_CHIP_TYPE                         _IOR(IO_TYPE, 0x12, u32)
#define IOCTL_CMD_SET_TOOL_OPS                          _IOW(IO_TYPE, 0x14, u32)

/* ioctl cmd for daemon */
#define INPUT_AGENT_IO_TYPE  (0xB9)
#define INPUT_AGENT_IOCTL_CMD_SET_COOR \
        _IOWR(INPUT_AGENT_IO_TYPE, 0x01, \
                struct thp_input_agent_ioctl_coor_data)
#define INPUT_AGENT_IOCTL_READ_STATUS \
        _IOR(INPUT_AGENT_IO_TYPE, 0x02, u32)
#define INPUT_AGENT_IOCTL_CMD_SET_EVENTS \
        _IOR(INPUT_AGENT_IO_TYPE, 0x03, u32)
#define INPUT_AGENT_IOCTL_CMD_GET_EVENTS \
        _IOR(INPUT_AGENT_IO_TYPE, 0x04, u32)
#define INPUT_AGENT_IOCTL_GET_CUSTOM_INFO \
        _IOR(INPUT_AGENT_IO_TYPE, 0x05, u32)
#define INPUT_AGENT_IOCTL_GET_DRIVER_STATE \
        _IOR(INPUT_AGENT_IO_TYPE, 0x06, u32)

typedef enum {
        REQUEST_TYPE_FRAME = 1,
        REQUEST_TYPE_CMD,
        REQUEST_TYPE_NOTIFY,
        REQUEST_TYPE_SEND_CFG,
        REQUEST_TYPE_GET_CFG,
        REQUEST_TYPE_GET_DATA
} REQUEST_TYPE_T;

typedef enum {
        NOTIFY_TYPE_SCREEN = 1,
        NOTIFY_TYPE_CHARGE,
        NOTIFY_TYPE_GESTURE,
        NOTIFY_TYPE_TSD_CTRL,
        NOTIFY_TYPE_DUMP_REP,
        NOTIFY_TYPE_STYLUS_CTRL,
        NOTIFY_TYPE_RAWDATA,
        NOTIFY_TYPE_LOGTOFILE,
} NOTIFY_TYPE_T;

struct zte_ctl {
	int is_single_tap;
	int is_single_aod;
	int is_single_fp;
	int is_single_game;
	int is_set_single_in_suspend;
	int is_wakeup_gesture;
	int is_set_wakeup_in_suspend;
	int is_sensibility;
#ifdef FOR_ZTE_CELL
	int is_smart_cover;
#endif
	int is_wake_irq;
	int is_one_key;
	int is_set_onekey_in_suspend;
	int is_play_game;
	int is_palm_mode;
	int is_fake_sleep;
	int is_fake_sleep_in_suspend;
	int is_screen_off_awake;
	int is_screen_off_awake_in_suspend;
	int tp_report_rate;
	int sensibility_level;
	int follow_hand_level;
	int stability_level;
	int display_rotation;
	int rotation_limit_level;
	int level;
	int finger_lock_flag;
};

#define ZTE_GOODIX_SIN_TAP	0x01
#define ZTE_GOODIX_DOU_TAP	0x02
#define ZTE_GOODIX_FP		0x04
#define ZTE_GOODIX_ONE_KEY	0x08

#pragma pack(push, 1)
struct driver_response_app_pkg {
        uint32_t id;
        uint32_t type;
        uint32_t status;
        uint8_t data[0];
};

struct driver_response_pkg {
        uint32_t size;
        struct driver_response_app_pkg response;
};

struct driver_request_app_pkg {
        uint32_t id;
        uint32_t type;
        uint8_t data[0];
};

struct driver_request_pkg {
        uint32_t size;
        struct driver_request_app_pkg request;
};
#pragma pack(pop)

struct thp_ioctl_frame {
        uint32_t pos;
        uint32_t size;
        uint64_t tv_us; /* tiemstamp us */
};

struct thp_ioctl_spi_trans_data {
        char __user *tx;
        char __user *rx;
        unsigned int size;
};

struct thp_ioctl_spi_xfer_data {
    char __user *tx;
    char __user *rx;
    unsigned int len;
    unsigned short delay_usecs;
    unsigned char cs_change;
    unsigned char reserved[3];
};

struct thp_ioctl_multi_spi_trans_data {
    unsigned int speed_hz;
    unsigned int xfer_num;
    unsigned int reserved[2];
    struct thp_ioctl_spi_xfer_data __user * xfer_data;
};

enum {
        SVC_CMD_MMAP_DEQUEUE = 31,
        SVC_CMD_BLE_MAC,
        SVC_CMD_GAME_FILTER,
        SVC_CMD_UPDATE_VERSION,
};

struct thp_ioctl_tsc_msg {
        u32 cmd;
        u16 len;
        u8 value[64];
};

struct thp_ioctl_update_info {
        u32 frame_addr;
        u32 cmd_addr;
        u32 ges_addr;
};

/* struct definition*/
struct thp_spi_setting {
        u32 spi_max_speed;
        u16 spi_mode;
        u8 bits_per_word;
};

struct goodix_thp_board_data {

        char avdd_name[GOODIX_MAX_STR_LABLE_LEN];
        char iovdd_name[GOODIX_MAX_STR_LABLE_LEN];
        unsigned int iovdd_enable_gpio;
        unsigned int reset_gpio;
        unsigned int irq_gpio;
        int irq;
        unsigned int irq_flags;

        unsigned int avdd_voltage;
        unsigned int power_on_delay_us;
        unsigned int power_off_delay_us;
        unsigned int panel_max_x;
        unsigned int panel_max_y;
        unsigned int panel_max_w; /*major and minor*/
        unsigned int panel_max_p; /*pressure*/
        unsigned int chip_type;
        unsigned int frame_addr;
        unsigned int cmd_addr;
        unsigned int ges_addr;
        char thp_ver[64];
        struct thp_spi_setting spi_setting;
};

#define MMAP_BUFFER_SIZE (GOODIX_THP_MAX_FRAME_LEN * GOODIX_THP_MAX_FRAME_BUF_COUNT)
struct thp_frame_mmap_list {
        char *buf;
        u32 head;
        u32 tail;
};

#define MAX_SCAN_FREQ_NUM            8
#define MAX_SCAN_RATE_NUM            8
#define MAX_FREQ_NUM_STYLUS          8
#define MAX_STYLUS_SCAN_FREQ_NUM     6

struct thp_ts_device {
        char *name;
        char *tx_buff;
        char *rx_buff;
        struct mutex spi_mutex;
        struct device *dev;
        struct spi_device *spi_dev;
        const struct goodix_thp_hw_ops *hw_ops;
        struct goodix_thp_board_data board_data;
};

struct input_agent_coor_data {
        unsigned char down;
        unsigned char touch_valid; /* 0:invalid !=0:valid */
        int x;
        int y;
        int p;          // pen only
        int tilt_x;     // pen only
        int tilt_y;     // pen only
        int track_id;
        int major;
        int minor;
        unsigned int touch_type;
};

struct timeval64 {
        uint64_t tv_sec;
        uint64_t tv_usec;
};

struct thp_input_agent_ioctl_coor_data {
        struct input_agent_coor_data touch[INPUT_AGENT_MAX_POINTS];
        int touch_num;
        int down_num;
        unsigned char fp_mode;				/* 0:normal mode;1:fp mode*/
        unsigned char hover_stat;			/* 0:normal stat;1:hover stat*/
        unsigned char large_touch_stat;		/* 0:normal touch stat;1:large_touch_stat*/
        struct timeval64 time_stamp;
};

struct goodix_thp_hw_ops {
        int (*read)(struct thp_ts_device *dev, unsigned int addr,
                         unsigned char *data, unsigned int len);
        int (*write)(struct thp_ts_device *dev, unsigned int addr,
                        unsigned char *data, unsigned int len);
        int (*send_cmd)(struct thp_ts_device *tdev, u8 cmd, u16 data);
        int (*board_init)(struct thp_ts_device *ts_dev);
        int (*get_custom_info)(struct thp_ts_device *tdev, char *buf, unsigned int len);
        int (*get_frame)(struct thp_ts_device *dev, char *data);
        int (*get_version)(struct thp_ts_device *dev, u64 *version);
        //int (*set_fp_int_pin)(struct thp_ts_device *dev, u8 level);
};

struct goodix_thp_core {
        struct spi_device *sdev;
        struct thp_ts_device *ts_dev;
        struct platform_device *pdev;
        struct input_dev *input_dev;
        struct input_dev *pen_dev;
        struct regulator *avdd;
        struct regulator *vdd;
        struct thp_frame_mmap_list frame_mmap_list;
        struct mutex frame_mutex;
        struct mutex ts_mutex;
        struct mutex irq_mutex;

#ifdef CONFIG_PINCTRL
        struct pinctrl *pinctrl;
        struct pinctrl_state *pin_sta_active;
        struct pinctrl_state *pin_sta_suspend;
#endif
#if IS_ENABLED(CONFIG_FB) || IS_ENABLED(CONFIG_DRM_MEDIATEK)
        struct notifier_block pm_notif;
#endif
        bool logtofile_on;
        bool irq_state;
        atomic_t suspended;
        u16 gesture_enable;
        u32 state_change_flag;
        int irq;
        int iovdd_enable_gpio;
        int power_on;
        int open_num;
        int get_frame_wait_mode;
        unsigned int frame_len;
        unsigned int frame_wait_time;
        u8 reset_state;
        u8 frame_waitq_state;
        u8 frame_read_data[GOODIX_THP_MAX_FRAME_LEN];
        char custom_info[GOODIX_THP_CUSTOM_INFO_LEN + 1];
        wait_queue_head_t frame_wq;
        u8 gesture_type[GESTURE_TYPE_LEN];
        u8 gesture_data[GESTURE_KEY_DATA_LEN];
        u8 gesture_buffer_data[GESTURE_BUFFER_DATA_LEN];
        
        void *notifier_cookie;
        struct zte_ctl ztec;
#ifdef GOODIX_USB_DETECT_GLOBAL
	struct delayed_work charger_work;
	struct workqueue_struct *charger_wq;
	struct notifier_block charger_notifier;
#endif

#if IS_ENABLED(CONFIG_PM) && GTP_PATCH_COMERR_PM
	struct completion pm_completion;
	bool pm_suspend;
#endif

};

/* func definition & statement*/
/* log macro */
//#define CONFIG_GOODIX_DEBUG
#define ts_info(fmt, arg...)\
        pr_info("[ZTE_LDD_TP][THP-INF][%s:%d] "fmt"\n", __func__, __LINE__, ##arg)

#define ts_err(fmt, arg...)\
        pr_err("[ZTE_LDD_TP][THP-ERR][%s:%d] "fmt"\n", __func__, __LINE__, ##arg)
#ifdef CONFIG_GOODIX_DEBUG
#define ts_debug(fmt, arg...)\
        pr_info("[ZTE_LDD_TP][THP-DBG][%s:%d] "fmt"\n", __func__, __LINE__, ##arg)
#else
#define ts_debug(fmt, arg...)	do {} while (0)
#endif

/*
 * get board data pointer
 */
static inline struct goodix_thp_board_data *board_data(
                struct goodix_thp_core *core)
{
        if (!core || !core->ts_dev)
                return NULL;
        return &(core->ts_dev->board_data);
}

extern struct goodix_thp_core *gdix_thp_core;

void goodix_thp_reset(struct thp_ts_device *ts_dev, int delay_ms);
int goodix_thp_set_spi_speed(struct thp_ts_device *dev, u32 speed);
u16 checksum16_cmp(u8 *data, u32 size, int mode);
u8 checksum_u8(u8 *data, u32 size);
u8 checksum8_u16(const u8 *data, u32 size);

#endif /* _GOODIX_THP_H_ */
