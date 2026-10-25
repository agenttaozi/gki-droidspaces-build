#include "sitronix_ts.h"
#ifdef ST_REPLACE_TEST_CMD_BY_DISPLAY_ID
#include "sitronix_ts_test_id1.h"
#endif /* ST_REPLACE_TEST_CMD_BY_DISPLAY_ID */
#include <linux/export.h>




//#define SITRONIX_TEST_ST7123
#define SITRONIX_TEST_ST7121P
//#define SITRONIX_TEST_ST7123P

#ifdef SITRONIX_TEST_ST7123
# include "sitronix_ts_test_st7123.h"
#elif defined(SITRONIX_TEST_ST7121P)
# include "sitronix_ts_test_st7121p.h"
#elif defined(SITRONIX_TEST_ST7123P)
# include "sitronix_ts_test_st7123p.h"
#else
# error "Self-Test not defined!"
#endif


//#define SITRONIX_UNIFORMITY_TEST_ENABLED

typedef struct sensing_setting {
	uint8_t	row_cnt;	//stmsg("ROW_CNT : %d\n",tMode[2]);
	uint8_t aa_unit;	//stmsg("AA_UNIT : %d\n",tMode[1]);
	uint8_t	self_unit;	//stmsg("SELF_UNIT : %d\n",tMode[5]);
	uint8_t noise_unit;	//stmsg("NOISE_UNIT : %d\n",tMode[4]);
	uint8_t	cmnc_ch_wr_en;	//stmsg("CMNC_CH_WR_EN : %d\n",tMode[7]);
	uint8_t	key;
} sensing_setting_t;

//static sensing_setting_t	sensing_setting;

//#define __RAW_DATA_DEBUG__

#define USE_ST_SQRT
#ifdef USE_ST_SQRT
int st_sqrt(int x);
#endif

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002
#define TEST_GT_OPEN				0x0200
#define TEST_GT_SHORT				0x0400
#define BEYOND_UNIFORMITY_LIMIT 0x00800000
extern int sitronix_tptest_result;

int st_address_mode_hardcode_write(uint8_t *code, int max_len)
{
	int ret = 0;
	int index = 0;
	uint16_t key = 0;
	uint16_t len = 0;
	uint32_t addr = 0;

	while (index < max_len - 1 ) {
		key = code[index] << 8 | code[index + 1];		
		if (key == 0x5354){
			index += 2;
			switch(code[index]) {
				case 0x01:
					index += 1;
					addr = code[index] << 16 | code[index + 1] << 8 | code[index + 2];
					len = code[index + 3] << 8 | code[index + 4];
					index += 5;
					ret = sitronix_spi_pram_rw(false, addr, code + index, NULL, len);
					index += len;
					//stmsg("write addr: %x , len :%x \n", addr, len);
					break;
				case 0x02:
					index += 1;
					len = code[index] << 8 | code[index + 1];
					//stmsg("delay 0x%x ms \n", len);
					msleep(len);
					index += 2;
					break;
				default:
					index = max_len;
					stmsg("unknown code: 0x%x\n", code[index]);
					break;
			}
		} else {
			index ++;
		}
	}
	return ret;
}

bool st_get_test_enable(int col, int row)
{
	int index = col * 4 + row / 8;
	unsigned char mask = 0x80 >> (row % 8);
	
	if( index < sizeof(test_disable_sensor))
		return !(test_disable_sensor[index] & mask);
	else
		return false;
}

#ifdef ST_SELFTEST_LOG_FILE
bool sitonix_createlogfileok = true; // FIH self test open log file success or not

void sitronix_vfswrite(char *data1, int str_len)
{
	/* if(sitonix_createlogfileok) */
		/* vfs_write(filp, data1, str_len, ppos); */
	tpd_copy_to_tp_firmware_data(data1);
}
#endif /* ST_SELFTEST_LOG_FILE */

int st_open_short_test(char func, int skipcol)
{
	unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	int ret = 0;
	unsigned char *raw_buf = NULL;
	signed short *rawI = NULL;
	signed short *rawP = NULL;
	int frameCounter;
	int retryCounter;
	int max_retry = 50; //5;
	unsigned char raw_dat_rd_on[2] = { 0x02, 0x00 };
	unsigned char raw_dat_rd_off[2] = { 0x00, 0x00 };
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int total_error = 0;
	int error_count = 0;
	int count_frame = 1;
	int buf_index , aa_index;
	int col , row;
#ifdef ST_SELFTEST_LOG_FILE
	char data1[150];
#endif
	int percentage = 0;	
	int *rawS = NULL;	//Raw data STD for STD test
	int raw_avg;	
	unsigned long sqrt;
#ifdef __RAW_DATA_DEBUG__
	//int i, j, k;
#endif //__RAW_DATA_DEBUG__


	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
	tMode[2] = cmd[0]&0x3F;

	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1]&0x01;

	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	tMode[1] = (cmd[0]&0xF0)>>3;

	/* SELF_UNIT */		
	tMode[5] = (cmd[0]&0xC)>>1;

	/* NOISE_UNIT */
	tMode[4] = (cmd[0]&0x3)<<2;

	/* KEY */
	tMode[3] = 0;

	stmsg("ROW_CNT : %d\n",tMode[2]);
	stmsg("AA_UNIT : %d\n",tMode[1]);
	stmsg("SELF_UNIT : %d\n",tMode[5]);
	stmsg("NOISE_UNIT : %d\n",tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n",tMode[7]);

	read_len = (tMode[2] + tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) * 2;
	raw_buf = (unsigned char*)kmalloc(read_len, GFP_KERNEL);
	rawI = (signed short *)kmalloc((tMode[2]) * sizeof(short), GFP_KERNEL);	

	//Ignore
	frameCounter = 0;
	retryCounter = 0;

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

#ifdef __RAW_DATA_DEBUG__
#if 0
		// Read Rawdata.
		memset(raw_buf, 0, read_len);
		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
		for (i = 0, j = 0; j < (tMode[1] + tMode[5] + tMode[4] + skipcol); j++) {
			stmsg("AA_UNIT[%d]: ", j);
			for (k = 0; k < (tMode[2] + tMode[7]); i++, k++) {
				short raw = (short)(raw_buf[i*2] << 8 | raw_buf[i*2 + 1]);
				printk("%d ", raw);
			}
			printk(" (i=%d, k=%d)\n", i, k);
		}
#endif //0
#endif //__RAW_DATA_DEBUG__

		stmsg("header %x \n",raw_header[1]);
		if(frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}

		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME + 1)
			break;
	}

	if( retryCounter >=  max_retry)
	{
		if(func == 0) {
			sterr("st open test fail ,  can't wait IRQ \n");
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 50, "st open test fail ,  can't wait IRQ\n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
		} 
		//add by maibo for test begin
		/*else if(func == 1 || func == 2) {
			sterr("st short test fail ,  can't wait IRQ \n");
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 50, "st short test fail ,  can't wait IRQ \n");
			sitronix_vfswrite(filp, data1, strlen(data1), ppos);
#endif
		}*/
		 else if(func == 3) {
			sterr("st uniformity test fail ,  can't wait IRQ \n");
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 50, "st uniformity test fail ,  can't wait IRQ \n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
		} else if(func == 4) {
			sterr("st STD test fail ,  can't wait IRQ \n");
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 50, "st STD test fail ,  can't wait IRQ \n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
		}
		ret = -1;
		goto st_open_short_test_finish;
	}

	//uniformity test
	if( func == 3) {
		rawP = (signed short *)kmalloc((tMode[2]) * sizeof(short), GFP_KERNEL);
	}
	
	//STD test
	if (func == 4) {
		retryCounter = 0;
		count_frame = ST_SELFTEST_STD_FRAME_CNT;
		rawS = (int *)kmalloc((tMode[2] * tMode[1]) * sizeof(int), GFP_KERNEL);
		while (retryCounter++ < max_retry) {
			rawP = (signed short *)kmalloc((tMode[2] * tMode[1] * ST_SELFTEST_STD_FRAME_CNT) * sizeof(short), GFP_KERNEL);
			if ( rawP != NULL)
				break;
		}
		
		if( retryCounter >=  max_retry) {
			sterr("st STD test fail for can not kmalloc\n");
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 50, "st STD test fail for can not kmalloc\n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
			ret = -1;
			goto st_open_short_test_finish;
		}
	}

	//raw
	frameCounter = 0;
	retryCounter = 0;

	while (count_frame > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
		if(frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];

			sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

			aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) *2;
			buf_index = (skipcol) * (tMode[2] + tMode[7]) *2;

			if (skipcol != 0)
				for(col = 0; col < aa_index; col++)
					raw_buf[aa_index + col ] = raw_buf[aa_index + col + buf_index];

			buf_index = 0;
			aa_index = 0;

			//AA A
			for (col = 0; col < tMode[1] / 2; col++)
			{
				error_count = 0;
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					//stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
					if(func == 3) // uniformity
					{
						//judge
						if (sizeof(golden_buf) > 0) {
							if( sizeof(golden_buf) > buf_index + 1)
								rawP[row] = (signed short)((golden_buf[buf_index]) + (golden_buf[buf_index + 1]<<8));
							else
								rawP[row] = rawI[row];

							rawI[row] = rawI[row] + gts->self_test_uniformity_shift;
							if(rawP[row] < 1)
								percentage = 100;
							else
								percentage = rawI[row] * 100 / rawP[row];							
							if(st_get_test_enable(col, row) && (percentage < gts->self_test_uniformity_min || percentage > gts->self_test_uniformity_max))
								error_count++;
						}
					}
					else if(func == 1) // short odd
					{
						if(row %2 == 0 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
					else if(func == 2) //short even
					{
						if(row %2 == 1 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
					else if(func == 4)
					{
						percentage = ((frameCounter - 1) * tMode[1] * tMode[2]) + (col * tMode[2]) + row;
						rawP[percentage] = rawI[row];						
						//stmsg("sensor (%2d,%2d) , index %2d RAW (%4d) \n" , col, row, percentage, rawI[row]);
					}
					else //open
					{
						if(st_get_test_enable(col, row) && (rawI[row] < gts->self_test_open_min || rawI[row] > gts->self_test_open_max))
							error_count++;
					}

					buf_index +=2 ;
				}

				if(error_count > ST_SELFTEST_ADJUST_COUNT)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						if(func == 3)
						{	
							if(rawP[row] < 1)
								percentage = 100;
							else
								percentage = rawI[row] * 100 / rawP[row];
							if(st_get_test_enable(col, row) && (percentage < gts->self_test_uniformity_min || percentage > gts->self_test_uniformity_max))
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%3d%%) out of percentage (%d%% ~ %d%%) in uniformity test\n" , col, row, percentage, gts->self_test_uniformity_min , gts->self_test_uniformity_max);
#ifdef ST_SELFTEST_LOG_FILE
								snprintf(data1, 150, "sensor (%2d,%2d) RAW (%3d%%) out of percentage (%d%% ~ %d%%) in uniformity test\n" , col, row, percentage, gts->self_test_uniformity_min , gts->self_test_uniformity_max);
								sitronix_vfswrite(data1, strlen(data1));
#endif
							}
						}
						else if(func == 1)
						{
							if(row %2 == 0 && rawI[row] > gts->self_test_short_max) {
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n" , col, row, rawI[row], gts->self_test_short_max);
#ifdef ST_SELFTEST_LOG_FILE
								snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								sitronix_vfswrite(data1, strlen(data1));
#endif
							}
						}
						else if(func == 2 )
						{
							if(row %2 == 1 && rawI[row] > gts->self_test_short_max) {
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n" , col, row, rawI[row], gts->self_test_short_max);
#ifdef ST_SELFTEST_LOG_FILE
								snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								sitronix_vfswrite(data1, strlen(data1));
#endif
							}
						}
						else if(st_get_test_enable(col, row) && (rawI[row] < gts->self_test_open_min || rawI[row] > gts->self_test_open_max))
						{
							total_error++;
							sterr("sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in open test\n" , col, row, rawI[row], gts->self_test_open_min, gts->self_test_open_max);
#ifdef ST_SELFTEST_LOG_FILE
							snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in open test\n", col, row, rawI[row], gts->self_test_open_min, gts->self_test_open_max);
							sitronix_vfswrite(data1, strlen(data1));
#endif
						}
					}
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			//SE A
			for (col = 0; col < tMode[5] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index +=2 ; 
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			//AA B
			for (col = tMode[1] / 2; col < tMode[1]; col++)
			{
				error_count = 0;
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					//stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);
					if(func == 3) // uniformity
					{
						//judge
						if (sizeof(golden_buf) > 0) {
							if( sizeof(golden_buf) > buf_index + 1)
								rawP[row] = (signed short)((golden_buf[buf_index]) + (golden_buf[buf_index + 1]<<8));
							else
								rawP[row] = rawI[row];
							
							rawI[row] = rawI[row] + gts->self_test_uniformity_shift;
							if(rawP[row] < 1)
								percentage = 100;
							else
								percentage = rawI[row] * 100 / rawP[row];
							if(st_get_test_enable(col, row) && (percentage < gts->self_test_uniformity_min || percentage > gts->self_test_uniformity_max))
								error_count++;
						}
					}
					else if(func == 1) // short odd
					{
						if(row %2 == 0 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
					else if(func == 2) //short even
					{
						if(row %2 == 1 && rawI[row] > gts->self_test_short_max)
							error_count++;
					}
					else if(func == 4)
					{
						percentage = ((frameCounter - 1) * tMode[1] * tMode[2]) + (col * tMode[2]) + row;
						rawP[percentage] = rawI[row];
					}
					else //open
					{
						if(st_get_test_enable(col, row) && (rawI[row] < gts->self_test_open_min || rawI[row] > gts->self_test_open_max))
							error_count++;
					}

					buf_index +=2 ;
				}

				if(error_count > ST_SELFTEST_ADJUST_COUNT)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						if(func == 3)
						{
							if(rawP[row] < 1)
								percentage = 100;
							else
								percentage = rawI[row] * 100 / rawP[row];
							if(st_get_test_enable(col, row) && (percentage < gts->self_test_uniformity_min || percentage > gts->self_test_uniformity_max))
							{
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%3d%%) out of percentage (%d%% ~ %d%%) in uniformity test\n" , col, row, percentage, gts->self_test_uniformity_min , gts->self_test_uniformity_max);
#ifdef ST_SELFTEST_LOG_FILE
								snprintf(data1, 150, "sensor (%2d,%2d) RAW (%3d%%) out of percentage (%d%% ~ %d%%) in uniformity test\n" , col, row, percentage, gts->self_test_uniformity_min , gts->self_test_uniformity_max);
								sitronix_vfswrite(data1, strlen(data1));
#endif
							}
						}
						else if(func == 1)
						{
							if(row %2 == 0 && rawI[row] > gts->self_test_short_max) {
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n" , col, row, rawI[row], gts->self_test_short_max);
#ifdef ST_SELFTEST_LOG_FILE
								snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_odd test\n", col, row, rawI[row], gts->self_test_short_max);
								sitronix_vfswrite(data1, strlen(data1));
#endif
							}
						}
						else if(func == 2 )
						{
							if(row %2 == 1 && rawI[row] > gts->self_test_short_max) {
								total_error++;
								sterr("sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n" , col, row, rawI[row], gts->self_test_short_max);
#ifdef ST_SELFTEST_LOG_FILE
								snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) > standard value (%d) in short_even test\n", col, row, rawI[row], gts->self_test_short_max);
								sitronix_vfswrite(data1, strlen(data1));
#endif
							}
						}
						else if(st_get_test_enable(col, row) && (rawI[row] < gts->self_test_open_min || rawI[row] > gts->self_test_open_max))
						{
							total_error++;
							sterr("sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in open test\n" , col, row, rawI[row], gts->self_test_open_min, gts->self_test_open_max);
#ifdef ST_SELFTEST_LOG_FILE
							snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in open test\n", col, row, rawI[row], gts->self_test_open_min, gts->self_test_open_max);
							sitronix_vfswrite(data1, strlen(data1));
#endif
						}
					}
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			//SE B
			for (col = tMode[5] / 2; col < tMode[5] ; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index +=2 ; 
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			if (func == 3 || func == 2 || func == 1 || func == 0)
			{
#ifdef ST_SELFTEST_LOG_FILE
				if (func == 0)
					snprintf(data1, 100, "[OPEN RAW Data start]\n");
				else if(func == 1)
					snprintf(data1, 100, "[SHORT_ODD RAW Data start]\n");
				else if(func == 2)
					snprintf(data1, 100, "[SHORT_EVEN RAW Data start]\n");
				else
					snprintf(data1, 100, "[UNIFORMITY RAW Data start]\n");
				sitronix_vfswrite(data1, strlen(data1));
#endif
				buf_index = 0;
				aa_index = 0;
				//AA A
				for (col = 0; col < tMode[1] / 2; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);	
#ifdef ST_SELFTEST_LOG_FILE
						if (row == tMode[2] - 1 )
							snprintf(data1, 100, "%6d \n", rawI[row]);
						else
							snprintf(data1, 100, "%6d ", rawI[row]);
						sitronix_vfswrite(data1, strlen(data1));
#endif
						buf_index +=2;
					}

					if (tMode[7] != 0)
						buf_index += 2;
				}
				//SE A
				for (col = 0; col < tMode[5] / 2; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						buf_index +=2;
					}

					if (tMode[7] != 0)
						buf_index += 2;
				}
				//AA B
				for (col = tMode[1] / 2; col < tMode[1]; col++)
				{
					for (row = 0; row < tMode[2]; row++)
					{
						rawI[row] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
#ifdef ST_SELFTEST_LOG_FILE
						if (row == tMode[2] - 1 )
							snprintf(data1, 100, "%6d \n", rawI[row]);
						else
							snprintf(data1, 100, "%6d ", rawI[row]);
						sitronix_vfswrite(data1, strlen(data1));
#endif
						buf_index +=2 ;
					}
					if (tMode[7] != 0)
						buf_index += 2;
				}

#ifdef ST_SELFTEST_LOG_FILE
				snprintf(data1, 100, "[RAW Data end]\n");
				sitronix_vfswrite(data1, strlen(data1));
#endif
			}
		}
		else
		{
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
		}

		if (frameCounter >= count_frame)
			break;
	}

	if (func == 4) {
#ifdef ST_SELFTEST_LOG_FILE
		snprintf(data1, 100, "[STD start]\n");
		sitronix_vfswrite(data1, strlen(data1));		
#endif
		for (col = 0; col < tMode[1]; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				aa_index = col * tMode[2] + row;
				percentage = 0;
				for (frameCounter = 0 ; frameCounter < ST_SELFTEST_STD_FRAME_CNT ; frameCounter++) {
					buf_index = frameCounter * tMode[1] * tMode[2];
					percentage += (int)rawP[buf_index + aa_index];
				}

				raw_avg = percentage * 10 / ST_SELFTEST_STD_FRAME_CNT;

				rawS[aa_index] = 0;
				for (frameCounter = 0 ; frameCounter < ST_SELFTEST_STD_FRAME_CNT ; frameCounter++) {					
					buf_index = frameCounter * tMode[1] * tMode[2];
					if( raw_avg >= (int)(rawP[buf_index + aa_index] * 10))
						percentage = raw_avg - (int)(rawP[buf_index + aa_index] * 10);
					else
						percentage = (int)(rawP[buf_index + aa_index] * 10) - raw_avg;
					if (percentage > ST_SELFTEST_STD_CALCULATE_LIMIT)
						percentage = ST_SELFTEST_STD_CALCULATE_LIMIT;
					rawS[aa_index] += percentage * percentage;
				}
				rawS[aa_index] = rawS[aa_index] / ST_SELFTEST_STD_FRAME_CNT;		
#ifndef USE_ST_SQRT		
				sqrt = int_sqrt((unsigned long) rawS[aa_index]);
#else
				sqrt = st_sqrt(rawS[aa_index]);
#endif	//end of USE_ST_SQRT

#ifdef ST_SELFTEST_LOG_FILE
				if (row == tMode[2] - 1 )
					snprintf(data1, 100, "%4ld.%1ld \n", sqrt/10, sqrt%10); //snprintf(data1, 100, "%6d \n", rawS[aa_index]);
				else
					snprintf(data1, 100, "%4ld.%1ld ", sqrt/10, sqrt%10);
				sitronix_vfswrite(data1, strlen(data1));
#endif
			}
		}
		
		for (col = 0; col < tMode[1]; col++)
		{
			for (row = 0; row < tMode[2]; row++)
			{
				aa_index = col * tMode[2] + row;
				if(st_get_test_enable(col, row) && (rawS[aa_index] > gts->self_test_std_square100_max)) {
					total_error++;
#ifndef USE_ST_SQRT	
					sqrt = int_sqrt((unsigned long) rawS[aa_index]);
#else
					sqrt = st_sqrt(rawS[aa_index]);
#endif//end of USE_ST_SQRT
					sterr("sensor (%2d,%2d) STD (%4ld.%1ld) > standard value (%4d.%1d) in STD test\n" , col, row, sqrt/10, sqrt%10, gts->self_test_std_max/10, gts->self_test_std_max%10);
#ifdef ST_SELFTEST_LOG_FILE
					snprintf(data1, 150, "sensor (%2d,%2d) STD (%4ld.%1ld) > standard value (%4d.%1d) in STD test\n" , col, row, sqrt/10, sqrt%10, gts->self_test_std_max/10, gts->self_test_std_max%10);
					sitronix_vfswrite(data1, strlen(data1));
#endif
				}
			}
		}
#ifdef ST_SELFTEST_LOG_FILE
		snprintf(data1, 100, "[STD end]\n");
		sitronix_vfswrite(data1, strlen(data1));
#endif
	}

st_open_short_test_finish:

	if (rawI)
		kfree(rawI);

	if (raw_buf)
		kfree(raw_buf);

	if (rawP)
		kfree(rawP);

	if (rawS)
		kfree(rawS);

	if ( ret < 0)
		return ret;
	else if (total_error == 0)
		return 0;
	else
		return total_error;
}


signed short *st_get_aa_rawdata(uint8_t *raw_cnt, uint8_t *aa_unit, int skipcol)
{
	unsigned char cmd[0x08];
	int tMode[8];
	int read_len;
	unsigned char *raw_buf = NULL;
	signed short *rawI = NULL;
	int frameCounter;
	int retryCounter;
	int max_retry = 50; //5;
	unsigned char raw_dat_rd_on[2] = { 0x02, 0x00 };
	unsigned char raw_dat_rd_off[2] = { 0x00, 0x00 };
	unsigned char raw_header[18];
	unsigned char frame_counter = 0;
	int buf_index , aa_index;
	int col , row;
#ifdef ST_SELFTEST_LOG_FILE
	char data1[150];
#endif
#ifdef __RAW_DATA_DEBUG__
	int i, j, k;
#endif //__RAW_DATA_DEBUG__


	/* ROW_CNT */
	sitronix_spi_pram_rw(true, 0xF04A, NULL, cmd, 2);
	tMode[2] = cmd[0]&0x3F;
	*raw_cnt = tMode[2];

	/* CMNC_CH_WR_EN */
	tMode[7] = cmd[1]&0x01;

	/* AA_UNIT */
	sitronix_spi_pram_rw(true, 0xF024, NULL, cmd, 2);
	tMode[1] = (cmd[0]&0xF0)>>3;
	*aa_unit = tMode[1];

	/* SELF_UNIT */		
	tMode[5] = (cmd[0]&0xC)>>1;

	/* NOISE_UNIT */
	tMode[4] = (cmd[0]&0x3)<<2;

	/* KEY */
	tMode[3] = 0;

	stmsg("ROW_CNT : %d\n",tMode[2]);
	stmsg("AA_UNIT : %d\n",tMode[1]);
	stmsg("SELF_UNIT : %d\n",tMode[5]);
	stmsg("NOISE_UNIT : %d\n",tMode[4]);
	stmsg("CMNC_CH_WR_EN : %d\n",tMode[7]);

	read_len = (tMode[2] + tMode[7]) * (tMode[1] + tMode[5] + tMode[4] + skipcol) * 2;
	raw_buf = (unsigned char*)kmalloc(read_len, GFP_KERNEL);
	rawI = (signed short *)kmalloc((tMode[1] * tMode[2] * sizeof(short)), GFP_KERNEL);

	//Ignore
	frameCounter = 0;
	retryCounter = 0;

	while (ST_SELFTEST_IGNORE_FRAME > 0 && retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);

#ifdef __RAW_DATA_DEBUG__
		// Read Rawdata.
		memset(raw_buf, 0, read_len);
		sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
		for (i = 0, j = 0; j < (tMode[1] + tMode[5] + tMode[4] + skipcol); j++) {
			stmsg("AA_UNIT[%d]: ", j);
			for (k = 0; k < (tMode[2] + tMode[7]); i++, k++) {
				short raw = (short)(raw_buf[i*2] << 8 | raw_buf[i*2 + 1]);
				printk("%d ", raw);
			}
			printk(" (i=%d, k=%d)\n", i, k);
		}
#endif //__RAW_DATA_DEBUG__

		stmsg("header %x \n",raw_header[1]);
		if(frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];
		}

		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

		if (frameCounter >= ST_SELFTEST_IGNORE_FRAME + 1)
			break;
	}

	if( retryCounter >=  max_retry)
	{
		sterr("st open test fail ,  can't wait IRQ \n");
#ifdef ST_SELFTEST_LOG_FILE
		snprintf(data1, 50, "st open test fail ,  can't wait IRQ\n");
		sitronix_vfswrite(data1, strlen(data1));
#endif
		goto rawdata_retry_err;
	}

	//raw
	frameCounter = 0;
	retryCounter = 0;

	while (retryCounter++ < max_retry)
	{
		msleep(10);
		sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_on, NULL, 2);
		sitronix_spi_pram_rw(true, 0xF180, NULL, raw_header, 18);
		if(frame_counter != raw_header[1])
		{
			frameCounter++;
			retryCounter = 0;
			frame_counter = raw_header[1];

			sitronix_spi_pram_rw(true, 0xD000, NULL, raw_buf, read_len);
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);

			aa_index = (tMode[1] / 2) * (tMode[2] + tMode[7]) *2;
			buf_index = (skipcol) * (tMode[2] + tMode[7]) *2;

			if (skipcol != 0)
				for(col = 0; col < aa_index; col++)
					raw_buf[aa_index + col ] = raw_buf[aa_index + col + buf_index];

			buf_index = 0;
			aa_index = 0;

			//AA A
			for (col = 0; col < tMode[1] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[aa_index++] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					//stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);

					buf_index +=2 ;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			//SE A
			for (col = 0; col < tMode[5] / 2; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index +=2 ; 
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			//AA B
			for (col = tMode[1] / 2; col < tMode[1]; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					rawI[aa_index++] = (signed short)((raw_buf[buf_index] << 8) + raw_buf[buf_index + 1]);
					//stmsg("sensor (%2d,%2d) RAW (%4d) \n" , col, row, rawI[row]);

					buf_index +=2 ;
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}

			//SE B
			for (col = tMode[5] / 2; col < tMode[5] ; col++)
			{
				for (row = 0; row < tMode[2]; row++)
				{
					buf_index +=2 ; 
				}

				if (tMode[7] != 0)
					buf_index += 2;
			}
			break;
		} else {
			sitronix_spi_pram_rw(false, 0xF004, raw_dat_rd_off, NULL, 2);
		}
	}

rawdata_retry_err:

	if (raw_buf)
		kfree(raw_buf);

	if (retryCounter >= max_retry) {
		if (rawI)
			kfree(rawI);
		return NULL;
	} else {
		return rawI;
	}
}
int st_test_open(void)
{
	int ret = 0;

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			return ret;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_open, sizeof(test_cmd_open));	//Open test without Mux On/Off.
	if (ret < 0)
		return ret;

	ret = st_address_mode_hardcode_write(test_cmd_channel_mapping, sizeof(test_cmd_channel_mapping));
	if (ret < 0)
		return ret;

	msleep(100);

	ret = st_open_short_test(0, 0);
	return ret;
}

int normal_rawdata_check(uint8_t raw_cnt, uint8_t aa_unit, signed short *normal_rawdata)
{
	int col, row, raw_index;
	signed short rawdata;
	int total_error = 0;
#ifdef ST_SELFTEST_LOG_FILE
	char data1[150];
	int i, j;
	int min = 0xFFF0, max = -0xFFF0;
#endif	//ST_SELFTEST_LOG_FILE

#ifdef __RAW_DATA_DEBUG__
	stmsg("NORMAL_RAW:\n");
	for (raw_index = 0, i = 0; i < aa_unit; i++) {
		stmsg("AA_UNIT[%d]: ", i);
		for (j = 0; j < raw_cnt; j++) {
			printk("%d ", normal_rawdata[raw_index++]);
		}
		printk("\n");
	}
#endif //__RAW_DATA_DEBUG__

#ifdef ST_SELFTEST_LOG_FILE
	snprintf(data1, 100, "[NORMAL RAW Data start]\n");
	sitronix_vfswrite(data1, strlen(data1));

	for (raw_index = 0, i = 0; i < aa_unit; i++) {
		for (j = 0; j < raw_cnt; j++) {
			rawdata = normal_rawdata[raw_index];
			snprintf(data1, 100, "%6d ", rawdata);
			sitronix_vfswrite(data1, strlen(data1));
			raw_index++;
			if (rawdata < min)
				min = (int)rawdata;
			if (rawdata > max)
				max = (int)rawdata;
		}
		snprintf(data1, 100, "\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	snprintf(data1, 100, "[NORMAL RAW Data end]        (Min/Max: %d / %d)\n", min, max);
	sitronix_vfswrite(data1, strlen(data1));
#endif //ST_SELFTEST_LOG_FILE

	for (raw_index = 0, col = 0; col < aa_unit; col++) {
		for (row = 0; row < raw_cnt; row++) {
			rawdata = normal_rawdata[raw_index];
			if(st_get_test_enable(col, row) && (rawdata < gts->self_test_normal_min || rawdata > gts->self_test_normal_max))
			{
				total_error++;
				sterr("sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in normal rawdata test.\n",
						col, row, rawdata, gts->self_test_normal_min, gts->self_test_normal_max);
#ifdef ST_SELFTEST_LOG_FILE
				snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) out of range (%d ~ %d) in normal rawdata test.\n",
						col, row, rawdata, gts->self_test_normal_min, gts->self_test_normal_max);
				sitronix_vfswrite(data1, strlen(data1));
#endif	//ST_SELFTEST_LOG_FILE
			}
			raw_index++;
		}
	}

	return total_error;
}

int open_mux_on_off_check_delta(uint8_t raw_cnt, uint8_t aa_unit, signed short *mux_on_raw, signed short *mux_off_raw)
{
	int col, row, raw_index;
	signed short delta;
	int total_error = 0;
#ifdef ST_SELFTEST_LOG_FILE
	char data1[150];
	int i, j;
	int min = 0xFFF0, max = -0xFFF0;
#endif	//ST_SELFTEST_LOG_FILE

#ifdef __RAW_DATA_DEBUG__
	stmsg("MUX_ON_RAW:\n");
	for (raw_index = 0, i = 0; i < aa_unit; i++) {
		stmsg("AA_UNIT[%d]: ", i);
		for (j = 0; j < raw_cnt; j++) {
			printk("%d ", mux_on_raw[raw_index++]);
		}
		printk("\n");
	}

	stmsg("MUX_OFF_RAW:\n");
	for (raw_index = 0, i = 0; i < aa_unit; i++) {
		stmsg("AA_UNIT[%d]: ", i);
		for (j = 0; j < raw_cnt; j++) {
			printk("%d ", mux_off_raw[raw_index++]);
		}
		printk("\n");
	}

	stmsg("MUX_ON_OFF_DELTA:\n");
	for (raw_index = 0, i = 0; i < aa_unit; i++) {
		stmsg("AA_UNIT[%d]: ", i);
		for (j = 0; j < raw_cnt; j++) {
			delta = mux_on_raw[raw_index] - mux_off_raw[raw_index];
			printk("%d ", delta);
			raw_index++;
		}
		printk("\n");
	}
#endif //__RAW_DATA_DEBUG__

#ifdef ST_SELFTEST_LOG_FILE
	snprintf(data1, 100, "[OPEN RAW Data start]\n");
	sitronix_vfswrite(data1, strlen(data1));

	for (raw_index = 0, i = 0; i < aa_unit; i++) {
		for (j = 0; j < raw_cnt; j++) {
			delta = mux_on_raw[raw_index] - mux_off_raw[raw_index];
			snprintf(data1, 100, "%6d ", delta);
			sitronix_vfswrite(data1, strlen(data1));
			raw_index++;
			if (delta < min)
				min = (int)delta;
			if (delta > max)
				max = (int)delta;
		}
		snprintf(data1, 100, "\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	snprintf(data1, 100, "[OPEN RAW Data end]        (Min/Max: %d / %d)\n", min, max);
	sitronix_vfswrite(data1, strlen(data1));
#endif //ST_SELFTEST_LOG_FILE

	for (raw_index = 0, col = 0; col < aa_unit; col++) {
		for (row = 0; row < raw_cnt; row++) {
			delta = mux_on_raw[raw_index] - mux_off_raw[raw_index];
			if(st_get_test_enable(col, row) && (delta < gts->self_test_open_mux_on_off_min))
			{
				total_error++;
				sterr("sensor (%2d,%2d) RAW (%4d) is less than min value(< %d) in open mux on/off test.\n",
						col, row, delta, gts->self_test_open_mux_on_off_min);
#ifdef ST_SELFTEST_LOG_FILE
				snprintf(data1, 100, "sensor (%2d,%2d) RAW (%4d) is less than min value(< %d) in open mux on/off test.\n",
						col, row, delta, gts->self_test_open_mux_on_off_min);
				sitronix_vfswrite(data1, strlen(data1));
#endif	//ST_SELFTEST_LOG_FILE
			}
			raw_index++;
		}
	}

	return total_error;
}


int st_test_normal_rawdata(void)
{
	int ret = 0;
	signed short *normal_rawdata = NULL;
	uint8_t raw_cnt, aa_unit;
	char *test_cmd_normal_rawdata_ptr=test_cmd_normal_rawdata;//add by lny
	if (test_cmd_normal_rawdata_ptr == NULL) {
		sterr("Normal rawdata test fail! (test_cmd_normal_rawdata==NULL)\n");
		return -1;
	}

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			goto normal_rawdata_err;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_normal_rawdata, sizeof(test_cmd_normal_rawdata));
	if (ret < 0)
		goto normal_rawdata_err;

	ret = st_address_mode_hardcode_write(test_cmd_channel_mapping, sizeof(test_cmd_channel_mapping));
	if (ret < 0)
		goto normal_rawdata_err;

	stmsg("Normal Raw\n");
	normal_rawdata = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0);

	if (!normal_rawdata) {
		sterr("Normal rawdata test fail! (normal_rawdata==NULL)\n");
		ret = -1;
		goto normal_rawdata_err;
	}

	ret = normal_rawdata_check(raw_cnt, aa_unit, normal_rawdata);
	if (ret < 0)
		goto normal_rawdata_err;

normal_rawdata_err:

	if (normal_rawdata)
		kfree(normal_rawdata);

	msleep(100);

	return ret;
}


int st_test_open_mux_on_off(void)
{
	int ret = 0;
	signed short *mux_on_raw = NULL;
	signed short *mux_off_raw = NULL;
	uint8_t raw_cnt, aa_unit;
	char *test_cmd_open_mux_on_ptr=test_cmd_open_mux_on;
	char *test_cmd_open_mux_off_ptr=test_cmd_open_mux_off;
	if ((test_cmd_open != NULL) && (test_cmd_open_mux_on_ptr == NULL || test_cmd_open_mux_off_ptr == NULL)) {	// for ST7123 Open test.
		ret = st_test_open();
		msleep(100);
		return ret;
	}

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			goto open_mux_on_off_err;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_open_mux_on, sizeof(test_cmd_open_mux_on));
	if (ret < 0)
		goto open_mux_on_off_err;

	ret = st_address_mode_hardcode_write(test_cmd_channel_mapping, sizeof(test_cmd_channel_mapping));
	if (ret < 0)
		goto open_mux_on_off_err;

	stmsg("MuxOn Raw\n");
	mux_on_raw = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0);

	if (!mux_on_raw) {
		sterr("Open test fail! (mux_on_raw==NULL)\n");
		ret = -1;
		goto open_mux_on_off_err;
	}


	ret = st_address_mode_hardcode_write(test_cmd_open_mux_off, sizeof(test_cmd_open_mux_off));
	if (ret < 0)
		goto open_mux_on_off_err;

	ret = st_address_mode_hardcode_write(test_cmd_channel_mapping, sizeof(test_cmd_channel_mapping));
	if (ret < 0)
		goto open_mux_on_off_err;

	stmsg("MuxOff Raw\n");
	mux_off_raw = st_get_aa_rawdata(&raw_cnt, &aa_unit, 0);

	if (!mux_off_raw) {
		sterr("Open test fail! (mux_off_raw==NULL)\n");
		ret = -1;
		goto open_mux_on_off_err;
	}

	ret = open_mux_on_off_check_delta(raw_cnt, aa_unit, mux_on_raw, mux_off_raw);

	if (ret < 0)
		goto open_mux_on_off_err;

open_mux_on_off_err:

	if (mux_on_raw)
		kfree(mux_on_raw);

	if (mux_off_raw)
		kfree(mux_off_raw);

	msleep(100);

	return ret;
}

int st_test_short_even(void)
{
	int ret = 0;

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			return ret;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_short_even, sizeof(test_cmd_short_even));
	if (ret < 0)
		return ret;

	msleep(100);

	ret = st_open_short_test(2, ST_SELFTEST_SKIP_COLS);

	return ret;
}
int st_test_short_odd(void)
{
	int ret = 0;

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			return ret;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_short_odd, sizeof(test_cmd_short_odd));
	if (ret < 0)
		return ret;

	msleep(100);

	ret = st_open_short_test(1, ST_SELFTEST_SKIP_COLS);
	return ret;
}

int st_test_uniformity(void)
{
	int ret = 0;

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			return ret;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_uniformity, sizeof(test_cmd_uniformity));
	if (ret < 0)
		return ret;

	ret = st_address_mode_hardcode_write(test_cmd_channel_mapping, sizeof(test_cmd_channel_mapping));
	if (ret < 0)
		return ret;

	msleep(100);

	ret = st_open_short_test(3, 0);
	return ret;
}

int st_test_std(void)
{
	int ret = 0;

	sitronix_ts_reset_device(gts);
//#ifdef SITRONIX_TP_WITH_FLASH
	if (test_flash_afe_df != NULL) {
		ret = st_address_mode_hardcode_write(test_flash_afe_df, sizeof(test_flash_afe_df));
		if (ret < 0)
			return ret;
	}
//#endif /* SITRONIX_TP_WITH_FLASH */

	ret = st_address_mode_hardcode_write(test_cmd_std, sizeof(test_cmd_std));
	if (ret < 0)
		return ret;

	ret = st_address_mode_hardcode_write(test_cmd_channel_mapping, sizeof(test_cmd_channel_mapping));
	if (ret < 0)
		return ret;

	msleep(100);

	ret = st_open_short_test(4, 0);
	return ret;
}


#ifdef ST_SELFTEST_LOG_FILE
void st_record_ic_info(void)
{
	int ret = 0;
	char data1[50];

	ret = sitronix_ts_get_device_info(gts);
	if (ret < 0 ) {
		snprintf(data1, 50, "sitronix_ts_get_device_info failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	snprintf(data1, 50, "Chip ID = %02X\n", gts->ts_dev_info.chip_id);
	sitronix_vfswrite(data1, strlen(data1));
	snprintf(data1, 50, "FW Verison = %02X\n", gts->ts_dev_info.fw_version);
	sitronix_vfswrite(data1, strlen(data1));
	snprintf(data1, 50, "FW Revision = %02X %02X %02X %02X\n", gts->ts_dev_info.fw_revision[0], gts->ts_dev_info.fw_revision[1], gts->ts_dev_info.fw_revision[2], gts->ts_dev_info.fw_revision[3]);
	sitronix_vfswrite(data1, strlen(data1));
	snprintf(data1, 50, "Resolution = %d x %d\n", gts->ts_dev_info.x_res, gts->ts_dev_info.y_res);
	sitronix_vfswrite(data1, strlen(data1));
	snprintf(data1, 50, "Channels = %d x %d\n", gts->ts_dev_info.x_chs, gts->ts_dev_info.y_chs);
	sitronix_vfswrite(data1, strlen(data1));
	snprintf(data1, 50, "Max touches = %d\n", gts->ts_dev_info.max_touches);
	sitronix_vfswrite(data1, strlen(data1));
	snprintf(data1, 50, "Misc. Info = 0x%X\n", gts->ts_dev_info.misc_info);
	sitronix_vfswrite(data1, strlen(data1));

	ret = sitronix_get_ic_sfrver();
	if (ret < 0 ) {
		snprintf(data1, 50, "sitronix_get_ic_sfrver failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		snprintf(data1, 50, "IC SFR VER = 0x%X\n", ret);
		sitronix_vfswrite(data1, strlen(data1));
	}
}
#endif


#define TEST_FW_BUF_LEN			80
#define TEST_FW_FRAME_SKIP		10
#define TEST_FW_FRAME_CNT		10
#define TEST_FW_N_STD			64	//8^2

int st_test_fw(void)
{	
	int testFailCnt = 0;
	int ret = 0;
	uint8_t *buf = NULL;
	int *rawFrame = NULL;
	int *rawRec = NULL;
	int *rawStd = NULL;
	int lastSenCnt = 0;
	int checkCnt = 0;
	int i, rawCnt, retry, retrymax, ix, iy, datasize, frameNum;
	int *rawP = NULL;
	int tmpsVal;
#ifdef ST_SELFTEST_LOG_FILE
	char data1[150];
	
	snprintf(data1, 100, "[Test Firmware Start]\n");
	sitronix_vfswrite(data1, strlen(data1));
#endif
	stmsg("[Test Firmware Start]\n");


	buf = kmalloc(TEST_FW_BUF_LEN, GFP_KERNEL);

	//fw info
	if(gts->ts_dev_info.x_chs <= 0 || gts->ts_dev_info.y_chs <= 0){
#ifdef ST_SELFTEST_LOG_FILE
		snprintf(data1, 100, "Failed : Invalid Channel Number = %d x %d\n",
			gts->ts_dev_info.x_chs, gts->ts_dev_info.y_chs);
		sitronix_vfswrite(data1, strlen(data1));
#endif
		stmsg("Failed : Invalid Channel Number = %d x %d\n", 
				gts->ts_dev_info.x_chs, gts->ts_dev_info.y_chs);

			testFailCnt++;
	}

	rawCnt = (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs) * gts->ts_dev_info.y_chs;
	rawFrame =  (int *)kmalloc( rawCnt* sizeof(int), GFP_KERNEL);
	rawStd =  (int *)kmalloc( rawCnt* sizeof(int), GFP_KERNEL);
	rawRec = (int *)kmalloc( (rawCnt* sizeof(int) * TEST_FW_FRAME_CNT), GFP_KERNEL);

	//ic status
	memset(buf, 0, TEST_FW_BUF_LEN);
	ret = sitronix_ts_reg_read(gts, STATUS_REG, buf, 1);
	if(ret < 0){
#ifdef ST_SELFTEST_LOG_FILE
		snprintf(data1, 100, "Error : Read Status register error!\n");
		sitronix_vfswrite(data1, strlen(data1));
#endif
		sterr("%s: Read Status register error!(%d)\n", __func__, ret);
		testFailCnt++;
	}
	else{
		if((buf[0] & 0xF0) > 0){
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 100, "Failed : IC Status Error Code = %02X\n", ((buf[0] & 0xF0) >> 4));
			sitronix_vfswrite(data1, strlen(data1));
#endif	
			stmsg("Failed : IC Status Error Code = %02X\n", ((buf[0] & 0xF0) >> 4));

			testFailCnt++;
		}
	}
	
	//sensing count
	for(i = 0 ; i < 5 ; i++){
		memset(buf, 0, TEST_FW_BUF_LEN);
		ret = sitronix_ts_reg_read(gts, SENSING_COUNTER_H, buf, 2);
		if(ret < 0){
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 100, "Error : Read sensing counter error!\n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
			sterr("%s: Read sensing counter error!(%d)\n", __func__, ret);
			testFailCnt++;
		}
		else{
			//stmsg("sensing counter = %02X %02X\n", buf[0], buf[1]);
			if(lastSenCnt != (int)(buf[0] << 8 | buf[1])){
				lastSenCnt = (int)(buf[0] << 8 | buf[1]);
				checkCnt = 0;
			}
			else{
				checkCnt++;
				if(checkCnt >= 3){
#ifdef ST_SELFTEST_LOG_FILE
					snprintf(data1, 100, "Failed : sensing counter error\n");
					sitronix_vfswrite(data1, strlen(data1));
#endif
					stmsg("Failed : sensing counter error.\n");
					testFailCnt++;
					break;
				}
			}
		}
		msleep(10);
	}
	//touch valid bit
	for(i = 0 ; i < 5 ; i++){
		memset(buf, 0, sizeof(TEST_FW_BUF_LEN));
		ret = sitronix_ts_reg_read(gts, TOUCH_INFO, buf, TEST_FW_BUF_LEN);
		if(ret < 0){
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 100, "Error : Read touch info error\n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
			sterr("%s: Read touch info error!(%d)\n", __func__, ret);		
			testFailCnt++;
		}
		else{
			//stmsg("X0H = %02X\n", buf[4]);
			if((buf[4] & 0x80) != 0){
#ifdef ST_SELFTEST_LOG_FILE
				snprintf(data1, 100, "Failed : touch detected\n");
				sitronix_vfswrite(data1, strlen(data1));
#endif
				stmsg("Failed : touch detected!\n");
				testFailCnt++;
				break;
			}
		}
		msleep(10);
	}

	
	//stay in Active mode
	memset(buf, 0, TEST_FW_BUF_LEN);
	buf[0] = 0x01;
	buf[1] = 0x06;
	buf[2] = 0x01;
	buf[5] = 0x01;
	buf[7] = 0x26;
	sitronix_ts_reg_write(gts, CMDIO_PORT, buf, 8);

	memset(buf, 0, TEST_FW_BUF_LEN);
	buf[0] = 0x01;
	sitronix_ts_reg_write(gts, CMDIO_CONTROL, buf, 1);
	msleep(50);
	//dist should not be 0 by line
	frameNum = 0;
	retry = 0;
	retrymax = (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs) * 2;
	//stmsg("Do Dist test\n");
	while(retry < retrymax){	//wait for header
		ret = sitronix_ts_reg_read(gts, DATA_OUTPUT_BUFFER, buf, TEST_FW_BUF_LEN);
		if(ret < 0){
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 100, "Error : Wait Dist. error\n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
			sterr("%s:Wait Dist. error!(%d)\n", __func__, ret);

			testFailCnt++;
			goto st_test_fw_end;
		}
		if(buf[0] == 0x10)	//Header
		{
			break;
		}
		else{
			retry++;
			if(retry >= retrymax){
#ifdef ST_SELFTEST_LOG_FILE
				snprintf(data1, 100, "Error : Read Dist Header error\n");
				sitronix_vfswrite(data1, strlen(data1));
#endif
				sterr("Error : Read Dist Header error!\n");
				testFailCnt++;
				goto st_test_fw_end;
			}
		}
	}//end of wait header
	frameNum = 0;
	retry = 0;
	while( (frameNum < (TEST_FW_FRAME_SKIP + TEST_FW_FRAME_CNT)) && retry < retrymax){
		
		ret = sitronix_ts_reg_read(gts, DATA_OUTPUT_BUFFER, buf, TEST_FW_BUF_LEN);
		//stmsg("Do Dist test step 2 type = 0x%02X\n", buf[0]);
		if(ret < 0){
#ifdef ST_SELFTEST_LOG_FILE
			snprintf(data1, 100, "Error : Read Dist error\n");
			sitronix_vfswrite(data1, strlen(data1));
#endif
			sterr("%s: Read Dist. error!(%d)\n", __func__, ret);
			testFailCnt++;
			goto st_test_fw_end;
		} 
		if(buf[0] == 0x10)
		{	//header
			if(frameNum >= TEST_FW_FRAME_SKIP){
				//verify data
				//printk("Frame 0x%02X ======= \n", frameNum);
				//for(ix = 0; ix < (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs); ix++){
				for(ix = 0; ix < (gts->ts_dev_info.x_chs); ix++){
					checkCnt = 0;
					for(iy = 0 ; iy < gts->ts_dev_info.y_chs; iy++){
		
						//printk("%3d ", rawFrame[ix * gts->ts_dev_info.y_chs + iy]);
						if(rawFrame[ix * gts->ts_dev_info.y_chs + iy] == 0){
							checkCnt++;
						}
					}
					//printk("\n");
					if(checkCnt == gts->ts_dev_info.y_chs){
#ifdef ST_SELFTEST_LOG_FILE
						snprintf(data1, 100, "Failed : Dist of X%02d are 0!\n", ix);
						sitronix_vfswrite(data1, strlen(data1));
#endif
						stmsg("Failed : Dist of X%02d are 0!\n", ix);
						testFailCnt++;
					} 
				}
				rawP = &(rawRec[ rawCnt*(frameNum-TEST_FW_FRAME_SKIP) ]);
				memcpy(rawP, rawFrame, rawCnt* sizeof(int));
				//memcpy(rawRec+((rawCnt* sizeof(short)*(frameNum-TEST_FW_FRAME_SKIP))), rawFrame, rawCnt* sizeof(short));
				//printk("===== =====\n");
				if(testFailCnt > 0){
					goto st_test_fw_end;
				}
			}
			retry = 0;
			frameNum++;
			memset(rawFrame, 0, rawCnt* sizeof(int));
			if(frameNum >= (TEST_FW_FRAME_SKIP + TEST_FW_FRAME_CNT)){
				break;
			}
		}//endof header
		else if(buf[0] == 0x93){	//AA
			ix = buf[2];
			iy = buf[3];
			datasize =  (buf[1] - 3) / 2; 
			for(i = 0; i < datasize; i++){   
				rawFrame[ix * gts->ts_dev_info.y_chs + iy + i] = (signed short)((buf[2 + i*2 + 2] << 8) | (buf[2+ i*2 + 3]));
			}
			retry = 0;
		}
		else if(buf[0] == 0x95){	//noise
			ix = buf[2] + gts->ts_dev_info.x_chs;
			iy = buf[3];
			datasize =  (buf[1] - 3) / 2; 
			for(i = 0; i < datasize; i++){   
				rawFrame[ix * gts->ts_dev_info.y_chs + iy + i] = (signed short)((buf[2 + i*2 + 2] << 8) | (buf[2+ i*2 + 3]));
			}
			retry = 0;
		}
		//else if(buf[0] == 0x00){	//not ready
		//	retry++;
		//}
		else{	//unexpected data, include 0x00 data not ready
			retry++;
			if(buf[0] == 0x00){
				msleep(10);
			}
			if(retry >= retrymax){
#ifdef ST_SELFTEST_LOG_FILE
				snprintf(data1, 100, "Error : Read Dist. timeout!\n");
				sitronix_vfswrite(data1, strlen(data1));
#endif	
				sterr("%s: Read Dist. timeout!\n", __func__);
				testFailCnt++;
				goto st_test_fw_end;
			}
		}
	}//end of read dist

	//std of noise line0/1 < 8
	memset(rawFrame, 0, rawCnt* sizeof(int));
	memset(rawStd, 0, rawCnt* sizeof(int));
	for(i = 0 ; i < TEST_FW_FRAME_CNT; i++){
		rawP = &(rawRec[ rawCnt * i]);
		for(ix = 0; ix < (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs); ix++){
			for(iy = 0 ; iy < gts->ts_dev_info.y_chs; iy++){
				rawFrame[ix * gts->ts_dev_info.y_chs + iy] += rawP[ix * gts->ts_dev_info.y_chs + iy];
				tmpsVal = 0;
			}
		}
	}//end of sum

	for(ix = 0; ix < (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs); ix++){
		for(iy = 0 ; iy < gts->ts_dev_info.y_chs; iy++){
			rawFrame[ix * gts->ts_dev_info.y_chs + iy] = rawFrame[ix * gts->ts_dev_info.y_chs + iy] / TEST_FW_FRAME_CNT;
		}
	}//end of mean
	for(i = 0 ; i < TEST_FW_FRAME_CNT; i++){
		rawP = &(rawRec[ rawCnt * i]);
		for(ix = 0; ix < (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs); ix++){
			for(iy = 0 ; iy < gts->ts_dev_info.y_chs; iy++){
				tmpsVal = rawP[ix * gts->ts_dev_info.y_chs + iy] - rawFrame[ix * gts->ts_dev_info.y_chs + iy];
				rawStd[ix * gts->ts_dev_info.y_chs + iy] += (tmpsVal*tmpsVal);
			}
		}
	}//end of sum of diffence square
	//printk("=== STD ===== \n");
	for(ix = 0; ix < (gts->ts_dev_info.x_chs + gts->ts_dev_info.n_chs); ix++){
		for(iy = 0 ; iy < gts->ts_dev_info.y_chs; iy++){
			rawStd[ix * gts->ts_dev_info.y_chs + iy] = rawStd[ix * gts->ts_dev_info.y_chs + iy] / TEST_FW_FRAME_CNT;
			//printk("%3d ", rawStd[ix * gts->ts_dev_info.y_chs + iy]);

			if(ix == gts->ts_dev_info.x_chs || ix == (gts->ts_dev_info.x_chs+1)){	//verify noise 0/1 line std
				if(rawStd[ix * gts->ts_dev_info.y_chs + iy] > TEST_FW_N_STD){
#ifdef ST_SELFTEST_LOG_FILE
					snprintf(data1, 100, "Failed : STD^2 of Noise(%2d, %2d) = %d", 
							ix, iy, rawStd[ix * gts->ts_dev_info.y_chs + iy]);
					sitronix_vfswrite(data1, strlen(data1));
#endif
					stmsg("Failed : STD^2 of Noise(%2d, %2d) = %d", 
						ix, iy, rawStd[ix * gts->ts_dev_info.y_chs + iy] );
					testFailCnt++;
				}
			}
		}
		//printk("\n");
	}//end of STD square
	//printk("==========\n");

st_test_fw_end:
#ifdef ST_SELFTEST_LOG_FILE
	snprintf(data1, 100, "[Test Firmware End]\n");
	sitronix_vfswrite(data1, strlen(data1));
#endif
	stmsg("[Test Firmware End]\n");

	if (buf)
		kfree(buf);
	if (rawFrame)
		kfree(rawFrame);
	if (rawRec)
		kfree(rawRec);
	if (rawStd)
		kfree(rawStd);

	return (testFailCnt>0)?1:0;
}//end of st_test_fw


/*
return of st_self_test
/ ret = 0 : test success
/ ret < 0 : test failed with error
/ ret > 0 : test failed with N sensors :
*/
int st_self_test(void)
{
	int ret = 0;
	int result_normal_rawdata = 0;
	int result_open = 0;
	int result_short_odd = 0;
	int result_short_even = 0;
	int result_uniformity = 0;
	int result_std = 0;
	int result_fw = 0;
	char ic_position[2];
#ifdef ST_SELFTEST_LOG_FILE
	char data1[50];
	
	sitonix_createlogfileok = true;
	/* st_record_ic_info(); */
#endif	
	//get IC position
	sitronix_get_ic_position(ic_position);
	stmsg("IC position X,Y = (%03d,%03d)\n", (((ic_position[0] & 0x1) <<4) | ic_position[1]),(ic_position[0] >> 4));
#ifdef ST_SELFTEST_LOG_FILE
	snprintf(data1, 50, "IC position X,Y = (%03d,%03d)\n", (((ic_position[0] & 0x1) <<4) | ic_position[1]),(ic_position[0] >> 4));
	sitronix_vfswrite(data1, strlen(data1));

	//fw test
	result_fw = st_test_fw();
	if( result_fw == 0) {
		stmsg("Test Firmware successed!\n");
		snprintf(data1, 50, "Test Firmware successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test Firmware failed!\n");
		snprintf(data1, 50, "Test Firmware failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	//normal rawdata test
	result_normal_rawdata = st_test_normal_rawdata();
	if( result_normal_rawdata == 0) {
		stmsg("Test normal rawdata successed!\n");
		snprintf(data1, 50, "Test normal rawdata successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test normal rawdata failed!\n");
		snprintf(data1, 50, "Test normal rawdata failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	//open test(mux on/off)
	result_open = st_test_open_mux_on_off();
	if( result_open == 0) {
		stmsg("Test open successed!\n");
		snprintf(data1, 50, "Test open successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test open failed!\n");
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_OPEN;
		snprintf(data1, 50, "Test open failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	//short test even
	result_short_even = st_test_short_even();
	if( result_short_even == 0) {
		stmsg("Test short_even successed!\n");
		snprintf(data1, 50, "Test short_even successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test short_even failed!\n");
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_SHORT;
		snprintf(data1, 50, "Test short_even failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
	//short test odd
	result_short_odd = st_test_short_odd();
	if( result_short_odd == 0) {
		stmsg("Test short_odd successed!\n");
		snprintf(data1, 50, "Test short_odd successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} 
	//add by maibo for close factory test begin
	/* else {
		stmsg("Test short_odd failed!\n");
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_SHORT;
		snprintf(data1, 50, "Test short_odd failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} */
#ifdef SITRONIX_UNIFORMITY_TEST_ENABLED
	//uniformity test
	result_uniformity = st_test_uniformity();
	if( result_uniformity == 0) {
		stmsg("Test uniformity successed!\n");
		snprintf(data1, 50, "Test uniformity successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test uniformity failed!\n");
		sitronix_tptest_result = sitronix_tptest_result | BEYOND_UNIFORMITY_LIMIT;
		snprintf(data1, 50, "Test uniformity failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
#endif //SITRONIX_UNIFORMITY_TEST_ENABLED

	//STD test
	result_std = st_test_std();
	if( result_std == 0) {
		stmsg("Test STD successed!\n");
		snprintf(data1, 50, "Test STD successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test STD failed!\n");
		sitronix_tptest_result = sitronix_tptest_result | TEST_BEYOND_MAX_LIMIT | TEST_BEYOND_MIN_LIMIT;
		snprintf(data1, 50, "Test STD failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
#else

	//firmware test
	result_fw = st_test_fw();
	if( result_fw == 0)
		stmsg("Test Firmware successed!\n");
	else
		stmsg("Test Firmware failed!\n");

	//normal rawdata test
	result_normal_rawdata = st_test_normal_rawdata();
	if( result_normal_rawdata == 0) {
		stmsg("Test normal rawdata successed!\n");
	} else {
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_OPEN;
		stmsg("Test normal rawdata failed!\n");
	}

	//open test
	result_open = st_test_open_mux_on_off();
	if( result_open == 0) {
		stmsg("Test open successed!\n");
	} else {
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_OPEN;
		stmsg("Test open failed!\n");
	}
	//short test odd
	result_short_odd = st_test_short_odd();
	if( result_short_odd == 0) {
		stmsg("Test short_odd successed!\n");
	} else {
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_SHORT;
		stmsg("Test short_odd failed!\n");
	}
	//short test even
	result_short_even = st_test_short_even();
	if( result_short_even == 0) {
		stmsg("Test short_even successed!\n");
	} else {
		sitronix_tptest_result = sitronix_tptest_result | TEST_GT_SHORT;
		stmsg("Test short_even failed!\n");
	}
#ifdef SITRONIX_UNIFORMITY_TEST_ENABLED
	//uniformity test
	result_uniformity = st_test_uniformity();
	if( result_uniformity == 0) {
		stmsg("Test uniformity successed!\n");
	} else {
		sitronix_tptest_result = sitronix_tptest_result | BEYOND_UNIFORMITY_LIMIT;
		stmsg("Test uniformity failed!\n");
	}
#endif //SITRONIX_UNIFORMITY_TEST_ENABLED

	//STD test
	result_std = st_test_std();
	if( result_std == 0) {
		stmsg("Test STD successed!\n");
	} else {
		sitronix_tptest_result = sitronix_tptest_result | TEST_BEYOND_MAX_LIMIT | TEST_BEYOND_MIN_LIMIT;
		stmsg("Test STD failed!\n");
	}
#endif
	if ( result_open < 0 || result_short_even < 0 || result_uniformity < 0 || result_std < 0 || result_fw < 0)
		ret = -1;
	else
		ret = result_open + result_short_even + result_uniformity + result_std + result_fw;

	return ret;
}

void sitronix_set_default_test_criteria(void)
{
	gts->self_test_normal_min = ST_SELFTEST_NORMAL_MIN;
	gts->self_test_normal_max = ST_SELFTEST_NORMAL_MAX;
	gts->self_test_short_max = ST_SELFTEST_SHORT_MAX;
	gts->self_test_open_min = ST_SELFTEST_OPEN_MIN;
	gts->self_test_open_max = ST_SELFTEST_OPEN_MAX;
	gts->self_test_open_mux_on_off_min = ST_SELFTEST_OPEN_MUX_ON_OFF_MIN;
	gts->self_test_uniformity_shift = ST_SELFTEST_UNIFORMITY_SHIFT;
	gts->self_test_uniformity_min = ST_SELFTEST_UNIFORMITY_MIN;
	gts->self_test_uniformity_max = ST_SELFTEST_UNIFORMITY_MAX;
	gts->self_test_std_max = ST_SELFTEST_STD_MAX;
	
	sitronix_request_test_criteria(ST_SELFTEST_INI_PATH);

	gts->self_test_std_square100_max = gts->self_test_std_max * gts->self_test_std_max;
}

void sitronix_replace_test_cmd(unsigned char *id)
{
#ifdef ST_REPLACE_TEST_CMD_BY_DISPLAY_ID
	if(id[0] == test_id_1[0] && id[1] == test_id_1[1] && id[2] == test_id_1[2]) {
		//do replace here
		stmsg("find display id = %X %X %X , replace test_cmd_*_id1 to test_cmd_*  \n",test_id_1[0], test_id_1[1], test_id_1[2]);

		if (test_cmd_normal_rawdata != NULL)
			memcpy(test_cmd_normal_data, test_cmd_open_normal_rawdata_id1, SITRONIX_TEST_CMD_NORMAL_MAX_LEN);

		if (test_cmd_open != NULL)
			memcpy(test_cmd_open , test_cmd_open_id1, SITRONIX_TEST_CMD_OPEN_MAX_LEN);

		if (test_cmd_open_mux_on != NULL)
			memcpy(test_cmd_open_mux_on , test_cmd_open_mux_on_id1, SITRONIX_TEST_CMD_OPEN_MAX_LEN);

		if (test_cmd_open_mux_off != NULL)
			memcpy(test_cmd_open_mux_off , test_cmd_open_mux_off_id1, SITRONIX_TEST_CMD_OPEN_MAX_LEN);

		memcpy(test_cmd_short_odd , test_cmd_short_odd_id1, SITRONIX_TEST_CMD_SHORT_ODD_MAX_LEN);
		memcpy(test_cmd_short_even , test_cmd_short_even_id1, SITRONIX_TEST_CMD_SHORT_EVEN_MAX_LEN);
		memcpy(test_cmd_uniformity , test_cmd_uniformity_id1, SITRONIX_TEST_CMD_UNIFORMITY_MAX_LEN);
		memcpy(test_cmd_std , test_cmd_std_id1, SITRONIX_TEST_CMD_STD_MAX_LEN);
		memcpy(golden_buf , golden_buf_id1, sizeof(golden_buf));
		
		gts->self_test_normal_min = ST_SELFTEST_NORMAL_MIN_ID1;
		gts->self_test_normal_max = ST_SELFTEST_NORMAL_MAX_ID1;
		gts->self_test_short_max = ST_SELFTEST_SHORT_MAX_ID1;
		gts->self_test_open_min = ST_SELFTEST_OPEN_MIN_ID1;
		gts->self_test_open_max = ST_SELFTEST_OPEN_MAX_ID1;
		gts->self_test_open_mux_on_off_min = ST_SELFTEST_OPEN_MUX_ON_OFF_MIN_ID1;
		gts->self_test_uniformity_shift = ST_SELFTEST_UNIFORMITY_SHIFT_ID1;
		gts->self_test_uniformity_min = ST_SELFTEST_UNIFORMITY_MIN_ID1;
		gts->self_test_uniformity_max = ST_SELFTEST_UNIFORMITY_MAX_ID1;
		gts->self_test_std_max = ST_SELFTEST_STD_MAX_ID1;

		sitronix_request_test_criteria(ST_SELFTEST_INI_PATH_ID1);

		gts->self_test_std_square100_max = gts->self_test_std_max * gts->self_test_std_max;
	}
	
#endif	/* ST_REPLACE_TEST_CMD_BY_DISPLAY_ID */
}

int sitronix_request_test_criteria(const char *name){
#ifdef ST_REQUEST_SELF_TEST_INI	
	u8 *tmp = NULL;
	u8 *buff = NULL;
	int ret = 0;
	const struct firmware *fw = NULL;

	ret = request_firmware(&fw, name, &gts->pdev->dev);
	if (ret == 0) {
		stmsg("selftesst INI request(%s) success\n", name);
		buff = kzalloc(fw->size, GFP_KERNEL);
		//stmsg("INI size is:%d\n", fw->size);
		if (!buff) {
			sterr("selftest INI buffer kzalloc fail\n");
			return -EPERM;
		}
		memcpy(buff, fw->data, fw->size);
		release_firmware(fw);

		/* self_test_normal_min */
		tmp = strstr(buff, "self_test_normal_min=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_normal_min=%d", &gts->self_test_normal_min);
			if (ret < 0)
				sterr("%s read self_test_normal_min error.\n", __func__);
			//stmsg("self_test_normal_min = %d\n", gts->self_test_normal_min);
		}
		/* self_test_normal_max */
		tmp = strstr(buff, "self_test_normal_max=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_normal_max=%d", &gts->self_test_normal_max);
			if (ret < 0)
				sterr("%s read self_test_normal_max error.\n", __func__);
			//stmsg("self_test_normal_max = %d\n", gts->self_test_normal_max);
		}

		/* self_test_short_max */
		tmp = strstr(buff, "self_test_short_max=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_short_max=%d", &gts->self_test_short_max);
			if (ret < 0)
				sterr("%s read self_test_short_max error.\n", __func__);
			//stmsg("self_test_short_max = %d\n", gts->self_test_short_max);
		}
		/* self_test_open_min */
		tmp = strstr(buff, "self_test_open_min=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_open_min=%d", &gts->self_test_open_min);
			if (ret < 0)
				sterr("%s read self_test_open_min error.\n", __func__);
			//stmsg("self_test_open_min = %d\n", gts->self_test_open_min);
		}
		/* self_test_open_max */
		tmp = strstr(buff, "self_test_open_max=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_open_max=%d", &gts->self_test_open_max);
			if (ret < 0)
				sterr("%s read self_test_open_max error.\n", __func__);
			//stmsg("self_test_open_max = %d\n", gts->self_test_open_max);
		}
		/* self_test_open_mux_on_off_min */
		tmp = strstr(buff, "self_test_open_mux_on_off_min=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_open_mux_on_off_min=%d", &gts->self_test_open_mux_on_off_min);
			if (ret < 0)
				sterr("%s read self_test_open_mux_on_off_min error.\n", __func__);
			//stmsg("self_test_open_mux_on_off_min = %d\n", gts->self_test_open_mux_on_off_min);
		}
		/* self_test_uniformity_shift */
		tmp = strstr(buff, "self_test_uniformity_shift=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_uniformity_shift=%d", &gts->self_test_uniformity_shift);
			if (ret < 0)
				sterr("%s read self_test_uniformity_shift error.\n", __func__);
			//stmsg("self_test_uniformity_shift = %d\n", gts->self_test_uniformity_shift);
		}
		/* self_test_uniformity_min */
		tmp = strstr(buff, "self_test_uniformity_min=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_uniformity_min=%d", &gts->self_test_uniformity_min);
			if (ret < 0)
				sterr("%s read self_test_uniformity_min error.\n", __func__);
			//stmsg("self_test_uniformity_min = %d\n", gts->self_test_uniformity_min);
		}
		/* self_test_uniformity_max */
		tmp = strstr(buff, "self_test_uniformity_max=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_uniformity_max=%d", &gts->self_test_uniformity_max);
			if (ret < 0)
				sterr("%s read self_test_uniformity_max error.\n", __func__);
			//stmsg("self_test_uniformity_max = %d\n", gts->self_test_uniformity_max);
		}
		/* self_test_std_max */
		tmp = strstr(buff, "self_test_std_max=");
		if (tmp != NULL) {
			ret = sscanf(tmp, "self_test_std_max=%d", &gts->self_test_std_max);
			if (ret < 0)
				sterr("%s read self_test_std_max error.\n", __func__);
			//stmsg("self_test_std_max = %d\n", gts->self_test_std_max);
		}
		kfree(buff);
	} else {
		sterr("firmware request(%s) fail,ret=%d", name, ret);
	}
#endif //ST_REQUEST_SELF_TEST_INI
	return 0;
}

//[CC]FIH factory functions
bool sitonix_resultselftest = false; // FIH self test result


void sitronix_touch_selftest(void)
{
	int ret = 0;
	int result_normal_rawdata = 0;
	int result_open = 0;
	int result_short_odd = 0;
	int result_short_even = 0;
	int result_uniformity = 0;
	int result_std = 0;
	//int result_fw;
	char ic_position[2];
#ifdef ST_SELFTEST_LOG_FILE
	char data1[50];

	sitonix_createlogfileok = true;
#endif

	sitronix_ts_irq_enable(gts, false);
	gts->upgrade_doing = true;
	mutex_lock(&gts->mutex);
#ifdef ST_SELFTEST_LOG_FILE
	st_record_ic_info();
#endif
	//get IC position
	sitronix_get_ic_position(ic_position);
	stmsg("IC position X,Y = (%03d,%03d)\n", (((ic_position[0] & 0x1) <<4) | ic_position[1]),(ic_position[0] >> 4));
#ifdef ST_SELFTEST_LOG_FILE
	snprintf(data1, 50, "IC position X,Y = (%03d,%03d)\n", (((ic_position[0] & 0x1) <<4) | ic_position[1]),(ic_position[0] >> 4));
	sitronix_vfswrite(data1, strlen(data1));


	result_fw = st_test_fw();
	if( result_fw == 0) {
		stmsg("Test Firmware successed!\n");
		snprintf(data1, 50, "Test Firmware successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test Firmware failed!\n");
		snprintf(data1, 50, "Test Firmware failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
	
	//normal rawdata test
	result_normal_rawdata = st_test_normal_rawdata();
	if( result_normal_rawdata == 0) {
		stmsg("Test normal rawdata successed!\n");
		snprintf(data1, 50, "Test normal rawdata successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test normal rawdata failed!\n");
		snprintf(data1, 50, "Test normal rawdata failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

	//open test
	result_open = st_test_open();
	if( result_open == 0) {
		stmsg("Test open successed!\n");
		snprintf(data1, 50, "Test open successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test open failed!\n");
		snprintf(data1, 50, "Test open failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
	//short test odd
	result_short_odd = st_test_short_odd();
	if( result_short_odd == 0) {
		stmsg("Test short_odd successed!\n");
		snprintf(data1, 50, "Test short_odd successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test short_odd failed!\n");
		snprintf(data1, 50, "Test short_odd failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
	//short test even
	result_short_even = st_test_short_even();
	if( result_short_even == 0) {
		stmsg("Test short_even successed!\n");
		snprintf(data1, 50, "Test short_even successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test short_even failed!\n");
		snprintf(data1, 50, "Test short_even failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}

#ifdef SITRONIX_UNIFORMITY_TEST_ENABLED
	//uniformity test
	result_uniformity = st_test_uniformity();
	if( result_uniformity == 0) {
		stmsg("Test uniformity successed!\n");
		snprintf(data1, 50, "Test uniformity successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test uniformity failed!\n");
		snprintf(data1, 50, "Test uniformity failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
#endif //SITRONIX_UNIFORMITY_TEST_ENABLED

	//STD test
	result_std = st_test_std();
	if( result_std == 0) {
		stmsg("Test STD successed!\n");
		snprintf(data1, 50, "Test STD successed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	} else {
		stmsg("Test STD failed!\n");
		snprintf(data1, 50, "Test STD failed!\n");
		sitronix_vfswrite(data1, strlen(data1));
	}
#else

	//normal rawdata test
	result_normal_rawdata = st_test_normal_rawdata();
	if( result_normal_rawdata == 0)
		stmsg("Test normal rawdata successed!\n");
	else
		stmsg("Test normal rawdata failed!\n");

	//open test(mux on/off)
	result_open = st_test_open_mux_on_off();
	if( result_open == 0)
		stmsg("Test open successed!\n");
	else
		stmsg("Test open failed!\n");
	//short test odd
	result_short_odd = st_test_short_odd();
	if( result_short_odd == 0)
		stmsg("Test short_odd successed!\n");
	else
		stmsg("Test short_odd failed!\n");	
	//short test even
	result_short_even = st_test_short_even();
	if( result_short_even == 0)
		stmsg("Test short_even successed!\n");
	else
		stmsg("Test short_even failed!\n");

#ifdef SITRONIX_UNIFORMITY_TEST_ENABLED
	//uniformity test
	result_uniformity = st_test_uniformity();
	if( result_uniformity == 0)
		stmsg("Test uniformity successed!\n");
	else
		stmsg("Test uniformity failed!\n");
#endif //SITRONIX_UNIFORMITY_TEST_ENABLED

	//STD test
	result_std = st_test_std();
	if( result_std == 0)
		stmsg("Test STD successed!\n");
	else
		stmsg("Test STD failed!\n");
#endif	
	if ( result_open < 0 || result_short_odd < 0 || result_short_even < 0 || result_uniformity < 0 || result_std < 0)
		ret = -1;
	else
		ret = result_open + result_short_odd + result_short_even + result_uniformity + result_std;

	if(ret == 0)
		sitonix_resultselftest = 1;
	else
		sitonix_resultselftest = 0;

	mutex_unlock(&gts->mutex);

	sitronix_ts_mt_reset_process();
	gts->upgrade_doing = false;
	sitronix_ts_irq_enable(gts, true);

}

int sitronix_selftest_result_read(void)
{
    int num_read_chars = 0;
    if(sitonix_resultselftest)
        num_read_chars = 0;
    else
        num_read_chars = 1;
    return num_read_chars;
}

#ifdef USE_ST_SQRT
int st_sqrt(int x) {
	int l , h, mid, sqrt;
	if (x <= 1) {
        return x;
    }
    l = 1;
	h = x;
    while (l <= h) {
        mid = l + (h - l) / 2;
        sqrt = x / mid;
        if (sqrt == mid) {
            return mid;
        } else if (mid > sqrt) {
            h = mid - 1;
        } else {
            l = mid + 1;
        }
    }
    return h;
}
#endif
