#include "receiver_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int levels[10],rises,acks,done;
static void output(int n,int high){assert(n>=0&&n<10);levels[n]=high;if(high)rises++;}
static void ack(const char *id,char a,const char *state){assert(id[0]);acks++;if(!strcmp(state,"DONE"))done++;}
static void feed(receiver_core *s,const char *v,uint32_t now){receiver_feed(s,(const unsigned char*)v,strlen(v),now);}
int main(void){
 receiver_core s;unsigned i;char cmd[96];
 receiver_init(&s,output,ack);
 feed(&s,"CMD,first,",0);assert(rises==0);feed(&s,"a\n",0);assert(levels[0]);
 receiver_tick(&s,999);assert(levels[0]);receiver_tick(&s,1000);assert(!levels[0]&&done==1);
 feed(&s,"CMD,first,a\n",1100);assert(rises==1&&done==2);
 feed(&s,"ACK,first,a,TIMER,1,DONE\nCMD,bad,z\nCMD,,a\n",1200);assert(rises==1);
 feed(&s,"CMD,two,b\r\nCMD,three,j\n",2000);assert(levels[1]&&levels[9]);
 receiver_tick(&s,3000);assert(!levels[1]&&!levels[9]);
 feed(&s,"CMD,r1,c\n",4000);feed(&s,"CMD,r2,c\n",4500);
 receiver_tick(&s,5000);assert(levels[2]);receiver_tick(&s,5500);assert(!levels[2]);
 feed(&s,"CMD,wrap,d\n",0xffffff00u);receiver_tick(&s,743);assert(levels[3]);receiver_tick(&s,744);assert(!levels[3]);
 for(i=0;i<110;i++){feed(&s,"x",8000);}
 feed(&s,"\nCMD,recover,e\n",8000);assert(levels[4]);receiver_tick(&s,9000);
 receiver_feed(&s,(const unsigned char*)"CMD,null,f\0\n",12,10000);assert(!levels[5]);
 for(i=0;i<10;i++){sprintf(cmd,"CMD,ch%u,%c\n",i,'a'+i);feed(&s,cmd,11000);}
 for(i=0;i<10;i++)assert(levels[i]);receiver_tick(&s,12000);for(i=0;i<10;i++)assert(!levels[i]);
 puts("Receiver tests passed: fragmentation, CRLF, multiple frames, invalid/overflow/NUL, dedup, retrigger, wrap, 10 channels");
 return 0;
}
