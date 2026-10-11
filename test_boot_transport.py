"""Run actual sender startup against sampled GPIO values on a host compiler."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).parent
source=(root/'w801-sdk/wm_sdk_w80x_20211115/app/remote_sender.c').read_text()
startup=source[source.index('void remote_sender_start(void)'):]
stub=r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned char u8;
typedef int tls_os_sem_t;
#define TLS_OS_SUCCESS 0
#define HZ 100
#define REMOTE_TRANSPORT_SELECT_PIN 11
#define WM_GPIO_DIR_INPUT 0
#define WM_GPIO_ATTR_PULLHIGH 1
static int serial_sender,authenticated,net_task,key_task,ack_task;
static int net_stack[4],key_stack[4],ack_stack[4];
static int values[5],sample,created,configured;
static tls_os_sem_t *network_wake;
static void remote_power_start(void){}
static int tls_os_sem_create(tls_os_sem_t **s,unsigned count){static int sem;*s=&sem;return 0;}
static void network_entry(void *p){}
static void keys_entry(void *p){}
static void ack_entry(void *p){}
static void tls_get_mac_addr(u8 *mac){for(int i=0;i<6;i++)mac[i]=(u8)(0x10+i);}
static void tls_gpio_cfg(int pin,int dir,int attr){assert(pin==11&&dir==0&&attr==1);configured++;}
static int tls_gpio_read(int pin){assert(pin==11&&sample<5);return values[sample++];}
static void tls_os_time_delay(int ticks){assert(ticks>=1);}
static int tls_os_task_create(int *task,const char *name,void (*fn)(void*),void *arg,u8 *stack,unsigned size,int priority,int flags){created++;return 0;}
'''
main=r'''
int main(void){
 for(int pattern=0;pattern<32;pattern++){
  sample=created=configured=0;
  for(int i=0;i<5;i++)values[i]=(pattern>>i)&1;
  remote_sender_start();
  assert(serial_sender==(pattern==0));
  assert(sample==5&&created==3&&configured==1&&authenticated==1);
 }
 puts("Boot wiring tests passed: all 32 samples; only stable GND selects DTU, default/mixed selects WiFi");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.c').write_text(stub+startup+main)
    subprocess.run(['C:/MinGW/bin/gcc.exe','-std=c99',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
    r=subprocess.run([str(p/'test.exe')],check=True,capture_output=True,text=True)
    print(r.stdout.splitlines()[-1])
