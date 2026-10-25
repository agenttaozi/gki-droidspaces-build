//read emmc ddr infomation.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/kprobes.h>
#include <linux/jiffies.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>


/* Vendor add, for display the emmc id string in desired format, 1/2 */
#include <linux/string_helpers.h>

#define ZTE_BSP_LOG_PREFIX "[ZTE_EMMC_INFO][EMMC]"
#define log_err(fmt, ...) pr_err(ZTE_BSP_LOG_PREFIX "[ERR]" fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) pr_warn(ZTE_BSP_LOG_PREFIX "[WARN]" fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) pr_info(ZTE_BSP_LOG_PREFIX "[INFO]" fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) pr_debug(ZTE_BSP_LOG_PREFIX "[DEBUG]" fmt, ##__VA_ARGS__)
#define UNSTUFF_BITS(resp,start,size)					\
	({								\
		const int __size = size;				\
		const u32 __mask = (__size < 32 ? 1 << __size : 0) - 1;	\
		const int __off = 3 - ((start) / 32);			\
		const int __shft = (start) & 31;			\
		u32 __res;						\
									\
		__res = resp[__off] >> __shft;				\
		if (__size + __shft > 32)				\
			__res |= resp[__off-1] << ((32 - __shft) % 32);	\
		__res & __mask;						\
	})

static struct device  *device_t;
static struct class  *device_class;

static int memory;
static char *manf = "UNKNOW";
static struct mmc_host *host;
static struct mmc_card *card;
static char emmcinfo_buf[128]= {0};

//ddr manfname list
typedef struct _ddr_manf_info {
	int id;
	char *listname;
} ddr_manf_info;

ddr_manf_info ddr_list[] = {
	{0x01, "Samsung"},
	{0x02, "Infineon"},
	{0x03, "Elpida"},
	{0x04, "Etpon"},
	{0x05, "Nanya"},
	{0x06, "Hynix"},
	{0x07, "Mosel"},
	{0x08, "Winbond"},
	{0x09, "Esmt"},
	{0x13, "Cxmt"},
	{0xff, "Micro"}
};

#ifndef _MMC_MANF_INFO_H
#define _MMC_MANF_INFO_H
typedef struct _mmc_manf_info {
	int id;
	char *name;
} mmc_manf_info;

mmc_manf_info man_list[] = {
	{0x02, "Sandisk"},
	{0x11, "Toshiba"},
	{0x13, "Micro"},
	{0x15, "Sumsung"},
	{0x45, "Sandisk"},
	{0x70, "Kingston"},
	{0x90, "Hynix"},
	{0xF4, "Biwin"},
	{0x88, "Longsys"},
	{0xD6, "Longsys"},//if bank =0x0b ,hosin (D6 0B)
	{0x32, "Phison"},
	{0xDF, "Scy"},
	{0xec, "Rayson"},
	{0xea, "SiliconGo"},
	{0x9b, "Ymtc"},
	{0xAB, "Biwin"},
};


#endif

static int zte_get_ddr_info(void)
{
	struct device_node *cmdline_node;
	const char *cmdline, *size, *name;
	static char ddr_manfname[32];
	int ret;
	int ddr_id;
	int i = 0;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmdline);
	if (ret) {
		log_info("Can't not parse bootargs\n");
		return 0;
	}

	size =  strstr(cmdline, "zte.ddrsize=");
	name =  strstr(cmdline, "zte_mr5=");

	if (!size || !sscanf(size, "zte.ddrsize=%dM", &memory)) {
		return 0;
	}

	if (!name || !sscanf(name, "zte_mr5=%s*", ddr_manfname)) {
		return 0;
	}

	ddr_id = simple_strtol(ddr_manfname, NULL, 16);

	for (i = 0; i < ARRAY_SIZE(ddr_list); i++) {
		if (ddr_list[i].id == ddr_id) {
			manf = ddr_list[i].listname;
			break;
		}
	}
	return 1;
}

static int zte_string_get_size(u64 size, const enum string_size_units units,
		    char *buf, int len)
{
	static const char *const units_10[] = {
		"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB", NULL
	};
	static const char *const units_2[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB",
		NULL
	};
	static const char *const *const units_str[] = {
		[STRING_UNITS_10] = units_10,
		[STRING_UNITS_2] = units_2,
	};
	static const unsigned int divisor[] = {
		[STRING_UNITS_10] = 1000,
		[STRING_UNITS_2] = 1024,
	};
	int i, j;
	u64 remainder = 0, sf_cap;
	char tmp[8];
	tmp[0] = '\0';
	i = 0;
	if (size >= divisor[units]) {
		while (size >= divisor[units] && units_str[units][i]) {
			remainder = do_div(size, divisor[units]);
			i++;
		}
		sf_cap = size;
		for (j = 0; sf_cap*10 < 1000; j++)
			sf_cap *= 10;
		if (j) {
			remainder *= 1000;
			do_div(remainder, divisor[units]);
			snprintf(tmp, sizeof(tmp), ".%02lld",
				 (unsigned long long)remainder);
			tmp[2] = '\0';
		}
	}
	snprintf(buf, len, "%lld%sGB", (unsigned long long)size,
		 tmp);
	return 0;
}

static int zte_get_emmc_info(void)
{
	int i;
	char *manfname = "UNKNOWN";
	char cap_str[10];
	char fwrev[32] = {0};
	unsigned int  sectors;
	unsigned int bank;

	bank	= UNSTUFF_BITS(card->raw_cid, 114, 6);

	for (i = 0; i < ARRAY_SIZE(man_list); i++) {
		if (man_list[i].id == card->cid.manfid) {
			if ((card->cid.manfid == 0xD6) && (bank == 0x0B))
				manfname = "Hosin";
			 else
				manfname = man_list[i].name;
			break;
		}
	}
	sectors = card->ext_csd.sectors;
	zte_string_get_size(((u64)sectors) * 512, STRING_UNITS_10, cap_str, sizeof(cap_str));
	sprintf(fwrev, "0X%*phN", MMC_FIRMWARE_LEN, card->ext_csd.fwrev);

	snprintf(emmcinfo_buf, sizeof(emmcinfo_buf), "%s-%s-%s-%s-0x%02x/0x%02x", manfname, card->cid.prod_name, fwrev, cap_str, card->ext_csd.device_life_time_est_typ_a, card->ext_csd.device_life_time_est_typ_b);
	log_err("%s(): emmc fwrev report success, %s\n", __func__, emmcinfo_buf);
	return 0;
}

static ssize_t ddr_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *ddr_type = "LPDDR4";
	zte_get_ddr_info();
	return snprintf(buf, PAGE_SIZE, "%s-NA-NA-%dGB-%s\n" , manf, memory/1024, ddr_type);
}

static DEVICE_ATTR(ddr_id, S_IRUGO | S_IWUSR, ddr_info_show, NULL);

static ssize_t emmc_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	zte_get_emmc_info();
	return snprintf(buf, PAGE_SIZE, "%s\n" , emmcinfo_buf);
}

static DEVICE_ATTR(emmc_id, S_IRUGO | S_IWUSR, emmc_info_show, NULL);

static struct device_attribute *emmc_attributes[] = {
	&dev_attr_ddr_id,
	&dev_attr_emmc_id,
	NULL
};

static struct device_attribute *ufs_attributes[] = {
	&dev_attr_ddr_id,
	NULL
};

static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct mmc_host *host_tmp;
	if (host) {
		return 0;
	}
#if defined(CONFIG_ARM64)
		host_tmp = (struct mmc_host *)(regs_get_kernel_argument(regs, 0));
#elif defined(CONFIG_ARM)
		host_tmp = (struct mmc_host *)(regs->ARM_r0);
#endif

	if (!host_tmp) {
		log_err("%s(): host_tmp is nullptr!\n", __func__);
		snprintf(emmcinfo_buf, sizeof(emmcinfo_buf), "%s", "unknow");
		return 0;
	}
	card = host_tmp->card;
	if (!card) {
		log_err("%s(): card is nullptr!\n", __func__);
		snprintf(emmcinfo_buf, sizeof(emmcinfo_buf), "%s", "unknow");
		return 0;
	} else {
		if (card->type == MMC_TYPE_MMC) {
			host = host_tmp;
		} else {
			snprintf(emmcinfo_buf, sizeof(emmcinfo_buf), "%s", "unknow");
		}
	}
	return 0;
}

static struct kprobe kp = {
	.symbol_name	= "mmc_cqe_start_req",
};

static void zte_emmc_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(zte_emmc_dwork, zte_emmc_work_func);
static void zte_emmc_work_func(struct work_struct *work)
{
	if (host) {
		log_info("%s(): host get ok\n", __func__);
		unregister_kprobe(&kp);
		zte_get_emmc_info();
	} else
		schedule_delayed_work(&zte_emmc_dwork, msecs_to_jiffies(500));
}

static int creat_node()
{
	struct device_attribute **attrs;
	struct device_attribute *attr;
	struct device_node *cmdline_node;
	const char *cmdline, *device;
	static char flashtype[32];
	int err;
	int ret;

	kp.pre_handler = handler_pre;
	ret = register_kprobe(&kp);
	if (ret < 0) {
		log_err("%s(): register_kprobe failed\n", __func__);
		return ret;
	}
	schedule_delayed_work(&zte_emmc_dwork, msecs_to_jiffies(500));

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmdline);
	if (ret) {
		log_info("Can't not parse bootargs\n");
		return 0;
	}
	device = strstr(cmdline, "sprdboot.flash=");
	if ( !device ) {
		attrs = emmc_attributes;
        } else {
		sscanf(device, "sprdboot.flash=%s*", flashtype);
		if ( strcmp(flashtype, "emmc") == 0 ) {
			attrs = emmc_attributes;
		} else {
			attrs = ufs_attributes;
		}
        }
	device_class = class_create(THIS_MODULE, "zte");
	if (IS_ERR(device_class))
		return PTR_ERR(device_class);

	device_t = device_create(device_class, NULL, 0, NULL, "driver");
	if (IS_ERR(device_t))
		return PTR_ERR(device_t);
	while ((attr = *attrs++)) {
		err = device_create_file(device_t, attr);
		if (err) {
			device_destroy(device_class, 0);
			return err;
		}
	}
	return 0;
}


static int __init zte_storage_id_show(void)
{
	creat_node();
	return 0;
}
fs_initcall(zte_storage_id_show);
MODULE_LICENSE("GPL");