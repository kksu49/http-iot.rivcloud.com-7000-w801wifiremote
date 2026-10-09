#include "receiver_core.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int levels[10],ack_count;
static void output(int channel,int high){levels[channel]=high;}
static void ack(const char *id,char action,const char *state){assert(!strcmp(id,"286dcdce69cd-6f1c466d-0000a44e-16"));assert(action=='d');assert(!strcmp(state,"DONE"));ack_count++;}
int main(void){
 receiver_core core;const char *frame="CMD,286dcdce69cd-6f1c466d-0000a44e-16,d\n";
 receiver_init(&core,output,ack);
 receiver_feed(&core,(const unsigned char*)frame,strlen(frame),100);
 assert(levels[3]==1);receiver_tick(&core,1099);assert(levels[3]==1);
 receiver_tick(&core,1100);assert(levels[3]==0&&ack_count==1);
 receiver_init(&core,output,ack);
 frame="CMD,286dcdce69cd-6f1c466d-0000a44e-16,d \n";
 receiver_feed(&core,(const unsigned char*)frame,strlen(frame),100);assert(levels[3]==1);
 receiver_tick(&core,1100);assert(levels[3]==0&&ack_count==2);
 receiver_init(&core,output,ack);
 frame="[RIV]# TCP Recv Len : 26.\r\n[RIV]# TCP Recv data : CMD,286dcdce69cd-6f1c466d-0000a44e-16,d\r\n.\r\n";
 for(unsigned i=0;i<strlen(frame);i++)receiver_feed(&core,(const unsigned char*)frame+i,1,100);
 assert(levels[3]==1);receiver_tick(&core,1099);assert(levels[3]==1);
 receiver_tick(&core,1100);assert(levels[3]==0&&ack_count==3);
 receiver_init(&core,output,ack);
 frame="unrelated CMD,286dcdce69cd-6f1c466d-0000a44e-16,d\n";
 receiver_feed(&core,(const unsigned char*)frame,strlen(frame),100);assert(levels[3]==0);
 puts("Raw, spaced, fragmented DTU-prefixed frames passed; unrelated text ignored.");return 0;
}
