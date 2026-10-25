#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/of.h>

static const char *system_serial = NULL;
static const char *cpuinfo_hardware = NULL;

/* per-instance private data */
struct my_data {
	struct seq_file *m;
};

static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct my_data *data;

	if (!current->mm)
		return 1;	/* Skip kernel threads */

	data = (struct my_data *)ri->data;
	#if IS_ENABLED(CONFIG_ARM64)
	data->m = (struct seq_file *)pt_regs_read_reg(regs, 0);
	#else
	data->m = (struct seq_file *)regs->ARM_r0;
	#endif

	return 0;
}
NOKPROBE_SYMBOL(entry_handler);

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct my_data *data = (struct my_data *)ri->data;
	struct seq_file *m = data->m;

#ifdef ZTE_CPUINFO_ADD_SERIAL
	if (system_serial) {
		seq_printf(m, "Serial\t\t: %s\n", system_serial);
	}
#endif

	if (cpuinfo_hardware) {
		seq_printf(m, "Hardware\t: %s\n", cpuinfo_hardware);
	}

	return 0;
}
NOKPROBE_SYMBOL(ret_handler);

static struct kretprobe c_show_kp = {
	.handler		= ret_handler,
	.entry_handler		= entry_handler,
	.data_size		= sizeof(struct my_data),
	.maxactive		= 5,
};

static struct kprobe cpuinfo_op_kp = {
	.symbol_name	= "cpuinfo_op",
};

static int __init cpuinfo_init(void)
{
	int ret;
	struct seq_operations *cpuinfo_op = NULL;
	struct device_node *root;

	root = of_find_node_by_path("/");
	if (root) {
		ret = of_property_read_string(root, "serial-number",
									  &system_serial);
		if (ret)
			system_serial = NULL;

		ret = of_property_read_string(root, "cpuinfo_hardware",
									  &cpuinfo_hardware);
		if (ret)
			cpuinfo_hardware = NULL;
	}

	if (!system_serial)
		system_serial = kasprintf(GFP_KERNEL, "%08x%08x", 0, 0);

	cpuinfo_op_kp.addr = 0;
	cpuinfo_op_kp.offset = 0;
	ret = register_kprobe(&cpuinfo_op_kp);
	if (ret < 0) {
		if (cpuinfo_op_kp.addr != 0)
			cpuinfo_op = (struct seq_operations *)(cpuinfo_op_kp.addr);
		else
			return ret;
	} else {
		unregister_kprobe(&cpuinfo_op_kp);
		return -EINVAL;
	}

	c_show_kp.kp.addr = (kprobe_opcode_t *)cpuinfo_op->show;
	ret = register_kretprobe(&c_show_kp);
	if (ret < 0) {
		pr_err("register_kretprobe c_show_kp failed, returned %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit cpuinfo_exit(void)
{
	unregister_kretprobe(&c_show_kp);
	pr_info("kretprobe at %p unregistered\n", c_show_kp.kp.addr);
}

module_init(cpuinfo_init)
module_exit(cpuinfo_exit)
MODULE_LICENSE("GPL");
