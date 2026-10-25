#include "include/include.h"



static unsigned char g_ucLogLevel = LOG_DEBUG;
static char *messageTypeStr[] = {"DEBUG","INFO","WARN", "ERROR","CRIT"};

//void Ocm_PrintLevel_Set(unsigned char ucLvl)
//{
//    g_ucLogLevel = ucLvl;
//}

void LTLog(unsigned char ucLvl, const char *fmt, ...)
{
    char buf[MAX_NUMBER_BYTES] = {0};
    va_list args;
    if (ucLvl > LOG_NOTRACE)
    {
        return;
    }
    
    //打印大于等于该级别的字符串
    if(ucLvl >= g_ucLogLevel)
    {
        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        va_end(args);
        //左对齐5个字符
        printk("\n[%-5s] %s", messageTypeStr[ucLvl], buf);
    }
}




int HDMI_WriteI2C_Byte(u8 addr,u8 data)
{
	int ret = 0;
	
	
	ret = regmap_write(lt8911exb->chip_regmap, addr, data);
	
	if (ret < 0) {
		printk("regmap_write error: (0x%02x 0x%02x 0x%02x) %d",
			lt8911exb->trans_i2c->addr, addr, data, ret);
		return ret;
	}
	

	return 0;
}

int HDMI_ReadI2C_Byte(u8 addr)
{
	int ret = 0;
	unsigned int  val = 0;
	
	ret = regmap_read(lt8911exb->chip_regmap, addr, &val);
	
	if (ret < 0) {
		printk("regmap_read error: (0x%02x 0x%02x) %d",
		lt8911exb->trans_i2c->addr, addr, ret);
		
		return ret;
	}

    return (u8)val;
}


EXPORT_SYMBOL(HDMI_WriteI2C_Byte);
EXPORT_SYMBOL(HDMI_ReadI2C_Byte);
