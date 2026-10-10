#include "receiver_core.h"
#include <string.h>
#include "short_frame.h"
void receiver_init(receiver_core *s,void(*out)(int,int),void(*ack)(const char*,char,const char*)) {
 int i;memset(s,0,sizeof(*s));s->output=out;s->ack=ack;s->pulse_ms=1000;
 for(i=0;i<10;i++){s->active[i]=-1;out(i,0);}
}
static void frame(receiver_core *s,uint32_t now) {
 char *id,*comma,action,*payload=s->line;unsigned i,n,slot;int channel,compact;
 /* This DTU can output its diagnostic receive line instead of raw TCP data. */
 static const char prefix[]="[RIV]# TCP Recv data : ";
 if(!strncmp(payload,prefix,sizeof(prefix)-1))payload+=sizeof(prefix)-1;
 compact=short_control(payload,'C');
 if(compact){id=payload+2;action=payload[1];}
 else {
  if(strncmp(payload,"CMD,",4))return;
  id=payload+4;comma=strchr(id,',');if(!comma)return;
  n=comma-id;if(!n||n>=RX_ID_SIZE)return;
  if(comma[1]<'a'||comma[1]>'j'||comma[2])return;
  for(i=0;i<n;i++)if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='A'&&id[i]<='Z')||(id[i]>='0'&&id[i]<='9')||id[i]=='-'||id[i]=='_'))return;
  *comma=0;action=comma[1];
 }
 channel=action-'a';
 if(s->command)s->command(id,action);
 for(i=0;i<32;i++)if(s->records[i].state&&!strcmp(s->records[i].id,id)) {
  if(s->records[i].action==action&&s->records[i].state==2){s->records[i].compact=compact;s->ack(id,action,"DONE");}
  return;
 }
 slot=s->next;
 for(i=0;i<32;i++){slot=(s->next+i)%32;if(s->records[slot].state!=1)break;}
 if(i==32)return;
 if(s->active[channel]>=0) {
  rx_record *old=&s->records[s->active[channel]];old->state=3;
  s->ack(old->id,old->action,"SUPERSEDED");
 }
 strcpy(s->records[slot].id,id);s->records[slot].action=action;s->records[slot].state=1;
 s->records[slot].compact=compact;
 s->next=(slot+1)%32;s->active[channel]=slot;s->due[channel]=now+s->pulse_ms;
 s->output(channel,1);
}
void receiver_tick(receiver_core *s,uint32_t now) {
 int i;for(i=0;i<10;i++)if(s->active[i]>=0&&(int32_t)(now-s->due[i])>=0) {
  rx_record *r=&s->records[s->active[i]];
  s->output(i,0);s->active[i]=-1;r->state=2;s->ack(r->id,r->action,"DONE");
 }
}
void receiver_feed(receiver_core *s,const unsigned char *data,unsigned count,uint32_t now) {
 unsigned i;receiver_tick(s,now);
 for(i=0;i<count;i++) {
  unsigned char c=data[i];
  if(c=='\n') {
   if(!s->discard){while(s->length&&(s->line[s->length-1]=='\r'||s->line[s->length-1]==' '||s->line[s->length-1]=='\t'))s->length--;s->line[s->length]=0;frame(s,now);}
   s->length=0;s->discard=0;
  }else if(!s->discard) {
   if(!c||s->length>=sizeof(s->line)-1){s->discard=1;s->length=0;}
   else s->line[s->length++]=c;
  }
 }
}
