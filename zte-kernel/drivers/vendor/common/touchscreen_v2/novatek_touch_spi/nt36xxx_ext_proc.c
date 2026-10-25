/*
 * Copyright (C) 2024 Novatek, Inc.
 *
 * $Revision: 142039 $
 * $Date: 2024-06-07 09:48:12 +0800 (週五, 07 六月 2024) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */


#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "nt36xxx.h"

#if NVT_TOUCH_EXT_PROC
#define NVT_FW_VERSION "nvt_fw_version"
#define NVT_BASELINE "nvt_baseline"
#define NVT_RAW "nvt_raw"
#define NVT_DIFF "nvt_diff"

#define NVT_EDGE_REJECT "nvt_edge_reject"
#define NVT_AC_POWER_PLUG "nvt_ac_power_plug"
#define NVT_GESTURE_CONTROL "nvt_gesture_control"

#define EDGE_REJECT_VERTICAL 0xBA
#define EDGE_REJECT_LEFT_UP 0xBB
#define EDGE_REJECT_RIGHT_UP 0xBC

#define AC_POWER_PLUG_DISABLE 0x51
#define AC_POWER_PLUG_ENABLE 0x53

#define NVT_HEADSET_LINE_REJECT "nvt_headset_line_reject"
#define HEADSET_LINE_REJECT 0x79

#define NVT_FW_RESET "nvt_fw_reset"

#define NVT_INT_CTRL "nvt_int_ctrl"
#define INT_CTRL_HIGH 0x71
#define INT_CTRL_LOW 0x72

#define NVT_PEN_ONLY_MODE "nvt_pen_only_mode"
#define PEN_ONLY_MODE 0x7B
#define PEN_ONLY_MODE_SUB 0x04

#define NORMAL_MODE 0x00
#define TEST_MODE_2 0x22
#define HANDSHAKING_HOST_READY 0xBB

#define XDATA_SECTOR_SIZE   256

static uint8_t xdata_tmp[8192] = {0};
static int32_t xdata[4096] = {0};

static struct proc_dir_entry *NVT_proc_fw_version_entry;
static struct proc_dir_entry *NVT_proc_baseline_entry;
static struct proc_dir_entry *NVT_proc_raw_entry;
static struct proc_dir_entry *NVT_proc_diff_entry;

static struct proc_dir_entry *NVT_proc_edge_reject_entry;
static struct proc_dir_entry *NVT_proc_ac_power_plug_entry;
static struct proc_dir_entry *NVT_proc_gesture_entry;
static struct proc_dir_entry *NVT_proc_headset_line_reject_entry;
static struct proc_dir_entry *NVT_proc_int_ctrl_entry;
static struct proc_dir_entry *NVT_proc_fw_reset_entry;
static struct proc_dir_entry *NVT_proc_pen_only_mode_entry;

extern int zte_output_offset;
extern bool zte_get_noise_flag;
static void zte_save_noise_data(int32_t *data)
{
	int32_t i = 0, j = 0;

	NVT_LOG("++");

	if (!data) {
		NVT_ERR("error, data is null, return");
		return;
	}

	for (i = 0; i < ts->y_num; i++) {
		zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
			RT_DATA_LEN * 30 - zte_output_offset, "TP[%3d]:", (i+1));
		for (j = 0; j < ts->x_num; j++) {
			zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
				RT_DATA_LEN * 30 - zte_output_offset, "%6d,", xdata[i * ts->x_num + j]);
		}
		zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
			RT_DATA_LEN * 30 - zte_output_offset, "\n");
	}

	zte_output_offset += snprintf((char *)(tpd_cdev->tp_firmware->data + zte_output_offset),
		RT_DATA_LEN * 30 - zte_output_offset, "\n\n");

	NVT_LOG("--");
}
/*******************************************************
Description:
	Novatek touchscreen change mode function.

return:
	n.a.
*******************************************************/
void nvt_change_mode(uint8_t mode)
{
	uint8_t buf[8] = {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	//---set mode---
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = mode;
	CTP_SPI_WRITE(ts->client, buf, 2);

	if (mode == NORMAL_MODE) {
		usleep_range(20000, 20000);
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = HANDSHAKING_HOST_READY;
		CTP_SPI_WRITE(ts->client, buf, 2);
		usleep_range(20000, 20000);
	}
}

/*******************************************************
Description:
	Novatek touchscreen get firmware pipe function.

return:
	Executive outcomes. 0---pipe 0. 1---pipe 1.
*******************************************************/
uint8_t nvt_get_fw_pipe(void)
{
	uint8_t buf[8]= {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

	//---read fw status---
	buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
	buf[1] = 0x00;
	CTP_SPI_READ(ts->client, buf, 2);

	//NVT_LOG("FW pipe=%d, buf[1]=0x%02X\n", (buf[1]&0x01), buf[1]);

	return (buf[1] & 0x01);
}

/*******************************************************
Description:
	Novatek touchscreen read meta data function.

return:
	n.a.
*******************************************************/
void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr)
{
	int32_t transfer_len = 0;
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[BUS_TRANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;

	if (BUS_TRANSFER_LENGTH <= XDATA_SECTOR_SIZE)
		transfer_len = BUS_TRANSFER_LENGTH;
	else
		transfer_len = XDATA_SECTOR_SIZE;

	//---set xdata sector address & length---
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = ts->x_num * ts->y_num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	//printk("head_addr=0x%05X, dummy_len=0x%05X, data_len=0x%05X, residual_len=0x%05X\n", head_addr, dummy_len, data_len, residual_len);

	//read xdata : step 1
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		//---change xdata index---
		nvt_set_page(head_addr + XDATA_SECTOR_SIZE * i);

		//---read xdata by transfer_len
		for (j = 0; j < (XDATA_SECTOR_SIZE / transfer_len); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));
	}

	//read xdata : step2
	if (residual_len != 0) {
		//---change xdata index---
		nvt_set_page(xdata_addr + data_len - residual_len);

		//---read xdata by transfer_len
		for (j = 0; j < (residual_len / transfer_len + 1); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));
	}

	//---remove dummy data and 2bytes-to-1data---
	for (i = 0; i < (data_len / 2); i++) {
		xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}

#if TOUCH_KEY_NUM > 0
	//read button xdata : step3
	//---change xdata index---
	nvt_set_page(xdata_btn_addr);

	//---read data---
	buf[0] = (xdata_btn_addr & 0xFF);
	CTP_SPI_READ(ts->client, buf, (TOUCH_KEY_NUM * 2 + 1));

	//---2bytes-to-1data---
	for (i = 0; i < TOUCH_KEY_NUM; i++) {
		xdata[ts->x_num * ts->y_num + i] = (int16_t)(buf[1 + i * 2] + 256 * buf[1 + i * 2 + 1]);
	}
#endif

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
    Novatek touchscreen get meta data function.

return:
    n.a.
*******************************************************/
void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num, uint8_t *m_y_num)
{
    *m_x_num = ts->x_num;
    *m_y_num = ts->y_num;
    memcpy(buf, xdata, ((ts->x_num * ts->y_num + TOUCH_KEY_NUM) * sizeof(int32_t)));
}

/*******************************************************
Description:
	Novatek touchscreen read and get number of meta data function.

return:
	n.a.
*******************************************************/
void nvt_read_get_num_mdata(uint32_t xdata_addr, int32_t *buffer, uint32_t num)
{
	int32_t transfer_len = 0;
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[BUS_TRANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;

	if (BUS_TRANSFER_LENGTH <= XDATA_SECTOR_SIZE)
		transfer_len = BUS_TRANSFER_LENGTH;
	else
		transfer_len = XDATA_SECTOR_SIZE;

	//---set xdata sector address & length---
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	//printk("head_addr=0x%05X, dummy_len=0x%05X, data_len=0x%05X, residual_len=0x%05X\n", head_addr, dummy_len, data_len, residual_len);

	//read xdata : step 1
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		//---change xdata index---
		nvt_set_page(head_addr + XDATA_SECTOR_SIZE * i);

		//---read xdata by transfer_len
		for (j = 0; j < (XDATA_SECTOR_SIZE / transfer_len); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));
	}

	//read xdata : step2
	if (residual_len != 0) {
		//---change xdata index---
		nvt_set_page(xdata_addr + data_len - residual_len);

		//---read xdata by transfer_len
		for (j = 0; j < (residual_len / transfer_len + 1); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));
	}

	//---remove dummy data and 2bytes-to-1data---
	for (i = 0; i < (data_len / 2); i++) {
		buffer[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
	Novatek touchscreen firmware version show function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t c_fw_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "fw_ver=%d, x_num=%d, y_num=%d, button_num=%d\n", ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num);
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print show
	function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t c_show(struct seq_file *m, void *v)
{
	int32_t i = 0;
	int32_t j = 0;

	for (i = 0; i < ts->y_num; i++) {
		for (j = 0; j < ts->x_num; j++) {
			seq_printf(m, "%6d,", xdata[i * ts->x_num + j]);
		}
		seq_puts(m, "\n");
	}

#if TOUCH_KEY_NUM > 0
	for (i = 0; i < TOUCH_KEY_NUM; i++) {
		seq_printf(m, "%6d,", xdata[ts->x_num * ts->y_num + i]);
	}
	seq_puts(m, "\n");
#endif

	seq_printf(m, "\n\n");
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print start
	function.

return:
	Executive outcomes. 1---call next function.
	NULL---not call next function and sequence loop
	stop.
*******************************************************/
static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print next
	function.

return:
	Executive outcomes. NULL---no next and call sequence
	stop function.
*******************************************************/
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print stop
	function.

return:
	n.a.
*******************************************************/
static void c_stop(struct seq_file *m, void *v)
{
	return;
}

const struct seq_operations nvt_fw_version_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_fw_version_show
};

const struct seq_operations nvt_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show
};

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_fw_version open
	function.

return:
	n.a.
*******************************************************/
static int32_t nvt_fw_version_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return seq_open(file, &nvt_fw_version_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_fw_version_fops = {
	.proc_open = nvt_fw_version_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_fw_version_fops = {
	.owner = THIS_MODULE,
	.open = nvt_fw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_baseline open function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_baseline_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_read_mdata(ts->mmap->BASELINE_ADDR, ts->mmap->BASELINE_BTN_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_baseline_fops = {
	.proc_open = nvt_baseline_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_baseline_fops = {
	.owner = THIS_MODULE,
	.open = nvt_baseline_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_raw open function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
int32_t nvt_do_raw_open(void)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->RAW_PIPE0_ADDR, ts->mmap->RAW_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->RAW_PIPE1_ADDR, ts->mmap->RAW_BTN_PIPE1_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("--");

	if (zte_get_noise_flag) {
		zte_save_noise_data(xdata);
	}

	return 0;
}

static int32_t nvt_raw_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	NVT_LOG("++");

	ret = nvt_do_raw_open();
	if (ret) {
		NVT_ERR("nvt_raw_open failed");
	} else {
		NVT_LOG("nvt_raw_open pass");
	}

	NVT_LOG("--");

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_raw_fops = {
	.proc_open = nvt_raw_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_raw_fops = {
	.owner = THIS_MODULE,
	.open = nvt_raw_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_diff open function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t nvt_do_diff_open(void)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	if (zte_get_noise_flag) {
		zte_save_noise_data(xdata);
	}

	return 0;
}

static int32_t nvt_diff_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	NVT_LOG("++");

	ret = nvt_do_diff_open();
	if (ret) {
		NVT_ERR("nvt_diff_open failed");
	} else {
		NVT_LOG("nvt_diff_open pass");
	}

	NVT_LOG("--");

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_diff_fops = {
	.proc_open = nvt_diff_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_diff_fops = {
	.owner = THIS_MODULE,
	.open = nvt_diff_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

int32_t nvt_ext_cmd_store(uint8_t u8Cmd, uint8_t u8subCmd1,uint8_t u8subCmd2) {
	int i, retry = 5;
	uint8_t buf[10] = {0};
	int32_t ret = 0;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		mutex_unlock(&ts->lock);
		return ret;
	}

	for (i = 0; i < retry; i++) {
		//---set cmd status---
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = u8Cmd;
		buf[2] = u8subCmd1;
		buf[3] = u8subCmd2;
		CTP_SPI_WRITE(ts->client, buf, 4);

		msleep(20);

		//---read cmd status---
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);
		if (buf[1] == 0x00)
			break;
	}

	if (unlikely(i == retry)) {
		NVT_LOG("send Cmd 0x%02X 0x%02X 0x%02X failed, buf[1]=0x%02X\n", u8Cmd, u8subCmd1, u8subCmd2, buf[1]);
		ret = -1;
	} else {
		NVT_LOG("send Cmd 0x%02X 0x%02X 0x%02X success, tried %d times\n", u8Cmd, u8subCmd1, u8subCmd2, i);
	}

	mutex_unlock(&ts->lock);

	return ret;
}

int32_t nvt_edge_reject_set(uint8_t status) {
	int ret = 0;

	if ((status == 0) || (status == 2)) //rotate 0/180 degree
		ret = nvt_ext_cmd_store(EDGE_REJECT_VERTICAL, 0x00, 0x00);
	else if(status == 3) //rotate 270 degree
		ret = nvt_ext_cmd_store(EDGE_REJECT_LEFT_UP, 0x00, 0x00);
	else if(status == 1) //rotate 90 degree
		ret = nvt_ext_cmd_store(EDGE_REJECT_RIGHT_UP, 0x00, 0x00);

	return ret;
}

static ssize_t nvt_edge_reject_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	char dbg[10] = { 0 };
	uint8_t state;
	uint8_t ret;

	ret = copy_from_user(dbg, (uint8_t *) buffer, sizeof(uint8_t));
	if (ret)
		return -EINVAL;

	ret = kstrtou8(dbg, 16, &state);
	if (ret < 0)
		return ret;

	NVT_LOG("edge_reject_state = %d\n", state);
	ret = nvt_edge_reject_set(state);
	if (ret)
		return ret;

	ts->edge_reject_state = state;

	return count;
}

static int nvt_edge_reject_show(struct seq_file *sfile, void *v) {
	if(ts->edge_reject_state == 1)
		seq_printf(sfile, "Edge Reject vertical\n");
	else if(ts->edge_reject_state == 2)
		seq_printf(sfile, "Edge Reject left up\n");
	else if(ts->edge_reject_state == 3)
		seq_printf(sfile, "Edge Reject right up\n");
	else
		seq_printf(sfile, "Not Support!\n");

	return 0;
}

static int32_t nvt_edge_reject_open(struct inode *inode, struct file *file) {
	return single_open(file, nvt_edge_reject_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_edge_reject_fops = {
	.proc_open = nvt_edge_reject_open,
	.proc_read = seq_read,
	.proc_write = nvt_edge_reject_store,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,

};
#else
static const struct file_operations nvt_edge_reject_fops = {
	.owner = THIS_MODULE,
	.open = nvt_edge_reject_open,
	.read = seq_read,
	.write = nvt_edge_reject_store,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

int32_t nvt_ac_power_plug_set(uint8_t status) {
	int ret = 0;

	if(status == 0)
		ret = nvt_ext_cmd_store(AC_POWER_PLUG_DISABLE, 0x00, 0x00);
	else if(status == 1)
		ret = nvt_ext_cmd_store(AC_POWER_PLUG_ENABLE, 0x00, 0x00);

	return ret;
}

static ssize_t nvt_ac_power_plug_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	char dbg[10] = { 0 };
	uint8_t state;
	uint8_t ret;

	ret = copy_from_user(dbg, (uint8_t *) buffer, sizeof(uint8_t));
	if (ret)
		return -EINVAL;

	ret = kstrtou8(dbg, 16, &state);
	if (ret < 0)
		return ret;

	NVT_LOG("ac_power_plug_state = %d\n", state);
	ret = nvt_ac_power_plug_set(state);
	if (ret)
		return ret;

	ts->ac_power_plug_state = state;

	return count;
}

static int nvt_ac_power_plug_show(struct seq_file *sfile, void *v) {
	if(ts->ac_power_plug_state == 0)
		seq_printf(sfile, "AC Power Plug Disable\n");
	else if(ts->ac_power_plug_state == 1)
		seq_printf(sfile, "AC Power Plug Enable\n");
	else
		seq_printf(sfile, "Not Support!\n");

	return 0;
}

static int32_t nvt_ac_power_plug_open(struct inode *inode, struct file *file) {
	return single_open(file, nvt_ac_power_plug_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_ac_power_plug_fops = {
	.proc_open = nvt_ac_power_plug_open,
	.proc_read = seq_read,
	.proc_write = nvt_ac_power_plug_store,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_ac_power_plug_fops = {
	.owner = THIS_MODULE,
	.open = nvt_ac_power_plug_open,
	.read = seq_read,
	.write = nvt_ac_power_plug_store,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

static ssize_t nvt_gesture_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	char dbg[10] = { 0 };
	int ret = 0;
	uint8_t state;

	ret = copy_from_user(dbg, (uint8_t *) buffer, sizeof(uint8_t));
	if (ret)
		return -EINVAL;

	ret = kstrtou8(dbg, 16, &state);
	if (ret < 0)
		return ret;

	if(state == 0)
		ts->gesture_enabled = false;
	else
		ts->gesture_enabled = true;

	return count;
}

static int nvt_gesture_show(struct seq_file *sfile, void *v) {
	if(ts->gesture_enabled)
		seq_printf(sfile, "Enable Gesture!\n");
	else
		seq_printf(sfile, "Disable Gesture!\n");

	return 0;
}

static int32_t nvt_gesture_open(struct inode *inode, struct file *file) {
	return single_open(file, nvt_gesture_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_gesture_fops = {
	.proc_open = nvt_gesture_open,
	.proc_read = seq_read,
	.proc_write = nvt_gesture_store,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_gesture_fops = {
	.owner = THIS_MODULE,
	.open = nvt_gesture_open,
	.read = seq_read,
	.write = nvt_gesture_store,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

int32_t nvt_headset_line_reject_set(uint8_t status) {
	int ret = 0;

	if(status == 0)
		ret = nvt_ext_cmd_store(HEADSET_LINE_REJECT, 0x00, 0x00);
	else if(status == 1)
		ret = nvt_ext_cmd_store(HEADSET_LINE_REJECT, 0x01, 0x00);

	return ret;
}

static ssize_t nvt_headset_line_reject_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	char dbg[10] = { 0 };
	int ret = 0;
	uint8_t state;

	ret = copy_from_user(dbg, (uint8_t *) buffer, sizeof(uint8_t));
	if (ret)
		return -EINVAL;

	ret = kstrtou8(dbg, 16, &state);
	if (ret < 0)
		return ret;

	ret = nvt_headset_line_reject_set(state);
	if (ret)
		return ret;

	ts->headset_line_reject_state = state;

	return count;
}

static int nvt_headset_line_reject_show(struct seq_file *sfile, void *v) {
	if(ts->headset_line_reject_state)
		seq_printf(sfile, "Enable headset line reject\n");
	else
		seq_printf(sfile, "Disable headset line reject\n");

	return 0;
}

static int32_t nvt_headset_line_reject_open(struct inode *inode, struct file *file) {
	return single_open(file, nvt_headset_line_reject_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_headset_line_reject_fops = {
	.proc_open = nvt_headset_line_reject_open,
	.proc_read = seq_read,
	.proc_write = nvt_headset_line_reject_store,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_headset_line_reject_fops = {
	.owner = THIS_MODULE,
	.open = nvt_headset_line_reject_open,
	.read = seq_read,
	.write = nvt_headset_line_reject_store,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

int32_t nvt_int_ctrl_set(uint8_t status) {
	int ret = 0;

	if(status == 0)
		ret = nvt_ext_cmd_store(INT_CTRL_LOW, 0x00, 0x00);
	else if(status == 1)
		ret = nvt_ext_cmd_store(INT_CTRL_HIGH, 0x00, 0x00);

	return ret;
}

static ssize_t nvt_int_ctrl_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	char dbg[10] = { 0 };
	int ret = 0;
	uint8_t state;

	ret = copy_from_user(dbg, (uint8_t *) buffer, sizeof(uint8_t));
	if (ret)
		return -EINVAL;

	ret = kstrtou8(dbg, 16, &state);
	if (ret < 0)
		return ret;

	ret = nvt_int_ctrl_set(state);
	if (ret)
		return ret;

	ts->int_ctrl_state = state;

	return count;
}

static int nvt_int_ctrl_show(struct seq_file *sfile, void *v) {
	if(ts->int_ctrl_state)
		seq_printf(sfile, "INT set to high\n");
	else
		seq_printf(sfile, "INT set to low\n");

	return 0;
}

static int32_t nvt_int_ctrl_open(struct inode *inode, struct file *file) {
	return single_open(file, nvt_int_ctrl_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_int_ctrl_fops = {
	.proc_open = nvt_int_ctrl_open,
	.proc_read = seq_read,
	.proc_write = nvt_int_ctrl_store,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_int_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = nvt_int_ctrl_open,
	.read = seq_read,
	.write = nvt_int_ctrl_store,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

static ssize_t nvt_fw_reset_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	int ret = 0;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}
	ret = nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	mutex_unlock(&ts->lock);
	if (ret)
		return ret;

	return count;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_fw_reset_fops = {
	.proc_write = nvt_fw_reset_store,
};
#else
static const struct file_operations nvt_fw_reset_fops = {
	.owner = THIS_MODULE,
	.write = nvt_fw_reset_store,
};
#endif

static ssize_t nvt_pen_only_mode_store(struct file *file, const char *buffer, size_t count, loff_t *pos) {
	char dbg[10] = { 0 };
	int ret = 0;
	uint8_t state;

	ret = copy_from_user(dbg, (uint8_t *) buffer, sizeof(uint8_t));
	if (ret)
		return -EINVAL;

	ret = kstrtou8(dbg, 16, &state);
	if (ret < 0)
		return ret;

	ret = nvt_ext_cmd_store(PEN_ONLY_MODE, PEN_ONLY_MODE_SUB, !!state);
	if (ret)
		return ret;

	ts->pen_only_mode_state = state;

	return count;
}

static int nvt_pen_only_mode_show(struct seq_file *sfile, void *v) {
	if(ts->pen_only_mode_state)
		seq_printf(sfile, "pen only mode enabled\n");
	else
		seq_printf(sfile, "pen only mode disabled\n");

	return 0;
}

static int32_t nvt_pen_only_mode_open(struct inode *inode, struct file *file) {
	return single_open(file, nvt_pen_only_mode_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_pen_only_mode_fops = {
	.proc_open = nvt_pen_only_mode_open,
	.proc_read = seq_read,
	.proc_write = nvt_pen_only_mode_store,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_pen_only_mode_fops = {
	.owner = THIS_MODULE,
	.open = nvt_pen_only_mode_open,
	.read = seq_read,
	.write = nvt_pen_only_mode_store,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen extra function proc. file node
	initial function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
int32_t nvt_extra_proc_init(void)
{
	NVT_proc_fw_version_entry = proc_create(NVT_FW_VERSION, 0444, NULL,&nvt_fw_version_fops);
	if (NVT_proc_fw_version_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_FW_VERSION);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_FW_VERSION);
	}

	NVT_proc_baseline_entry = proc_create(NVT_BASELINE, 0444, NULL,&nvt_baseline_fops);
	if (NVT_proc_baseline_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_BASELINE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_BASELINE);
	}

	NVT_proc_raw_entry = proc_create(NVT_RAW, 0444, NULL,&nvt_raw_fops);
	if (NVT_proc_raw_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_RAW);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_RAW);
	}

	NVT_proc_diff_entry = proc_create(NVT_DIFF, 0444, NULL,&nvt_diff_fops);
	if (NVT_proc_diff_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_DIFF);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_DIFF);
	}

	NVT_proc_edge_reject_entry = proc_create(NVT_EDGE_REJECT, 0644, NULL, &nvt_edge_reject_fops);
	if (NVT_proc_edge_reject_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_EDGE_REJECT);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_EDGE_REJECT);
	}

	NVT_proc_ac_power_plug_entry = proc_create(NVT_AC_POWER_PLUG, 0644, NULL, &nvt_ac_power_plug_fops);
	if (NVT_proc_ac_power_plug_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_AC_POWER_PLUG);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_AC_POWER_PLUG);
	}

	NVT_proc_gesture_entry = proc_create(NVT_GESTURE_CONTROL, 0644, NULL, &nvt_gesture_fops);
	if (NVT_proc_gesture_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_GESTURE_CONTROL);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_GESTURE_CONTROL);
	}

	NVT_proc_headset_line_reject_entry = proc_create(NVT_HEADSET_LINE_REJECT, 0644, NULL, &nvt_headset_line_reject_fops);
	if (NVT_proc_headset_line_reject_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_HEADSET_LINE_REJECT);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_HEADSET_LINE_REJECT);
	}

	NVT_proc_int_ctrl_entry = proc_create(NVT_INT_CTRL, 0644, NULL, &nvt_int_ctrl_fops);
	if (NVT_proc_int_ctrl_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_INT_CTRL);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_INT_CTRL);
	}

	NVT_proc_fw_reset_entry = proc_create(NVT_FW_RESET, 0644, NULL, &nvt_fw_reset_fops);
	if (NVT_proc_fw_reset_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_FW_RESET);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_FW_RESET);
	}

	NVT_proc_pen_only_mode_entry = proc_create(NVT_PEN_ONLY_MODE, 0644, NULL, &nvt_pen_only_mode_fops);
	if (NVT_proc_pen_only_mode_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_PEN_ONLY_MODE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_PEN_ONLY_MODE);
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen extra function proc. file node
	deinitial function.

return:
	n.a.
*******************************************************/
void nvt_extra_proc_deinit(void)
{
	if (NVT_proc_fw_version_entry != NULL) {
		remove_proc_entry(NVT_FW_VERSION, NULL);
		NVT_proc_fw_version_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_FW_VERSION);
	}

	if (NVT_proc_baseline_entry != NULL) {
		remove_proc_entry(NVT_BASELINE, NULL);
		NVT_proc_baseline_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_BASELINE);
	}

	if (NVT_proc_raw_entry != NULL) {
		remove_proc_entry(NVT_RAW, NULL);
		NVT_proc_raw_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_RAW);
	}

	if (NVT_proc_diff_entry != NULL) {
		remove_proc_entry(NVT_DIFF, NULL);
		NVT_proc_diff_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_DIFF);
	}

	if (NVT_proc_edge_reject_entry != NULL) {
		remove_proc_entry(NVT_EDGE_REJECT, NULL);
		NVT_proc_edge_reject_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_EDGE_REJECT);
	}

	if (NVT_proc_ac_power_plug_entry != NULL) {
		remove_proc_entry(NVT_AC_POWER_PLUG, NULL);
		NVT_proc_ac_power_plug_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_AC_POWER_PLUG);
	}

	if (NVT_proc_gesture_entry != NULL) {
		remove_proc_entry(NVT_GESTURE_CONTROL, NULL);
		NVT_proc_gesture_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_GESTURE_CONTROL);
	}

	if (NVT_proc_headset_line_reject_entry != NULL) {
		remove_proc_entry(NVT_HEADSET_LINE_REJECT, NULL);
		NVT_proc_headset_line_reject_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_HEADSET_LINE_REJECT);
	}

	if (NVT_proc_int_ctrl_entry != NULL) {
		remove_proc_entry(NVT_INT_CTRL, NULL);
		NVT_proc_int_ctrl_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_INT_CTRL);
	}

	if (NVT_proc_fw_reset_entry != NULL) {
		remove_proc_entry(NVT_FW_RESET, NULL);
		NVT_proc_fw_reset_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_FW_RESET);
	}

	if (NVT_proc_pen_only_mode_entry != NULL) {
		remove_proc_entry(NVT_PEN_ONLY_MODE, NULL);
		NVT_proc_pen_only_mode_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_PEN_ONLY_MODE);
	}
}
#endif
