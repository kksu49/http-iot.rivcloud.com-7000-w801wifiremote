#include "wm_include.h"
#include "wm_gpio_afsel.h"
#include "receiver_core.h"
#include "remote_board.h"
#include <string.h>
#include "random.h"
#include "short_frame.h"
static char link_frames[32][96];
static unsigned link_stamp[32],link_head,link_tail;
static unsigned char link_serial[32];
static volatile int link_ready;
int remote_link_enqueue(const char *frame,int serial) {
 unsigned next;u32 lock;
 if(!frame||strlen(frame)>=96||!link_ready)return 0;
 lock=tls_os_set_critical();next=(link_head+1)%32;
 if(next==link_tail){tls_os_release_critical(lock);return 0;}
 strcpy(link_frames[link_head],frame);link_serial[link_head]=serial;
 link_stamp[link_head]=tls_os_get_time();link_head=next;
 tls_os_release_critical(lock);return 1;
}
int remote_serial_button(char action) {
 static unsigned seq,session;u8 mac[6];char frame[96],id[20];
 if(!session&&(random_get_bytes(&session,sizeof(session))!=0||!session))return 0;
 tls_get_mac_addr(mac);
 if(seq==0xffffffffu){session=0;seq=0;return 0;}
 short_id(id,mac,session,++seq);snprintf(frame,sizeof(frame),"R%c%s\n",action,id);
 return remote_link_enqueue(frame,1);
}
static receiver_core core;
static OS_STK receiver_stack[1024];
static tls_os_task_t receiver_task;
static const enum tls_io_name outputs[]=REMOTE_OUTPUT_PINS;
static const unsigned char on_levels[]=REMOTE_OUTPUT_LEVELS;
static void log_command(const char *id,char action){printf("RX_COMMAND id=%s action=%c\n",id,action);}
static void set_output(int channel,int high){
 int level=high?on_levels[channel]:!on_levels[channel];
 tls_gpio_write(outputs[channel],level);
 printf("RX_GPIO out=%d pin_enum=%d level=%d read=%d active=%d tick=%lu\n",channel+1,(int)outputs[channel],level,tls_gpio_read(outputs[channel]),high,(unsigned long)tls_os_get_time());
}
static void reply_ack(const char *id,char action,const char *state) {
 char frame[128];int length,i,compact=0;
 for(i=0;i<32;i++)if(core.records[i].state&&!strcmp(core.records[i].id,id)){compact=core.records[i].compact;break;}
 if(compact)snprintf(frame,sizeof(frame),"%c%c%s\n",!strcmp(state,"DONE")?'K':'S',action,id);
 else snprintf(frame,sizeof(frame),"ACK,%s,%c,TIMER,%d,%s\n",id,action,action-'a'+1,state);
 length=strlen(frame);
 if(tls_uart_write(TLS_UART_5,frame,length)!=WM_SUCCESS)printf("RX_ACK_WRITE_FAILED\n");
 printf("RX_OUT%d %s id=%s\n",action-'a'+1,state,id);
}
static void receiver_entry(void *arg) {
 tls_uart_options_t opt;unsigned char buffer[128];char hex[257];int i,n;
 uint32_t last_tick,now=0,fraction=0,last_status=0,rx_bytes=0,last_rx=0,last_heartbeat=0;
 for(i=0;i<10;i++){tls_gpio_write(outputs[i],!on_levels[i]);tls_gpio_cfg(outputs[i],WM_GPIO_DIR_OUTPUT,WM_GPIO_ATTR_FLOATING);}
 receiver_init(&core,set_output,reply_ack);
 core.command=log_command;
 core.pulse_ms=REMOTE_PULSE_MS;
 memset(&opt,0,sizeof(opt));opt.baudrate=REMOTE_UART_BAUD;opt.paritytype=TLS_UART_PMODE_DISABLED;
 opt.stopbits=TLS_UART_ONE_STOPBITS;opt.charlength=TLS_UART_CHSIZE_8BIT;opt.flow_ctrl=TLS_UART_FLOW_CTRL_NONE;
 wm_uart5_rx_config(REMOTE_UART_RX_PIN);wm_uart5_tx_config(REMOTE_UART_TX_PIN);
 if(tls_uart_port_init(TLS_UART_5,&opt,0)!=WM_SUCCESS){printf("RECEIVER_UART_INIT_FAILED (outputs low)\n");return;}
 printf("RECEIVER_READY UART5 rx_enum=%d tx_enum=%d baud=%d pulse=%dms on=%d\n",(int)REMOTE_UART_RX_PIN,(int)REMOTE_UART_TX_PIN,REMOTE_UART_BAUD,REMOTE_PULSE_MS,REMOTE_OUTPUT_ON);
 last_tick=tls_os_get_time();
 link_ready=1;
 for(;;){
  uint32_t ticks=tls_os_get_time();
  /* Millisecond clock derived from OS ticks; unsigned subtraction survives wrap. */
  uint64_t scaled=(uint64_t)(uint32_t)(ticks-last_tick)*1000+fraction;
  now+=(uint32_t)(scaled/HZ);fraction=scaled%HZ;last_tick=ticks;
  receiver_tick(&core,now);
  {
   char frame[96];unsigned stamp=0;int serial=0,got=0;u32 lock=tls_os_set_critical();
   if(link_tail!=link_head){strcpy(frame,link_frames[link_tail]);stamp=link_stamp[link_tail];serial=link_serial[link_tail];link_tail=(link_tail+1)%32;got=1;}
   tls_os_release_critical(lock);
   if(got){
    if((unsigned)(ticks-stamp)>5*HZ)printf("LINK_STALE_DROPPED\n");
    else if(serial){int result=tls_uart_write(TLS_UART_5,frame,strlen(frame));printf("REVERSE_UART_TX %s frame=%s",result==WM_SUCCESS?"QUEUED":"FAILED",frame);}
    else {printf("REVERSE_PLATFORM_RX frame=%s",frame);receiver_feed(&core,(const unsigned char*)frame,strlen(frame),now);}
   }
  }
  if(REMOTE_SERIAL_HEARTBEAT_MS && (uint32_t)(now-last_heartbeat)>=REMOTE_SERIAL_HEARTBEAT_MS){
   static char heartbeat[]="HB,W801\r\n";
   int result=tls_uart_write(TLS_UART_5,heartbeat,sizeof(heartbeat)-1);
   last_heartbeat=now;
   printf("SERIAL_HEARTBEAT %s uptime_s=%lu tx=PA12\n",result==WM_SUCCESS?"QUEUED":"FAILED",(unsigned long)(now/1000));
  }
  n=tls_uart_read(TLS_UART_5,buffer,sizeof(buffer));
  if(n>0){
   rx_bytes+=(uint32_t)n;last_rx=now;
   for(i=0;i<n;i++)snprintf(hex+2*i,3,"%02x",buffer[i]);
   printf("RX_UART5_BYTES count=%d hex=%s\n",n,hex);
   receiver_feed(&core,buffer,n,now);
  }
  if((uint32_t)(now-last_status)>=10000){
   uint32_t base=HR_UART0_BASE_ADDR+TLS_UART_5*(HR_UART1_BASE_ADDR-HR_UART0_BASE_ADDR);
   printf("RX_ALIVE uptime_s=%lu bytes=%lu idle_s=%lu partial=%u discard=%d lc=%08lx mask=%08lx fifo=%08lx\n",
    (unsigned long)(now/1000),(unsigned long)rx_bytes,(unsigned long)((now-last_rx)/1000),core.length,core.discard,
    (unsigned long)tls_reg_read32(base),(unsigned long)tls_reg_read32(base+0x14),(unsigned long)tls_reg_read32(base+0x1c));
   last_status=now;
  }
  tls_os_time_delay(1);
 }
}
void remote_receiver_start(void) {
 if(tls_os_task_create(&receiver_task,"remote_rx",receiver_entry,0,(u8*)receiver_stack,sizeof(receiver_stack),31,0)!=WM_SUCCESS)printf("RECEIVER_TASK_CREATE_FAILED\n");
}
