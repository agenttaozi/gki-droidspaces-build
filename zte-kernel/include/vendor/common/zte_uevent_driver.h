#ifndef __ZTE_UEVENT_DRIVER__
#define __ZTE_UEVENT_DRIVER__

// 预定义的TYPE类型
typedef enum {
    TYPE_HDMI_DP_CAST,
    TYPE_TYPEC,
    TYPE_MAX
} UEventType;

static const char *type_names[] = {
    [TYPE_HDMI_DP_CAST] = "HDMI_DP_CAST",
    [TYPE_TYPEC] = "TYPEC",
};

struct uevent_data {
    UEventType type;
    int value;
};


#define HDMI_CAST_UEVENT_PLUG_OUT	0x00
#define HDMI_CAST_UEVENT_PLUG_IN	0x01

#define DP_CAST_UEVENT_PLUG_OUT	    0x02
#define DP_CAST_UEVENT_PLUG_IN	    0x03

#define HDMI_CAST_UEVENT_CASTING	0x04
#define HDMI_CAST_UEVENT_SWITCHING	0x05


#if IS_ENABLED(CONFIG_VENDOR_ZTE_UEVENT_INTF)
int send_uevent_int(UEventType type, int value);
int send_uevent_str(const char *type_str, const char *value_str);

#else
static inline int send_uevent_int(UEventType type, int value)
{
	return -ENOSYS;
}

static inline int send_uevent_str(const char *type_str, const char *value_str)
{
	return -ENOSYS;
}
#endif

#endif
