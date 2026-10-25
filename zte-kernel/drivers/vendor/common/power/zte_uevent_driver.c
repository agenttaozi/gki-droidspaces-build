#define pr_fmt(fmt)	"[ZTE_LDD_UEVENT][UEVENT_INTERFACE]: %s: " fmt, __func__


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <vendor/common/zte_uevent_driver.h>

#define DEVICE_NAME "zte_uevent_dev"
#define CLASS_NAME "zte_uevent"
#define MAX_VALUE 16

static int major_num;
static struct class *my_class = NULL;
static struct device *my_device = NULL;

#define UE_IOCTL_MAGIC 'U'
#define UE_SEND_EVENT _IOW(UE_IOCTL_MAGIC, 1, struct uevent_data)

static DEFINE_MUTEX(zte_uevent_mutex);

int send_uevent_int(UEventType type, int value)
{
    char *envp[5] = {NULL};
    int ret = 0;
    int i = 0;

    pr_info("type:%d value:%d\n", type, value);
    mutex_lock(&zte_uevent_mutex);

    // 动态分配环境变量字符串
    envp[0] = kasprintf(GFP_KERNEL, "ACTION=change");
    envp[1] = kasprintf(GFP_KERNEL, "SUBSYSTEM=zte_uevent");
    envp[2] = kasprintf(GFP_KERNEL, "TYPE=%s", type_names[type]);
    envp[3] = kasprintf(GFP_KERNEL, "VALUE=%d", value);

    // 检查内存分配是否成功
    for (i = 0; i < 4; i++) {
        if (!envp[i]) {
            pr_err("Failed to allocate memory for envp[%d]\n", i);
            ret = -ENOMEM;
            goto cleanup;
        }
    }

    // 发送UEvent
    ret = kobject_uevent_env(&my_device->kobj, KOBJ_CHANGE, envp);
    if (ret < 0) {
        pr_err("Failed to send uevent: %d\n", ret);
    }

cleanup:
    // 释放内存
    for (i = 0; i < 4; i++) {
        kfree(envp[i]);
    }

    mutex_unlock(&zte_uevent_mutex);
    return ret;
}
EXPORT_SYMBOL_GPL(send_uevent_int);

int send_uevent_str(const char *type_str, const char *value_str)
{
    char *envp[5] = {NULL};
    int ret = 0;
    int i = 0;

    mutex_lock(&zte_uevent_mutex);
    // 参数检查
    if (!type_str || !value_str) {
        pr_err("Invalid parameters: type or value is NULL\n");
        ret = -EINVAL;
        goto out;
    }

    pr_info("type:%s, value:%s\n", type_str, value_str);

    // 动态分配环境变量字符串
    envp[0] = kasprintf(GFP_KERNEL, "ACTION=change");
    envp[1] = kasprintf(GFP_KERNEL, "SUBSYSTEM=zte_uevent");
    envp[2] = kasprintf(GFP_KERNEL, "TYPE=%s", type_str);
    envp[3] = kasprintf(GFP_KERNEL, "VALUE=%s", value_str);

    // 检查内存分配是否成功
    for (i = 0; i < 4; i++) {
        if (!envp[i]) {
            pr_err("Failed to allocate memory for envp[%d]\n", i);
            ret = -ENOMEM;
            goto cleanup;
        }
    }

    // 发送UEvent
    ret = kobject_uevent_env(&my_device->kobj, KOBJ_CHANGE, envp);
    if (ret < 0) {
        pr_err("Failed to send uevent: %d\n", ret);
    }

cleanup:
    // 释放内存
    for (i = 0; i < 4; i++) {
        kfree(envp[i]);
    }
out:
    mutex_unlock(&zte_uevent_mutex);
    return ret;
}
EXPORT_SYMBOL_GPL(send_uevent_str);

static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct uevent_data data;
    
    if (_IOC_TYPE(cmd) != UE_IOCTL_MAGIC)
        return -ENOTTY;

    if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
        return -EFAULT;

    if (data.type >= TYPE_MAX)
        return -EINVAL;

    pr_info("type:%d value:%d\n", data.type, data.value);

    send_uevent_int(data.type, data.value);

    return 0;
}

static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf, "Supported types: HDMI, TYPEC\n");
}
static DEVICE_ATTR(type, 0664, type_show, NULL);

static struct file_operations fops = {
    .unlocked_ioctl = my_ioctl,
};

static int __init zte_uevent_init(void) {
    int ret = 0;

    // 注册字符设备
    major_num = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_num < 0) {
        pr_err("Failed to register device\n");
        return major_num;
    }

    // 创建设备类
    my_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        goto fail_class;
    }

    // 创建设备节点
    my_device = device_create(my_class, NULL, MKDEV(major_num, 0), NULL, DEVICE_NAME);
    if (IS_ERR(my_device)) {
        ret = PTR_ERR(my_device);
        goto fail_device;
    }

    // 注册sysfs属性（可选）
    ret = device_create_file(my_device, &dev_attr_type);
    if (ret) {
        pr_err("Failed to create sysfs file\n");
        goto fail_sysfs;
    }

    pr_info("ZTE UEvent driver loaded (Supports HDMI/TYPEC, value 1~%d)\n", MAX_VALUE);
    return 0;

fail_sysfs:
    device_destroy(my_class, MKDEV(major_num, 0));
fail_device:
    class_destroy(my_class);
fail_class:
    unregister_chrdev(major_num, DEVICE_NAME);
    return ret;
}

static void __exit zte_uevent_exit(void) {
    device_remove_file(my_device, &dev_attr_type);
    device_destroy(my_class, MKDEV(major_num, 0));
    class_destroy(my_class);
    unregister_chrdev(major_num, DEVICE_NAME);
    pr_info("ZTE UEvent driver unloaded\n");
}

module_init(zte_uevent_init);
module_exit(zte_uevent_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("zte.charger <zte.charger@zte.com>");
MODULE_DESCRIPTION("ZTE UEvent Driver with HDMI/TYPEC support");