#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/f2fs_fs.h>
#include <linux/kprobes.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/kdev_t.h>
#include <asm/ptrace.h>

#include "../../../../fs/f2fs/f2fs.h"
#include "../../../../fs/f2fs/segment.h"
#include "../../../../fs/f2fs/gc.h"

#include <trace/events/f2fs.h>

#define ZTE_BSP_LOG_PREFIX "[ZTE_FS][F2FS]"
#define log_err(fmt, ...) pr_err(ZTE_BSP_LOG_PREFIX "[ERR]" fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) pr_warn(ZTE_BSP_LOG_PREFIX "[WARN]" fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) pr_info(ZTE_BSP_LOG_PREFIX "[INFO]" fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) pr_debug(ZTE_BSP_LOG_PREFIX "[DEBUG]" fmt, ##__VA_ARGS__)

static DEFINE_SPINLOCK(zte_f2fs_lock);
static struct f2fs_sb_info *sbi;

/*
 * This function calculates BDF of every segments
 */
void f2fs_update_sit_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si;
	unsigned long long blks_per_sec, hblks_per_sec, total_vblocks;
	unsigned long long bimodal, dist;
	unsigned int segno, vblocks;
	int ndirty = 0;

	bimodal = 0;
	total_vblocks = 0;

	if (!sbi) {
		log_err("%s(): sbi is nullptr\n", __func__);
		return;
	}

	si = F2FS_STAT(sbi);
	blks_per_sec = BLKS_PER_SEC(sbi);
	hblks_per_sec = blks_per_sec / 2;
	for (segno = 0; segno < MAIN_SEGS(sbi); segno += sbi->segs_per_sec) {
		vblocks = get_valid_blocks(sbi, segno, true);
		dist = abs(vblocks - hblks_per_sec);
		bimodal += dist * dist;

		if (vblocks > 0 && vblocks < blks_per_sec) {
			total_vblocks += vblocks;
			ndirty++;
		}
	}
	dist = div_u64(MAIN_SECS(sbi) * hblks_per_sec * hblks_per_sec, 100);
	si->bimodal = div64_u64(bimodal, dist);
	if (si->dirty_count)
		si->avg_vblocks = div_u64(total_vblocks, ndirty);
	else
		si->avg_vblocks = 0;
}

static ssize_t prefree_segments_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n",
			(unsigned long long)(prefree_segments(sbi)));
}

static ssize_t bdf_of_every_seg_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct f2fs_stat_info *si = F2FS_STAT(sbi);

	si->dirty_count = dirty_segments(sbi);
	f2fs_update_sit_info(sbi);
	return sprintf(buf, "%llu\n", (unsigned long long)(si->bimodal));
}

static struct kobj_attribute f2fs_attr_bdf_of_every_seg =
__ATTR_RO(bdf_of_every_seg);

static struct kobj_attribute f2fs_attr_prefree_segments =
__ATTR_RO(prefree_segments);

static struct attribute *f2fs_attrs_attributes[] = {
	&f2fs_attr_bdf_of_every_seg.attr,
	&f2fs_attr_prefree_segments.attr,
	NULL,
};

static const struct attribute_group f2fs_attrs_attributes_group = {
	.attrs = f2fs_attrs_attributes,
	NULL,
};

static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct super_block *sb;
	struct f2fs_sb_info *sbi_tmp;
	spin_lock(&zte_f2fs_lock);

#if defined(CONFIG_ARM64)
	sbi_tmp = (struct f2fs_sb_info *)(regs_get_kernel_argument(regs, 0));
#elif defined(CONFIG_ARM)
	sbi_tmp = (struct f2fs_sb_info *)(regs->ARM_r0);
#endif

	if (!sbi_tmp) {
		log_err("%s(): sbi_tmp is nullptr\n", __func__);
		spin_unlock(&zte_f2fs_lock);
		return 0;
	}
	sb = sbi_tmp->sb;
	log_info("%s(): major=%d, minor=%d\n", __func__, MAJOR(sb->s_dev), MINOR(sb->s_dev));
	if (MAJOR(sb->s_dev) == 254 ) { // major=254, then it is a dm device
		sbi = sbi_tmp;
		log_info("%s(): major=254, register sysfs f2fs node (bdf and prefree)\n", __func__);
	} else {
		log_err("%s(): major!=254, so not dm device\n", __func__);
	}
	spin_unlock(&zte_f2fs_lock);
	return 0;
}

static struct kprobe kp = {
	.symbol_name	= "f2fs_register_sysfs",
};

static int __init zte_sysfs_f2fs_show_init(void)
{
	int ret = 0;
	int err = 0;
	struct kobject *zte_sysfs_bdf_prefree_kobj;

	kp.pre_handler = handler_pre;

	ret = register_kprobe(&kp);
	if (ret < 0) {
		log_err("%s(): register_kprobe failed, returned %d\n", __func__, ret);
		return ret;
	}

	zte_sysfs_bdf_prefree_kobj = kobject_create_and_add("zte_f2fs", fs_kobj);
	if (!zte_sysfs_bdf_prefree_kobj) {
		log_err("%s(): unable to create zte_sysfs_bdf_prefree_kobj\n", __func__);
		return -ENOMEM;
	}

	err = sysfs_create_group(zte_sysfs_bdf_prefree_kobj, &f2fs_attrs_attributes_group);
	if (err != 0) {
		log_err("%s(): sysfs_create_group failed\n", __func__);
		kobject_put(zte_sysfs_bdf_prefree_kobj);
		return err;
	}
	log_info("%s(): register sysfs f2fs node (bdf and prefree) success\n", __func__);
	return 0;
}

static void __exit zte_sysfs_f2fs_show_exit(void)
{
	log_info("%s(): zte_sys_f2fs_show_exit done\n", __func__);
}

module_init(zte_sysfs_f2fs_show_init); //define fs_initcall(fn) module_init(fn)
module_exit(zte_sysfs_f2fs_show_exit);

MODULE_DESCRIPTION("ZTE Fragmentation level of Userdata Partition");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE");