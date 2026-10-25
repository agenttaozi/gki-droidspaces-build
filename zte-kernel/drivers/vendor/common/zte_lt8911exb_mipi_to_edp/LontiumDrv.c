/* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

//#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/component.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/init.h>
#include <linux/hdmi.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_crtc_helper.h>

#define XBL_HAS_INITIALED	1

struct lt8911 {
	struct device *dev;

	struct mipi_dsi_device *dsi;

	u8 i2c_addr;
	int irq;

	u32 irq_gpio;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *blenable_gpio;
	struct gpio_desc *blpower_gpio;
	struct gpio_desc *lcdpower_gpio;
	u32 enpsr_gpio;
	u32 standby_gpio;

	struct i2c_client *i2c_client;
};
//global vaiable
struct i2c_client *lt8911_i2c_client = NULL;
#define LT8911_ADDR_LENGTH 1
#define IIC_MAX_TRANSFER_SIZE       250

#define _ERROR(e)      ((0x01 << e) | (0x01 << (sizeof(s32) * 8 - 1)))
#define ERROR          _ERROR(1)	//for common use
//system relevant
#define ERROR_IIC      _ERROR(2)	//IIC communication error.
#define ERROR_MEM      _ERROR(3)	//memory error.
#define u8 unsigned char

static void *g_reg_mipi_tx_base = 0;

struct lt8911 *g_pdata = NULL;


#define sys_config_ioremap_return(reg_name, addr, reg_len) \
    do {                                                   \
        (reg_name) = (void *)ioremap((addr), (reg_len));   \
        if ((reg_name) == NULL) {                          \
            return (-1);                                   \
        }                                                  \
    } while (0)

#define sys_config_iounmap(reg_name) \
    do {                             \
        if ((reg_name) != NULL) {    \
            iounmap((reg_name));     \
            (reg_name) = 0;          \
        }                            \
    } while (0)


#define sys_writel(addr, value) ((*((volatile unsigned int *)(addr))) = (value))
#define sys_read(addr) (*((volatile int *)(addr)))

extern int zte_get_boardid(void);

#if 0
static void write_reg32(void* addr, unsigned long value, unsigned long mask)
{
    unsigned long t;

    t = sys_read((const volatile void *)(uintptr_t)addr);
    t &= ~mask;
    t |= value & mask;

    sys_writel((volatile void *)(uintptr_t)addr, t);
}
#endif



static int lt8911_parse_dt(struct device *dev,
	struct lt8911 *pdata)
{
	struct device_node *np = dev->of_node;
	int ret = 0;

	np = dev->of_node;

	pdata->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(pdata->power_gpio)) {
		dev_err(dev, "%s: can't get power_gpio: %ld\n",
						__func__, PTR_ERR(pdata->power_gpio));
		ret = -EINVAL;
	} else {
		dev_err(dev, "%s: get power_gpio OK\n", __func__);
	}

	pdata->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(pdata->reset_gpio)) {
		dev_err(dev, "%s: can't get reset gpio: %ld\n",
						__func__, PTR_ERR(pdata->reset_gpio));
		ret = -EINVAL;
	} else {
		dev_err(dev, "%s: get reset gpio OK\n", __func__);
	}

	pdata->blenable_gpio = devm_gpiod_get_optional(dev, "blenable", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(pdata->blenable_gpio)) {
		dev_err(dev, "%s: can't get blenable gpio: %ld\n",
						__func__, PTR_ERR(pdata->blenable_gpio));
		ret = -EINVAL;
	} else {
		dev_err(dev, "%s: get blenable gpio OK\n", __func__);
	}

	pdata->blpower_gpio = devm_gpiod_get_optional(dev, "blpower", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(pdata->blpower_gpio)) {
		dev_err(dev, "%s: can't get blpower gpio: %ld\n",
						__func__, PTR_ERR(pdata->blpower_gpio));
		ret = -EINVAL;
	} else {
		dev_err(dev, "%s: get blpower gpio OK\n", __func__);
	}

	pdata->lcdpower_gpio = devm_gpiod_get_optional(dev, "lcdpower", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(pdata->lcdpower_gpio)) {
		dev_err(dev, "%s: can't get lcdpower gpio: %ld\n",
						__func__, PTR_ERR(pdata->lcdpower_gpio));
		ret = -EINVAL;
	} else {
		dev_err(dev, "%s: get lcdpower gpio OK\n", __func__);
	}

	return ret;
}

static int lt8911_gpio_configure(struct lt8911 *pdata, bool on)
{
	int ret = 0;
	
	dev_err(&lt8911_i2c_client->dev, " irq_gpio = %d\n",pdata->irq_gpio);
	//dev_err(&lt8911_i2c_client->dev, " reset_gpio = %d\n",pdata->reset_gpio);
	dev_err(&lt8911_i2c_client->dev, " enpsr_gpio = %d\n",pdata->enpsr_gpio);
	dev_err(&lt8911_i2c_client->dev, " standby_gpio = %d\n",pdata->standby_gpio);
	
	
	if(on) {
#if !XBL_HAS_INITIALED
		msleep(1);
		dev_err(&lt8911_i2c_client->dev, " lt8911 powerup \n");
		gpiod_direction_output(pdata->reset_gpio, 0);
		dev_err(&lt8911_i2c_client->dev, " lt8911 rst down \n");
		msleep(100);
#endif
		gpiod_direction_output(pdata->power_gpio, 1);
		dev_err(&lt8911_i2c_client->dev, " lt8911 power up \n");
		msleep(20);

		gpiod_direction_output(pdata->reset_gpio, 1);
		dev_err(&lt8911_i2c_client->dev, " lt8911 rst up \n");
		msleep(50);

		return 0;
	} else {

		if (gpio_is_valid(pdata->standby_gpio))
			gpio_free(pdata->standby_gpio);
		
		if (gpio_is_valid(pdata->enpsr_gpio))
			gpio_free(pdata->enpsr_gpio);

		return 0;
	}

/*standby_error:
	gpio_free(pdata->standby_gpio);
enpsr_error:
	gpio_free(pdata->enpsr_gpio);*/
//reset_error:
	//gpio_free(pdata->reset_gpio);

	return ret;
}

static int lt8911_write_reg(int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(lt8911_i2c_client, reg, val);
	if (ret < 0) {
		dev_err(&lt8911_i2c_client->dev, "i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
				val, reg, ret);
		return ret;
	}


	return 0;
}

static int lt8911_read_reg(u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(lt8911_i2c_client, reg);
	if (ret < 0) {
		dev_err(&lt8911_i2c_client->dev, "i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8)ret;


	return 0;
}

void LT8911EXB_IIC_Write_byte(u8 RegAddr, u8 data)
{
	int ret;
	ret = lt8911_write_reg(RegAddr, data);
	if(ret < 0){
		dev_err(&lt8911_i2c_client->dev, " can not write data to lt8911\n");
	}
}

int LT8911EXB_IIC_Read_byte( u8 RegAddr )
{
	u8 readdata = 0;
	int ret;
	ret = lt8911_read_reg(RegAddr, &readdata);
	if(ret < 0){
		dev_err(&lt8911_i2c_client->dev, " can not read data from lt8911\n");
		return -1;
	}
	return readdata;
	
}


//////////////////////////////////////////////////////////////////////////////////////////////////
#define _lt8911_debug_ 1
#define Delay_ms(x) msleep(x)




void LT8911EX_ChipID( void )                                                        // read Chip ID
{
	/*u8 readdata = 0;
	int ret;
	//mutex_lock(&bq->i2c_rw_lock);
	ret = lt8911_write_reg(0xff, 0x81);
	ret = lt8911_write_reg(0x08, 0x7f);
	lt8911_read_reg( 0x00, &readdata);
	dev_err(&lt8911_i2c_client->dev,  " LT8911EXB chip ID: 0x%x", readdata);  // 0x17
	lt8911_read_reg( 0x01, &readdata);
	dev_err(&lt8911_i2c_client->dev,  ", 0x%x", readdata);                      // 0x05
	lt8911_read_reg( 0x02, &readdata);
	dev_err(&lt8911_i2c_client->dev,  ", 0x%x\n", readdata);                      // 0xE0
	//mutex_unlock(&bq->i2c_rw_lock);*/
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );                                         //register bank
	LT8911EXB_IIC_Write_byte( 0x08, 0x7f );

	dev_err(&lt8911_i2c_client->dev, " LT8911EXB chip ID: %02x-%02x-%02x\n",
		LT8911EXB_IIC_Read_byte( 0x00 ),
		LT8911EXB_IIC_Read_byte( 0x01 ),
		LT8911EXB_IIC_Read_byte( 0x02 ));
	
}


/************************************************************
 * Copyright (C), 2009-2018, Lontium Tech. Co., Ltd.
 * FileName:
 * Author:
 * Date:
 * Description:
 * Version:
 * Function List:
 *
 * History:
 *     <author>  <time>   <version >   <desc>
 *
 ***********************************************************/


/*******************************************************

   1、LT8911EXB的IIC地址：
   1. IIC address of lt8911exb:

   a)如果LT8911EXB的第31脚（S_ADR）为低，则LT8911EXB的I2C 地址为0x52; // bit0 是读写标志位；如果是Linux系统，IIC address 的 bit7作为读写标志位，则I2C地址 应该是 0x29
   A) if the 31st pin (s_adr) of lt8911exb is low, the I2C address of lt8911exb is 0x52; // bit0 is the read-write flag ; if it is Linux system, the bit7 of IIC address is the read-write flag, then the I2C address should be 0x29

   b)如果LT8911EXB的第31脚（S_ADR）为高，则LT8911EXB的I2C 地址为0x5a; // bit0 是读写标志位；如果是Linux系统，IIC address 的 bit7作为读写标志位，则I2C地址 应该是 0x2d
   b) if the 31st pin (s_adr) of lt8911exb is high, the I2C address of lt8911exb is 0x5a; // bit0 is the read-write flag ; if it is a Linux system, the bit7 of IIC address is the read-write flag, then the I2C address should be 0x2d

   2、IIC速率不要超过100KHz。
   2. IIC speed shall not exceed 100kHz.

   3、要确定MIPI信号给到LT8911EXB之后，再初始化LT8911EXB。
   3. Confirm that Mipi signal is sent to lt8911exb, and then initialize lt8911exb.

   4、必须由前端主控GPIO来复位LT8911EXB；刷寄存器之前，先Reset LT8911EXB ,用GPIO 先拉低LT8911EXB的复位脚 100ms左右，再拉高，保持100ms。
   4. The lt8911exb must be reset by the Master GPIO; before write the register, reset the lt8911exb first, pull down the reset pin by GPIO for about 100ms, and then pull up for 100ms.

   5、LT8911EXB 对MIPI输入信号的要求：
   5. LT8911EXB MIPI input signal requirements:
   a) MIPI DSI
   b) Video mode

   MIPI 输入需要关闭展频(SSC - Spread-Spectrum Clock)，打开EOTP(End Of Transmite Packet)
   Mipi signal needs to turn off SSC(Spread-Spectrum Clock) and turn on eotp(End Of Transmite Packet)
 *********************************************************/

//#define _Test_Pattern_ // Output test pattern

//#define  _read_edid_ // read eDP panel EDID

//#define _Msa_Active_Only_

#define _eDP_2G7_
//#define _eDP_1G62_

//extern void LT8911EXB_IIC_Write_byte( u8 RegAddr, u8 data );    // IIC write operation, IIC rate do not exceed 100KHz
//extern u8 LT8911EXB_IIC_Read_byte( u8 RegAddr );                // IIC read operation, IIC rate do not exceed 100KHz


//********************************************************//

enum {
	hfp = 0,
	hs,
	hbp,
	hact,
	htotal,
	vfp,
	vs,
	vbp,
	vact,
	vtotal,
	pclk_10khz
};

u8		Read_DPCD010A = 0x00;

bool	ScrambleMode = 0;

bool	flag_mipi_on = 0;

#ifdef _read_edid_ // read eDP panel EDID

u8		EDID_DATA[128] = { 0 };
u16		EDID_Timing[11] = { 0 };

bool	EDID_Reply = 0;
#endif

//////////////////////LT8911EXB Config////////////////////////////////
#define _1920x1200_eDP_Panel_
//#define _1080P_eDP_Panel_
//#define _1366x768_eDP_Panel_
//#define _1280x800_eDP_Panel_
//#define _1280x7200_eDP_Panel_
//#define _1600x900_eDP_Panel_
//#define _1920x1200_eDP_Panel_

#define _MIPI_Lane_ 4   // 3 /2 / 1

//#define _eDP_scramble_ // eDP scramble mode

#define _Nvid 0         // 0: 0x0080,default
static int Nvid_Val[] = { 0x0080, 0x0800 };

#ifdef _1920x1200_eDP_Panel_

#define eDP_lane		2
#define PCR_PLL_PREDIV	0x40

//#define _6bit_
#define _8bit_

//const struct video_timing video[] =
static int MIPI_Timing[] =
// hfp, hs,	hbp,	hact,	htotal,	vfp,	vs,	vbp,	vact,	vtotal,	pixel_CLK/10000
//-----|---|------|-------|--------|-----|-----|-----|--------|--------|---------------
{24, 48, 80, 1920,  2072,  3,  12,  21, 1200, 1236, 15360};


/*******************************************************************
   全志平台的屏参 lcd_hbp 是 上面参数 hbp + hs 的和；
   The lcd_hbp of Allwinner platform is the sum of the above parameters hbp + hs;

   同样的，全志平台的屏参 lcd_vbp 是 上面参数 vbp + vs的 和，这点要注意。
   In the same, lcd_vbp are the sum of the above parameters vbp + vs, which should be noted.
   //-------------------------------------------------------------------

   EOTP(End Of Transmite Packet，hs 传完了，会发这样一个包) 要打开，(之前的LT8911B 需要关闭EOTP，这里LT8911EXB 需要打开)
   Eotp (end of transmit packet, HS will send such a packet) must be turn-on (lt8911b needs to turn-off eotp, here lt8911exb needs to turn-on)

   1、MTK平台 的 dis_eotp_en 的值，改成 false;   LK和kernel都需要修改,改成false。
   1. The value of dis_eotp_en of MTK platform should be changed to 'false'; LK and kernel need to be changed to 'false'.

   2、展讯平台的 tx_eotp 的值置 1 。
   2. The value of tx_eotp of Spreadtrum platform is set to 1.

   3、RK平台 EN_EOTP_TX 置1.
   3. The value of EN_EOTP_TX of RK platform is set to 1.

   4、高通平台 找到 dsi_ctrl_hw_cmn.c -->void dsi_ctrl_hw_cmn_host_setup(struct dsi_ctrl_hw *ctrl,struct dsi_host_common_cfg *cfg)-->增加cfg->append_tx_eot = true;
   4、Qualcomm: dsi_ctrl_hw_cmn.c -->void dsi_ctrl_hw_cmn_host_setup(struct dsi_ctrl_hw *ctrl,struct dsi_host_common_cfg *cfg)-->add cfg->append_tx_eot = true;
*******************************************************************/

#endif

#ifdef _1080P_eDP_Panel_

#define eDP_lane		2
#define PCR_PLL_PREDIV	0x40

// 根据前端MIPI信号的Timing，修改以下参数：
//According to the timing of the Mipi signal, modify the following parameters:
static int MIPI_Timing[] =
// hfp,	hs,	hbp,	hact,	htotal,	vfp,	vs,	vbp,	vact,	vtotal,	pixel_CLK/10000
//-----|---|------|-------|--------|-----|-----|-----|--------|--------|---------------
//{ 88, 44, 148, 1920, 2200, 4, 5, 36, 1080, 1125, 14850 };   // VESA
//{ 48, 32, 187, 1920, 2187 ,3, 6, 51, 1080, 1140, 14960 };   // lifei
//{ 120, 47, 100, 1920, 2187 ,30, 10, 20, 1080, 1140, 14960 }; 
//{ 48, 32, 40, 1920, 2040 ,3, 6, 31, 1080, 1120, 13900 };   // VESA
//  { 48, 32, 80,   1920,   2080,   3,  5,  23,    1080,   1111,   13850 };    // SL156PP36
//{ 120, 110, 100, 1920, 2250 ,20, 10, 13, 1080, 1132, 15200 }; 
{ 88, 44, 148, 1920, 2200 ,4, 5, 36, 1080, 1125, 14850 }; 
//{ 48, 32, 124, 1920, 2124 ,3, 6, 21, 1080, 1110, 14146 }; 


/*******************************************************************
   全志平台的屏参 lcd_hbp 是 上面参数 hbp + hs 的和；
   The lcd_hbp of Allwiner platform is the sum of the above parameters hbp + hs;

   同样的，全志平台的屏参 lcd_vbp 是 上面参数 vbp + vs的 和，这点要注意。
   In the same, lcd_vbp are the sum of the above parameters vbp + vs, which should be noted.
   //-------------------------------------------------------------------

   EOTP(End Of Transmite Packet，hs 传完了，会发这样一个包) 要打开，(之前的LT8911B 需要关闭EOTP，这里LT8911EXB 需要打开)
   Eotp (end of transmit packet, HS will send such a packet) must be turn-on (lt8911b needs to turn-off eotp, here lt8911exb needs to turn-on)

   1、MTK平台 的 dis_eotp_en 的值，改成 false;   LK和kernel都需要修改,改成false。
   1. The value of dis_eotp_en of MTK platform should be changed to 'false'; LK and kernel need to be changed to 'false'.

   2、展讯平台的 tx_eotp 的值置 1 。
   2. The value of tx_eotp of Spreadtrum platform is set to 1.

   3、RK平台 EN_EOTP_TX 置1.
   3. The value of EN_EOTP_TX of RK platform is set to 1.

   4、高通平台 找到 dsi_ctrl_hw_cmn.c -->void dsi_ctrl_hw_cmn_host_setup(struct dsi_ctrl_hw *ctrl,struct dsi_host_common_cfg *cfg)-->增加cfg->append_tx_eot = true;
   4、Qualcomm: dsi_ctrl_hw_cmn.c -->void dsi_ctrl_hw_cmn_host_setup(struct dsi_ctrl_hw *ctrl,struct dsi_host_common_cfg *cfg)-->add cfg->append_tx_eot = true;

*******************************************************************/

#define _6bit_ // eDP panel Color Depth，262K color
//#define _8bit_   // eDP panel Color Depth，16.7M color

#endif

//-----------------------------------------

#ifdef _1366x768_eDP_Panel_

#define eDP_lane		1
#define PCR_PLL_PREDIV	0x40

// 根据前端MIPI信号的Timing，修改以下参数：
static int MIPI_Timing[] =
// hfp,	hs,	hbp,	hact,	htotal,	vfp,	vs,	vbp,	vact,	vtotal,	pixel_CLK/10000
//-----|---|------|-------|--------|-----|-----|-----|--------|--------|---------------
{ 14, 56, 64, 1366, 1500, 1, 3, 28, 768, 800, 7200 };


/*******************************************************************
   全志平台的屏参 lcd_hbp 是 上面参数 hbp + hs 的和；
   The lcd_hbp of Allwiner platform is the sum of the above parameters hbp + hs;

   同样的，全志平台的屏参 lcd_vbp 是 上面参数 vbp + vs的 和，这点要注意。
   In the same, lcd_vbp are the sum of the above parameters vbp + vs, which should be noted.
   //-------------------------------------------------------------------

   EOTP(End Of Transmite Packet，hs 传完了，会发这样一个包) 要打开，(之前的LT8911B 需要关闭EOTP，这里LT8911EXB 需要打开)
   Eotp (end of transmit packet, HS will send such a packet) must be turn-on (lt8911b needs to turn-off eotp, here lt8911exb needs to turn-on)

   1、MTK平台 的 dis_eotp_en 的值，改成 false;	 LK和kernel都需要修改,改成false。
   1. The value of dis_eotp_en of MTK platform should be changed to 'false'; LK and kernel need to be changed to 'false'.

   2、展讯平台的 tx_eotp 的值置 1 。
   2. The value of tx_eotp of Spreadtrum platform is set to 1.

   3、RK平台 EN_EOTP_TX 置1.
   3. The value of EN_EOTP_TX of RK platform is set to 1.

   4、高通平台 找到 dsi_ctrl_hw_cmn.c -->void dsi_ctrl_hw_cmn_host_setup(struct dsi_ctrl_hw *ctrl,struct dsi_host_common_cfg *cfg)-->增加cfg->append_tx_eot = true;
   4、Qualcomm: dsi_ctrl_hw_cmn.c -->void dsi_ctrl_hw_cmn_host_setup(struct dsi_ctrl_hw *ctrl,struct dsi_host_common_cfg *cfg)-->add cfg->append_tx_eot = true;

*******************************************************************/

//#define _6bit_                                                                      // eDP panel Color Depth，262K color

#endif


void LT8911EXB_read_edid( void )
{
#ifdef _read_edid_
	u8 reg, i, j;
//	bool	aux_reply, aux_ack, aux_nack, aux_defer;
	LT8911EXB_IIC_Write_byte( 0xff, 0xac );
	LT8911EXB_IIC_Write_byte( 0x00, 0x20 ); //Soft Link train
	LT8911EXB_IIC_Write_byte( 0xff, 0xa6 );
	LT8911EXB_IIC_Write_byte( 0x2a, 0x01 );

	/*set edid offset addr*/
	LT8911EXB_IIC_Write_byte( 0x2b, 0x40 ); //CMD
	LT8911EXB_IIC_Write_byte( 0x2b, 0x00 ); //addr[15:8]
	LT8911EXB_IIC_Write_byte( 0x2b, 0x50 ); //addr[7:0]
	LT8911EXB_IIC_Write_byte( 0x2b, 0x00 ); //data lenth
	LT8911EXB_IIC_Write_byte( 0x2b, 0x00 ); //data lenth
	LT8911EXB_IIC_Write_byte( 0x2c, 0x00 ); //start Aux read edid

#ifdef _lt8911_debug_
	dev_err(&lt8911_i2c_client->dev, "Read eDP EDID......" );
#endif

	Delay_ms( 20 );                         //more than 10ms
	reg = LT8911EXB_IIC_Read_byte( 0x25 );
	if( ( reg & 0x0f ) == 0x0c )
	{
		for( j = 0; j < 8; j++ )
		{
			if( j == 7 )
			{
				LT8911EXB_IIC_Write_byte( 0x2b, 0x10 ); //MOT
			}else
			{
				LT8911EXB_IIC_Write_byte( 0x2b, 0x50 );
			}

			LT8911EXB_IIC_Write_byte( 0x2b, 0x00 );
			LT8911EXB_IIC_Write_byte( 0x2b, 0x50 );
			LT8911EXB_IIC_Write_byte( 0x2b, 0x0f );
			LT8911EXB_IIC_Write_byte( 0x2c, 0x00 ); //start Aux read edid
			Delay_ms( 50 );                         //more than 50ms

			if( LT8911EXB_IIC_Read_byte( 0x39 ) == 0x31 )
			{
				LT8911EXB_IIC_Read_byte( 0x2b );
				for( i = 0; i < 16; i++ )
				{
					EDID_DATA[j * 16 + i] = LT8911EXB_IIC_Read_byte( 0x2b );
				}

				EDID_Reply = 1;
			}else
			{
				EDID_Reply = 0;
#ifdef _lt8911_debug_
				dev_err(&lt8911_i2c_client->dev,  "no_reply\n" );
				
#endif
				//		dev_err(&lt8911_i2c_client->dev, "*************End***************");
				return;
			}
		}

#if _lt8911_debug_

		for( i = 0; i < 128; i++ ) //print edid data
		{
			dev_err(&lt8911_i2c_client->dev, "0x%x ", EDID_DATA[i]);
		}

		
		dev_err(&lt8911_i2c_client->dev, "eDP Timing = { H_FP / H_pluse / H_BP / H_act / H_tol / V_FP / V_pluse / V_BP / V_act / V_tol / D_CLK };" );
		dev_err(&lt8911_i2c_client->dev, "eDP Timing = { " );
		EDID_Timing[hfp] = ( ( EDID_DATA[0x41] & 0xC0 ) * 4 + EDID_DATA[0x3e] );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[hfp] );         // HFB

		EDID_Timing[hs] = ( ( EDID_DATA[0x41] & 0x30 ) * 16 + EDID_DATA[0x3f] );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[hs] );          // Hsync Wid

		EDID_Timing[hbp] = ( ( ( EDID_DATA[0x3a] & 0x0f ) * 0x100 + EDID_DATA[0x39] ) - ( ( EDID_DATA[0x41] & 0x30 ) * 16 + EDID_DATA[0x3f] ) - ( ( EDID_DATA[0x41] & 0xC0 ) * 4 + EDID_DATA[0x3e] ) );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[hbp] );         // HBP

		EDID_Timing[hact] = ( ( EDID_DATA[0x3a] & 0xf0 ) * 16 + EDID_DATA[0x38] );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[hact] );        // H active

		EDID_Timing[htotal] = ( ( EDID_DATA[0x3a] & 0xf0 ) * 16 + EDID_DATA[0x38] + ( ( EDID_DATA[0x3a] & 0x0f ) * 0x100 + EDID_DATA[0x39] ) );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[htotal] );      // H total

		EDID_Timing[vfp] = ( ( EDID_DATA[0x41] & 0x0c ) * 4 + ( EDID_DATA[0x40] & 0xf0 ) / 16 );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[vfp] );         // VFB

		EDID_Timing[vs] = ( ( EDID_DATA[0x41] & 0x03 ) * 16 + (EDID_DATA[0x40] & 0x0f ));
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[vs] );          // Vsync Wid

		EDID_Timing[vbp] = ( ( ( EDID_DATA[0x3d] & 0x03 ) * 0x100 + EDID_DATA[0x3c] ) - ( ( EDID_DATA[0x41] & 0x03 ) * 16 + (EDID_DATA[0x40] & 0x0f) ) - ( ( EDID_DATA[0x41] & 0x0c ) * 4 + ( EDID_DATA[0x40] & 0xf0 ) / 16 ) );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[vbp] );         // VBP

		EDID_Timing[vact] = ( ( EDID_DATA[0x3d] & 0xf0 ) * 16 + EDID_DATA[0x3b] );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[vact] );        // V active

		EDID_Timing[vtotal] = ( ( EDID_DATA[0x3d] & 0xf0 ) * 16 + EDID_DATA[0x3b] + ( ( EDID_DATA[0x3d] & 0x03 ) * 0x100 + EDID_DATA[0x3c] ) );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[vtotal] );      // V total

		EDID_Timing[pclk_10khz] = ( EDID_DATA[0x37] * 0x100 + EDID_DATA[0x36] );
		dev_err(&lt8911_i2c_client->dev, "0x%x,", (u32)EDID_Timing[pclk_10khz] );  // CLK
		
#endif
	}

	return;

#endif
}

/***********************************************************

***********************************************************/
void LT8911EXB_MIPI_Video_Timing( void )                                    // ( struct video_timing *video_format )
{
	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );
	LT8911EXB_IIC_Write_byte( 0x0d, (u8)( MIPI_Timing[vtotal] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0e, (u8)( MIPI_Timing[vtotal] % 256 ) );    //vtotal
	LT8911EXB_IIC_Write_byte( 0x0f, (u8)( MIPI_Timing[vact] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x10, (u8)( MIPI_Timing[vact] % 256 ) );      //vactive

	LT8911EXB_IIC_Write_byte( 0x11, (u8)( MIPI_Timing[htotal] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x12, (u8)( MIPI_Timing[htotal] % 256 ) );    //htotal
	LT8911EXB_IIC_Write_byte( 0x13, (u8)( MIPI_Timing[hact] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x14, (u8)( MIPI_Timing[hact] % 256 ) );      //hactive

	LT8911EXB_IIC_Write_byte( 0x15, (u8)( MIPI_Timing[vs] % 256 ) );        //vsa
	LT8911EXB_IIC_Write_byte( 0x16, (u8)( MIPI_Timing[hs] % 256 ) );        //hsa
	LT8911EXB_IIC_Write_byte( 0x17, (u8)( MIPI_Timing[vfp] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x18, (u8)( MIPI_Timing[vfp] % 256 ) );       //vfp

	LT8911EXB_IIC_Write_byte( 0x19, (u8)( MIPI_Timing[hfp] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x1a, (u8)( MIPI_Timing[hfp] % 256 ) );       //hfp
}

//------------------------------------

void LT8911EXB_eDP_Video_cfg( void )                                        // ( struct video_timing *video_format )
{
	LT8911EXB_IIC_Write_byte( 0xff, 0xa8 );
	LT8911EXB_IIC_Write_byte( 0x2d, 0x88 );                                 // MSA from register

#ifdef _Msa_Active_Only_
	LT8911EXB_IIC_Write_byte( 0x05, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x06, 0x00 );                                 //htotal
	LT8911EXB_IIC_Write_byte( 0x07, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x08, 0x00 );                                 //h_start

	LT8911EXB_IIC_Write_byte( 0x09, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x0a, 0x00 );                                 //hsa
	LT8911EXB_IIC_Write_byte( 0x0b, (u8)( MIPI_Timing[hact] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0c, (u8)( MIPI_Timing[hact] % 256 ) );      //hactive

	LT8911EXB_IIC_Write_byte( 0x0d, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x0e, 0x00 );                                 //vtotal

	LT8911EXB_IIC_Write_byte( 0x11, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x12, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x14, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x15, (u8)( MIPI_Timing[vact] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x16, (u8)( MIPI_Timing[vact] % 256 ) );      //vactive

#else

	LT8911EXB_IIC_Write_byte( 0x05, (u8)( MIPI_Timing[htotal] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x06, (u8)( MIPI_Timing[htotal] % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x07, (u8)( ( MIPI_Timing[hs] + MIPI_Timing[hbp] ) / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x08, (u8)( ( MIPI_Timing[hs] + MIPI_Timing[hbp] ) % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x09, (u8)( MIPI_Timing[hs] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0a, (u8)( MIPI_Timing[hs] % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0b, (u8)( MIPI_Timing[hact] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0c, (u8)( MIPI_Timing[hact] % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0d, (u8)( MIPI_Timing[vtotal] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x0e, (u8)( MIPI_Timing[vtotal] % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x11, (u8)( ( MIPI_Timing[vs] + MIPI_Timing[vbp] ) / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x12, (u8)( ( MIPI_Timing[vs] + MIPI_Timing[vbp] ) % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x14, (u8)( MIPI_Timing[vs] % 256 ) );
	LT8911EXB_IIC_Write_byte( 0x15, (u8)( MIPI_Timing[vact] / 256 ) );
	LT8911EXB_IIC_Write_byte( 0x16, (u8)( MIPI_Timing[vact] % 256 ) );
#endif
}

/***********************************************************

***********************************************************/

//------------------------------------

void LT8911EXB_init( void )
{
	u8	i;
	u8	pcr_pll_postdiv;
	u8	pcr_m;
	u16 Temp16;

	/* init */
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x08, 0x7f ); // i2c over aux issue
	LT8911EXB_IIC_Write_byte( 0x49, 0xff ); // enable 0x87xx

	LT8911EXB_IIC_Write_byte( 0xff, 0x82 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x5a, 0x0e ); // GPIO test output

	//for power consumption//
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
	LT8911EXB_IIC_Write_byte( 0x05, 0x06 );
	LT8911EXB_IIC_Write_byte( 0x43, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x44, 0x1f );
	LT8911EXB_IIC_Write_byte( 0x45, 0xf7 );
	LT8911EXB_IIC_Write_byte( 0x46, 0xf6 );
	LT8911EXB_IIC_Write_byte( 0x49, 0x7f );

	LT8911EXB_IIC_Write_byte( 0xff, 0x82 );
	LT8911EXB_IIC_Write_byte( 0x12, 0x33 );

	/* mipi Rx analog */
	LT8911EXB_IIC_Write_byte( 0xff, 0x82 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x32, 0x51 );
	LT8911EXB_IIC_Write_byte( 0x35, 0x62 ); //EQ current
	LT8911EXB_IIC_Write_byte( 0x3a, 0x77 ); //EQ 12.5db
	LT8911EXB_IIC_Write_byte( 0x3b, 0x77 ); //EQ 12.5db

	LT8911EXB_IIC_Write_byte( 0x4c, 0x0c );
	LT8911EXB_IIC_Write_byte( 0x4d, 0x00 );

	/* dessc_pcr  pll analog */
	LT8911EXB_IIC_Write_byte( 0xff, 0x82 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x6a, 0x40 );
	LT8911EXB_IIC_Write_byte( 0x6b, PCR_PLL_PREDIV );

	Temp16 = MIPI_Timing[pclk_10khz];

	if( MIPI_Timing[pclk_10khz] < 8800 )
	{
		LT8911EXB_IIC_Write_byte( 0x6e, 0x82 ); //0x44:pre-div = 2 ,pixel_clk=44~ 88MHz
		pcr_pll_postdiv = 0x08;
	}else
	{
		LT8911EXB_IIC_Write_byte( 0x6e, 0x81 ); //0x40:pre-div = 1, pixel_clk =88~176MHz
		pcr_pll_postdiv = 0x04;
	}

	pcr_m = (u8)( Temp16 * pcr_pll_postdiv / 25 / 100 );

	/* dessc pll digital */
	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );     // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0xa9, 0x31 );
	LT8911EXB_IIC_Write_byte( 0xaa, 0x17 );
	LT8911EXB_IIC_Write_byte( 0xab, 0xba );
	LT8911EXB_IIC_Write_byte( 0xac, 0xe1 );
	LT8911EXB_IIC_Write_byte( 0xad, 0x47 );
	LT8911EXB_IIC_Write_byte( 0xae, 0x01 );
	LT8911EXB_IIC_Write_byte( 0xae, 0x11 );

	/* Digital Top */
	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );             // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0xc0, 0x01 );             //select mipi Rx
#ifdef _6bit_
	LT8911EXB_IIC_Write_byte( 0xb0, 0xd0 );             //enable dither
#else
	LT8911EXB_IIC_Write_byte( 0xb0, 0x00 );             // disable dither
#endif

	/* mipi Rx Digital */
	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );             // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x00, _MIPI_Lane_ % 4 );  // 0: 4 Lane / 1: 1 Lane / 2 : 2 Lane / 3: 3 Lane
	LT8911EXB_IIC_Write_byte( 0x02, 0x08 );             //settle
	LT8911EXB_IIC_Write_byte( 0x03, 0x00 );//by lifei
	LT8911EXB_IIC_Write_byte( 0x08, 0x00 );
//	LT8911EXB_IIC_Write_byte( 0x0a, 0x12 );               //pcr mode

	LT8911EXB_IIC_Write_byte( 0x0c, 0x20 );             //fifo position
	LT8911EXB_IIC_Write_byte( 0x1c, 0x20 );             //fifo position

	//	hs mode:MIPI行采样；vs mode:MIPI帧采样
	LT8911EXB_IIC_Write_byte( 0x24, 0x70 );             // 0x30  [3:0]  line limit	  //pcr mode( de hs vs)

	LT8911EXB_IIC_Write_byte( 0x31, 0x0a );

	/*stage1 hs mode*/
	LT8911EXB_IIC_Write_byte( 0x25, 0x90 );             // 0x80		   // line limit
	LT8911EXB_IIC_Write_byte( 0x2a, 0x3a );             // 0x04		   // step in limit
	LT8911EXB_IIC_Write_byte( 0x21, 0x4f );             // hs_step
	LT8911EXB_IIC_Write_byte( 0x22, 0xff );

	/*stage2 de mode*/
	LT8911EXB_IIC_Write_byte( 0x0a, 0x02 );             //de adjust pre line
	LT8911EXB_IIC_Write_byte( 0x38, 0x02 );             //de_threshold 1
	LT8911EXB_IIC_Write_byte( 0x39, 0x04 );             //de_threshold 2
	LT8911EXB_IIC_Write_byte( 0x3a, 0x08 );             //de_threshold 3
	LT8911EXB_IIC_Write_byte( 0x3b, 0x10 );             //de_threshold 4

	LT8911EXB_IIC_Write_byte( 0x3f, 0x04 );             //de_step 1
	LT8911EXB_IIC_Write_byte( 0x40, 0x08 );             //de_step 2
	LT8911EXB_IIC_Write_byte( 0x41, 0x10 );             //de_step 3
	LT8911EXB_IIC_Write_byte( 0x42, 0x60 );             //de_step 4

	/*stage2 hs mode*/
	LT8911EXB_IIC_Write_byte( 0x1e, 0x01 );             // 0x11
	LT8911EXB_IIC_Write_byte( 0x23, 0xf0 );             // 0x80			   //

	LT8911EXB_IIC_Write_byte( 0x2b, 0x80 );             // 0xa0

#ifdef _Test_Pattern_
	LT8911EXB_IIC_Write_byte( 0x26, ( pcr_m | 0x80 ) );
#else

	LT8911EXB_IIC_Write_byte( 0x26, pcr_m );

//	LT8911EXB_IIC_Write_byte( 0x27, Read_0xD095 );
//	LT8911EXB_IIC_Write_byte( 0x28, Read_0xD096 );
#endif

	LT8911EXB_MIPI_Video_Timing( );         //defualt setting is 1080P

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x03, 0x7b ); //PCR reset
	LT8911EXB_IIC_Write_byte( 0x03, 0xff );

#ifdef _eDP_2G7_
	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x19, 0x31 );
//	LT8911EXB_IIC_Write_byte( 0x1a, 0x36 ); // sync m
	LT8911EXB_IIC_Write_byte(0x1a,0x1b);
	LT8911EXB_IIC_Write_byte( 0x1b, 0x00 ); // sync_k [7:0]
	LT8911EXB_IIC_Write_byte( 0x1c, 0x00 ); // sync_k [13:8]

	// txpll Analog
	LT8911EXB_IIC_Write_byte( 0xff, 0x82 );
	LT8911EXB_IIC_Write_byte( 0x09, 0x00 );// div hardware mode, for ssc.
//	LT8911EXB_IIC_Write_byte( 0x01, 0x18 );// default : 0x18
	LT8911EXB_IIC_Write_byte( 0x02, 0x42 );
	LT8911EXB_IIC_Write_byte( 0x03, 0x00 ); // txpll en = 0
	LT8911EXB_IIC_Write_byte( 0x03, 0x01 ); // txpll en = 1
//	LT8911EXB_IIC_Write_byte( 0x04, 0x3a );// default : 0x3A
	LT8911EXB_IIC_Write_byte(0x0a,0x1b);
	LT8911EXB_IIC_Write_byte(0x04,0x2a);

	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x0c, 0x10 ); // cal en = 0

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfc );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfd );

	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x0c, 0x11 ); // cal en = 1

	// ssc
	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x13, 0x83 );
	LT8911EXB_IIC_Write_byte( 0x14, 0x41 );
	LT8911EXB_IIC_Write_byte( 0x16, 0x0a );
	LT8911EXB_IIC_Write_byte( 0x18, 0x0a );
	LT8911EXB_IIC_Write_byte( 0x19, 0x33 );
#endif

#ifdef _eDP_1G62_
	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x19, 0x31 );
	LT8911EXB_IIC_Write_byte( 0x1a, 0x20 ); // sync m
	LT8911EXB_IIC_Write_byte( 0x1b, 0x19 ); // sync_k [7:0]
	LT8911EXB_IIC_Write_byte( 0x1c, 0x99 ); // sync_k [13:8]

	// txpll Analog
	LT8911EXB_IIC_Write_byte( 0xff, 0x82 );
	LT8911EXB_IIC_Write_byte( 0x09, 0x00 );// div hardware mode, for ssc.
	//	LT8911EXB_IIC_Write_byte( 0x01, 0x18 );// default : 0x18
	LT8911EXB_IIC_Write_byte( 0x02, 0x42 );
	LT8911EXB_IIC_Write_byte( 0x03, 0x00 ); // txpll en = 0
	LT8911EXB_IIC_Write_byte( 0x03, 0x01 ); // txpll en = 1
	//	LT8911EXB_IIC_Write_byte( 0x04, 0x3a );// default : 0x3A

	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x0c, 0x10 ); // cal en = 0

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfc );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfd );

	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x0c, 0x11 ); // cal en = 1

	//ssc
	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
	LT8911EXB_IIC_Write_byte( 0x13, 0x83 );
	LT8911EXB_IIC_Write_byte( 0x14, 0x41 );
	LT8911EXB_IIC_Write_byte( 0x16, 0x0a );
	LT8911EXB_IIC_Write_byte( 0x18, 0x0a );
	LT8911EXB_IIC_Write_byte( 0x19, 0x33 );
#endif

	LT8911EXB_IIC_Write_byte( 0xff, 0x87 );

	for( i = 0; i < 5; i++ ) //Check Tx PLL
	{
		Delay_ms( 5 );
		if( LT8911EXB_IIC_Read_byte( 0x37 ) & 0x02 )
		{
			dev_err(&lt8911_i2c_client->dev, "LT8911 tx pll locked" );
			LT8911EXB_IIC_Write_byte(0xff,0x87);
			LT8911EXB_IIC_Write_byte(0x1a,0x36);
			LT8911EXB_IIC_Write_byte(0xff,0x82);
			LT8911EXB_IIC_Write_byte(0x0a,0x36);
			LT8911EXB_IIC_Write_byte(0x04,0x3a);
			break;
		}else
		{
			dev_err(&lt8911_i2c_client->dev, "LT8911 tx pll unlocked" );
			LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
			LT8911EXB_IIC_Write_byte( 0x09, 0xfc );
			LT8911EXB_IIC_Write_byte( 0x09, 0xfd );

			LT8911EXB_IIC_Write_byte( 0xff, 0x87 );
			LT8911EXB_IIC_Write_byte( 0x0c, 0x10 );
			LT8911EXB_IIC_Write_byte( 0x0c, 0x11 );
		}
	}

	// AUX reset
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x07, 0xfe );
	LT8911EXB_IIC_Write_byte( 0x07, 0xff );
	LT8911EXB_IIC_Write_byte( 0x0a, 0xfc );
	LT8911EXB_IIC_Write_byte( 0x0a, 0xfe );

	/* tx phy */
	LT8911EXB_IIC_Write_byte( 0xff, 0x82 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x11, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x13, 0x10 );
	LT8911EXB_IIC_Write_byte( 0x14, 0x0c );
	LT8911EXB_IIC_Write_byte( 0x14, 0x08 );
	LT8911EXB_IIC_Write_byte( 0x13, 0x20 );

	LT8911EXB_IIC_Write_byte( 0xff, 0x82 ); // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x0e, 0x35 );
//	LT8911EXB_IIC_Write_byte( 0x12, 0xff );
//	LT8911EXB_IIC_Write_byte( 0xff, 0x80 );
//	LT8911EXB_IIC_Write_byte( 0x40, 0x22 );

	/*eDP Tx Digital */
	LT8911EXB_IIC_Write_byte( 0xff, 0xa8 ); // Change Reg bank

#ifdef _Test_Pattern_

	//LT8911EXB_IIC_Write_byte( 0x24, 0x50 ); // bit2 ~ bit 0 : test panttern image mode
	LT8911EXB_IIC_Write_byte( 0x24, 0x50 ); // bit2 ~ bit 0 : test panttern image mode
	LT8911EXB_IIC_Write_byte( 0x25, 0x70 ); // bit6 ~ bit 4 : test Pattern color
	LT8911EXB_IIC_Write_byte( 0x27, 0x50 ); //0x50:Pattern; 0x10:mipi video

//	LT8911EXB_IIC_Write_byte( 0x2d, 0x00 ); //  pure color setting
//	LT8911EXB_IIC_Write_byte( 0x2d, 0x84 ); // black color
	LT8911EXB_IIC_Write_byte( 0x2d, 0x88 ); //  block

#else
	LT8911EXB_IIC_Write_byte( 0x27, 0x10 ); //0x50:Pattern; 0x10:mipi video
#endif

#ifdef _6bit_
	LT8911EXB_IIC_Write_byte( 0x17, 0x00 );
	LT8911EXB_IIC_Write_byte( 0x18, 0x00 );
#else
	// _8bit_
	printk("------------------------------------\n"); //zhanzs_debug
	LT8911EXB_IIC_Write_byte( 0x17, 0x10 );
	LT8911EXB_IIC_Write_byte( 0x18, 0x20 );
#endif

	/* nvid */
	LT8911EXB_IIC_Write_byte( 0xff, 0xa0 );                             // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x00, (u8)( Nvid_Val[_Nvid] / 256 ) );    // 0x08
	LT8911EXB_IIC_Write_byte( 0x01, (u8)( Nvid_Val[_Nvid] % 256 ) );    // 0x00
}

//-------------------------------------------------------------------
/* mipi should be ready before configuring below video check setting*/
void LT8911EXB_video_check( void )
{
	//u8	temp;
	u32 reg = 0x00;
	/* mipi byte clk check*/
	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );     // Change Reg bank
	LT8911EXB_IIC_Write_byte( 0x1d, 0x00 );     //FM select byte clk
	LT8911EXB_IIC_Write_byte( 0x40, 0xf7 );
	LT8911EXB_IIC_Write_byte( 0x41, 0x30 );

	//#ifdef _eDP_scramble_
	if( ScrambleMode )
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x82 ); //eDP scramble mode;
	}
	//#else
	else
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x02 ); // DP scramble mode;
	}
	//#endif

//	LT8911EXB_IIC_Write_byte( 0x17, 0xf0 ); // 0xf0:Close scramble; 0xD0 : Open scramble

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
	LT8911EXB_IIC_Write_byte( 0x09, 0x7d );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfd );

	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );
	Delay_ms( 20 );
	if( LT8911EXB_IIC_Read_byte( 0x50 ) == 0x03 )
	{
		reg	   = LT8911EXB_IIC_Read_byte( 0x4d );
		reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x4e );
		reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x4f );

		dev_err(&lt8911_i2c_client->dev, "video check: mipi byteclk =  0x%x\n", reg); // mipi byteclk = reg * 1000
	}else
	{
		dev_err(&lt8911_i2c_client->dev,  "video check: mipi clk unstable" );
	}

	/* mipi vtotal check*/
	reg	   = LT8911EXB_IIC_Read_byte( 0x76 );
	reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x77 );

	dev_err(&lt8911_i2c_client->dev, "video check: Vtotal =  0x%x\n", reg);

	/* mipi word count check*/
	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );
	reg	   = LT8911EXB_IIC_Read_byte( 0x82 );
	reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x83 );
	reg	   = reg / 3;

	dev_err(&lt8911_i2c_client->dev, "video check: Hact(word counter) =  0x%x\n", reg );

	/* mipi Vact check*/
	reg	   = LT8911EXB_IIC_Read_byte( 0x85 );
	reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x86 );

	dev_err(&lt8911_i2c_client->dev, "video check: Vact = 0x%x\n", reg );
}

//------------------------------------------------------------------
void DpcdWrite( u32 Address, u8 Data )
{
	/***************************
	   注意大小端的问题!
	   这里默认是大端模式

	   Pay attention to the Big-Endian and Little-Endian!
	   The default mode is Big-Endian here.

	 ****************************/
	u8	AddressH   = 0x0f & ( Address >> 16 );
	u8	AddressM   = 0xff & ( Address >> 8 );
	u8	AddressL   = 0xff & Address;

	u8	reg;

	LT8911EXB_IIC_Write_byte( 0xff, 0xa6 );
	LT8911EXB_IIC_Write_byte( 0x2b, ( 0x80 | AddressH ) );  //CMD
	LT8911EXB_IIC_Write_byte( 0x2b, AddressM );             //addr[15:8]
	LT8911EXB_IIC_Write_byte( 0x2b, AddressL );             //addr[7:0]
	LT8911EXB_IIC_Write_byte( 0x2b, 0x00 );                 //data lenth
	LT8911EXB_IIC_Write_byte( 0x2b, Data );                 //data
	LT8911EXB_IIC_Write_byte( 0x2c, 0x00 );                 //start Aux

	Delay_ms( 20 );                                         //more than 10ms
	reg = LT8911EXB_IIC_Read_byte( 0x25 );

	if( ( reg & 0x0f ) == 0x0c )
	{
		return;
	}
}

//------------------------------------------------------------------

u8 DpcdRead( u32 Address )
{
	/***************************
	   注意大小端的问题!
	   这里默认是大端模式

	   Pay attention to the Big-Endian and Little-Endian!
	   The default mode is Big-Endian here.

	 ****************************/

	u8	DpcdValue  = 0x00;
	u8	AddressH   = 0x0f & ( Address >> 16 );
	u8	AddressM   = 0xff & ( Address >> 8 );
	u8	AddressL   = 0xff & Address;
	u8	reg;

	LT8911EXB_IIC_Write_byte( 0xff, 0xac );
	LT8911EXB_IIC_Write_byte( 0x00, 0x20 );                 //Soft Link train
	LT8911EXB_IIC_Write_byte( 0xff, 0xa6 );
	LT8911EXB_IIC_Write_byte( 0x2a, 0x01 );

	LT8911EXB_IIC_Write_byte( 0xff, 0xa6 );
	LT8911EXB_IIC_Write_byte( 0x2b, ( 0x90 | AddressH ) );  //CMD
	LT8911EXB_IIC_Write_byte( 0x2b, AddressM );             //addr[15:8]
	LT8911EXB_IIC_Write_byte( 0x2b, AddressL );             //addr[7:0]
	LT8911EXB_IIC_Write_byte( 0x2b, 0x00 );                 //data lenth
	LT8911EXB_IIC_Write_byte( 0x2c, 0x00 );                 //start Aux read edid

	Delay_ms( 50 );                                         //more than 10ms
	reg = LT8911EXB_IIC_Read_byte( 0x25 );
	if( ( reg & 0x0f ) == 0x0c )
	{
		if( LT8911EXB_IIC_Read_byte( 0x39 ) == 0x22 )
		{
			LT8911EXB_IIC_Read_byte( 0x2b );
			DpcdValue = LT8911EXB_IIC_Read_byte( 0x2b );
		}


		/*
		   else
		   {
		   //	goto no_reply;
		   //	DpcdValue = 0xff;
		   return DpcdValue;
		   }//*/
	}else
	{
		LT8911EXB_IIC_Write_byte( 0xff, 0x81 ); // change bank
		LT8911EXB_IIC_Write_byte( 0x07, 0xfe );
		LT8911EXB_IIC_Write_byte( 0x07, 0xff );
		LT8911EXB_IIC_Write_byte( 0x0a, 0xfc );
		LT8911EXB_IIC_Write_byte( 0x0a, 0xfe );
	}

	return DpcdValue;
}

//------------------------------------------------------------------

void LT8911EX_link_train( void )
{
	//u8 reg;
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
	LT8911EXB_IIC_Write_byte( 0x06, 0xdf ); // rset VID TX
	LT8911EXB_IIC_Write_byte( 0x06, 0xff );
	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );

//	LT8911EXB_IIC_Write_byte( 0x17, 0xf0 ); // 0xf0:Close scramble; 0xD0 : Open scramble
//#ifdef _eDP_scramble_
	if( ScrambleMode )
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x82 ); // eDP scramble mode;

		/* Aux operater init */
   LT8911EXB_IIC_Write_byte( 0xff, 0xac );
   LT8911EXB_IIC_Write_byte( 0x00, 0x20 ); //Soft Link train
   LT8911EXB_IIC_Write_byte( 0xff, 0xa6 );
   LT8911EXB_IIC_Write_byte( 0x2a, 0x01 );

   DpcdWrite( 0x010a, 0x01 );
   Delay_ms( 10 );
   DpcdWrite( 0x0102, 0x00 );
   Delay_ms( 10 );
   DpcdWrite( 0x010a, 0x01 );
#if 0
	DpcdWrite( 0x010a, 0x80 );
    Delay_ms( 10 );
	reg = DpcdRead(0x010a);
	dev_err(&lt8911_i2c_client->dev, "Reg0x010a =  0x%x\n", reg );
    DpcdWrite( 0x0102, 0x00 );
    Delay_ms( 10 );
    DpcdWrite( 0x010a, 0x80 );
    Delay_ms( 200 );
#endif 
	}
//#else
	else
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x02 ); // DP scramble mode;
	}
//#endif

	/* Aux setup */
	LT8911EXB_IIC_Write_byte( 0xff, 0xac );
	LT8911EXB_IIC_Write_byte( 0x00, 0x60 );     //Soft Link train
	LT8911EXB_IIC_Write_byte( 0xff, 0xa6 );
	LT8911EXB_IIC_Write_byte( 0x2a, 0x00 );

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );
	LT8911EXB_IIC_Write_byte( 0x07, 0xfe );
	LT8911EXB_IIC_Write_byte( 0x07, 0xff );
	LT8911EXB_IIC_Write_byte( 0x0a, 0xfc );
	LT8911EXB_IIC_Write_byte( 0x0a, 0xfe );

	/* link train */

	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );
	LT8911EXB_IIC_Write_byte( 0x1a, eDP_lane );

	LT8911EXB_IIC_Write_byte( 0xff, 0xac );
	LT8911EXB_IIC_Write_byte( 0x00, 0x64 );
	LT8911EXB_IIC_Write_byte( 0x01, 0x0a );
	LT8911EXB_IIC_Write_byte( 0x0c, 0x85 );
	LT8911EXB_IIC_Write_byte( 0x0c, 0xc5 );
//	Delay_ms( 500 );


}

//-------------------------------------------

//*
void LT8911EX_link_train_result( void )
{
	u8 i, reg;
	LT8911EXB_IIC_Write_byte( 0xff, 0xac );
	for( i = 0; i < 10; i++ )
	{
		reg = LT8911EXB_IIC_Read_byte( 0x82 );
		 dev_err(&lt8911_i2c_client->dev,  "0x82 = 0x%x\n", reg );
		if( reg & 0x20 )
		{
			if( ( reg & 0x1f ) == 0x1e )
			{
				dev_err(&lt8911_i2c_client->dev,  "Link train success, 0x82 = 0x%02x\n", reg );
			} else
			{
				dev_err(&lt8911_i2c_client->dev,  "Link train fail, 0x82 = 0x%02x\n", reg );
			}

			dev_err(&lt8911_i2c_client->dev,  "panel link rate: 0x%02x\n", LT8911EXB_IIC_Read_byte( 0x83 ) );
			dev_err(&lt8911_i2c_client->dev,  "panel link count: 0x%02x\n", LT8911EXB_IIC_Read_byte( 0x84 ) );
			return;
		}else
		{
			dev_err(&lt8911_i2c_client->dev,  "link trian on going..." );
		}
		Delay_ms( 100 );
	}
}

//*/

void LT8911EXB_MainLoop( void )
{
#ifndef _Test_Pattern_
	u16 reg;

	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );
	//LT8911EXB_IIC_Write_byte(0x1d,0x00); //FM select byte clk
	//LT8911EXB_IIC_Write_byte(0x40,0xf7);
	//LT8911EXB_IIC_Write_byte(0x41,0x30);

	if( ScrambleMode )
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x82 ); //
	}else
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x02 ); //
	}

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );     //video check rst
	LT8911EXB_IIC_Write_byte( 0x09, 0x7d );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfd );
	Delay_ms( 50 );

	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );
	reg	   = LT8911EXB_IIC_Read_byte( 0x76 );
	reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x77 );

//	if( reg == MIPI_Timing[vtotal] )
	if( ( reg > ( MIPI_Timing[vtotal] - 5 ) ) && ( reg < ( MIPI_Timing[vtotal] + 5 ) ) )
	{
		if( !flag_mipi_on )
		{
			LT8911EXB_IIC_Write_byte( 0xff, 0x81 ); //PCR reset
			LT8911EXB_IIC_Write_byte( 0x03, 0x7b );
			LT8911EXB_IIC_Write_byte( 0x03, 0xff );

			LT8911EXB_IIC_Write_byte( 0xff, 0xa8 );
			LT8911EXB_IIC_Write_byte( 0x2d, 0x88 );
			flag_mipi_on = 1;
			dev_err(&lt8911_i2c_client->dev, "PCR reset" );
		}
	}else
	{
		LT8911EXB_IIC_Write_byte( 0xff, 0xa8 );
		LT8911EXB_IIC_Write_byte( 0x2d, 0x8c ); //edp output idle pattern;
		flag_mipi_on = 0;
	}

#if _lt8911_debug_
	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );
	reg = LT8911EXB_IIC_Read_byte( 0x87 );
//	reg	   = reg * 256 + HDMI_ReadI2C_Byte( 0x77 );

	dev_err(&lt8911_i2c_client->dev, "Reg0xD087 =  0x%x\n", reg );
	if( reg & 0x10 )
	{
		dev_err(&lt8911_i2c_client->dev, "PCR Clock stable" );
	}else
	{
		dev_err(&lt8911_i2c_client->dev,  "PCR Clock unstable" );
	}
#endif

#endif
}

/***********************************************************/


/*
   reg 0x822/0x8226 : bit1 bit0
   Da_edptx0_tap0_current_tune software output set value:

   2'b00 = None;
   2'b01 = 12.8mA;
   2'b10 = 15mA

   //--------------------------//

   reg 0x8223/ 0x8227 bit7 ~ bit0
   Da_edptx_tap0_current_tune software output set value:

   8'b00000000 = None;
   8'b00000001 = 50uA;
   8'b00000010 = 100uA;
   8'b00000100 = 200uA;
   8'b00001000 = 400uA;
   8'b00010000 = 800uA;
   8'b00100000 = 1.6mA;
   8'b01000000 = 3.2mA;
   8'b10000000 = 6.4mA

   //--------------------------//

 */
enum
{
	_Level0_ = 0,                                               // 27.8 mA  0x83/0x00
	_Level1_,                                                   // 26.2 mA  0x82/0xe0
	_Level2_,                                                   // 24.6 mA  0x82/0xc0
	_Level3_,                                                   // 23 mA    0x82/0xa0
	_Level4_,                                                   // 21.4 mA  0x82/0x80
	_Level5_,                                                   // 18.2 mA  0x82/0x40
	_Level6_,                                                   // 16.6 mA  0x82/0x20
	_Level7_,                                                   // 15mA     0x82/0x00
	_Level8_,                                                   // 12.8mA   0x81/0x00
	_Level9_,                                                   // 11.2mA   0x80/0xe0
	_Level10_,                                                  // 9.6mA    0x80/0xc0
	_Level11_,                                                  // 8mA      0x80/0xa0
	_Level12_,                                                  // 6mA      0x80/0x80
};

u8	Swing_Setting1[] = { 0x83, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x81, 0x80, 0x80, 0x80, 0x80 };
u8	Swing_Setting2[] = { 0x00, 0xe0, 0xc0, 0xa0, 0x80, 0x40, 0x20, 0x00, 0x00, 0xe0, 0xc0, 0xa0, 0x80 };

u8	Level = _Level7_;                                           // normal


/***********************************************************

***********************************************************/
void LT8911EX_TxSwingPreSet( void )
{
	LT8911EXB_IIC_Write_byte( 0xFF, 0x82 );
	LT8911EXB_IIC_Write_byte( 0x22, Swing_Setting1[Level] );    //lane 0 tap0
	LT8911EXB_IIC_Write_byte( 0x23, Swing_Setting2[Level] );
	LT8911EXB_IIC_Write_byte( 0x24, 0x80 );                     //lane 0 tap1
	LT8911EXB_IIC_Write_byte( 0x25, 0x38 );
	LT8911EXB_IIC_Write_byte( 0x26, Swing_Setting1[Level] );    //lane 1 tap0
	LT8911EXB_IIC_Write_byte( 0x27, Swing_Setting2[Level] );
	LT8911EXB_IIC_Write_byte( 0x28, 0x80 );                     //lane 1 tap1
	LT8911EXB_IIC_Write_byte( 0x29, 0x38 );
}

/***********************************************************

***********************************************************/
void PCR_Status( void )                                         // for debug
{
#ifdef _lt8911_debug_
	u8 reg;

	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );
	reg = LT8911EXB_IIC_Read_byte( 0x87 );

	dev_err(&lt8911_i2c_client->dev,  "Reg0xD087 = 0x%x\n",reg);
	if( reg & 0x10 )
	{
		dev_err(&lt8911_i2c_client->dev, "PCR Clock stable 1" );
	}else
	{
		dev_err(&lt8911_i2c_client->dev,  "PCR Clock unstable 2" );
	}
#endif
}

/***********************************************************

***********************************************************/
/*void Reset_LT8911EXB( void )
{
	_LT8911_RSTN_LOW;   // GPIO Low
	Delay_ms( 100 );
	_LT8911_RSTN_High;  // GPIO High
	Delay_ms( 100 );
}*/

/***********************************************************

***********************************************************/
void LT8911_MainLoop( void )
{
	u16 reg, H_act, V_act;
//	bool	flag_mipi_on = 0;

	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );

	if( ScrambleMode )
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x82 ); //video check from mipi
	}else
	{
		LT8911EXB_IIC_Write_byte( 0xa1, 0x02 );
	}

	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );     //video check rst
	LT8911EXB_IIC_Write_byte( 0x09, 0x7d );
	LT8911EXB_IIC_Write_byte( 0x09, 0xfd );
	Delay_ms( 50 );

	/* mipi word count check*/
	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );
	reg	   = LT8911EXB_IIC_Read_byte( 0x82 );
	reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x83 );
	H_act  = reg / 3;

#ifdef _lt8911_debug_
	dev_err(&lt8911_i2c_client->dev, "Hact(word counter) = 0x%x\n", H_act); // H active = word counter / 3
#endif

	/* mipi Vact check*/
	reg	   = LT8911EXB_IIC_Read_byte( 0x85 );
	V_act  = reg * 256 + LT8911EXB_IIC_Read_byte( 0x86 );

#ifdef _lt8911_debug_
	dev_err(&lt8911_i2c_client->dev, "Vact = 0x%x\n", V_act);
#endif

	LT8911EXB_IIC_Write_byte( 0xff, 0x85 );
	reg	   = LT8911EXB_IIC_Read_byte( 0x76 );
	reg	   = reg * 256 + LT8911EXB_IIC_Read_byte( 0x77 );

#ifdef _lt8911_debug_
	dev_err(&lt8911_i2c_client->dev, "video check: Vtotal = 0x%x\n", reg );
#endif

//	if( reg == MIPI_Timing[vtotal] )
	if( ( reg > ( MIPI_Timing[vtotal] - 5 ) ) && ( reg < ( MIPI_Timing[vtotal] + 5 ) ) )
	{
		if( !flag_mipi_on )
		{
			LT8911EXB_IIC_Write_byte( 0xff, 0x81 ); //PCR reset
			LT8911EXB_IIC_Write_byte( 0x03, 0x7b );
			LT8911EXB_IIC_Write_byte( 0x03, 0xff );

			LT8911EXB_IIC_Write_byte( 0xff, 0xa8 );
			LT8911EXB_IIC_Write_byte( 0x2d, 0x88 );
			flag_mipi_on = 1;
#ifdef _lt8911_debug_
			dev_err(&lt8911_i2c_client->dev, "PCR reset" );
#endif
		}
	}else
	{
		LT8911EXB_IIC_Write_byte( 0xff, 0xa8 );
		LT8911EXB_IIC_Write_byte( 0x2d, 0x8c ); //edp output idle pattern;
		flag_mipi_on = 0;
	}

#ifdef _lt8911_debug_
	LT8911EXB_IIC_Write_byte( 0xff, 0xd0 );
	reg = LT8911EXB_IIC_Read_byte( 0x87 );

	dev_err(&lt8911_i2c_client->dev, "Reg0xD087 =  0x%x\n", reg );
	if( reg & 0x10 )
	{
		dev_err(&lt8911_i2c_client->dev, "PCR Clock stable" );
	}else
	{
		dev_err(&lt8911_i2c_client->dev,  "PCR Clock unstable" );
	}
#endif
}

/***********************************************************/
void TurnOff_eDP_output( void )
{
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );  // register bank
	LT8911EXB_IIC_Write_byte( 0x46, 0x9e );  // Turn off output
}

void TurnOn_eDP_output( void )
{
	LT8911EXB_IIC_Write_byte( 0xff, 0x81 );  // register bank
	LT8911EXB_IIC_Write_byte( 0x46, 0xfe );  // Turn on output
}

void LT8911EXB_LinkTrainResultCheck( void )
{
#if 1 // def _link_train_enable_
	u8	i;
	u8	val;
	//int ret;

	LT8911EXB_IIC_Write_byte( 0xff, 0xac );
	for( i = 0; i < 50; i++ )
	{
		val = LT8911EXB_IIC_Read_byte( 0x82 );
		if( val & 0x20 )
		{
			if( ( val & 0x1f ) == 0x1e )
			{
#ifdef _lt8911_debug_
				//   printf("LT8911_LinkTrainResultCheck: edp link train successed: 0x%bx", val);
				printk( "edp link train successed: 0x%x\n", val );
#endif
//				TurnOn_eDP_output();
				return;
			}else
			{
#ifdef _lt8911_debug_
				//printf("LT8911_LinkTrainResultCheck: edp link train failed: 0x%bx", val);
				printk( "edp link train failed: 0x%x\n", val );
#endif
				LT8911EXB_IIC_Write_byte( 0xff, 0xac );
				LT8911EXB_IIC_Write_byte( 0x00, 0x00 );
				LT8911EXB_IIC_Write_byte( 0x01, 0x0a );
				LT8911EXB_IIC_Write_byte( 0x14, 0x80 );
				LT8911EXB_IIC_Write_byte( 0x14, 0x81 );
				Delay_ms( 50 );
				LT8911EXB_IIC_Write_byte( 0x14, 0x84 );
				Delay_ms( 50 );
				LT8911EXB_IIC_Write_byte( 0x14, 0xc0 );
				//printf("LT8911_LinkTrainResultCheck: Enable eDP video output while linktrian fail");
			}

#ifdef _lt8911_debug_

			val = LT8911EXB_IIC_Read_byte( 0x83 );
			//printf("LT8911_LinkTrainResultCheck: panel link rate: 0x%bx",val);
			printk( "panel link rate: 0x%x\n", val );
			val = LT8911EXB_IIC_Read_byte( 0x84 );
			//printf("LT8911_LinkTrainResultCheck: panel link count: 0x%bx",val);
			printk( "panel link count: 0x%x\n", val );
#endif
			Delay_ms( 20 );
		}else
		{
			printk("LT8911_LinkTrainResultCheck: link trian on going...\n");
			Delay_ms( 20 );
		}
	}
#endif
}

//----------------------------------------

void LT8911EXB_config( void )
{
	u8 val_hpd = 0;
	u8 hpd_count = 0;
	//Reset_LT8911EXB( );     // 先Reset LT8911EXB ,用GPIO 先拉低LT8911EXB的复位脚 100ms左右，再拉高，保持100ms。

	LT8911EX_ChipID( );     // read Chip ID

	LT8911EXB_eDP_Video_cfg( );

	while(hpd_count < 50) {
		LT8911EXB_IIC_Write_byte(0xff, 0x85);
		val_hpd = LT8911EXB_IIC_Read_byte(0xB7);
		hpd_count = hpd_count + 1;
		printk( "LT8911EXB_config: val_hpd=0x%2x hpd_count=%d\n", val_hpd, hpd_count);

		if (val_hpd & 0x04)
			break;

		Delay_ms(100);
	}

	LT8911EXB_init( );

	LT8911EXB_read_edid( ); // for debug

/*
	Read_DPCD010A = DpcdRead( 0x010A ) & 0x01;

#ifdef _lt8911_debug_
	
	dev_err(&lt8911_i2c_client->dev, "DPCD010Ah:%x,", Read_DPCD010A );
#endif

	if( Read_DPCD010A )
	{
		ScrambleMode = 1;
	}else
	{
		ScrambleMode = 0;
	}
	//*/
	ScrambleMode = 0;

	LT8911EX_TxSwingPreSet( );

	LT8911EX_link_train( );

	LT8911EXB_LinkTrainResultCheck( );
//======================================//
	LT8911EX_link_train_result( );  // for debug

	LT8911EXB_video_check( );       // just for Check MIPI Input

	PCR_Status( );                  // just for Check PCR CLK
//======================================//


/* If lt8911exb is controlled by MCU,LT8911_MainLoop( ) function is added in the main loop.
   while( 1 )
   {
   // 循环检测MIPI信号，如果有断续，会出现reset PCR
   //Loop detection of Mipi signal. If there is interruption, reset PCR will appear
   LT8911_MainLoop( );

   Delay_ms( 1000 );
   }
   //*/
}

/************************************** The End Of File **************************************/




//////////////////////////////////////////////////////////////////////////////////////////////////

static int lt8911_probe(struct i2c_client *client,
	 const struct i2c_device_id *id)
{
	struct lt8911 *pdata;
	int ret = 0;
	int boardid = zte_get_boardid();

	pr_info("%s: start probe lt8911 boardid=%d --- 20250923V3\n", __func__, boardid);

	#if 0 //zte modify
	sys_config_ioremap_return(g_reg_mipi_tx_base, 0x17A80000, 0x0350);

	//EoTp
	write_reg32(g_reg_mipi_tx_base + 0x002c, 0x1f, 0xff); //enable EoTp	
	#endif

	dev_err(&client->dev, " start probe lt8911\n");
	if (!client || !client->dev.of_node) {
		dev_err(&client->dev, " invalid input\n");
		return -EINVAL;
	}

	if (boardid > 4) {
		client->addr = 0x2d;
	} else {
		client->addr = 0x29;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, " device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&client->dev,
		sizeof(struct lt8911), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = &client->dev;
	pdata->i2c_client = client;
	lt8911_i2c_client = client;
	dev_err(&client->dev, "I2C address is %x\n", client->addr);
	dev_err(&client->dev, " start init gpio\n");

	ret = lt8911_parse_dt(&lt8911_i2c_client->dev, pdata);
	if (ret) {
		dev_err(&lt8911_i2c_client->dev, " failed to parse device tree\n");
		goto err_dt_parse;
	}

#if !XBL_HAS_INITIALED
	pr_info("%s: LT8911EXB_config\n", __func__);
	ret = lt8911_gpio_configure(pdata, true);
	if (ret) {
		dev_err(&client->dev, " failed to configure GPIOs\n");
	}

	LT8911EXB_config();
#endif

	i2c_set_clientdata(client, pdata);
	dev_set_drvdata(&client->dev, pdata);

	g_pdata = pdata;

	pr_info("%s: probe suceesully!\n", __func__);
	return 0;


err_dt_parse:
	devm_kfree(&client->dev, pdata);
	return ret;
}

static int lt8911_remove(struct i2c_client *client)
{
	int ret = -EINVAL;
	struct lt8911 *pdata = NULL;

	sys_config_iounmap(g_reg_mipi_tx_base);
	
	pdata = i2c_get_clientdata(client);
	
	if (!pdata)
		goto end;

	disable_irq(pdata->irq);
	free_irq(pdata->irq, pdata);

	ret = lt8911_gpio_configure(pdata, false);

	devm_kfree(&lt8911_i2c_client->dev, pdata);

end:
	return ret;
}


/*0: sleep
  1: power on
  2: pre sleep
  3: pre power on
  */
int lt8911_main_config(int enable, int lcd_id)
{
	pr_info("%s: enable=%d lcd_id=%d\n", __func__, enable, lcd_id);

	if (g_pdata != NULL) {
		switch (enable) {
		case 0:
			pr_info("%s: pull down blpower_gpio for sleep\n", __func__);
			gpiod_direction_output(g_pdata->blpower_gpio, 0);
			msleep(5);

			gpiod_direction_output(g_pdata->reset_gpio, 0);
			msleep(5);

			gpiod_direction_output(g_pdata->power_gpio, 0);

			gpiod_direction_output(g_pdata->lcdpower_gpio, 0);
			if (lcd_id == 1) {
				msleep(1500);
			} else {
				msleep(500);
			}
			break;
		case 1:
			pr_info("%s: pull up blenable_gpio for wakeup\n", __func__);
			gpiod_direction_output(g_pdata->blenable_gpio, 1);
			break;
		case 2:
			pr_info("%s: pull down blenable_gpio for pre sleep\n", __func__);
			gpiod_direction_output(g_pdata->blenable_gpio, 0);
			msleep(5);
			break;
		case 3:
			pr_info("%s: pull up lcdpower blpower gpio for pre wakeup\n", __func__);
			gpiod_direction_output(g_pdata->lcdpower_gpio, 1);
			gpiod_direction_output(g_pdata->blenable_gpio, 0);
			if (lcd_id == 1) {
				msleep(30);
			} else {
				msleep(10);
			}

			gpiod_direction_output(g_pdata->power_gpio, 1);
			msleep(10);

			gpiod_direction_output(g_pdata->reset_gpio, 1);
			msleep(100);

			LT8911EXB_config();
			pr_info("%s: LT8911EXB_config end\n", __func__);
			msleep(5);

			gpiod_direction_output(g_pdata->blpower_gpio, 1);
			msleep(5);
			break;
		default:
			pr_info("%s: not support code\n", __func__);
			break;
		}
	} else {
		pr_info("%s: g_pdata is NULL, skip\n", __func__);
	}

	return 0;
}
EXPORT_SYMBOL(lt8911_main_config);

#ifdef CONFIG_PM_SLEEP
static int lt8911_suspend(struct device *dev)
{
	/*struct i2c_client *client = to_i2c_client(dev);
	struct lt8911 *pdata = i2c_get_clientdata(client);

	if (pdata->regulator_on) {
		disable_irq(pdata->irq);
		//lt8911_device_power_ctl(pdata, false);
		pdata->regulator_on = false;
	}*/

	return 0;
}

static int lt8911_resume(struct device *dev)
{
	/*struct i2c_client *client = to_i2c_client(dev);
	struct lt8911 *pdata = i2c_get_clientdata(client);

	if (!pdata->regulator_on) {
		lt8911_device_power_ctl(pdata, true);
		enable_irq(pdata->irq);
		pdata->regulator_on = true;
	}*/

	return 0;
}

static const struct dev_pm_ops lt8911_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(lt8911_suspend, lt8911_resume)
};
#endif

static struct i2c_device_id lt8911_id[] = {
	{ "lt8911exb", 0},
	{}
};

static const struct of_device_id lt8911_match_table[] = {
	{.compatible = "lontium,lt8911exb"},
	{}
};
MODULE_DEVICE_TABLE(of, lt8911_match_table);

static struct i2c_driver lt8911_driver = {
	.driver = {
		.name = "lt8911exb",
		.owner = THIS_MODULE,
		.of_match_table = lt8911_match_table,
#ifdef CONFIG_PM_SLEEP
		.pm = &lt8911_pm,
#endif
	},
	.probe = lt8911_probe,
	.remove = lt8911_remove,
	.id_table = lt8911_id,
};

module_i2c_driver(lt8911_driver);

MODULE_AUTHOR("microlite.cn");
MODULE_DESCRIPTION("lt8911exb driver");
MODULE_LICENSE("GPL");