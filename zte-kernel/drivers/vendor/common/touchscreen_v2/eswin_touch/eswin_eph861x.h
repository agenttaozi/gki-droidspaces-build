/* Started by AICoder, pid:kb9e2d9d3fzce2a14dc30a5a70d5ae26c2c4d00c */
#ifndef __LINUX_PLATFORM_DATA_ESWIN_EPH_H
#define __LINUX_PLATFORM_DATA_ESWIN_EPH_H

#include "ztp_common.h"
#include "eswin_config.h"

typedef enum {
    ERR_LOG = 0,
    WARN_LOG,
    INFO_LOG,
    DEBUG_LOG,
    ALL_LOG,
} eswin_debug_level_t;

static eswin_debug_level_t eswin_debug_level = INFO_LOG;

#define eswin_log(level, fmt, args...) do { \
    if ((eswin_debug_level >= level) || tpd_cdev->debug_log_enable) { \
        pr_warn("[ZTE_LDD_TP][ESWIN_LOG]:(%s, %d):" fmt, __func__, __LINE__, ##args); \
        tpd_save_last_log("[ZTE_LDD_TP][ESWIN_LOG]:(%s, %d):" fmt, __func__, __LINE__, ##args); \
    } \
} while (0)

#endif /* __LINUX_PLATFORM_DATA_ESWIN_EPH_ */
/* Ended by AICoder, pid:kb9e2d9d3fzce2a14dc30a5a70d5ae26c2c4d00c */