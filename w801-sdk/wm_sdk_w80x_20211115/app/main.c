/***************************************************************************** 
* 
* File Name : main.c
* 
* Description: main 
* 
* Copyright (c) 2014 Winner Micro Electronic Design Co., Ltd. 
* All rights reserved. 
* 
* Author : dave
* 
* Date : 2014-6-14
*****************************************************************************/ 
#include "wm_include.h"

void UserMain(void)
{
	extern void remote_sender_start(void);
	extern void remote_receiver_start(void);
	extern void wifi_provision_start(void);
	wifi_provision_start();
	remote_receiver_start();
	remote_sender_start();

#if DEMO_CONSOLE
	CreateDemoTask();
#endif
//用户自己的task
}

