// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/
#define pr_fmt(fmt)	"[ZTE_LDD_CHARGE][HDMI_IC][LT8711]: %s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/extcon-provider.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/pinctrl/consumer.h>
#include <linux/firmware.h>

#define fw_file "lt8711uxc.bin"

#define LT8911_PAGE_SIZE 32

#define FW_32K_SIZE  (32 * 1024)

#define UPGRADE  1
#define NOT_UPGRADE  0


#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>
int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif

typedef struct
{
    u8 width;
    u32  poly;
    u32  crcinit;
    u32  xorout;
    bool refin;
    bool refout;
} CrcInfoTypeS;

struct lt8711_dp_device {
    struct i2c_client *client;
    struct device		*dev;
    /* update mutex */
    struct regulator *regulator_vdd_3v3;
    struct regulator *regulator_vdd_1v25;
    struct gpio_desc *vdd_3v3_gpio;
    struct gpio_desc *vdd_1v25_gpio;
    struct gpio_desc *gpio_reset;
    struct gpio_desc *gpio_hdmi_5v;
    struct gpio_desc *gpio_usb_hub_5v;
    struct gpio_desc *gpio_hdmi_irq;
    struct gpio_desc *gpio_chip_irq;
    struct workqueue_struct *upgrade_fw_wq;
    struct delayed_work upgrade_fw_work;
    struct mutex ocm_lock;
    const struct firmware *fw;
    int hdmi_irq;
    int chip_irq;
    int poweron_status;
    int is_poweroff_mode;
    u8 *fw_buffer;
    u8 fw_crc_value;
};

extern int zte_get_boardid(void);
static int lt8711_dp_enable_power(struct lt8711_dp_device *dp_device, bool enable);

static int lt8711_write_reg(struct lt8711_dp_device *dp_device, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(dp_device->client, reg, val);
	if (ret < 0) {
		dev_err(dp_device->dev, "i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
				val, reg, ret);
		return ret;
	}


	return 0;
}

static int lt8711_read_reg(struct lt8711_dp_device *dp_device, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(dp_device->client, reg);
	if (ret < 0) {
		dev_err(dp_device->dev, "i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8)ret;


	return 0;
}

/**
 *******************************************************************************
 * @brief   bit reverse
 * @param   [in] inval	- in data
 * @param   [in] bits   - inverse bit
 * @return  out data
 * @note
 *******************************************************************************
 */
static unsigned int bitsreverse(u32 inval, u8 bits)
{
	u32 outval = 0;
	u8 i;

	for(i=0; i<bits; i++)
	{
		if(inval & (1 << i)) outval |= 1 << (bits - 1 - i);
	}

	return outval;
}

/**
 *******************************************************************************
 * @brief   get crc value
 * @param   [in] type   - crc config
 * @param   [in] *buf   - data buffer
 * @param   [in] buflen - data len
 * @return  crc value
 * @note
 *******************************************************************************
 */
static unsigned int getcrc(CrcInfoTypeS type, const  u8 *buf, u64 buflen)
{
	u8 width  = type.width;
	u32  poly   = type.poly;
	u32  crc    = type.crcinit;
	u32  xorout = type.xorout;
	bool refin  = type.refin;
	bool refout = type.refout;
	u8 n;
	u32  bits;
	u32  data;
	u8 i;

	n    =  (width < 8) ? 0 : (width-8);
	crc  =  (width < 8) ? (crc<<(8-width)) : crc;
	bits =  (width < 8) ? 0x80 : (1 << (width-1));
	poly =  (width < 8) ? (poly<<(8-width)) : poly;
	while(buflen--)
	{
		data = *(buf++);
		if(refin == true)
			data = bitsreverse(data, 8);
		crc ^= (data << n);
		for(i=0; i<8; i++)
		{
			if(crc & bits)
			{
				crc = (crc << 1) ^ poly;
			}
			else
			{
				crc = crc << 1;
			}
		}
	}
	crc = (width<8) ? (crc>>(8-width)) : crc;
	if(refout == true)
		crc = bitsreverse(crc, width);
	crc ^= xorout;

	return (crc & ((2<<(width-1)) - 1));
}

static u8 calculate_crc(const u8 *upgradedata, u64 len)
{

	CrcInfoTypeS type =
	{
		.width = 8,
		.poly  = 0x31,
		.crcinit = 0,
		.xorout = 0,
		.refout = false,
		.refin = false,
	};
	u64 crc_size = FW_32K_SIZE - 1;
	u8 default_val = 0xff;

	type.crcinit = getcrc(type, upgradedata, len);

	crc_size -= len;
	while(crc_size--)
	{
		type.crcinit = getcrc(type, &default_val, 1);
	}
	return type.crcinit;
}


static void lt8911_i2c_enable(struct lt8711_dp_device *dp_device)
{
   lt8711_write_reg(dp_device, 0xff, 0xe0);
   lt8711_write_reg(dp_device, 0xee, 0x01);
}


static void lt8911_i2c_disable(struct lt8711_dp_device *dp_device)
{

   lt8711_write_reg(dp_device, 0xff, 0xe0);
   lt8711_write_reg(dp_device, 0xee, 0x00);
}

/*
static void lt8911_power_on(struct lt8711_dp_device *dp_device)
{
  gpiod_set_value(dp_device->power_gpio,1);

  msleep(10);
  dev_err(dev, "%s power on.\n", __func__);

}


static void lt8911_reset(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;

	gpiod_set_value(dp_device->gpio_reset,1);
	msleep(20);
	gpiod_set_value(dp_device->gpio_reset,0);
	msleep(20);
	gpiod_set_value(dp_device->gpio_reset,1);
	msleep(20);
	dev_err(dev, "%s reset chip.\n", __func__);
}
*/

static void lt8911_config_parameters(struct lt8711_dp_device *dp_device)
{

	lt8711_write_reg(dp_device, 0xff, 0xe0);
	lt8711_write_reg(dp_device, 0xee, 0x01);
	lt8711_write_reg(dp_device, 0x5e, 0xc1);
	lt8711_write_reg(dp_device, 0x58, 0x00);
	lt8711_write_reg(dp_device, 0x59, 0x50);
	lt8711_write_reg(dp_device, 0x5a, 0x10);
	lt8711_write_reg(dp_device, 0x5a, 0x00);
	lt8711_write_reg(dp_device, 0x58, 0x21);

}

static void lt8911_wren(struct lt8711_dp_device *dp_device)
{
   lt8711_write_reg(dp_device, 0xff, 0xe1);
   lt8711_write_reg(dp_device, 0x03, 0x3f);
   lt8711_write_reg(dp_device, 0x03, 0xff);
   lt8711_write_reg(dp_device, 0xff, 0xe0);
   lt8711_write_reg(dp_device, 0x5a, 0x04);
   lt8711_write_reg(dp_device, 0x5a, 0x00);
}

static void lt8911_wrdi(struct lt8711_dp_device *dp_device)
{
	lt8711_write_reg(dp_device, 0x5a, 0x08);
	lt8711_write_reg(dp_device, 0x5a, 0x00);
}

/*
static void lt8911_fifo_reset(struct lt8711_dp_device *dp_device)
{
   lt8711_write_reg(dp_device, 0xff, 0xe1);
   lt8711_write_reg(dp_device, 0x08, 0xaa);
   lt8711_write_reg(dp_device, 0x08, 0xea);
}
*/

static void lt8911_fifo_to_flash(struct lt8711_dp_device *dp_device, u64 addr)
{
	lt8711_write_reg(dp_device, 0x5b, ((addr & 0xff0000) >> 16));
	lt8711_write_reg(dp_device, 0x5c, ((addr & 0xff00) >> 8));
	lt8711_write_reg(dp_device, 0x5d, (addr & 0xff));
	lt8711_write_reg(dp_device, 0x5a, 0x10);
	lt8711_write_reg(dp_device, 0x5a, 0x00);
}

static void lt8911_i2c_to_fifo(struct lt8711_dp_device *dp_device)
{
	lt8711_write_reg(dp_device, 0x5e, 0xdf);
	lt8711_write_reg(dp_device, 0x5a, 0x20);
	lt8711_write_reg(dp_device, 0x5a, 0x00);
	lt8711_write_reg(dp_device, 0x58, 0x21);
}

static void lt8911_flash_to_fifo(struct lt8711_dp_device *dp_device, u64 addr)
{
	lt8711_write_reg(dp_device, 0x5e, 0x5f);
	lt8711_write_reg(dp_device, 0x5a, 0x20);
	lt8711_write_reg(dp_device, 0x5a, 0x00);
	lt8711_write_reg(dp_device, 0x5b, ((addr & 0xff0000) >> 16));
	lt8711_write_reg(dp_device, 0x5c, ((addr & 0xff00) >> 8));
	lt8711_write_reg(dp_device, 0x5d, (addr & 0xff));
	lt8711_write_reg(dp_device, 0x5a, 0x10);
	lt8711_write_reg(dp_device, 0x5a, 0x00);
}

static u8 flash_readflashstatusreg(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	u8 ucflashstatusreg = 0;

	lt8711_write_reg(dp_device, 0xff, 0xe1);//fifo_rst_n
	lt8711_write_reg(dp_device, 0x03, 0x3f);
	lt8711_write_reg(dp_device, 0x03, 0xff);

	lt8711_write_reg(dp_device, 0xff, 0xe0);
	lt8711_write_reg(dp_device, 0x5e, 0x40);
	lt8711_write_reg(dp_device, 0x56, 0x05);//opcode=read status register
	lt8711_write_reg(dp_device, 0x55, 0x25);//[5]=rd_reg_flag;[2]=opcode_en;[1:0]=cmd_len
	lt8711_write_reg(dp_device, 0x55, 0x01);
	lt8711_write_reg(dp_device, 0x58, 0x21);

	lt8711_read_reg(dp_device, 0x5f, &ucflashstatusreg);
	dev_err(dev, "%s ucflashstatusreg:%x\n", __func__, ucflashstatusreg);

	return ucflashstatusreg;
}

static void lt8911_block_erase(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	u32 i = 0;
	u8 ucflashstatuss = 0;
	u8 ucblocknum = 0x00;
	u32 ulflashaddr = 0x00;
	for(ucblocknum = 0;ucblocknum<1;ucblocknum++)
	{
		ulflashaddr = ucblocknum* 0x008000;
		lt8711_write_reg(dp_device, 0xff, 0xe0);
		lt8711_write_reg(dp_device, 0xee, 0x01);
		lt8711_write_reg(dp_device, 0x5a, 0x04);
		lt8711_write_reg(dp_device, 0x5a, 0x00);
		lt8711_write_reg(dp_device, 0x5b, ulflashaddr>>16);//set flash address[23:16]
		lt8711_write_reg(dp_device, 0x5c, ulflashaddr>>8);//set flash address[15:8]
		lt8711_write_reg(dp_device, 0x5d, ulflashaddr);//set flash address[7:0]
		lt8711_write_reg(dp_device, 0x5a, 0x01);
		lt8711_write_reg(dp_device, 0x5a, 0x00);
		msleep(100); //delay 100ms
		i=0;
		while(1)
		{
			ucflashstatuss = flash_readflashstatusreg(dp_device); //wait erase finish
			if((ucflashstatuss & 0x01) == 0)
			{
				break;
			}

			if(i > 50)
			{
				break;
			}
			i++;
			msleep(50); //delay 50ms
		}
	}

	dev_err(dev, "%s erase flash done.\n", __func__);
}

static void lt8911_load_fw_to_sram(struct lt8711_dp_device *dp_device)
{
	lt8711_write_reg(dp_device, 0xff, 0xe0);
	lt8711_write_reg(dp_device, 0xee, 0x01);
	lt8711_write_reg(dp_device, 0x68, 0x00);
	lt8711_write_reg(dp_device, 0x69, 0x00);
	lt8711_write_reg(dp_device, 0x6a, 0x00);
	lt8711_write_reg(dp_device, 0x65, 0x00);
	lt8711_write_reg(dp_device, 0x66, 0x7f);
	lt8711_write_reg(dp_device, 0x67, 0xff);
	lt8711_write_reg(dp_device, 0x6b, 0x00);
	lt8711_write_reg(dp_device, 0x6c, 0x00);
	lt8711_write_reg(dp_device, 0x60, 0x01);
	msleep(200);
	lt8711_write_reg(dp_device, 0x60, 0x00);
}

static int lt8911_write_data(struct lt8711_dp_device *dp_device, const u8 *pfile, u64 filesize, u64 addr)
{
    struct device *dev = dp_device->dev;
	int page = 0, num = 0, i = 0;

	page = (filesize % LT8911_PAGE_SIZE) ? ((filesize / LT8911_PAGE_SIZE) + 1) : (filesize / LT8911_PAGE_SIZE);


	dev_err(dev, "%s writing to fifo=%u pages, total size = %llu bytes\n", __func__, page, filesize);

	for(num=0; num<page; num++){

		lt8911_wren(dp_device);
		lt8911_i2c_to_fifo(dp_device);

	    for(i=0; i < LT8911_PAGE_SIZE; i++){

			if((num*LT8911_PAGE_SIZE + i) < filesize){

				lt8711_write_reg(dp_device, 0x59, *(pfile + (num * LT8911_PAGE_SIZE + i)));

			}else{
				lt8711_write_reg(dp_device, 0x59, 0xff);
			}
		}

	   lt8911_fifo_to_flash(dp_device, addr);
	   udelay(150);

	   addr += LT8911_PAGE_SIZE;

	}

	lt8911_wrdi(dp_device);

	return 0;
}

static void lt8911_upgrade_result(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	u8 dma_crc_result;

	lt8711_write_reg(dp_device, 0xff, 0xe0);
	lt8711_read_reg(dp_device, 0x23, &dma_crc_result);

	dev_err(dev, "%s dma load crc: 0x%02x\n",  __func__, dma_crc_result);

	if(dma_crc_result == dp_device->fw_crc_value)
	{
	  dev_err(dev, "%s firmware upgrade success.\n", __func__);

	}else{
	  dev_err(dev, "%s firmware upgrade failed.\n", __func__);
	}
}

static int lt8911_firmware_upgrade(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	int ret;

	lt8911_config_parameters(dp_device);

	lt8911_block_erase(dp_device);

	ret = lt8911_write_data(dp_device, dp_device->fw_buffer, FW_32K_SIZE, 0);
	if (ret < 0) {
		dev_err(dev, "%s failed to write firmware data: %d\n", __func__, ret);
		return ret;
	}


	dev_err(dev, "%s write data done.\n", __func__);

	return 0;
}


static int lt8911_prepare_firmware_data(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	int ret;

	ret = request_firmware(&dp_device->fw, fw_file, dp_device->dev);
	if (ret) {
	    dev_err(dev, "%s failed to load firmware: %d\n", __func__, ret);
	    return ret;
	}

	dev_err(dev, "%s firmware loaded, size: %zu bytes\n", __func__, dp_device->fw->size);

	if (dp_device->fw->size > FW_32K_SIZE - 1) {
	    dev_err(dev, "%s firmware size exceeds 32kb limit\n", __func__);
	    release_firmware(dp_device->fw);
	    return -1;
	}

	dp_device->fw_buffer = kzalloc(FW_32K_SIZE, GFP_KERNEL);
	if (!dp_device->fw_buffer) {
		  dev_err(dev, "%s failed to allocate firmware buffer\n", __func__);
		  release_firmware(dp_device->fw);
		  return -1;
	}

	memset(dp_device->fw_buffer, 0xff, FW_32K_SIZE);

	memcpy(dp_device->fw_buffer, dp_device->fw->data, dp_device->fw->size-4);

	//version
	dp_device->fw_buffer[FW_32K_SIZE - 5] = dp_device->fw->data[dp_device->fw->size-4];
	dp_device->fw_buffer[FW_32K_SIZE - 4] = dp_device->fw->data[dp_device->fw->size-3];
	dp_device->fw_buffer[FW_32K_SIZE - 3] = dp_device->fw->data[dp_device->fw->size-2];
	dp_device->fw_buffer[FW_32K_SIZE - 2] = dp_device->fw->data[dp_device->fw->size-1];

	//crc
	dp_device->fw_buffer[FW_32K_SIZE - 1] = calculate_crc(dp_device->fw->data, dp_device->fw->size);
	dp_device->fw_crc_value = dp_device->fw_buffer[FW_32K_SIZE - 1];
	dev_err(dev, "%s firmware data  crc=0x%02x\n", __func__, dp_device->fw_crc_value);

	return 0;

}

static int lt8911_upgrade_judgment(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	u8 read_flash_crc_value;
	u64 addr = FW_32K_SIZE-1;

	lt8911_config_parameters(dp_device);
	lt8911_flash_to_fifo(dp_device, addr);

	lt8711_write_reg(dp_device, 0x58, 0x21);
	lt8711_read_reg(dp_device, 0x5f, &read_flash_crc_value);
	udelay(150);
	lt8911_wrdi(dp_device);
	//lt8911_fifo_reset();
	dev_err(dev, "%s read flash firmware crc=0x%02x", __func__, read_flash_crc_value);
	if(dp_device->fw_crc_value == read_flash_crc_value){

		 return NOT_UPGRADE;
	}else{

		 return UPGRADE;
	}

}

//automatic upgrade during insmod driver
static void lt8911_upgrade_firmware(struct work_struct *work)
{
	struct lt8711_dp_device *dp_device =
			container_of(work, struct lt8711_dp_device, upgrade_fw_work.work);
	struct device *dev = dp_device->dev;
	int ret = 0;

	mutex_lock(&dp_device->ocm_lock);

	if (!dp_device->poweron_status) {
		dev_err(dev, "%s failed to prepare, poweron_status: %d\n", __func__, dp_device->poweron_status);
		lt8711_dp_enable_power(dp_device, true);
		dp_device->poweron_status = true;
		msleep(100);
	}

	lt8911_i2c_enable(dp_device);

	ret = lt8911_prepare_firmware_data(dp_device);
	if (ret < 0) {
	    dev_err(dev, "%s failed to prepare firmware data: %d\n", __func__, ret);
	    goto exit_loop;
	}

	ret = lt8911_upgrade_judgment(dp_device);
	if (ret == UPGRADE) {
		dev_err(dev, "%s crc is different, need update.\n", __func__);
		ret = lt8911_firmware_upgrade(dp_device);
		if (ret < 0) {
			dev_err(dev, "%s upgrade failure.\n", __func__);
			goto exit_loop;
		} else {
			lt8911_load_fw_to_sram(dp_device);
			lt8911_upgrade_result(dp_device);
			lt8911_wrdi(dp_device);
		}
	} else if(ret == NOT_UPGRADE) {
		dev_err(dev, "%s crc is same, not need update.\n", __func__);
	} else {
		dev_err(dev, "%s upgrade judgment failure.\n", __func__);
		goto exit_loop;
	}


exit_loop:
	lt8911_i2c_disable(dp_device);

	//lt8911_reset(dp_device);

	lt8711_dp_enable_power(dp_device, false);
	dp_device->poweron_status = false;

	if (!IS_ERR_OR_NULL(dp_device->fw_buffer))
		kfree(dp_device->fw_buffer);

	if (!IS_ERR_OR_NULL(dp_device->fw))
		release_firmware(dp_device->fw);
	//kthread_obj_b_stop = true;
	mutex_unlock(&dp_device->ocm_lock);

	return;
}


static int lt8711_dp_init_regulator(struct lt8711_dp_device *dp_device)
{
	struct device *dev = dp_device->dev;
	int ret;

    /*init vdd_3v3*/
	dp_device->regulator_vdd_3v3 = devm_regulator_get(dev, "vdd_3v3");
	if (IS_ERR_OR_NULL(dp_device->regulator_vdd_3v3)) {
		dev_err(dev, "XXX %s:failed to get vdd_3v3, no defined\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "%s:get vdd_3v3 regulator successed\n", __func__);

	ret = regulator_set_voltage(dp_device->regulator_vdd_3v3, 3300000, 3300000);
	if (ret) {
		dev_err(dev, "XXX %s:regulator_set_voltage vdd_3v3 failed, ret=%d\n", __func__, ret);
		return -EINVAL;
	}

	dev_info(dev, "%s:regulator_set_voltage vdd_3v3 3300000 successed\n", __func__);

/*
	ret = regulator_enable(dp_device->regulator_vdd_3v3);
	if (ret) {
		dev_err(dev, "XXX %s:regulator_enable vdd_3v3 failed, ret=%d\n", __func__, ret);
		return -EINVAL;
	}

	dev_info(dev, "%s:regulator_enable vdd_3v3 successed\n", __func__);
*/

    /*init vdd_1v25*/
	dp_device->regulator_vdd_1v25 = devm_regulator_get(dev, "vdd_1v25");
	if (IS_ERR_OR_NULL(dp_device->regulator_vdd_1v25)) {
		dev_err(dev, "XXX %s:failed to get vdd_1v25, no defined\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "%s:get vdd_1v25 regulator successed\n", __func__);

	ret = regulator_set_voltage(dp_device->regulator_vdd_1v25, 1260000, 1260000);
	if (ret) {
		dev_err(dev, "XXX %s:regulator_set_voltage vdd_1v25 failed, ret=%d\n", __func__, ret);
		return -EINVAL;
	}

	dev_info(dev, "%s:regulator_set_voltage vdd_1v25 1260000 successed\n", __func__);

/*
	ret = regulator_enable(dp_device->regulator_vdd_1v25);
	if (ret) {
		dev_err(dev, "XXX %s:regulator_enable vdd_1v25 failed, ret=%d\n", __func__, ret);
		return -EINVAL;
	}

	dev_info(dev, "%s:regulator_enable vdd_1v25 successed\n", __func__);
*/

	return 0;
}

static int lt8711_dp_parse_dt(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;

    /*init vdd_3v3*/
    dev_err(dev, "%s vdd_3v3_gpio\n", __func__);
	dp_device->vdd_3v3_gpio = devm_gpiod_get_optional(dp_device->dev, "vdd_3v3_enable", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(dp_device->vdd_3v3_gpio)) {
		dev_err(dev, "XXX failed to find vdd_3v3_gpio pin, no defined\n");
	}

    dev_err(dev, "%s vdd_1v25_gpio\n", __func__);
    dp_device->vdd_1v25_gpio = devm_gpiod_get_optional(dp_device->dev, "vdd_1v25_enable", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(dp_device->vdd_1v25_gpio)) {
		dev_err(dev, "XXX failed to find vdd_1v25_gpio pin, no defined\n");
	}

    dev_err(dev, "%s gpio_reset\n", __func__);
    dp_device->gpio_reset = devm_gpiod_get_optional(dp_device->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(dp_device->gpio_reset)) {
		dev_err(dev, "XXX failed to find gpio_reset pin, no defined\n");
	}

    dev_err(dev, "%s gpio_hdmi_5v\n", __func__);
    dp_device->gpio_hdmi_5v = devm_gpiod_get_optional(dp_device->dev, "hdmi_5v", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(dp_device->gpio_hdmi_5v)) {
		dev_err(dev, "XXX failed to find gpio_hdmi_5v pin, no defined\n");
	}

    dev_err(dev, "%s gpio_usb_hub_5v\n", __func__);
    dp_device->gpio_usb_hub_5v = devm_gpiod_get_optional(dp_device->dev, "usb_hub_5v", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(dp_device->gpio_usb_hub_5v)) {
		dev_err(dev, "XXX failed to find gpio_usb_hub_5v pin, no defined\n");
	}

    dev_err(dev, "%s gpio_hdmi_irq\n", __func__);
    dp_device->gpio_hdmi_irq = devm_gpiod_get_optional(dp_device->dev, "hdmi_irq", GPIOD_IN);
	if (IS_ERR_OR_NULL(dp_device->gpio_hdmi_irq)) {
		dev_err(dev, "XXX failed to find gpio_hdmi_irq pin, no defined\n");
	}

    dev_err(dev, "%s gpio_chip_irq\n", __func__);
    dp_device->gpio_chip_irq = devm_gpiod_get_optional(dp_device->dev, "chip_irq", GPIOD_IN);
	if (IS_ERR_OR_NULL(dp_device->gpio_chip_irq)) {
		dev_err(dev, "XXX failed to find gpio_chip_irq pin, no defined\n");
	}

	return 0;
}

static int lt8711_dp_enable_power(struct lt8711_dp_device *dp_device, bool enable)
{
    struct device *dev = dp_device->dev;
	int ret;

    dev_err(dev, "%s into\n", __func__);

    if (enable) {
        if (!IS_ERR_OR_NULL(dp_device->gpio_usb_hub_5v)) {
            gpiod_set_value(dp_device->gpio_usb_hub_5v, 1);
            dev_info(dev, "%s:gpiod_set_value gpio_usb_hub_5v to 1\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->gpio_hdmi_5v)) {
            gpiod_set_value(dp_device->gpio_hdmi_5v, 1);
            dev_info(dev, "%s:gpiod_set_value gpio_hdmi_5v to 1\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->regulator_vdd_3v3)) {
            ret = regulator_enable(dp_device->regulator_vdd_3v3);
            if (ret) {
                dev_err(dev, "XXX %s:regulator_enable vdd_3v3 failed, ret=%d\n", __func__, ret);
                return -EINVAL;
            }

            dev_info(dev, "%s:regulator_enable vdd_3v3 successed\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->vdd_3v3_gpio)) {
            gpiod_set_value(dp_device->vdd_3v3_gpio, 1);
            dev_info(dev, "%s:gpiod_set_value vdd_3v3_gpio to 1\n", __func__);
        }

        msleep(50);

        if (!IS_ERR_OR_NULL(dp_device->regulator_vdd_1v25)) {
            ret = regulator_enable(dp_device->regulator_vdd_1v25);
            if (ret) {
                dev_err(dev, "XXX %s:regulator_enable vdd_1v25 failed, ret=%d\n", __func__, ret);
                return -EINVAL;
            }

            dev_info(dev, "%s:enable vdd_1v25 successed\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->vdd_1v25_gpio)) {
            gpiod_set_value(dp_device->vdd_1v25_gpio, 1);
            dev_info(dev, "%s:gpiod_set_value vdd_1v25_gpio to 1\n", __func__);
        }

        msleep(50);

        if (!IS_ERR_OR_NULL(dp_device->gpio_reset)) {
            gpiod_set_value(dp_device->gpio_reset, 1);
            dev_info(dev, "%s:gpiod_set_value gpio_reset to 1\n", __func__);
        }
    } else {
        if (!IS_ERR_OR_NULL(dp_device->gpio_reset)) {
            gpiod_set_value(dp_device->gpio_reset, 0);
            dev_info(dev, "%s:gpiod_set_value gpio_reset to 0\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->vdd_1v25_gpio)) {
            gpiod_set_value(dp_device->vdd_1v25_gpio, 0);
            dev_info(dev, "%s:gpiod_set_value vdd_1v25_gpio to 0\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->regulator_vdd_1v25)) {
            ret = regulator_disable(dp_device->regulator_vdd_1v25);
            if (ret) {
                dev_err(dev, "XXX %s:regulator_disable vdd_1v25 failed, ret=%d\n", __func__, ret);
                return -EINVAL;
            }

            dev_info(dev, "%s:regulator_disable vdd_1v25 successed\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->vdd_3v3_gpio)) {
            gpiod_set_value(dp_device->vdd_3v3_gpio, 0);
            dev_info(dev, "%s:gpiod_set_value vdd_3v3_gpio to 0\n", __func__);
        }

        if (!IS_ERR_OR_NULL(dp_device->regulator_vdd_3v3)) {
            ret = regulator_disable(dp_device->regulator_vdd_3v3);
            if (ret) {
                dev_err(dev, "XXX %s:regulator_disable vdd_3v3 failed, ret=%d\n", __func__, ret);
                return -EINVAL;
            }

            dev_info(dev, "%s:regulator_disable vdd_3v3 successed\n", __func__);
        }
    }

	return 0;
}

static irqreturn_t hdmi_irq_handler(int irq, void *dev_id)
{
	struct lt8711_dp_device *dp_device = dev_id;

	dev_err(dp_device->dev, "--->>>hdmi_irq IRQ\n");

	return IRQ_HANDLED;
}

static irqreturn_t chip_irq_handler(int irq, void *dev_id)
{
	struct lt8711_dp_device *dp_device = dev_id;

	dev_err(dp_device->dev, "--->>>chip_irq IRQ\n");

	return IRQ_HANDLED;
}

static int lt8711_dp_init_irq(struct lt8711_dp_device *dp_device)
{
    struct device *dev = dp_device->dev;
	int ret;

    dev_err(dev, "%s into\n", __func__);

    dev_err(dev, "request handler for hdmi_irq IRQ, into\n");
	if (!IS_ERR_OR_NULL(dp_device->gpio_hdmi_irq))  {
		dp_device->hdmi_irq = gpiod_to_irq(dp_device->gpio_hdmi_irq);
		if (dp_device->hdmi_irq < 0) {
			dev_err(dev, "failed to get hdmi_irq IRQ\n");
			return dp_device->hdmi_irq;
		}

		ret = devm_request_threaded_irq(dev, dp_device->hdmi_irq, NULL,
						hdmi_irq_handler,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						"hdmi_irq", dp_device);
		if (ret < 0) {
			dev_err(dev, "failed to request handler for hdmi_irq IRQ\n");
			return ret;
		}

        dev_err(dev, "request handler for hdmi_irq IRQ, success\n");
	}

    dev_err(dev, "request handler for chip_irq IRQ, into\n");
    if (!IS_ERR_OR_NULL(dp_device->gpio_chip_irq))  {
		dp_device->chip_irq = gpiod_to_irq(dp_device->gpio_chip_irq);
		if (dp_device->chip_irq < 0) {
			dev_err(dev, "failed to get chip_irq IRQ\n");
			return dp_device->chip_irq;
		}

		ret = devm_request_threaded_irq(dev, dp_device->chip_irq, NULL,
						chip_irq_handler,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						"chip_irq", dp_device);
		if (ret < 0) {
			dev_err(dev, "failed to request handler for chip_irq IRQ\n");
			return ret;
		}

        dev_err(dev, "request handler for chip_irq IRQ, success\n");
	}

	return 0;
}

static int lt8711_get_charger_mode(struct lt8711_dp_device *dp_device)
{
	struct device_node *np;
	const char *cmd_line, *s;
	int ret = 0, boot_charging = 0;

	np = of_find_node_by_path("/chosen");
	if (!np) {
		dev_err(dp_device->dev, "%s read chosen failed\n", __func__);
		return boot_charging;
	}

	ret = of_property_read_string(np, "bootargs", &cmd_line);
	if (ret < 0) {
		dev_err(dp_device->dev, "%s read bootargs failed\n", __func__);
		return boot_charging;
	}

	s = strstr(cmd_line, "androidboot.mode=charger");
	if (s != NULL)
		boot_charging = 1;
	else {
		s = strstr(cmd_line, "sprdboot.mode=charger");
		if (s != NULL)
			boot_charging = 1;
		else
			boot_charging = 0;
	}

	dev_err(dp_device->dev, "%s read boot_charging is %d\n", __func__, boot_charging);

	return boot_charging;
}

static int lt8711_hdmi_power_en_set(const char *val, const void *arg)
{
	struct lt8711_dp_device *dp_device = (struct lt8711_dp_device *) arg;
	int set_val = 0;
	int ret = 0;

	if (IS_ERR_OR_NULL(dp_device)) {
		pr_err("%s dp_device is NULL\n", __func__);
		return 0;
	}

	if (dp_device->is_poweroff_mode) {
        dev_err(dp_device->dev, "%s is_poweroff_mode %d, Don't ctrl power\n", __func__, dp_device->is_poweroff_mode);
		return 0;
	}

	sscanf(val, "%d", &set_val);
	dev_err(dp_device->dev, "%s poweron_status %d, poweron set_val = %d\n", __func__, dp_device->poweron_status, set_val);

	mutex_lock(&dp_device->ocm_lock);

	if (!dp_device->poweron_status && set_val) {
		lt8711_dp_enable_power(dp_device, true);
		dp_device->poweron_status = true;
	} else if (dp_device->poweron_status && !set_val) {
		lt8711_dp_enable_power(dp_device, false);
		dp_device->poweron_status = false;
	}

	mutex_unlock(&dp_device->ocm_lock);

	return ret;
}

static int lt8711_hdmi_power_en_get(char *val, const void *arg)
{
	struct lt8711_dp_device *dp_device = (struct lt8711_dp_device *) arg;

	if (IS_ERR_OR_NULL(dp_device)) {
		pr_err("%s dp_device is NULL\n", __func__);
		return 0;
	}

	return snprintf(val, PAGE_SIZE, "%u", dp_device->poweron_status);
}

struct zte_misc_ops lt8711_hdmi_power_en_node = {
	.node_name = "hdmi_power_en",
	.set = lt8711_hdmi_power_en_set,
	.get = lt8711_hdmi_power_en_get,
	.free = NULL,
	.arg = NULL,
};

static int lt8711_charger_probe(struct i2c_client *client,
                  const struct i2c_device_id *id)
{
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct device *dev = &client->dev;
    struct lt8711_dp_device *dp_device = NULL;
    int boardid = zte_get_boardid();

    dev_err(dev, "lt8711_charger_probe into\n");

    if (!adapter) {
        pr_err("%s:XXX adapter is NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    dev_err(&client->dev, "%s into\n", __func__);

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
        return -ENODEV;
    }

    dp_device = devm_kzalloc(dev, sizeof(*dp_device), GFP_KERNEL);
    if (IS_ERR_OR_NULL(dp_device)) {
        dev_err(dev, "devm_kzalloc failed!!!\n");
        return -ENOMEM;
    }

    dp_device->client = client;
    dp_device->dev = dev;

    i2c_set_clientdata(client, dp_device);

    lt8711_dp_init_regulator(dp_device);

    lt8711_dp_parse_dt(dp_device);

    lt8711_dp_init_irq(dp_device);

    if (lt8711_get_charger_mode(dp_device)) {
        dp_device->is_poweroff_mode = true;
    } else {
        dp_device->is_poweroff_mode = false;
    }

    lt8711_dp_enable_power(dp_device, false);
    dp_device->poweron_status = false;

    if (boardid > 4) {
        client->addr = 0x29;
    } else {
        client->addr = 0x2d;
    }

    dev_err(dev, "boardid: %d, addr: 0x%02X\n", boardid, client->addr);

    i2c_set_clientdata(client, dp_device);

    mutex_init(&dp_device->ocm_lock);

	zte_misc_register_callback(&lt8711_hdmi_power_en_node, dp_device);

	dp_device->upgrade_fw_wq = create_singlethread_workqueue("lt8711_upgrade_firmware");
	INIT_DELAYED_WORK(&dp_device->upgrade_fw_work, lt8911_upgrade_firmware);

    queue_delayed_work(dp_device->upgrade_fw_wq, &dp_device->upgrade_fw_work, msecs_to_jiffies(1500));

    dev_err(dev, "%s successful.\n", __func__);

    return 0;
}

static void lt8711_charger_shutdown(struct i2c_client *client)
{
    struct lt8711_dp_device *dp_device = i2c_get_clientdata(client);

    if (!dp_device) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return;
    }

    dev_err(dp_device->dev, "%s successful.\n", __func__);

    lt8711_dp_enable_power(dp_device, false);

}

static int lt8711_charger_remove(struct i2c_client *client)
{
    struct lt8711_dp_device *dp_device = i2c_get_clientdata(client);

    if (!dp_device) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    dev_err(dp_device->dev, "%s successful.\n", __func__);

    lt8711_dp_enable_power(dp_device, false);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int lt8711_charger_suspend(struct device *dev)
{
    struct lt8711_dp_device *dp_device = dev_get_drvdata(dev);

    dp_device = dp_device ? dp_device : NULL;

    return 0;
}

static int lt8711_charger_resume(struct device *dev)
{
    struct lt8711_dp_device *dp_device = dev_get_drvdata(dev);

    dp_device = dp_device ? dp_device : NULL;

    return 0;
}
#endif

static const struct dev_pm_ops lt8711_charger_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(lt8711_charger_suspend,
                lt8711_charger_resume)
};

static const struct i2c_device_id lt8711_i2c_id[] = {
    {"lt8711_dp", 0},
    {}
};

static const struct of_device_id lt8711_charger_of_match[] = {
    { .compatible = "lontium,lt8711_dp", },
    { }
};

MODULE_DEVICE_TABLE(of, lt8711_charger_of_match);

static struct i2c_driver lt8711_charger_driver = {
    .driver = {
        .name = "lt8711",
        .of_match_table = lt8711_charger_of_match,
        .pm = &lt8711_charger_pm_ops,
    },
    .probe = lt8711_charger_probe,
    .shutdown = lt8711_charger_shutdown,
    .remove = lt8711_charger_remove,
    .id_table = lt8711_i2c_id,
};

module_i2c_driver(lt8711_charger_driver);

MODULE_DESCRIPTION("LT8711 DP Driver");
MODULE_LICENSE("GPL v2");
