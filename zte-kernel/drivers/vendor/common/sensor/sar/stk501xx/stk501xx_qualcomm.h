#ifndef __STK501XX_QUALCOMM_H__
#define __STK501XX_QUALCOMM_H__

#include "stk501xx.h"
#include "common_define.h"

#define STK_HEADER_VERSION          "0.0.7"
#define STK_C_VERSION               "0.0.3"
#define STK_DRV_I2C_VERSION         "0.0.2"
#define STK_QUALCOMM_VERSION        "0.0.1"
#define STK_ATTR_VERSION            "0.0.1"

typedef struct stk501xx_wrapper
{
    struct i2c_manager      i2c_mgr;
    struct stk_data         stk;
    struct input_dev        *input_dev;   /* data */

#if defined STK_QUALCOMM || defined STK_SPREADTRUM
#ifdef STK_QUALCOMM
#ifdef STK_SENSORS_DEV
    struct sensors_classdev     sar_cdev;
#endif
    u64                         fifo_start_ns;
#endif
    ktime_t                     timestamp;
#elif defined STK_MTK
    //struct sar_hw               hw;
    //struct hwmsen_convert       cvt;
#endif
} stk501xx_wrapper;

int stk_i2c_probe(struct i2c_client* client,
                  struct common_function *common_fn);
int stk_i2c_remove(struct i2c_client* client);
int stk501xx_suspend(struct device* dev);
int stk501xx_resume(struct device* dev);

static struct class *cls;
static struct device *class_dev;

#endif