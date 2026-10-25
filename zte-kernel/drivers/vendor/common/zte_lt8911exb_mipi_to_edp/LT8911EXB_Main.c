#include "include/include.h"

bool g_irq_flag = 0;


int LT8911EXB_Main(void *data)
{  

	printk("LT8911EXB Init Start:");

	LT8911_Reset();
	msleep(100);
    LT8911_ChipID();
	LT8911_SetVideoTiming(&video);
	LT8911_Init();
	LT8911_LinkTrain();
	
	LT8911_LinkTrainResultCheck();
    LT8911_video_check() ;
	LT8911_pcr_mk_printk();
	LT8911_htotal_stable_printk();

  
	while(!atomic_read(&thread_should_stop))
	{
	  LT8911_MainLoop(&video);
	  msleep(500);

	}
	return 0;
	
}
