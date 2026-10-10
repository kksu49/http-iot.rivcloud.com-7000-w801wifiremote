#include "wm_hal.h"
#include "receiver_core.h"
#include "short_frame.h"
#include "board_config.h"

static UART_HandleTypeDef uart;
static receiver_core core;
static const unsigned pins[10]=OUTPUT_PINS;
static const unsigned char levels[10]=OUTPUT_LEVELS;
static const unsigned char board_id[6]=BOARD_ID;
static volatile unsigned head,tail,overflow;
static unsigned char ring[2048],irq_buf[64];
static uint32_t session,sequence;
static int reverse_ready;

__attribute__((isr)) void CORET_IRQHandler(void){
 volatile uint32_t status=*(volatile uint32_t*)0xE000E010;(void)status;HAL_IncTick();
}
__attribute__((isr)) void UART2_5_IRQHandler(void){HAL_UART_IRQHandler(&uart);}
void HAL_UART_MspInit(UART_HandleTypeDef *h){
 if(h->Instance!=UART5)return;
 __HAL_RCC_UART5_CLK_ENABLE();__HAL_RCC_GPIO_CLK_ENABLE();
 /* Vendor UART5 TX macro refers to undefined GPIO_PIN_08. PA12 is OPT2. */
 __AFIO_REMAP_SET_OPT2(GPIOA,GPIO_PIN_12);
 __HAL_AFIO_REMAP_UART5_RX(GPIOA,GPIO_PIN_13);
 HAL_NVIC_SetPriority(UART2_5_IRQn,1);HAL_NVIC_EnableIRQ(UART2_5_IRQn);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h){
 unsigned i,next;if(h!=&uart)return;
 for(i=0;i<h->RxXferCount;i++){
  next=(head+1)%sizeof(ring);
  if(next==tail){overflow=1;continue;}
  ring[head]=h->pRxBuffPtr[i];head=next;
 }
}
static int transmit(const char *s){
 return HAL_UART_Transmit(&uart,(uint8_t*)s,strlen(s),20)==HAL_OK;
}
static void output(int channel,int active){
 unsigned level=active?levels[channel]:!levels[channel];
 HAL_GPIO_WritePin(GPIOB,1u<<pins[channel],level?GPIO_PIN_SET:GPIO_PIN_RESET);
 printf("RX_GPIO action=%c PB%u level=%u active=%d tick=%lu\r\n",'a'+channel,pins[channel],level,active,(unsigned long)HAL_GetTick());
}
static void command(const char *id,char action){printf("RX_COMMAND action=%c id=%s\r\n",action,id);}
static void ack(const char *id,char action,const char *state){
 char frame[128];int i,compact=0;
 for(i=0;i<32;i++)if(core.records[i].state&&!strcmp(core.records[i].id,id)){compact=core.records[i].compact;break;}
 if(compact)snprintf(frame,sizeof(frame),"%c%c%s\n",!strcmp(state,"DONE")?'K':'S',action,id);
 else snprintf(frame,sizeof(frame),"ACK,%s,%c,TIMER,%d,%s\n",id,action,action-'a'+1,state);
 printf("RX_ACK %s action=%c %s\r\n",state,action,transmit(frame)?"SENT":"FAILED");
}
/* Two separate reserved flash sectors: an interrupted write preserves the
   previous boot counter. Readback must pass before enabling reverse commands. */
typedef struct {uint32_t magic,count,inverse;} boot_record;
static int valid(const boot_record *r){return r->magic==0x806ac001u&&r->count&&r->inverse==~r->count;}
static int new_session(void){
 boot_record a,b,next,verify;uint32_t address,count=0;
 if(HAL_FLASH_Read(0xfe000,(uint8_t*)&a,sizeof(a))!=HAL_OK||HAL_FLASH_Read(0xff000,(uint8_t*)&b,sizeof(b))!=HAL_OK)return 0;
 if(valid(&a))count=a.count;
 if(valid(&b)&&b.count>count)count=b.count;
 if(count==0xffffffffu)return 0;
 address=valid(&a)&&a.count==count?0xff000:0xfe000;
 next.magic=0x806ac001u;next.count=count+1;next.inverse=~next.count;
 if(HAL_FLASH_Write(address,(uint8_t*)&next,sizeof(next))!=HAL_OK||HAL_FLASH_Read(address,(uint8_t*)&verify,sizeof(verify))!=HAL_OK||memcmp(&next,&verify,sizeof(next)))return 0;
 session=next.count;sequence=0;return 1;
}
static void buttons(uint32_t now){
 static uint32_t last;static unsigned char pressed[10],released[10],armed[10];int i,low;
 if((uint32_t)(now-last)<10)return;last=now;
 for(i=0;i<10;i++){
  low=HAL_GPIO_ReadPin(GPIOA,1u<<i)==GPIO_PIN_RESET;
  if(!low){pressed[i]=0;if(released[i]<3)released[i]++;if(released[i]>=3)armed[i]=1;continue;}
  released[i]=0;if(!armed[i]||++pressed[i]<3)continue;armed[i]=0;
  if(!reverse_ready||sequence==0xffffffffu){printf("KEY %c REVERSE_DISABLED\r\n",'a'+i);continue;}
  {char id[20],frame[24];short_id(id,board_id,session,++sequence);
   snprintf(frame,sizeof(frame),"R%c%s\n",'a'+i,id);
   printf("KEY %c REVERSE_UART_TX %s id=%s\r\n",'a'+i,transmit(frame)?"SENT":"FAILED",id);}
 }
}
int main(void){
 GPIO_InitTypeDef g={0};unsigned i;uint32_t heartbeat=0,report=0;
 SystemClock_Config(CPU_CLK_160M);HAL_Init();__HAL_RCC_GPIO_CLK_ENABLE();
 for(i=0;i<10;i++){
  HAL_GPIO_WritePin(GPIOB,1u<<pins[i],levels[i]?GPIO_PIN_RESET:GPIO_PIN_SET);
  g.Pin=1u<<pins[i];g.Mode=GPIO_MODE_OUTPUT;g.Pull=GPIO_NOPULL;HAL_GPIO_Init(GPIOB,&g);
 }
 g.Pin=0x3ff;g.Mode=GPIO_MODE_INPUT;g.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOA,&g);
 receiver_init(&core,output,ack);core.pulse_ms=PULSE_MS;core.command=command;
 reverse_ready=new_session();
 uart.Instance=UART5;uart.Init.BaudRate=UART_BAUD;uart.Init.WordLength=UART_WORDLENGTH_8B;
 uart.Init.StopBits=UART_STOPBITS_1;uart.Init.Parity=UART_PARITY_NONE;
 uart.Init.Mode=UART_MODE_TX|UART_MODE_RX;uart.Init.HwFlowCtl=UART_HWCONTROL_NONE;
 if(HAL_UART_Init(&uart)!=HAL_OK||HAL_UART_Receive_IT(&uart,irq_buf,0)!=HAL_OK){printf("UART_INIT_FAILED\r\n");for(;;);}
 printf("W806_DTU_V1 READY UART5 TX=PA12 RX=PA13 baud=%u pulse=%u heartbeat=%u reverse=%d session=%lu\r\n",UART_BAUD,PULSE_MS,HEARTBEAT_MS,reverse_ready,(unsigned long)session);
 for(;;){
  uint32_t now=HAL_GetTick();unsigned char batch[128];unsigned n=0;
  receiver_tick(&core,now);
  if(overflow){__disable_irq();tail=head;overflow=0;__enable_irq();core.length=0;core.discard=1;printf("RX_OVERFLOW_DROP_TO_NEWLINE\r\n");}
  while(tail!=head&&n<sizeof(batch)){batch[n++]=ring[tail];tail=(tail+1)%sizeof(ring);}
  if(n)receiver_feed(&core,batch,n,now);
  buttons(now);
  if(HEARTBEAT_MS&&(uint32_t)(now-heartbeat)>=HEARTBEAT_MS){heartbeat=now;printf("HEARTBEAT %s\r\n",transmit("HB,W806\r\n")?"SENT":"FAILED");}
  if((uint32_t)(now-report)>=10000){report=now;printf("RX_ALIVE uptime_s=%lu queued=%u\r\n",(unsigned long)(now/1000),(head+sizeof(ring)-tail)%sizeof(ring));}
 }
}
void assert_failed(uint8_t *file,uint32_t line){printf("ASSERT line=%lu\r\n",(unsigned long)line);for(;;);}
