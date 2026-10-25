#ifndef _NOVATEK_CONFIG_H_
#define _NOVATEK_CONFIG_H_

#define NVT_DEBUG 1

//---GPIO number---
#define NVTTOUCH_RST_PIN 62
#define NVTTOUCH_INT_PIN 368

//---INT trigger mode---
//#define IRQ_TYPE_EDGE_RISING 1
//#define IRQ_TYPE_EDGE_FALLING 2
#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING

//---bus transfer length---
#define BUS_TRANSFER_LENGTH  256

//---Touch info.---
#define TOUCH_MAX_WIDTH 1319
#define TOUCH_MAX_HEIGHT 2111
#define PEN_MAX_WIDTH 2640
#define PEN_MAX_HEIGHT 4224
#define TOUCH_MAX_FINGER_NUM 10
#define TOUCH_KEY_NUM 0

#define TOUCH_FORCE_NUM 1000

//---for Pen---
#define PEN_PRESSURE_MAX (4095)
#define PEN_DISTANCE_MAX (1)
#define PEN_TILT_MIN (-60)
#define PEN_TILT_MAX (60)

/* Enable only when module have tp reset pin and connected to host */
#define NVT_TOUCH_SUPPORT_HW_RST 0

//---Customerized func.---
#define NVT_TOUCH_PROC 1
#define NVT_TOUCH_EXT_PROC 1
#define NVT_TOUCH_MP 1
#define NVT_SAVE_TEST_DATA_IN_FILE 0
#define MT_PROTOCOL_B 1
#define WAKEUP_GESTURE 1

#define BOOT_UPDATE_FIRMWARE 1
#define POINT_DATA_CHECKSUM 1
#define POINT_DATA_CHECKSUM_LEN 65
#define NVT_PM_WAIT_BUS_RESUME_COMPLETE 1

//---ESD Protect.---
#define NVT_TOUCH_ESD_PROTECT 0
#define NVT_TOUCH_ESD_CHECK_PERIOD 1500	/* ms */
#define NVT_TOUCH_WDT_RECOVERY 1

#define CHECK_PEN_DATA_CHECKSUM 0

#define NVT_REPORT_BY_ZTE_ALGO

#define SPI_NUM     1

#define NVT_SUPPORT_PEN_HOUVER_UEVENT_REPORT
//#define NVT_GET_HALL_STATE

#define NOVATEK_VENDOR_ID_0     0x0
#define NOVATEK_VENDOR_ID_1     0x1
#define NOVATEK_VENDOR_ID_2     0x2
#define NOVATEK_VENDOR_ID_3     0x3

#define NOVATEK_VENDOR_NAME_0   "huaxin"
#define NOVATEK_VENDOR_NAME_1   "unknown"
#define NOVATEK_VENDOR_NAME_2   "unknown"
#define NOVATEK_VENDOR_NAME_3   "unknown"

#endif