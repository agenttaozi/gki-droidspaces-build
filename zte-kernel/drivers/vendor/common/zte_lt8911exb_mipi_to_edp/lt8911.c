#include "include/include.h"

#ifdef _1080P_eDP_Panel_
#define LANE_CNT 2
#define PCR_PLL_PREDIV 0x40
#define PCR_M 0x17 //148.5M	          //hfp, hs, hbp,hact,htotal,vfp, vs, vbp, vact,vtotal,
struct video_timing video ={88, 44, 148,1920,  2200,  4,  5,  36, 1080, 1125, 148500};
//const struct video_timing video ={40, 40, 80,1920,  2200,  3,  5,  23, 1080, 1111, 148500};
#endif

#ifdef _1366x768_eDP_Panel_
#define LANE_CNT 1
#define PCR_PLL_PREDIV 0x44
#define PCR_M 0x17   //74M
 							//hfp, hs, hbp,hact,htotal,vfp, vs, vbp, vact,vtotal,
struct video_timing video = {100, 26,100,1366,  1592,  10, 10, 10, 768, 798, 76225};
#endif

u8 EDID_DATA[128] = {0};
static bool edp_idle_flag = 1;

#define MSA_SW_MODE 0x80 //MSA from register
#define MSA_HW_MODE 0x00 //MSA from video check

#define EDP_IDLE_PTN_ON 0x04
#define EDP_IDLE_PTN_OFF 0x00



//init power gpio level and reset chip 
void LT8911_Reset(void)
{
	gpiod_set_value(lt8911exb->power_gpio,1);
	msleep(5);
	printk("set power High level ");	

	//complete chip reset logic
	gpiod_set_value(lt8911exb->reset_gpio,1);
	msleep(5);	
	gpiod_set_value(lt8911exb->reset_gpio,0);
	msleep(5);	
	gpiod_set_value(lt8911exb->reset_gpio,1);
	msleep(5);	
	printk("reset chip");	
}

void LT8911_ChipID(void)
{
	HDMI_WriteI2C_Byte(0xff,0x81);//register bank
	HDMI_WriteI2C_Byte(0x08,0x7f);
	printk("\r\nLT8911EX chip ID:%x,",HDMI_ReadI2C_Byte(0x00));
    printk("%x, ",HDMI_ReadI2C_Byte(0x01));
	printk("%x, ",HDMI_ReadI2C_Byte(0x02));
}

void LT8911_SetVideoTiming(struct video_timing *video_format)
{

	u32 pclk_khz;
	u8 dessc_m;
							 
    //edp msa 
	HDMI_WriteI2C_Byte(0xff,0xa8);
	HDMI_WriteI2C_Byte(0x2d,0x88); //bit[7]¡êo1 = register msa, 0 = hardware msa
	
#ifdef _Msa_Active_Only_
	HDMI_WriteI2C_Byte(0x05,0x00);
	HDMI_WriteI2C_Byte(0x06,0x00);//htotal
	HDMI_WriteI2C_Byte(0x07,0x00);
	HDMI_WriteI2C_Byte(0x08,0x00);//h_start
	HDMI_WriteI2C_Byte(0x09,0x00);
	HDMI_WriteI2C_Byte(0x0a,0x00);  //hsa
	HDMI_WriteI2C_Byte(0x0b,(u8)(video_format->hact/256));
	HDMI_WriteI2C_Byte(0x0c,(u8)(video_format->hact%256)); //hactive
	HDMI_WriteI2C_Byte(0x0d,0x00);
	HDMI_WriteI2C_Byte(0x0e,0x00);//vtotal
	HDMI_WriteI2C_Byte(0x11,0x00);
	HDMI_WriteI2C_Byte(0x12,0x00);
	HDMI_WriteI2C_Byte(0x14,0x00);
	HDMI_WriteI2C_Byte(0x15,(u8)(video_format->vact/256));
	HDMI_WriteI2C_Byte(0x16,(u8)(video_format->vact%256));  //vactive
			
#else
	HDMI_WriteI2C_Byte(0x05,(u8)(video_format->htotal/256));
	HDMI_WriteI2C_Byte(0x06,(u8)(video_format->htotal%256));//htotal
	HDMI_WriteI2C_Byte(0x07,(u8)((video_format->hs+video_format->hbp)/256));
	HDMI_WriteI2C_Byte(0x08,(u8)((video_format->hs+video_format->hbp)%256));//h_start
	HDMI_WriteI2C_Byte(0x09,(u8)(video_format->hs/256)); 
	HDMI_WriteI2C_Byte(0x0a,(u8)(video_format->hs%256));   //hsa
	HDMI_WriteI2C_Byte(0x0b,(u8)(video_format->hact/256));
	HDMI_WriteI2C_Byte(0x0c,(u8)(video_format->hact%256)); //hactive
	HDMI_WriteI2C_Byte(0x0d,(u8)(video_format->vtotal/256));
	HDMI_WriteI2C_Byte(0x0e,(u8)(video_format->vtotal%256));//vtotal
	HDMI_WriteI2C_Byte(0x11,(u8)((video_format->vs+video_format->vbp)/256));
	HDMI_WriteI2C_Byte(0x12,(u8)((video_format->vs+video_format->vbp)%256));//v_start
	HDMI_WriteI2C_Byte(0x14,(u8)(video_format->vs%256));    //vsa
	HDMI_WriteI2C_Byte(0x15,(u8)(video_format->vact/256));
	HDMI_WriteI2C_Byte(0x16,(u8)(video_format->vact%256));  //vactive
#endif

    // ldvd de only mode to regenerate h/v sync.
	HDMI_WriteI2C_Byte(0xff,0xd8);
	HDMI_WriteI2C_Byte(0x20,(u8)(video_format->hfp/256));
	HDMI_WriteI2C_Byte(0x21,(u8)(video_format->hfp%256)); 

	HDMI_WriteI2C_Byte(0x22,(u8)(video_format->hs/256));
	HDMI_WriteI2C_Byte(0x23,(u8)(video_format->hs%256)); 

	HDMI_WriteI2C_Byte(0x24,(u8)(video_format->htotal/256));
	HDMI_WriteI2C_Byte(0x25,(u8)(video_format->htotal%256)); 

	HDMI_WriteI2C_Byte(0x26,(u8)(video_format->vfp%256)); 
	HDMI_WriteI2C_Byte(0x27,(u8)(video_format->vs%256)); 


	//de-sscpll to gererate pixel clock for pattern
	pclk_khz = video_format->pclk_khz;
	dessc_m =  (pclk_khz * 4) /(25*1000);
	HDMI_WriteI2C_Byte(0xff,0x85);  
   	HDMI_WriteI2C_Byte(0xaa,dessc_m);	//MK[30:24]
	//	HDMI_WriteI2C_Byte(0xab,0xba);	//MK[23:16]
	//	HDMI_WriteI2C_Byte(0xac,0xe1);	//MK[15:8]
	//	HDMI_WriteI2C_Byte(0xad,0x47);  //MK[7:0]
}



void LT8911_MIPI_Video_Timing(struct video_timing *video_format) 
{
	HDMI_WriteI2C_Byte(0xff,0xd0);
	HDMI_WriteI2C_Byte(0x0d,(u8)(video_format->vtotal/256));
	HDMI_WriteI2C_Byte(0x0e,(u8)(video_format->vtotal%256));//vtotal
	HDMI_WriteI2C_Byte(0x0f,(u8)(video_format->vact/256));
	HDMI_WriteI2C_Byte(0x10,(u8)(video_format->vact%256));  //vactive
	HDMI_WriteI2C_Byte(0x11,(u8)(video_format->htotal/256));
	HDMI_WriteI2C_Byte(0x12,(u8)(video_format->htotal%256));//htotal
	HDMI_WriteI2C_Byte(0x13,(u8)(video_format->hact/256));
	HDMI_WriteI2C_Byte(0x14,(u8)(video_format->hact%256)); //hactive
	HDMI_WriteI2C_Byte(0x15,(u8)(video_format->vs%256));   //vsa
	HDMI_WriteI2C_Byte(0x16,(u8)(video_format->hs%256));   //hsa
	HDMI_WriteI2C_Byte(0x17,(u8)(video_format->vfp/256));  
	HDMI_WriteI2C_Byte(0x18,(u8)(video_format->vfp%256));  //vfp
	HDMI_WriteI2C_Byte(0x19,(u8)(video_format->hfp/256));  
	HDMI_WriteI2C_Byte(0x1a,(u8)(video_format->hfp%256));  //hfp
}

void LT8911_Init(void)
{
		u8 i;
		/* init */
		HDMI_WriteI2C_Byte(0xff,0x81);
		HDMI_WriteI2C_Byte(0x08,0x7f); //i2c over aux issue
	  	HDMI_WriteI2C_Byte(0x49,0xff); //enable 0x87xx
		
	  	HDMI_WriteI2C_Byte(0xff,0x82); //GPIO test output
	  	HDMI_WriteI2C_Byte(0x5a,0x0e);

		//for power consumption//
		HDMI_WriteI2C_Byte(0xff,0x81);
		HDMI_WriteI2C_Byte(0x05,0x06);
		HDMI_WriteI2C_Byte(0x43,0x00);
		HDMI_WriteI2C_Byte(0x44,0x1f);
		HDMI_WriteI2C_Byte(0x45,0xf7);
		HDMI_WriteI2C_Byte(0x46,0xf6);
		HDMI_WriteI2C_Byte(0x49,0x7f);

		HDMI_WriteI2C_Byte(0xff,0x82);
		HDMI_WriteI2C_Byte(0x12,0x33);

		/* mipi Rx analog */
		HDMI_WriteI2C_Byte(0xff,0x82);
		HDMI_WriteI2C_Byte(0x32,0x51);
		HDMI_WriteI2C_Byte(0x35,0x62); //EQ current 0x42
		HDMI_WriteI2C_Byte(0x3a,0x33); //0x77:EQ 12.5db  ,0x33:EQ 6.5db  
		HDMI_WriteI2C_Byte(0x3b,0x33); //0x77:EQ 12.5db  ,0x33:EQ 6.5db  
		HDMI_WriteI2C_Byte(0x4c,0x0c);
		HDMI_WriteI2C_Byte(0x4d,0x00);
		
		/* dessc_pcr  pll analog */
		HDMI_WriteI2C_Byte(0xff,0x82);
		HDMI_WriteI2C_Byte(0x6a,0x43); //final setting: 0x40
		HDMI_WriteI2C_Byte(0x6b,PCR_PLL_PREDIV); //0x44:pre-div = 2 
		//HDMI_WriteI2C_Byte(0x6b,0x44);
		HDMI_WriteI2C_Byte(0x6e,0x81);
		
		/* dessc pll digital */
		HDMI_WriteI2C_Byte(0xff,0x85);
		HDMI_WriteI2C_Byte(0xa9,0x31);
		HDMI_WriteI2C_Byte(0xaa,0x17);
		HDMI_WriteI2C_Byte(0xab,0xba);
		HDMI_WriteI2C_Byte(0xac,0xe1);
		HDMI_WriteI2C_Byte(0xad,0x47);
		HDMI_WriteI2C_Byte(0xae,0x01);
		HDMI_WriteI2C_Byte(0xae,0x11);

		/* Digital Top */
		HDMI_WriteI2C_Byte(0xff,0x85);
		HDMI_WriteI2C_Byte(0xc0,0x01); //select mipi Rx
		#ifdef _6bit_
		HDMI_WriteI2C_Byte(0xb0,0xd0); //enable dither
		#endif
	
		/* mipi Rx Digital */
		HDMI_WriteI2C_Byte(0xff, 0xd0);
		//HDMI_WriteI2C_Byte(0x00, 0x00); //0x03: 3lanes, 0x00: 4lanes
		HDMI_WriteI2C_Byte(0x02, 0x08);   //settle
		HDMI_WriteI2C_Byte(0x08, 0x00);
		
		HDMI_WriteI2C_Byte(0x0c, 0x80);  //fifo position
		HDMI_WriteI2C_Byte(0x1c, 0x80);  //fifo position
		HDMI_WriteI2C_Byte(0x24, 0x70);  //pcr mode( de hs vs)
		
		//HDMI_WriteI2C_Byte(0x2d, 0x1f); //M up limit
		HDMI_WriteI2C_Byte(0x31, 0x0a);   //M down limit
	
		/*stage1 hs mode*/
		HDMI_WriteI2C_Byte(0x25, 0x90);  //line limit
		HDMI_WriteI2C_Byte(0x2a, 0x3a);  //step in limit
		HDMI_WriteI2C_Byte(0x21, 0x4f);  //hs_step
		HDMI_WriteI2C_Byte(0x22, 0xff); 
	
	    /*stage2 de mode*/
	    HDMI_WriteI2C_Byte(0x0a, 0x02); //de adjust pre line
		HDMI_WriteI2C_Byte(0x38, 0x02); //de_threshold 1
		HDMI_WriteI2C_Byte(0x39, 0x04); //de_threshold 2
		HDMI_WriteI2C_Byte(0x3a, 0x08); //de_threshold 3
		HDMI_WriteI2C_Byte(0x3b, 0x10); //de_threshold 4
	
		HDMI_WriteI2C_Byte(0x3f, 0x02); //de_step 1
		HDMI_WriteI2C_Byte(0x40, 0x04); //de_step 2
		HDMI_WriteI2C_Byte(0x41, 0x08); //de_step 3
		HDMI_WriteI2C_Byte(0x42, 0x10); //de_step 4
	
	    /*stage2 hs mode*/
		HDMI_WriteI2C_Byte(0x1e, 0x01); // hs threshold
		HDMI_WriteI2C_Byte(0x23, 0xf0); // hs step
	
		HDMI_WriteI2C_Byte(0x2b, 0x80); //stable out // V1.8 20200417
		
		#ifdef _EDP_Pattern_
		HDMI_WriteI2C_Byte(0x26,(PCR_M|0x80));
		#else
		HDMI_WriteI2C_Byte(0x26,PCR_M);
		#endif
		
//		#ifndef _1080P_eDP_Panel_  
//		LT8911_MIPI_Video_Timing(&video); //defualt setting is 1080P
//		#endif
		
		LT8911_MIPI_Video_Timing(&video); //defualt setting is 1080P
		
		HDMI_WriteI2C_Byte(0xff,0x81); //PCR reset
		HDMI_WriteI2C_Byte(0x03,0x7b);
		HDMI_WriteI2C_Byte(0x03,0xff);
		
		/* Txpll 2.7G*/
		HDMI_WriteI2C_Byte(0xff,0x87);
		HDMI_WriteI2C_Byte(0x19,0x31);
		HDMI_WriteI2C_Byte(0x1a,0x1b);
		HDMI_WriteI2C_Byte(0xff,0x82);
		HDMI_WriteI2C_Byte(0x02,0x42);
		HDMI_WriteI2C_Byte(0x03,0x00);
		HDMI_WriteI2C_Byte(0x03,0x01);
		HDMI_WriteI2C_Byte(0x0a,0x1b);
		HDMI_WriteI2C_Byte(0x04,0x2a);
		HDMI_WriteI2C_Byte(0xff,0x81);
		HDMI_WriteI2C_Byte(0x09,0xfc);
		HDMI_WriteI2C_Byte(0x09,0xfd);
		HDMI_WriteI2C_Byte(0xff,0x87);
		HDMI_WriteI2C_Byte(0x0c,0x11);
		
		for(i=0;i<5;i++) //Check Tx PLL
		{
			msleep(5);
			if (HDMI_ReadI2C_Byte(0x37)&0x02)
			{
				printk("\r\nLT8911 tx pll locked");
			HDMI_WriteI2C_Byte(0xff,0x87);
			HDMI_WriteI2C_Byte(0x1a,0x36);
			HDMI_WriteI2C_Byte(0xff,0x82);
			HDMI_WriteI2C_Byte(0x0a,0x36);
			HDMI_WriteI2C_Byte(0x04,0x3a);
				break;
		  }
			else
			{
				printk("\r\nLT8911 tx pll unlocked");
				HDMI_WriteI2C_Byte(0xff,0x81);
				HDMI_WriteI2C_Byte(0x09,0xfc);
				HDMI_WriteI2C_Byte(0x09,0xfd);
				HDMI_WriteI2C_Byte(0xff,0x87);
				HDMI_WriteI2C_Byte(0x0c,0x10);
				HDMI_WriteI2C_Byte(0x0c,0x11);
			}
		}
		
		/* tx phy */
		HDMI_WriteI2C_Byte(0xff,0x82);
		HDMI_WriteI2C_Byte(0x11,0x00);
		HDMI_WriteI2C_Byte(0x13,0x10);
		HDMI_WriteI2C_Byte(0x14,0x0c);
		HDMI_WriteI2C_Byte(0x14,0x08);
		HDMI_WriteI2C_Byte(0x13,0x20);
		HDMI_WriteI2C_Byte(0xff,0x82);
		HDMI_WriteI2C_Byte(0x0e,0x25);
		//HDMI_WriteI2C_Byte(0x12,0xff);
		HDMI_WriteI2C_Byte(0xff,0x80);
		HDMI_WriteI2C_Byte(0x40,0x22);
		
		/*eDP Tx Digital */
		HDMI_WriteI2C_Byte(0xff,0xa8);
		#ifdef _EDP_Pattern_
		HDMI_WriteI2C_Byte(0x24,0x52);
		HDMI_WriteI2C_Byte(0x27,0x50); //0x50:Pattern; 0x10:mipi video
		#else
		HDMI_WriteI2C_Byte(0x27,0x10); //0x50:Pattern; 0x10:mipi video
		//HDMI_WriteI2C_Byte(0x27,0x50); //0x50:Pattern; 0x10:mipi video
		#endif
		
		#ifdef _6bit_
		HDMI_WriteI2C_Byte(0x17,0x00);
		HDMI_WriteI2C_Byte(0x18,0x00);
		#endif
		
//		#ifndef _1080P_eDP_Panel_ 
//		LT8911_eDP_Video_Timing(&video);
//		#endif
		
		HDMI_WriteI2C_Byte(0xff,0xa0); //nvid = 0x080000;
		HDMI_WriteI2C_Byte(0x00,0x00);
		HDMI_WriteI2C_Byte(0x01,0x80);
}

 void LT8911_InterruptEnable(void)
 {
	HDMI_WriteI2C_Byte(0xff,0x85); 
	HDMI_WriteI2C_Byte(0x08,0x3f); //fm clr
	
	HDMI_WriteI2C_Byte(0x65,0x7f); //vid chk hact clr
	HDMI_WriteI2C_Byte(0x07,0x7f); //vid chk clr
 }

u8 DpcdRead(u32 Address)
{
		u8 DpcdValue = 0x00;
		u8 AddressH = 0x0f & (Address>>16);
		u8 AddressM = 0xff & (Address>>8);
		u8 AddressL = 0xff &  Address    ;
		u8 reg;
		
		HDMI_WriteI2C_Byte(0xff,0xac);
		HDMI_WriteI2C_Byte(0x00,0x00); //Soft Link train
		HDMI_WriteI2C_Byte(0xff,0xa6);
		HDMI_WriteI2C_Byte(0x2a,0x01);
		
		HDMI_WriteI2C_Byte(0xff,0xa6);
		HDMI_WriteI2C_Byte(0x2a,0x01);
		HDMI_WriteI2C_Byte(0x2b,(0x90|AddressH)); //CMD
		HDMI_WriteI2C_Byte(0x2b,AddressM); //addr[15:8]
		HDMI_WriteI2C_Byte(0x2b,AddressL); //addr[7:0]
		HDMI_WriteI2C_Byte(0x2b,0x00); //data lenth
		HDMI_WriteI2C_Byte(0x2c,0x00); //start Aux read edid
	
		msleep(10); //more than 10ms
		reg = HDMI_ReadI2C_Byte(0x25);
		if((reg&0x0f)== 0x0c) 
		{
			if(HDMI_ReadI2C_Byte(0x39) == 0x22)
				{
					HDMI_ReadI2C_Byte(0x2b);
					DpcdValue = HDMI_ReadI2C_Byte(0x2b);
				}
				else
				goto no_reply;
		}
		else if((reg&0x0f)== 0x0a)
			goto reply_nack;
		else if((reg&0x0f)== 0x09)
			goto reply_defer;
		else
		  goto no_reply;
			
		printk("\r\nDpcdRead: 0x%x%x%x= 0x%x ",AddressH,AddressM,AddressL,DpcdValue);

	
		return DpcdValue;
/*error handle*/
no_reply:
    printk("\r\nDpcdRead error: no_reply ");
    return 0;
reply_nack:
    printk("\r\nDpcdRead error: reply_nack ");
    return 0;
reply_defer:
    printk("\r\nDpcdRead error: reply_nack ");
	return 0;
}


bool DpcdWrite(u32 Address,u8 value )
{
		u8 AddressH = 0x0f & (Address>>16);
		u8 AddressM = 0xff & (Address>>8);
		u8 AddressL = 0xff &  Address    ;
		u8 reg;
		printk("\r\nDpcdWrite: 0x%x,0x%x,0x%x= 0x%x ",AddressH,AddressM,AddressL,value);

		HDMI_WriteI2C_Byte(0xff,0xac);
		HDMI_WriteI2C_Byte(0x00,0x00); //Soft Link train
		HDMI_WriteI2C_Byte(0xff,0xa6);
		HDMI_WriteI2C_Byte(0x2a,0x01);
		
		HDMI_WriteI2C_Byte(0xff,0xa6);
		HDMI_WriteI2C_Byte(0x2a,0x01);
		HDMI_WriteI2C_Byte(0x2b,(0x80|AddressH)); //CMD
		HDMI_WriteI2C_Byte(0x2b,AddressM); //addr[15:8]
		HDMI_WriteI2C_Byte(0x2b,AddressL); //addr[7:0]
		HDMI_WriteI2C_Byte(0x2b,0x00); //data lenth
		HDMI_WriteI2C_Byte(0x2b,value); //data lenth
		HDMI_WriteI2C_Byte(0x2c,0x00); //start Aux read edid
	
		msleep(10); //more than 10ms
		reg = HDMI_ReadI2C_Byte(0x25);
		if((reg&0x0f)== 0x0c) 
		{
			return 0;
		}
		else if((reg&0x0f)== 0x0a)
			goto reply_nack;
		else if((reg&0x0f)== 0x09)
			goto reply_defer;
		else
		  goto no_reply;
			
/*error handle*/
no_reply:
    printk("\r\nDpcdRead error: no_reply ");
    return 1;
reply_nack:
    printk("\r\nDpcdRead error: reply_nack ");
    return 1;
reply_defer:
    printk("\r\nDpcdRead error: reply_nack ");
	return 1;
}
 /*
void LT8911_LinkTrain(void)
{
        printk("\r\nlink train start");

		HDMI_WriteI2C_Byte(0xff,0xac);
		HDMI_WriteI2C_Byte(0x00,0x60);	//Soft Link train
		HDMI_WriteI2C_Byte(0xff,0xa6);
		HDMI_WriteI2C_Byte(0x2a,0x00);
		
		HDMI_WriteI2C_Byte(0xff,0x81);
		HDMI_WriteI2C_Byte(0x07,0xfe);
		HDMI_WriteI2C_Byte(0x07,0xff);
		HDMI_WriteI2C_Byte(0x0a,0xfc);
		HDMI_WriteI2C_Byte(0x0a,0xfe);
	


		HDMI_WriteI2C_Byte(0xff,0xa8);
	   	HDMI_WriteI2C_Byte(0x2d,0x80); //edp output video ;

		HDMI_WriteI2C_Byte(0xff,0x85);
		HDMI_WriteI2C_Byte(0x1a,LANE_CNT); 
		//HDMI_WriteI2C_Byte(0x13,0xd1);
		HDMI_WriteI2C_Byte(0xff,0xac);
		HDMI_WriteI2C_Byte(0x00,0x64);
		HDMI_WriteI2C_Byte(0x01,0x0a);
		HDMI_WriteI2C_Byte(0x0c,0x85);
		HDMI_WriteI2C_Byte(0x0c,0xc5);
		msleep(500);

		edp_idle_flag = 0;
}
*/
 void LT8911_LinkTrain(void)
{
		if(SCRAMBLE_MODE == 0x80)
			{
				DpcdWrite(0x10a,0x01);
			}
		
		HDMI_WriteI2C_Byte(0xff,0xa6);
		HDMI_WriteI2C_Byte(0x2a,0x00);
		
		HDMI_WriteI2C_Byte(0xff,0x81);
		HDMI_WriteI2C_Byte(0x07,0xfe);
		HDMI_WriteI2C_Byte(0x07,0xff);
		HDMI_WriteI2C_Byte(0x0a,0xfc);
		HDMI_WriteI2C_Byte(0x0a,0xfe);
	
		/* link train */
		HDMI_WriteI2C_Byte(0xff,0xa8);
	   	HDMI_WriteI2C_Byte(0x2d,MSA_SW_MODE|EDP_IDLE_PTN_OFF); //edp output video ;

		HDMI_WriteI2C_Byte(0xff,0x85);
		HDMI_WriteI2C_Byte(0x17,0xc0);	
		HDMI_WriteI2C_Byte(0x1a,LANE_CNT); 
		HDMI_WriteI2C_Byte(0xa1,(SCRAMBLE_MODE|0x03)); //scramble mode
		//HDMI_WriteI2C_Byte(0x13,0xd1);

#ifdef _link_train_enable_
		HDMI_WriteI2C_Byte(0xff,0xac);
		HDMI_WriteI2C_Byte(0x00,0x60);
		HDMI_WriteI2C_Byte(0x01,0x0a);
		HDMI_WriteI2C_Byte(0x0c,0x05);
		HDMI_WriteI2C_Byte(0x0c,0x45);
		printk("\r\n\33[35mLT8911 link trian: hardware linktrain start...\033[37m");
//		msleep(500);
#else
		HDMI_WriteI2C_Byte(0xff,0xac);
		HDMI_WriteI2C_Byte(0x00,0x00);
		HDMI_WriteI2C_Byte(0x01,0x0a);
		HDMI_WriteI2C_Byte(0x14,0x80);
		HDMI_WriteI2C_Byte(0x14,0x81);
		msleep(50);
		HDMI_WriteI2C_Byte(0x14,0x84);
		msleep(50);
		HDMI_WriteI2C_Byte(0x14,0xc0);
		printk("\r\n\33[35mLT8911 link trian: no handshake linktrain\033[37m");
#endif

		edp_idle_flag = 0;
}



void LT8911_LinkTrainResultCheck(void)
{
#ifdef _link_train_enable_
	u8 i;
	u8 val;
    //int ret;

	HDMI_WriteI2C_Byte(0xff,0xac);
	for(i = 0; i< 10; i++)
	{
	    val = HDMI_ReadI2C_Byte(0x82); 
		if(val & 0x20)
		{
			if((val & 0x1f)==0x1e){ 
			    printk("\r\nLT8911_LinkTrainResultCheck: edp link train successed: 0x%x", val);
		    }
			else{ 
		        printk("\r\nLT8911_LinkTrainResultCheck: edp link train failed: 0x%x", val);
				//while(1);
				HDMI_WriteI2C_Byte(0xff,0xac);
				HDMI_WriteI2C_Byte(0x00,0x00);
				HDMI_WriteI2C_Byte(0x01,0x0a);
				HDMI_WriteI2C_Byte(0x14,0x80);
				HDMI_WriteI2C_Byte(0x14,0x81);
				msleep(50);
				HDMI_WriteI2C_Byte(0x14,0x84);
				msleep(50);
				HDMI_WriteI2C_Byte(0x14,0xc0);
				printk("\r\nLT8911_LinkTrainResultCheck: Enable eDP video output while linktrian fail");
				//while(1);
			 }
			
		  val = HDMI_ReadI2C_Byte(0x83);
		  printk("\r\nLT8911_LinkTrainResultCheck: panel link rate: 0x%x",val);
		  val = HDMI_ReadI2C_Byte(0x84);
		  printk("\r\nLT8911_LinkTrainResultCheck: panel link count: 0x%x",val);
		  return;
		}
		else
		printk("\r\nLT8911_LinkTrainResultCheck: link trian on going...");
	    msleep(100);
	}
#endif
}


/* mipi should be ready before configuring below video check setting*/
void LT8911_video_check(void) 
{
	//u8 temp;
	u32 reg=0x00;
	printk("\r\n----------------------------------------------------------------------");
	/* mipi byte clk check*/
	HDMI_WriteI2C_Byte(0xff,0x85); 
	HDMI_WriteI2C_Byte(0x1d,0x00); //FM select byte clk
	HDMI_WriteI2C_Byte(0x40,0xf7); 
	HDMI_WriteI2C_Byte(0x41,0x30); 
	HDMI_WriteI2C_Byte(0xa1,0x02); //video chech from mipi
	
	HDMI_WriteI2C_Byte(0xff,0x81); //video check rst
	HDMI_WriteI2C_Byte(0x09,0x7d); 
	HDMI_WriteI2C_Byte(0x09,0xfd);
	
	HDMI_WriteI2C_Byte(0xff,0x85); 
	msleep(30);
	if(HDMI_ReadI2C_Byte(0x50)== 0x03) 
	{
		reg = HDMI_ReadI2C_Byte(0x4d);
		reg = reg*256 + HDMI_ReadI2C_Byte(0x4e);
		reg = reg*256 + HDMI_ReadI2C_Byte(0x4f);
		
    	printk("\r\nvideo check: mipi clk = %d",reg); 
	}
	else
	printk("\r\nvideo check: mipi clk unstable");
	
	/* mipi vtotal check*/
	reg = HDMI_ReadI2C_Byte(0x76);
	reg = reg*256 + HDMI_ReadI2C_Byte(0x77);
	
	printk("\r\nvideo check: Vtotal = %d",reg);
	
	/* mipi word count check*/
	HDMI_WriteI2C_Byte(0xff,0xd0);
	reg = HDMI_ReadI2C_Byte(0x82);
	reg = reg*256 + HDMI_ReadI2C_Byte(0x83);
	reg = reg/3;
	
	printk("\r\nvideo check: Hact(word counter) = %d",reg);
	//printk(reg);
	
	/* mipi Vact check*/
	reg = HDMI_ReadI2C_Byte(0x85);
	reg = reg*256 + HDMI_ReadI2C_Byte(0x86);
	
	printk("\r\nvideo check: Vact = %d",reg);
	//printk(reg);

	printk("\r\nlane0 settle: 0x%x ",HDMI_ReadI2C_Byte(0x88));
	printk("\r\nlane1 settle: 0x%x ",HDMI_ReadI2C_Byte(0x8a));
	printk("\r\nlane2 settle: 0x%x ",HDMI_ReadI2C_Byte(0x8c));
	printk("\r\nlane3 settle: 0x%x ",HDMI_ReadI2C_Byte(0x8e));

	printk("\r\nlane0 sot: 0x%x ",HDMI_ReadI2C_Byte(0x89));
	printk("\r\nlane1 sot: 0x%x ",HDMI_ReadI2C_Byte(0x8b));
	printk("\r\nlane2 sot: 0x%x ",HDMI_ReadI2C_Byte(0x8d));
	printk("\r\nlane3 sot: 0x%x ",HDMI_ReadI2C_Byte(0x8f));
	printk("\r\n----------------------------------------------------------------------");	
}

void LT8911_MainLoop(struct video_timing *video_format)
{
	u16 reg;
	u16 vtotal;
	static int flag_mipi_on = 1;
	
	vtotal = video_format->vtotal;
	HDMI_WriteI2C_Byte(0xff,0x85); 
	//HDMI_WriteI2C_Byte(0x1d,0x00); //FM select byte clk
	//HDMI_WriteI2C_Byte(0x40,0xf7); 
	//HDMI_WriteI2C_Byte(0x41,0x30); 
	HDMI_WriteI2C_Byte(0xa1,0x02); //video chech from mipi
	
	HDMI_WriteI2C_Byte(0xff,0x81); //video check rst
	HDMI_WriteI2C_Byte(0x09,0x7d); 
	HDMI_WriteI2C_Byte(0x09,0xfd);
	msleep(50);

	HDMI_WriteI2C_Byte(0xff,0x85);
	reg = HDMI_ReadI2C_Byte(0x76);
	reg = reg*256 + HDMI_ReadI2C_Byte(0x77);

	//printk("\r\nPCR reset: %d",reg);
	//printk("\r\nvtotal: %d",vtotal);
	if((reg <= (vtotal + 3))&&(reg >= (vtotal - 3)))
		{
			if(!flag_mipi_on)
				{
					msleep(1000);
					HDMI_WriteI2C_Byte(0xff,0x81); //PCR reset
					HDMI_WriteI2C_Byte(0x03,0x7b);
					HDMI_WriteI2C_Byte(0x03,0xff);
					msleep(100);
					HDMI_WriteI2C_Byte(0xff,0xa8);
	  				HDMI_WriteI2C_Byte(0x2d,0x88);  //edp disable idle pattern;
					flag_mipi_on = 1;
					printk("\r\nPCR reset0");
				}

			 HDMI_WriteI2C_Byte(0xff,0xd0);
			 if((HDMI_ReadI2C_Byte(0x84)&0x40) == 0x00)
			 {
				 	HDMI_WriteI2C_Byte(0xff,0x81); //PCR reset
					HDMI_WriteI2C_Byte(0x03,0x7b);
					HDMI_WriteI2C_Byte(0x03,0xff);
					msleep(500);
					printk("\r\nPCR reset1");
			 }
			 else
			 {

//				printk("\r\npcr stable:%x, mk: %x, %x, %x, %x",
//				HDMI_ReadI2C_Byte(0x84),
//				HDMI_ReadI2C_Byte(0x94),
//				HDMI_ReadI2C_Byte(0x95),
//				HDMI_ReadI2C_Byte(0x96),
//				HDMI_ReadI2C_Byte(0x97));

//				if((HDMI_ReadI2C_Byte(0x94)==0x17)&&((HDMI_ReadI2C_Byte(0x95)==0x53)||(HDMI_ReadI2C_Byte(0x95)==0x47)))
//				{
//				   	return;
//				}
//				else
//				{	
//					HDMI_WriteI2C_Byte(0xff,0x81); //PCR reset
//					HDMI_WriteI2C_Byte(0x03,0x7b);
//					HDMI_WriteI2C_Byte(0x03,0xff);
//					msleep(500);
//					printk("\r\nPCR reset2");
//				}
			 } 
		}
	else
		{
			HDMI_WriteI2C_Byte(0xff,0xa8);
	  		HDMI_WriteI2C_Byte(0x2d,0x8c);  //edp enable idle pattern;
			flag_mipi_on = 0;
		}
}


void LT8911_pcr_mk_printk(void)
{
	#ifdef _pcr_mk_printk_
	u8 loopx = 0;

	for(loopx = 0; loopx < 30; loopx++)
	{
		HDMI_WriteI2C_Byte(0xff,0xd0); 
		printk("\r\npcr stable bit[4]:%x, mk: %x, %x, %x, %x",
			HDMI_ReadI2C_Byte(0x84),
			HDMI_ReadI2C_Byte(0x94),
			HDMI_ReadI2C_Byte(0x95),
			HDMI_ReadI2C_Byte(0x96),
			HDMI_ReadI2C_Byte(0x97));
		msleep(500);
	}
	#endif
}

void LT8911_htotal_stable_printk(void)
{
	#ifdef _htotal_stable_check_
	u8 loopx = 0;
	u32 val;
	
	for(loopx = 0; loopx < 30; loopx++)
	{
		HDMI_WriteI2C_Byte(0xff,0x85); 
		val = HDMI_ReadI2C_Byte(0x90);
		val = val*256 + HDMI_ReadI2C_Byte(0x91);
		printk("\r\nmipi htotal: %d ",val); 
		msleep(10);
	}
	#endif
}

void LT8911_LinkTrainRresultDebug(void)
{
	u8 i;
	u8 val;
	
	HDMI_WriteI2C_Byte(0xff,0xac);
	for(i = 0; i< 10; i++)
	{
	    val = HDMI_ReadI2C_Byte(0x82); 
		if(val & 0x20)
		{
			if((val & 0x1f)==0x1e)
			{
				//val = HDMI_ReadI2C_Byte(0x82); 
			    printk("\r\nedp link train successed: %x", val);
		    }
			else{
				//val = HDMI_ReadI2C_Byte(0x82); 
		        printk("\r\nedp link train failed: %x", val);
			 }
		  val = HDMI_ReadI2C_Byte(0x83);
		  printk("\r\npanel link rate: %x",val);
		  val = HDMI_ReadI2C_Byte(0x84);
		  printk("\r\npanel link count: %x",val);
		  return;
		}
		else
		printk("\r\nlink trian on going...");
	  msleep(100);
	}
}



//void IntbInterruptFun(void) interrupt 7
//{
//    g_irq_flag = 1;
//}
//
//void InterruptTaskHandle(void)
//{
//  if(g_irq_flag)  // interrupt event happened
//  {
//	printk("\r\nirq task happened...");
//
//	HDMI_WriteI2C_Byte(0xff,0xa8);
//  	HDMI_WriteI2C_Byte(0x2d,0x84);  //edp output idle pattern;
//
//	msleep(1500);
//    HDMI_WriteI2C_Byte(0xff,0x85);  //clr LT
//  	HDMI_WriteI2C_Byte(0x08,0xc0); 
//  	HDMI_WriteI2C_Byte(0x08,0x00);
//
//	
//	HDMI_WriteI2C_Byte(0x65,0xff); //vid chk hact clr
//	HDMI_WriteI2C_Byte(0x65,0x7f); //vid chk hact clr
//
//	HDMI_WriteI2C_Byte(0x07,0xff); //vid chk clr
//	HDMI_WriteI2C_Byte(0x07,0x7f); //vid chk clr
//
//  	edp_idle_flag = 1;
//   if(PIF == 0x08)	 //clear MCU interrupt flag to handle next interrupt request.
//	{
//		PIF = 0x00; 
//		clr_IE0;
//	}
//	g_irq_flag = 0;
//  }
//}

