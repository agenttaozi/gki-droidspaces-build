/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __LEDS_AW21024_H__
#define __LEDS_AW21024_H__

#define AW_INFO(fmt, arg...) pr_info("[ZTE_LDD_LED][AW_LED]:(%s, %d):" fmt, __func__, __LINE__, ##arg)
#define AW_ERROR(fmt, arg...) pr_err("[ZTE_LDD_LED][AW_LED]:(%s, %d):" fmt, __func__, __LINE__, ##arg)
/******************************************************
 *
 * Register List
 *
 *****************************************************/
#define AW21024_REG_GCR               0x00
#define AW21024_REG_BR0               0x01
#define AW21024_REG_BR1               0x02
#define AW21024_REG_BR2               0x03
#define AW21024_REG_BR3               0x04
#define AW21024_REG_BR4               0x05
#define AW21024_REG_BR5               0x06
#define AW21024_REG_BR6               0x07
#define AW21024_REG_BR7               0x08
#define AW21024_REG_BR8               0x09
#define AW21024_REG_BR9               0x0A
#define AW21024_REG_BR10              0x0B
#define AW21024_REG_BR11              0x0C
#define AW21024_REG_BR12              0x0D
#define AW21024_REG_BR13              0x0E
#define AW21024_REG_BR14              0x0F
#define AW21024_REG_BR15              0x10
#define AW21024_REG_BR16              0x11
#define AW21024_REG_BR17              0x12
#define AW21024_REG_BR18              0x13
#define AW21024_REG_BR19              0x14
#define AW21024_REG_BR20              0x15
#define AW21024_REG_BR21              0x16
#define AW21024_REG_BR22              0x17
#define AW21024_REG_BR23              0x18
#define AW21024_REG_UPDATE            0x49
#define AW21024_REG_COL0              0x4A
#define AW21024_REG_COL1              0x4B
#define AW21024_REG_COL2              0x4C
#define AW21024_REG_COL3              0x4D
#define AW21024_REG_COL4              0x4E
#define AW21024_REG_COL5              0x4F
#define AW21024_REG_COL6              0x50
#define AW21024_REG_COL7              0x51
#define AW21024_REG_COL8              0x52
#define AW21024_REG_COL9              0x53
#define AW21024_REG_COL10             0x54
#define AW21024_REG_COL11             0x55
#define AW21024_REG_COL12             0x56
#define AW21024_REG_COL13             0x57
#define AW21024_REG_COL14             0x58
#define AW21024_REG_COL15             0x59
#define AW21024_REG_COL16             0x5A
#define AW21024_REG_COL17             0x5B
#define AW21024_REG_COL18             0x5C
#define AW21024_REG_COL19             0x5D
#define AW21024_REG_COL20             0x5E
#define AW21024_REG_COL21             0x5F
#define AW21024_REG_COL22             0x60
#define AW21024_REG_COL23             0x61
#define AW21024_REG_GCCR              0x6E
#define AW21024_REG_PHCR              0x70
#define AW21024_REG_OSDCR             0x71
#define AW21024_REG_OSST0             0x72
#define AW21024_REG_OSST1             0x73
#define AW21024_REG_OSST2             0x74
#define AW21024_REG_OTCR              0x77
#define AW21024_REG_SSCR              0x78
#define AW21024_REG_UVCR              0x79
#define AW21024_REG_GCR2              0x7A
#define AW21024_REG_GCR4              0x7C
#define AW21024_REG_VER               0x7E
#define AW21024_REG_RESET             0x7F
#define AW21024_REG_WBR               0x90
#define AW21024_REG_WBG               0x91
#define AW21024_REG_WBB               0x92
#define AW21024_REG_PATCFG            0xA0
#define AW21024_REG_PATGO             0xA1
#define AW21024_REG_PATT0             0xA2
#define AW21024_REG_PATT1             0xA3
#define AW21024_REG_PATT2             0xA4
#define AW21024_REG_PATT3             0xA5
#define AW21024_REG_FADEH             0xA6
#define AW21024_REG_FADEL             0xA7
#define AW21024_REG_GCOLR             0xA8
#define AW21024_REG_GCOLG             0xA9
#define AW21024_REG_GCOLB             0xAA
#define AW21024_REG_GCFG0             0xAB
#define AW21024_REG_GCFG1             0xAC

#define AW21024_PATT2_SET					(0x00)
#define AW21024_PATT3_SET					(0x00)
#define AW21024_PATCFG_SET					(0x03)
#define AW21024_GCFG0_SET					(0x08)
#define AW21024_GCFG1_SET					(0x10)
#define AW21024_PATGO_SET					(0x01)

/******************************************************
 *
 * Register Write/Read Access
 *
 *****************************************************/
#define REG_NONE_ACCESS            0
#define REG_RD_ACCESS              (1<<0)
#define REG_WR_ACCESS              (1<<1)
#define AW21024_REG_MAX            0xFF

const unsigned char aw21024_reg_access[AW21024_REG_MAX] = {
	[AW21024_REG_GCR] = REG_RD_ACCESS,
	[AW21024_REG_BR0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR3] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR4] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR5] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR6] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR7] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR8] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR9] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR10] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR11] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR12] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR13] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR14] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR15] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR16] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR17] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR18] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR19] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR20] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR21] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR22] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_BR23] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_UPDATE] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL3] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL4] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL5] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL6] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL7] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL8] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL9] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL10] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL11] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL12] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL13] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL14] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL15] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL16] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL17] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL18] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL19] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL20] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL21] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL22] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_COL23] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCCR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PHCR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_OSDCR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_OSST0] = REG_RD_ACCESS,
	[AW21024_REG_OSST1] = REG_RD_ACCESS,
	[AW21024_REG_OSST2] = REG_RD_ACCESS,
	[AW21024_REG_OTCR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_SSCR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_UVCR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCR2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCR4] = REG_NONE_ACCESS,
	[AW21024_REG_VER] = REG_RD_ACCESS,
	[AW21024_REG_RESET] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_WBR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_WBG] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_WBB] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PATCFG] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PATGO] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PATT0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PATT1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PATT2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_PATT3] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_FADEH] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_FADEL] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCOLR] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCOLG] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCOLB] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCFG0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW21024_REG_GCFG1] = REG_RD_ACCESS | REG_WR_ACCESS,
};

/*********************************************************
 *
 * 0-7bite set
 *
 ********************************************************/

/* GCR  0x00 */
#define AW21024_BIT_CHIP_EN_CLOSE_MASK     (~(1<<0))
#define AW21024_BIT_CHIP_EN                (1<<0)
#define AW21024_BIT_CHIP_CLOSE             (0<<0)
#define AW21024_BIT_CLKPRQ_MASK            (~(7<<4))
#define AW21024_BIT_CLKPRQ_16MH            (0<<4)
#define AW21024_BIT_CLKPRQ_8MH             (1<<4)
#define AW21024_BIT_CLKPRQ_1MH             (2<<4)
#define AW21024_BIT_CLKPRQ_512KH           (3<<4)
#define AW21024_BIT_CLKPRQ_256KH           (4<<4)
#define AW21024_BIT_CLKPRQ_125KH           (5<<4)
#define AW21024_BIT_CLKPRQ_62KH            (6<<4)
#define AW21024_BIT_CLKPRQ_31KH            (7<<4)
#define AW21024_BIT_APSE_MASK              (~(1<<7))
#define AW21024_BIT_APSE_ENABLE            (1<<7)
#define AW21024_BIT_APSE_DISABLE           (0<<7)

#define COLOR_PURE_RED				0xff0000
#define COLOR_PURE_GREEN			0x00ff00
#define COLOR_PURE_BLUE				0x0000ff

#define COLOR_PINK				0xaf3719
#define COLOR_GREEN				0x007008
#define COLOR_BLUE				0x082c80
#define COLOR_CYAN				0x19b432
#define COLOR_ORANGE			0xb44b00
#define COLOR_YELLOW			0x875f01
#define COLOR_PURPLE			0x6f0986
#define COLOR_WHITE				0xffffff

/*********************************************************
 *
 * chip info
 *
 ********************************************************/

#define AW21024_CHIP_ID           0x18

/*********************************************************
 *
 * struct
 *
 ********************************************************/
enum aw21024_clk_pwm_mode {
	AW21024_CLKPRQ_16MH = 0,
	AW21024_CLKPRQ_8MH = 1,
	AW21024_CLKPRQ_1MH = 2,
	AW21024_CLKPRQ_512KH = 3,
	AW21024_CLKPRQ_256KH = 4,
	AW21024_CLKPRQ_125KH = 5,
	AW21024_CLKPRQ_62KH = 6,
	AW21024_CLKPRQ_31KH = 7,
};

enum aw21024_work_mode {
	MUSIC_DANCE = 0x50,
	LIGHT_LONG = 0x60,
	BREATH = 0x70,
	FLASH = 0x80,
	ALTER_FLASH = 0xF0,
	FAST_FLASH = 0xE0,
};

enum miniled_charging_state {
	MINI_LED_OFF = 0,
	MINI_LED_ON = 1,
	MINI_LED_RUNNING = 2,
};


enum aw21024_scene {
	ESPORTS = 0x01,
	MUSIC = 0x02,
	MESSAGE = 0x03,
	IN_CALL = 0x04,
	CHARGING = 0x05,
	ALARM = 0x06,
};

typedef enum {
	RED_CDEV=0,
	GREEN_CDEV,
	BLUE_CDEV,
	LED_CDEV,
	LED_DEV_MAX_NUM,
} cdev_type_t;

struct pat_time_t {
	uint16_t t0;
	uint16_t t1;
	uint16_t t2;
	uint16_t t3;
};

struct pat_time {
	uint32_t ms;
	uint32_t reg_val;
};

enum {
	WORK_STOP,
	WORK_RUNNING,
};

/* Started by AICoder, pid:n21e72cad2m26f2142160a888012fd009c18a864 */
enum {
    ALTERNATE_FLASHING = 0x1,
    RUNNING_LED_LEFTTORIGHT,
    RUNNING_LED_7COLOR,
    RUNNING_LED,
    BOOT_RUNNING_LED,
    LED1_3_CHARGING,
};
/* Ended by AICoder, pid:n21e72cad2m26f2142160a888012fd009c18a864 */
struct aw21024 {
	struct i2c_client *i2c;
	struct device *dev;
	struct led_classdev cdev;
	struct led_classdev rgbcdev[LED_DEV_MAX_NUM];
	struct work_struct brightness_work;
	struct work_struct rgbbrightness_work;
	struct work_struct running_led_work;
	struct work_struct fast_flashing_led_work;
	struct work_struct alternate_flashing_led_work;
	struct work_struct boot_running_led_work;
	struct work_struct boot_running_miniled_work;
	struct work_struct miniled_charging_work;
	struct pat_time_t pat_time_cfg;
	int color_cdev;
	int step;
	unsigned int clk_pwm;
	bool apse_mode;
	bool breath_mode;
	bool mini_led;
	bool mini_led_test;
	unsigned int dts_led_current;
	unsigned int conversion_led_current;
	unsigned int effect;
	unsigned int rgbcolor;
	unsigned int effect_mode;
	unsigned int work_mode;
	unsigned int scene;
	uint32_t effect_color;
	int reset_gpio;
	uint8_t effect_brightness;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t flash_times;
	uint8_t running_led_state;
	uint8_t boot_running_led_state;
	uint8_t boot_running_miniled_state;
	uint8_t fast_flashing_led_state;
	uint8_t alternate_flashing_state;
	uint8_t miniled_charging_state;
	bool led_test;
;
};

struct aw21024_cfg {
	unsigned char *p;
	unsigned int count;
};

/* Started by AICoder, pid:d6c5fr7e34s1c6b14475097c806caa1ca9c84189 */
struct pat_time pat_time_cfg_group[16] = {
    {0, 0x0},
    {130, 0x1},
    {260, 0x2},
    {380, 0x3},
    {510, 0x4},
    {770, 0x5},
    {1040, 0x6},
    {1600, 0x7},
    {2100, 0x8},
    {2600, 0x9},
    {3100, 0xA},
    {4200, 0xB},
    {5200, 0xC},
    {6200, 0xD},
    {7300, 0xE},
    {8300, 0xF},
};
/* Ended by AICoder, pid:d6c5fr7e34s1c6b14475097c806caa1ca9c84189 */

unsigned int aw21024_7color[] = {
   COLOR_PURE_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_PURE_GREEN, COLOR_CYAN, COLOR_PURE_BLUE, COLOR_PURPLE
};

#endif
