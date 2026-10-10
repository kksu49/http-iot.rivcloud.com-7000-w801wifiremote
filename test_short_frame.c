#include "receiver_core.h"
#include "short_frame.h"
#include <assert.h>
#include <stdio.h>
static int levels[10],rises,done;
static void output(int n,int active){levels[n]=active;if(active)rises++;}
static void ack(const char *id,char action,const char *state){assert(short_valid_id(id));if(!strcmp(state,"DONE"))done++;}
int main(void){
 unsigned char mac[6]={0x28,0x6d,0xcd,0xce,0x93,0x73};char id[20],other[20],frame[96];receiver_core core;
 short_id(id,mac,0x12345678u,1);assert(strlen(id)==19&&short_valid_id(id));
 short_id(other,mac,0x12345678u,2);assert(strcmp(id,other));
 mac[0]++;short_id(other,mac,0x12345678u,1);assert(strcmp(id,other));mac[0]--;
 short_id(other,mac,0x12345679u,1);assert(strcmp(id,other));
 receiver_init(&core,output,ack);
 for(int i=0;i<10;i++){
  short_id(id,mac,0x12345678u,i+1);sprintf(frame,"C%c%s\n",'a'+i,id);assert(strlen(frame)==22);
  for(unsigned j=0;j<strlen(frame);j++)receiver_feed(&core,(unsigned char*)frame+j,1,0);
  assert(levels[i]);receiver_feed(&core,(unsigned char*)frame,strlen(frame),100);assert(rises==i+1);
  assert(core.records[i].compact);
 }
 receiver_tick(&core,999);for(int i=0;i<10;i++)assert(levels[i]);
 receiver_tick(&core,1000);for(int i=0;i<10;i++)assert(!levels[i]);assert(done==10);
 sprintf(frame,"[RIV]# TCP Recv data : Cd%s\r\n",id);
 receiver_feed(&core,(unsigned char*)frame,strlen(frame),1100);assert(rises==10); /* same ID cannot change action */
 short_id(id,mac,0x12345678u,11);sprintf(frame,"[RIV]# TCP Recv data : Cd%s\r\n",id);
 receiver_feed(&core,(unsigned char*)frame,strlen(frame),1200);assert(levels[3]&&rises==11);
 receiver_tick(&core,2200);assert(!levels[3]&&done==11);
 sprintf(frame,"Ra%s\nKa%s\nSa%s\nCaBAD\nCz%s\n",id,id,id,id);
 receiver_feed(&core,(unsigned char*)frame,strlen(frame),3000);assert(rises==11);
 receiver_init(&core,output,ack);sprintf(frame,"CMD,%s,a\n",id);
 receiver_feed(&core,(unsigned char*)frame,strlen(frame),4000);assert(!core.records[0].compact);
 receiver_tick(&core,5000);assert(done==12);
 puts("Compact tests passed: 22-byte frames, MAC/session/sequence, a-j, fragmentation, dedup, 1s reset, debug prefix, invalid/direction filtering");
 return 0;
}
