/* Started by AICoder, pid:r49f70c0803e81b14e750bd5a0c5c12691782ea9 */
#ifndef _OMNIVISION_CONFIG_H_
#define _OMNIVISION_CONFIG_H_

#define OVT_TCM_MODULE1_ID                         0x0001
#define OVT_TCM_MODULE2_ID                         0x0002
#define OVT_TCM_MODULE3_ID                         0x0003

#define OVT_TCM_MODULE1_LCD_NAME                   "token"
#define OVT_TCM_MODULE2_LCD_NAME                   "Unknown"
#define OVT_TCM_MODULE3_LCD_NAME                   "Unknown"

#define TX_NUM_MAX                                 32
#define RX_NUM_MAX                                 18

/* default I2C */
#define USE_SPI_BUS
#ifdef USE_SPI_BUS
/* define use SPI num */
#define SPI_NUM                                    3
#endif

#define OVT_TCM_REPORT_BY_ZTE_ALGO

#define OVT_TCM_LCD_OPERATE_TP_RESET

#define OVT_TCM_SUSPEND_AFTER_LCD_CMD_OFF_END

#endif /* _OMNIVISION_CONFIG_H_ */
/* Ended by AICoder, pid:r49f70c0803e81b14e750bd5a0c5c12691782ea9 */