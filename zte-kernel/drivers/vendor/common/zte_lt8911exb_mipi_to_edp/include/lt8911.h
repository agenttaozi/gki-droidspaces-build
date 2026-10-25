#ifndef	 _LT8911EXB_H_
#define	 _LT8911EXB_H_

//////////////////////LT8911 Config///////////////////
//#define _1920x1200_eDP_Panel_
#define _1080P_eDP_Panel_
//#define _1366x768_eDP_Panel_
//#define _1280x800_eDP_Panel_
//#define _1600x900_eDP_Panel_

#define SCRAMBLE_MODE 0x00 //0x80: edp, 0x00: dp

//#define sync_polarity 0x00 //0x00: no_adj; 0x20: vs_adj; 0x10: hs_adj; 0x30: H/Vs adj;

//#define _6bit_

//#define _dither_enable_

#define _link_train_enable_

//////////option for debug///////////
//#define _read_edid_
//#define _EDP_Pattern_
//#define _Msa_Active_Only_
#define _pcr_mk_printk_
#define _htotal_stable_check_

//#define _gpio_sync_output_
//#define sync_source 0x01 //gpio output lvds Rx sync
#define sync_source 0x02 //gpio output lvds portA sync
//#define sync_source 0x03 //gpio output lvds portB sync
//gpio2: de, gpio3: vs, gpio4: hs.

struct video_timing{
u16 hfp;
u16 hs;
u16 hbp;
u16 hact;
u16 htotal;
u16 vfp;
u16 vs;
u16 vbp;
u16 vact;
u16 vtotal;
u32 pclk_khz;
};

struct Lane_No{
u8	swing_high_byte;
u8	swing_low_byte;
u8	emph_high_byte;
u8	emph_low_byte;
};


extern struct video_timing video;

extern void LT8911_Reset(void);
extern void LT8911_ChipID(void);
extern void LT8911_SetVideoTiming(struct video_timing *video_format);
extern void LT8911_Init(void);
extern void LT8911_LinkTrain(void);
extern void LT8911_LinkTrainResultCheck(void);
extern void LT8911_video_check(void);
extern void LT8911_pcr_mk_printk(void);
extern void LT8911_htotal_stable_printk(void);
extern void LT8911_MainLoop(struct video_timing *video_format);

#endif
