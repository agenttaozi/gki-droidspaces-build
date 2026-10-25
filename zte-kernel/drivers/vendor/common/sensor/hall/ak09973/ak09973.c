/* Started by AICoder, pid:2f1fay831fsa7c914d3d1afa60e2f8051250df43 */
/*
* ak09973.c
*
* Version:
*
* Copyright (c) 2021 ZTE
*
* Author:
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *****************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>
#include <linux/fcntl.h>
#include <linux/version.h>
#include <linux/pm_wakeup.h>

#define WAKELOCK_HOLD_TIME 1000 /* in ms*/
#define STATUS_NEAR        0
#define STATUS_FAR         1

/* Started by AICoder, pid:l358ap58c3j34c3140600910607b26023489f6ca */
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

static struct timer_list ak09973_report_timer;
static struct work_struct ak09973_report_work;
/* Ended by AICoder, pid:l358ap58c3j34c3140600910607b26023489f6ca */


static struct ak09973_data *pdata;
static struct ak09973_data *pdata2;
static unsigned int ak09973_report_interval_ms = 2;
static int calibrate_statue = 0;

/* add zte boardtest interface start */
static uint8_t is_three_hall_exist = 0;
#ifdef CONFIG_VENDOR_ZTE_MISC_COMMON
#include <vendor/common/zte_misc.h>

int is_three_hall_exist_get(char *val, const void *arg)
{
	return snprintf(val, PAGE_SIZE, "%d", is_three_hall_exist);
}

static struct zte_misc_ops is_three_hall_exist_node = {
	.node_name = "is_three_hall_exist",
	.set = NULL,
	.get = is_three_hall_exist_get,
	.free = NULL,
	.arg = NULL,
};
#endif
/* zte boardtest interface end */

#define akdbgprt printk

/*
 * Register definitions, as well as various shifts and masks to get at the
 * individual fields of the registers.
 */
// REGISTER MAP
#define AK09973_REG_WIA                 0x00
#define AK09973_DEVICE_ID               0x48C1

#define AK09973_REG_WORD_ST             0x10
#define AK09973_REG_WORD_ST_X           0x11
#define AK09973_REG_WORD_ST_Y           0x12
#define AK09973_REG_WORD_ST_Y_X         0x13
#define AK09973_REG_WORD_ST_Z           0x14
#define AK09973_REG_WORD_ST_Z_X         0x15
#define AK09973_REG_WORD_ST_Z_Y         0x16
#define AK09973_REG_WORD_ST_Z_Y_X       0x17
#define AK09973_REG_BYTE_ST_V           0x18
#define AK09973_REG_BYTE_ST_X           0x19
#define AK09973_REG_BYTE_ST_Y           0x1A
#define AK09973_REG_BYTE_ST_Y_X         0x1B
#define AK09973_REG_BYTE_ST_Z           0x1C
#define AK09973_REG_BYTE_ST_Z_X         0x1D
#define AK09973_REG_BYTE_ST_Z_Y         0x1E
#define AK09973_REG_BYTE_ST_Z_Y_X       0x1F
#define AK09973_REG_CNTL1               0x20
#define AK09973_REG_CNTL2               0x21
#define AK09973_REG_THX                 0x22
#define AK09973_REG_THY                 0x23
#define AK09973_REG_THZ                 0x24
#define AK09973_REG_THV                 0x25
#define AK09973_REG_SRST                0x30
#define AK09973_MAX_REGS                AK09973_REG_SRST

#define AK09973_MEASUREMENT_WAIT_TIME   2

#define AK09973_CNTL2_RESUME            0xE0

static int modeBitTable[] = {
    0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE, 0x10
};
// 0 : Power Down 
// 1 : Measurement Mode 1
// 2 : Measurement Mode 2
// 3 : Measurement Mode 3
// 4 : Measurement Mode 4
// 5 : Measurement Mode 5
// 6 : Measurement Mode 6
// 7 : Measurement Mode 7

static int measurementFreqTable[] = {
    0,     5,    10,    20,    50,   100,    500,    1000,   2000
//	0.0,  5Hz,  10Hz,  20Hz,  50Hz,  100Hz,  500Hz,  1000Hz, 2000
};

enum {
    AK09973_MSRNO_WORD_ST = 0,
    AK09973_MSRNO_WORD_ST_X,     // 1
    AK09973_MSRNO_WORD_ST_Y,     // 2
    AK09973_MSRNO_WORD_ST_Y_X,   // 3
    AK09973_MSRNO_WORD_ST_Z,     // 4
    AK09973_MSRNO_WORD_ST_Z_X,   // 5
    AK09973_MSRNO_WORD_ST_Z_Y,   // 6
    AK09973_MSRNO_WORD_ST_Z_Y_X, // 7
    AK09973_MSRNO_WORD_ST_V,     // 8
    AK09973_MSRNO_BYTE_ST_X,     // 9
    AK09973_MSRNO_BYTE_ST_Y,     // 10
    AK09973_MSRNO_BYTE_ST_Y_X,   // 11
    AK09973_MSRNO_BYTE_ST_Z,     // 12
    AK09973_MSRNO_BYTE_ST_Z_X,   // 13
    AK09973_MSRNO_BYTE_ST_Z_Y,   // 14
    AK09973_MSRNO_BYTE_ST_Z_Y_X, // 15
};

static int msrDataBytesTable[] = {
    1,  // AK09973_REG_WORD_ST
    3,  // AK09973_REG_WORD_ST_X
    3,  // AK09973_REG_WORD_ST_Y
    5,  // AK09973_REG_WORD_ST_Y_X
    3,  // AK09973_REG_WORD_ST_Z
    5,  // AK09973_REG_WORD_ST_Z_X
    5,  // AK09973_REG_WORD_ST_Z_Y
    7,  // AK09973_REG_WORD_ST_Z_Y_X
    5,  // AK09973_REG_WORD_ST_V
    2,  // AK09973_REG_BYTE_ST_X
    2,  // AK09973_REG_BYTE_ST_Y
    3,  // AK09973_REG_BYTE_ST_Y_X
    2,  // AK09973_REG_BYTE_ST_Z
    3,  // AK09973_REG_BYTE_ST_Z_X
    3,  // AK09973_REG_BYTE_ST_Z_Y
    4,  // AK09973_REG_BYTE_ST_Z_Y_X
};

/*
 * Per-instance context data for the device.
 */
struct ak09973_data {
    struct i2c_client *client;
    struct device *dev;
    struct input_dev *input;
    struct wakeup_source *hall_wakelock;
    struct delayed_work irq_dworker;

    int  int_gpio; 
    int  irq;
	int num;
	
    int status;

    int bopv;
    int brpv;

    u8  mode;
    s16 numMode;

    u8 msrNo;

    u8 DRDYENbit;
    u8 SWXENbit;
    u8 SWYENbit;
    u8 SWZENbit;
    u8 SWVENbit;
    u8 ERRENbit;

    u8 POLXbit;
    u8 POLYbit;
    u8 POLZbit;
    u8 POLVbit;

    u8 SDRbit;
    u8 SMRbit;

    u16 BOPVbits;
    u16 BRPVbits;
    u16 BOPV2bits;
    u16 BRPV2bits;
};

// ****************** Register R/W  ************************************
static int ak09973_i2c_read(struct i2c_client *client, u8 address)
{
    int ret;
    ret = i2c_smbus_read_byte_data(client, address);
    if (ret < 0) {
        dev_err(&client->dev, "[AK09973] I2C Read Error\n");
    }
    return ret;
}

static int ak09973_i2c_reads(struct i2c_client *client, u8 *reg,
                    int reglen, u8 *rdata, int datalen)
{
    struct i2c_msg xfer[2];
    int ret;

    /* Write register */
    xfer[0].addr = client->addr;
    xfer[0].flags = 0;
    xfer[0].len = reglen;
    xfer[0].buf = reg;

    /* Read data */
    xfer[1].addr = client->addr;
    xfer[1].flags = I2C_M_RD;
    xfer[1].len = datalen;
    xfer[1].buf = rdata;

    ret = i2c_transfer(client->adapter, xfer, 2);
    if (ret == 2)
        return 0;
    else if (ret < 0)
        return ret;
    else
        return -EIO;
}

static int ak09973_i2c_read8(struct i2c_client *client,
                        u8 address, int rLen, u8 *rdata)
{
    u8  tx[1];
    int i, ret;

    if (rLen > 8) {
        dev_err(&client->dev, "[AK09973] %s Read Word Length Error %d\n", __func__, rLen);
        return -EINVAL;
    }

    tx[0] = address;
    ret = ak09973_i2c_reads(client, tx, 1, rdata, rLen);
    if (ret < 0) {
        dev_err(&client->dev, "[AK09973] I2C Read Error\n");
        for (i = 0 ; i < rLen ; i ++) {
            rdata[i] = 0;
        }
    }
    return ret;
}

static int ak09973_i2c_read16(struct i2c_client *client,
                                u8 address, int wordLen, u16 *rdata)
{
    u8  tx[1];
    u8  rx[8];
    int i, ret;

    if (wordLen > 4) {
        dev_err(&client->dev, "[AK09973] %s Read Word Length Error %d\n", __func__, wordLen);
        return -EINVAL;
    }

    tx[0] = address;
    ret = ak09973_i2c_reads(client, tx, 1, rx, (2 * wordLen));
    if (ret < 0) {
        dev_err(&client->dev, "[AK09973] I2C Read Error %d\n", ret);
    } else {
        //akdbgprt("[AK09973] %s rx[0]=%02X rx[1]=%02X\n",__func__, (int)rx[0], (int)rx[1]);
        for (i = 0 ; i < wordLen ; i ++) {
            rdata[i] = ((u16)rx[2*i] << 8) + (u16)rx[2*i + 1];
        }
    }
    return ret;
}

// for read measurement data
// 1st  read data : Stat 1byte
// 2nd- read data : Stat 2byte
static int ak09973_i2c_read8_16(struct i2c_client *client,
                            u8 address, int wordLen, u16 *rdata)
{
    u8  tx[1];
    u8  rx[8];
    int i, ret;

    if ((wordLen < 1) || (wordLen > 4)) {
        dev_err(&client->dev, "[AK09973] %s Read Word Length Error %d\n", __func__, wordLen);
        return -EINVAL;
    }

    tx[0] = address;
    ret = ak09973_i2c_reads(client, tx, 1, rx, ((2 * wordLen) - 1));
    if (ret < 0) {
        dev_err(&client->dev, "[AK09973] I2C Read Error\n");
    } else {
        rdata[0] = (u16)rx[0]; 
        for (i = 1; i < wordLen; i++) {
            rdata[i] = ((u16)rx[2*i-1] << 8) + (u16)rx[2*i];
        }
    }

    return ret;
}

static int ak09973_i2c_writes(struct i2c_client *client,
                            const u8 *tx, size_t wlen)
{
    int ret;

    ret = i2c_master_send(client, tx, wlen);
    if(ret != wlen) {
        pr_err("%s: comm error, ret %d, wlen %d\n", __func__, ret, (int)wlen);
    }

    return ret;
}

static int ak09973_i2c_write8(struct i2c_client *client,
                                            u8 address, u8 value)
{
    int ret;

    ret = i2c_smbus_write_byte_data(client, address, value);
    if (ret < 0) {
        pr_err("%s: comm error, ret= %d\n", __func__, ret);
        //dbg_show(__func__);
    }

    return ret;
}

static s32 ak09973_i2c_write16(struct i2c_client *client, 
                u8 address, int valueNum, u16 value1, u16 value2)
{
    u8  tx[5];
    s32 ret;
    int n;

    if (( valueNum != 1 ) &&  ( valueNum != 2 )) {
        pr_err("%s: valueNum error, valueNum= %d\n", __func__, valueNum);
        return -EINVAL;
    }

    n = 0;
    tx[n++] = address;
    tx[n++] = (u8)((0xFF00 & value1) >> 8);
    tx[n++] = (u8)(0xFF & value1);

    if (valueNum == 2) {
        tx[n++] = (u8)((0xFF00 & value2) >> 8);
        tx[n++] = (u8)(0xFF & value2);
    }

    ret = ak09973_i2c_writes(client, tx, n);
    return ret;
}

static ssize_t ak09973_reg_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);
    u8  cntl2[1], stat[1];
    u16 rdata[2];
    s16 msrData[12];
    int i, j, n;
    int ret;
    ssize_t len = 0;

    ret = ak09973_i2c_read8(akm->client, AK09973_REG_WORD_ST, 1, stat);
    if (ret < 0)
        return ret;
    len += snprintf(buf + len, PAGE_SIZE - len, "ST(10H)=%02XH\n", (int)stat[0]);

    ak09973_i2c_read16(akm->client, AK09973_REG_CNTL1, 1, rdata);
    len += snprintf(buf + len, PAGE_SIZE - len, "CNTL1(20H)=%04XH\n", (int)rdata[0]);

    ak09973_i2c_read8(akm->client, AK09973_REG_CNTL2, 1, cntl2);
    len += snprintf(buf + len, PAGE_SIZE - len, "CNTL2(21H)=%02XH\n", (int)cntl2[0]);

    n = 0;
    for (i = AK09973_REG_THX; i <= AK09973_REG_THV; i++) {
        ak09973_i2c_read16(akm->client, i, 2, rdata);
        for (j = 0; j < 2; j++) {
            if (rdata[j] < 32768) {
                msrData[n] = rdata[j];
            } else {
                msrData[n] = -((s16)(~rdata[j]) + 1);
            }
            n++;
        }
    }

    len += snprintf(buf + len, PAGE_SIZE - len, "(25H) BOPV=%d, BRPV=%d\n", (int)msrData[6], (int)msrData[7]);

    return len;
}

static ssize_t ak09973_reg_store(struct device *dev,
        struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);
    char *ptr_data = (char *)buf;
    char *p;
    int  pt_count = 0;
    long val[3];
    unsigned char wdata[10];
    int n;
    int ret;

    dev_dbg(dev, "[AK09973] %s called: '%s'(%zu)", __func__, buf, count);

    if (buf == NULL)
        return -EINVAL;

    if (count == 0)
        return 0;

    for (n = 0 ; n < 3 ; n ++) {
        val[n] = 0;
    }
    while ((p = strsep(&ptr_data, ","))) {
        if (!*p)
            break;

        if (pt_count >= 3)
            break;

        if ((pt_count != 0) && (val[0] > 0x21)) {
            val[pt_count] = simple_strtol(p, NULL, 10);
        } else {
            val[pt_count] = simple_strtol(p, NULL, 16);
        }

        pt_count++;
    }

    if ((pt_count < 1) ||
            (((val[0] >= 0x20) && (val[0] != 0x21)) && (pt_count < 3))) {
        dev_err(dev, "[AK09973] %s pt_count = %d, Error\n", __func__, pt_count);
        return -EINVAL;
    }

    n = 0;
    wdata[n++] = val[0];
    ret = 0;

    if ((val[0] >= 0x20) && (pt_count > 1)) {
        switch(val[0]) {
            case 0x20:
                wdata[n++] = (unsigned char)(0xFF & val[1]);
                wdata[n++] = (unsigned char)(0xFF & val[2]);
                break;
            case 0x21:
                wdata[n++] = (unsigned char)(0xFF & val[1]);
                break;
            case 0x22:
            case 0x23:
            case 0x24:
            case 0x25:
                if (val[1] < 0) val[1] +=  65536;
                 wdata[n++] = (unsigned char)((0xFF00 & val[1]) >> 8);
                wdata[n++] = (unsigned char)(0xFF & val[1]);
                if (val[2] < 0) val[2] +=  65536;
                 wdata[n++] = (unsigned char)((0xFF00 & val[2]) >> 8);
                wdata[n++] = (unsigned char)(0xFF & val[2]);
                break;
            default:
                dev_err(dev, "[AK09973] %s Address Error\n", __func__);
                return -EINVAL;
                break;
        }
        ret = ak09973_i2c_writes(akm->client, wdata, n);
    } else if ((val[0] >= AK09973_REG_WORD_ST) 
                            && (val[0] <= AK09973_REG_BYTE_ST_Z_Y_X)) {
        akm->msrNo = val[0] - AK09973_REG_WORD_ST;
        akdbgprt("[AK09973] %smeasurement No = %d, address=%XH\n",
                    __func__, akm->msrNo, (int)val[0]);
    }

    if (ret < 0)
        return ret;

    return count;
}

static int ak09973_write_mode(struct ak09973_data *akm, int modeBit)
{
    int mode;
    int ret;

    mode = (akm->SMRbit << 6) + (akm->SDRbit << 5) + modeBit;
    akdbgprt("[AK09973] %s Reg[%d] = %d mode = %d \n", __func__, AK09973_REG_CNTL2, mode, akm->mode);
    ret = ak09973_i2c_write8(akm->client, AK09973_REG_CNTL2, mode);
    return ret;
}

/* Started by AICoder, pid:y358ax58c3e34c3140600910607b26023486f6ca */
static void ak09973_start_report_timer(struct ak09973_data *akm)
{
    del_timer_sync(&ak09973_report_timer);
    add_timer(&ak09973_report_timer);
}
/* Ended by AICoder, pid:y358ax58c3e34c3140600910607b26023486f6ca */

static int ak09973_set_mode(struct ak09973_data *akm, int freq)
{
    int n;

    if (freq < 0) {
        dev_err(&akm->client->dev, "[AK09973] %s freq = %d Error\n", __func__, freq);
        return -EINVAL;
    }

    n = 0;

    while ((freq > measurementFreqTable[n]) && (n < (akm->numMode - 1))) {
        n++;
    }

    akm->mode = n;
    ak09973_write_mode(akm, modeBitTable[akm->mode]);

	ak09973_report_interval_ms = (unsigned int) 1000/ freq;
	akdbgprt("[AK09973] %s freq = %d mode = %d, ak09973_report_interval_ms = %d\n", __func__, freq, akm->mode, ak09973_report_interval_ms);

    return 0;
}

static ssize_t ak09973_mode_store(struct device *dev,
        struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);
    int freq;

    if (sscanf(buf, "%d", &freq) != 1) {
        pr_err("%s - the number of data are wrong\n", __func__);
        return -EINVAL;
    }

    dev_dbg(dev, "[AK09973] %s freq=%d enter.\n", __func__, freq);
    ak09973_set_mode(akm, freq); // sample_frequency set

    return count;
}

static ssize_t ak09973_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);

    return snprintf(buf, PAGE_SIZE, "mode=%d freq=%d\n", akm->mode, measurementFreqTable[akm->mode]);
}

static ssize_t ak09973_raw_data_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);
    ssize_t len = 0;
    int msrDataAddr, index, v_value;
    u16 readValue[4], wordLen;

    readValue[0] = ak09973_i2c_read(akm->client, AK09973_REG_CNTL2);
    len += snprintf(buf + len, PAGE_SIZE - len,
            "[AK09973] freq=%d mode=%d MODEbits=%X\n", measurementFreqTable[akm->mode], akm->mode, readValue[0]);

    if (akm->mode == 0) {
        ak09973_write_mode(akm, 1); // single measurement mode
        msleep(AK09973_MEASUREMENT_WAIT_TIME);
    }

    index = AK09973_MSRNO_WORD_ST_Z_Y_X;
    msrDataAddr = index + AK09973_REG_WORD_ST;
    wordLen = (msrDataBytesTable[index] + 1) / 2;
    ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
    len += snprintf(buf + len, PAGE_SIZE - len,
            "[AK09973] ST value 0x%x\n", readValue[0]);
    len += snprintf(buf + len, PAGE_SIZE - len,
            "[AK09973] X Y Z value %d %d %d\n", (s16)readValue[3], (s16)readValue[2], (s16)readValue[1]);

    index = AK09973_MSRNO_WORD_ST_V;
    msrDataAddr = index + AK09973_REG_WORD_ST;
    wordLen = (msrDataBytesTable[index] + 1) / 2;
    ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);
    v_value = readValue[1];
    v_value = (v_value << 16) | readValue[2];
    len += snprintf(buf + len, PAGE_SIZE - len, "[AK09973] V value %d\n", v_value);

    if (akm->mode == 0) {
        ak09973_write_mode(akm, 0);
    }

    return len;
}

static ssize_t ak09973_status_show(struct device *dev,
                    struct device_attribute *attr, char *buf)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);

    return snprintf(buf, PAGE_SIZE, "%d\n", akm->status);
}

static DEVICE_ATTR(reg, 0664, ak09973_reg_show, ak09973_reg_store);
static DEVICE_ATTR(mode, 0644, ak09973_mode_show, ak09973_mode_store);
static DEVICE_ATTR(raw_data, 0664, ak09973_raw_data_show, NULL);
static DEVICE_ATTR(status, 0664, ak09973_status_show, NULL);

static struct attribute *ak09973_attributes[] = {
    &dev_attr_reg.attr,
    &dev_attr_mode.attr,
    &dev_attr_raw_data.attr,
    &dev_attr_status.attr,
    NULL
};

static struct attribute_group ak09973_attribute_group = {
    .attrs = ak09973_attributes
};

static int ak09973_check_chip_id(struct i2c_client *client)
{
    u16 deviceID[1];
    s32 err;

    err = ak09973_i2c_read16(client, AK09973_REG_WIA, 1, deviceID);
    if (err < 0) {
        pr_err("[AK09973]i2c read failure, AK09973_REG_WIA err=%d\n", err);
        return -EIO;
    }

    if (deviceID[0] != AK09973_DEVICE_ID) {
        pr_err("[AK09973] Device ID Error, Device ID =0x%X\n", deviceID[0]);
        return -EINVAL;
    }
	
	is_three_hall_exist = 1;

    printk("[AK09973] Device ID = %X. AK09973 check success!\n", deviceID[0]);
    return 0;
}

static int ak09973_setup(struct i2c_client *client)
{
    struct ak09973_data *akm = i2c_get_clientdata(client);
    u8  value;
    u16 value1;

    value1 = (u16)(akm->POLXbit + (akm->POLYbit << 1) 
                    + (akm->POLZbit << 2) + (akm->POLVbit << 3));
    value1 <<= 8;
    value1 += ((u16)(akm->DRDYENbit + (akm->SWXENbit << 1) 
                + (akm->SWYENbit << 2) + (akm->SWZENbit << 3) 
                + (akm->SWVENbit << 4) + (akm->ERRENbit << 5))); 
    ak09973_i2c_write16(client, AK09973_REG_CNTL1, 1, value1, 0);

    ak09973_i2c_write16(client, AK09973_REG_THV, 2,
                                akm->BOPVbits, akm->BRPVbits);

    if (akm->mode < ARRAY_SIZE(modeBitTable)) {
        value = modeBitTable[akm->mode];
    } else {
        value = modeBitTable[1];
    }
    ak09973_write_mode(akm, value);

    return 0;
}

static int ak09973_parse_dt(struct ak09973_data  *ak09973)
{
    u32 buf[8];
    struct device *dev;
    struct device_node *np;
    int ret;

    dev = &(ak09973->client->dev);
    np = dev->of_node;
    if (!np) {
        return -EINVAL;
    }

    ak09973->int_gpio = of_get_named_gpio(np, "ak09973,irq-gpio", 0);
    if (ak09973->int_gpio < 0) {
        dev_err(&ak09973->client->dev, "[AK09973] int gpio not found!\n");
        return ak09973->int_gpio;
    }
	
	if( ak09973->int_gpio == 172){
		ak09973->num = 0;
	}else {
		ak09973->num = 1;
	}

    ret = of_property_read_u32_array(np, "ak09973,mode", buf, 1);
    if (ret < 0) {
        ak09973->mode = 1;
    } else {
        ak09973->mode = buf[0];
        if (buf[0] > ak09973->numMode) {
            ak09973->mode = ak09973->numMode;
        }
    }

    ret = of_property_read_u32_array(np, "ak09973,DRDY_event", buf, 1);
    if (ret < 0) {
        ak09973->DRDYENbit = 1;
    } else {
        ak09973->DRDYENbit = buf[0];
    }

    ret = of_property_read_u32_array(np, "ak09973,ERR_event", buf, 1);
    if (ret < 0) {
        ak09973->ERRENbit = 1;
    } else {
        ak09973->ERRENbit = buf[0];
    }

    ret = of_property_read_u32_array(np, "ak09973,POL_setting", buf, 4);
    if (ret < 0) {
        ak09973->POLXbit = 0;
        ak09973->POLYbit = 0;
        ak09973->POLZbit = 0;
        ak09973->POLVbit = 0;
    } else {
        ak09973->POLXbit = buf[0];
        ak09973->POLYbit = buf[1];
        ak09973->POLZbit = buf[2];
        ak09973->POLVbit = buf[3];
    }

    ret = of_property_read_u32_array(np, "ak09973,SDR_setting", buf, 1);
    if (ret < 0) {
        ak09973->SDRbit = 0;
    } else {
        ak09973->SDRbit = buf[0];
    }

    ret = of_property_read_u32_array(np, "ak09973,SMR_setting", buf, 1);
    if (ret < 0) {
        ak09973->SMRbit = 0;
    } else {
        ak09973->SMRbit = buf[0];
    }

    ret = of_property_read_u32_array(np, "ak09973,SW_event", buf, 4);
    if (ret < 0) {
        ak09973->SWXENbit = 0;
        ak09973->SWYENbit = 0;
        ak09973->SWZENbit = 0;
        ak09973->SWVENbit = 0;
    } else {
        ak09973->SWXENbit = buf[0];
        ak09973->SWYENbit = buf[1];
        ak09973->SWZENbit = buf[2];
        ak09973->SWVENbit = buf[3];
    }

    ak09973->BOPVbits = 0xF20; //  bopv = 0xF20*0xF20 = 15 000000
    ak09973->BRPVbits = 0x8BC; //  brpv = 0x8BC*0x8BC = 5 000000
    ak09973->BOPV2bits = 0x171C; //  bopv2 = 0x171C*0x171C = 35 000000
    ak09973->BRPV2bits = 0xF20; //  brpv2 = 0xF20*0xF20=15 000000

    ak09973->bopv = ak09973->BOPVbits * ak09973->BOPVbits;
    ak09973->brpv = ak09973->BRPVbits * ak09973->BRPVbits;

    return 0;
}

static int ak09973_input_init(struct ak09973_data  *ak09973)
{
    int err = -1;

    ak09973->input = input_allocate_device();
    if (ak09973->input == NULL) {
        err = -1;
        goto err;
    }

    ak09973->input->name = "crdsensor";
    __set_bit(EV_KEY, ak09973->input->evbit);
    __set_bit(EV_SYN, ak09973->input->evbit);
    __set_bit(KEY_F1, ak09973->input->keybit);
    input_set_capability(ak09973->input, EV_ABS, ABS_X);
    input_set_capability(ak09973->input, EV_ABS, ABS_Z);
	
	input_set_capability(ak09973->input, EV_ABS, ABS_RY);

    err = input_register_device(ak09973->input);
    if (err) {
        dev_err(&ak09973->client->dev, "%s: failed to register input device: %s\n", __func__);
        input_free_device(ak09973->input);
        err = -1;
        goto err;
    }

    err = 0;

err:
    return err;
}

/* Started by AICoder, pid:z3d541b2a5q7a6514c840b3930473e657b929759 */
static void ak09973_report_data(struct work_struct *work)
{
    struct ak09973_data *akm = pdata;
	struct ak09973_data *akm2 = pdata2;
    int wordLen, msrDataAddr, index;
    u16 readValue[4], stat, readValue2[4];
	
    index = AK09973_MSRNO_WORD_ST_Z_Y_X;
    msrDataAddr = index + AK09973_REG_WORD_ST;
    wordLen = (msrDataBytesTable[index] + 1) / 2;
	if(akm){
		ak09973_i2c_read8_16(akm->client, msrDataAddr, wordLen, readValue);

		stat = readValue[0];
		if ((stat & 0x20) == 0x20) {
			dev_err(&akm->client->dev, "[AK09973] magnetic sensor overflow!\n");
			return;
		}
	}else {
		akdbgprt("[AK09973] %s akm0 is null \n", __func__);
	}
	if(akm2){
		ak09973_i2c_read8_16(akm2->client, msrDataAddr, wordLen, readValue2);

		stat = readValue2[0];
		if ((stat & 0x20) == 0x20) {
			dev_err(&akm2->client->dev, "[AK09973] magnetic sensor overflow!\n");
			return;
		}
	}else{
		akdbgprt("[AK09973] %s akm2 is null \n", __func__);
	}

	//akdbgprt("[AK09973] %s X1 Y1 Z1 X2 Y2 Z2 value  %d %d %d %d %d %d\n", __func__, 
	//	(s16)readValue[3], (s16)readValue[2], (s16)readValue[1],(s16)readValue2[3], (s16)readValue2[2], (s16)readValue2[1]);

	input_report_abs(akm->input, ABS_X, (s16)((s16)readValue2[3] - (s16)readValue[3]));
	input_report_abs(akm->input, ABS_Z, (s16)((s16)readValue2[1] - (s16)readValue[1]));
	
	input_sync(akm->input);
}


/* Started by AICoder, pid:h358aw58c3834c3140600910607b26123484f6ca */
static void ak09973_report_timer_callback(struct timer_list *t)
{
    schedule_work(&ak09973_report_work);
     mod_timer(&ak09973_report_timer, jiffies + msecs_to_jiffies(ak09973_report_interval_ms));
}

static int ak09973_init_report_timer()
{
    timer_setup(&ak09973_report_timer, ak09973_report_timer_callback, 0);
    INIT_WORK(&ak09973_report_work, ak09973_report_data);
    return 0;
}
/* Ended by AICoder, pid:h358aw58c3834c3140600910607b26123484f6ca */

static void ak09973_stop_report_timer(struct ak09973_data *akm)
{
    del_timer_sync(&ak09973_report_timer);
}
/* Ended by AICoder, pid:z3d541b2a5q7a6514c840b3930473e657b929759 */


static struct class crd_sensor_class = {
	.name = "crdsensor",
	.owner = THIS_MODULE,
};

static ssize_t delay_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	akdbgprt("[AK09973] %s enter\n", __func__);
	return snprintf(buf, 8, "%d\n", 200);
}

static ssize_t delay_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	akdbgprt("[AK09973] %s enter\n", __func__);
	return count;
}

static CLASS_ATTR_RW(delay);

static ssize_t enable_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
    struct ak09973_data *akm = pdata;
	struct ak09973_data *akm2 = pdata2;
	int en;

	if (!akm) {
		akdbgprt("[AK09973] %s akm is null \n", __func__);
		return count;
	}
	
	if (!akm2) {
		akdbgprt("[AK09973] %s akm2 is null \n", __func__);
		return count;
	}

	if (sscanf(buf, "%d", &en) != 1) {
		akdbgprt("[AK09973] %s The number of data are wrong \n", __func__);
		return -EINVAL;
	}

	if (en == 0) {
		ak09973_stop_report_timer(akm);
		ak09973_write_mode(akm, 0);
		ak09973_write_mode(akm2, 0);
		akdbgprt("[AK09973] %s enter  akm->mode = %d\n", __func__, akm->mode);
	} else {
		akdbgprt("[AK09973] %s enter  akm->mode = %d\n", __func__, akm->mode);
		ak09973_write_mode(akm, modeBitTable[akm->mode]);
		ak09973_write_mode(akm2, modeBitTable[akm->mode]);

		ak09973_start_report_timer(akm);
		input_report_abs(akm->input, ABS_RY, 0);
	}

	return count;
}

static CLASS_ATTR_WO(enable);

static ssize_t batch_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	akdbgprt("[AK09973] %s enter\n", __func__);
	return snprintf(buf, 64, "200\n");
}

static ssize_t batch_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	akdbgprt("[AK09973] %s enter\n", __func__);
	return count;
}
static CLASS_ATTR_RW(batch);

static ssize_t cali_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct ak09973_data *akm = pdata;

	if (!akm) {
		akdbgprt("[AK09973] %s akm is null \n", __func__);
	}
	akdbgprt("[AK09973] %s enter calibrate_statue=%d\n", __func__, calibrate_statue);
	input_report_abs(akm->input, ABS_RY, calibrate_statue);
	return snprintf(buf, 64, "%d\n", calibrate_statue);
}

static ssize_t cali_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	struct ak09973_data *akm = pdata;
	int en;

	if (!akm) {
		akdbgprt("[AK09973] %s akm is null \n", __func__);
		return count;
	}

	if (sscanf(buf, "%d", &en) != 1) {
		akdbgprt("[AK09973] %s The number of data are wrong \n", __func__);
		return -EINVAL;
	}

	if (en == 1) {
		input_report_abs(akm->input, ABS_RY, 1);
		// input_sync(akm->input);
	}
	calibrate_statue = en;

	akdbgprt("[AK09973] %s  calibrate_statue = %d \n", __func__, calibrate_statue);

	return count;
}
static CLASS_ATTR_RW(cali);

static ssize_t flush_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	akdbgprt("[AK09973] %s enter\n", __func__);
	return snprintf(buf, 64, "0\n");
}

static ssize_t flush_store(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	akdbgprt("[AK09973] %s enter\n", __func__);
	return count;
}
static CLASS_ATTR_RW(flush);

static int create_class_sys() {
	int32_t ret = 0;

	ret = class_register(&crd_sensor_class);
	if (ret < 0) {
		akdbgprt("[AK09973] %s class_register fail \n", __func__);
		return ret;
	}

	ret = class_create_file(&crd_sensor_class, &class_attr_delay);
	if (ret < 0) {
		akdbgprt("[AK09973] %s class_attr_delay fail \n", __func__);
		return ret;
	}

	ret = class_create_file(&crd_sensor_class, &class_attr_enable);
	if (ret < 0) {
		akdbgprt("[AK09973] %s class_attr_enable fail \n", __func__);
		return ret;
	}
	
	ret = class_create_file(&crd_sensor_class, &class_attr_batch);
	if (ret < 0) {
		akdbgprt("[AK09973] %s class_attr_batch fail \n", __func__);
		return ret;
	}

	ret = class_create_file(&crd_sensor_class, &class_attr_flush);
	if (ret < 0) {
		akdbgprt("[AK09973] %s class_attr_flush fail \n", __func__);
		return ret;
	}

	ret = class_create_file(&crd_sensor_class, &class_attr_cali);
	if (ret < 0) {
		akdbgprt("[AK09973] %s class_attr_cali fail \n", __func__);
		return ret;
	}
	return ret;
}

static int ak09973_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    struct ak09973_data *akm;
    int err = 0;

    akdbgprt("[AK09973] %s enter\n", __func__);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "%d check_functionality failed\n", __func__);
        return -EIO;
    }

    akm = devm_kzalloc(&client->dev, sizeof(struct ak09973_data), GFP_KERNEL);
    if (akm == NULL) {
        dev_err(&client->dev, "%s:failed to malloc memory for akm!\n", __func__);
        return -ENOMEM;
    }


    akm->dev = &client->dev;
    akm->client = client;
    i2c_set_clientdata(client, akm);

    akm->status = STATUS_FAR;
    akm->numMode = ARRAY_SIZE(measurementFreqTable);    
	akdbgprt("[AK09973] %s akm->numMode =%d\n", __func__, akm->numMode);

    err = ak09973_parse_dt(akm);
    if (err < 0){
        dev_err(&client->dev, "[AK09973] Device Tree Setting was not found!\n");
	}

    err = ak09973_check_chip_id(client);
    if (err < 0){
        return err;
	}

	if (akm->num == 0){
		pdata = akm;
	} else {
		pdata2 = akm;
	}

    akm->hall_wakelock = wakeup_source_register(NULL, "hall_wakelock");

    err = ak09973_setup(client);
    if (err < 0) {
        dev_err(&client->dev, "%s initialization fails\n", __func__);
        goto err;
    }
    dev_info(&akm->client->dev, "%s initialization success!\n", __func__);

	if (akm->num == 0){
		err = ak09973_input_init(akm);
		if (err < 0) {
			dev_err(&client->dev, "[AK09973] failed to init input %d\n", err);
			goto err;
		}
	}
		/* add boardtest interface start */
#ifdef CONFIG_VENDOR_ZTE_MISC_COMMON
	zte_misc_register_callback(&is_three_hall_exist_node, NULL);
#endif
	/* add boardtest interface end */

    err = sysfs_create_group(&client->dev.kobj, &ak09973_attribute_group);
    if (err < 0) {
        dev_err(&client->dev, "%s error creating sysfs attr files\n", __func__);
        goto err;
    }

	if (akm->num == 1){
		/* Started by AICoder, pid:x3d54eb2a5o7a6514c840b3930473e157b969759 */
		err = ak09973_init_report_timer();
		if (err < 0) {
			dev_err(&client->dev, "%s initialization of report timer fails\n", __func__);
			goto err;
		}
		/* Ended by AICoder, pid:x3d54eb2a5o7a6514c840b3930473e157b969759 */

		err = create_class_sys();
		if (err < 0) {
			akdbgprt("[AK09973] %s create_class_sys fail\n", __func__);
			goto err_class_sysfs;
		}
	}

    // enable_irq_wake(akm->irq);
    akdbgprt("[AK09973] probe successfully!\n");
    return err;
err_class_sysfs:
	class_unregister(&crd_sensor_class);
err:
    wakeup_source_unregister(akm->hall_wakelock);
    devm_kfree(&client->dev, akm);
    dev_err(&client->dev, "%s: probe failed!\n", __func__);
    return err;
}

static int ak09973_remove(struct i2c_client *client)
{
    struct ak09973_data *akm = (struct ak09973_data *)i2c_get_clientdata(client);

    cancel_delayed_work_sync(&akm->irq_dworker);
    ak09973_i2c_write8(client, AK09973_REG_CNTL2, 0);

    sysfs_remove_group(&client->dev.kobj, &ak09973_attribute_group);

    input_unregister_device(akm->input);
    input_free_device(akm->input);
    devm_gpio_free(&client->dev, akm->int_gpio);
    wakeup_source_unregister(akm->hall_wakelock);
    devm_kfree(&client->dev, akm);
    return 0;
}

static int ak09973_i2c_suspend(struct device *dev)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);
    akdbgprt("[AK09973] %s enter  akm->mode = %d\n", __func__, akm->mode);
	
	ak09973_write_mode(akm, 0);

    return 0;
}

static int ak09973_i2c_resume(struct device *dev)
{
    struct ak09973_data *akm = dev_get_drvdata(dev);
    akdbgprt("[AK09973] %s enter  akm->mode = %d\n", __func__, akm->mode);
	
    ak09973_write_mode(akm, modeBitTable[akm->mode]);
	
    return 0;
}

static const struct dev_pm_ops ak09973_i2c_pops = {
    .suspend	= ak09973_i2c_suspend,
    .resume		= ak09973_i2c_resume,
};

static const struct i2c_device_id ak09973_id[] = {
    { "ak09973", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ak09973_id);

static const struct of_device_id ak09973_of_match[] = {
    { .compatible = "akm,ak09973"},
    { }
};
MODULE_DEVICE_TABLE(of, ak09973_of_match);

static struct i2c_driver ak09973_driver = {
    .driver = {
        .name	= "ak09973",
        .pm = &ak09973_i2c_pops,
        .of_match_table = of_match_ptr(ak09973_of_match),
    },
    .probe		= ak09973_probe,
    .remove		= ak09973_remove,
    .id_table	= ak09973_id,
};
module_i2c_driver(ak09973_driver);

MODULE_AUTHOR("ZTE");
MODULE_DESCRIPTION("AK09973 magnetometer driver");
MODULE_LICENSE("GPL v2");
/* Ended by AICoder, pid:2f1fay831fsa7c914d3d1afa60e2f8051250df43 */