#ifndef _LONTIUMDRV_H_
#define _LONTIUMDRV_H_


struct _lt8911exb{
    struct device *dev;
	struct mutex ocm_lock;
	struct gpio_desc *reset_gpio;
    struct gpio_desc *power_gpio;
	struct i2c_client *trans_i2c;
	struct regmap *chip_regmap;
};



typedef struct
{
    u8 address; // ¼Ä´æÆ÷µØÖ·
    u8 value;   // ¼Ä´æÆ÷Öµ
} Chip_Control_Args;


extern struct _lt8911exb  *lt8911exb;
extern atomic_t thread_should_stop;


#endif
