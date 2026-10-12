"""Exercise actual flash-save code and profile validation using simulated flash."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).parent
app=root/'w801-sdk/wm_sdk_w80x_20211115/app'
source=(app/'wifi_provision.c').read_text(encoding='utf-8')
save=source[source.index('static int storage_safe'):source.index('static void load_wifi')]
page=source[source.index('static void escaped'):source.index('static int respond')]
stub=r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned char u8;
#define USER_ADDR_START 0x81e0000u
#define TLS_FLS_STATUS_OK 0
static unsigned TLS_FLASH_PARAM_DEFAULT=0x81fb000,TLS_FLASH_END_ADDR=0x81fffff;
static wifi_record profiles;
static int active_slot=-1,fail_write;
static unsigned char flash[8192];
static char page_multi[6000];
static int tls_fls_read(unsigned addr,u8 *buf,unsigned length){memcpy(buf,flash+addr-USER_ADDR_START,length);return 0;}
static int tls_fls_write(unsigned addr,u8 *buf,unsigned length){memcpy(flash+addr-USER_ADDR_START,buf,fail_write?length/2:length);return fail_write?-1:0;}
'''
a=source.index('  if(!ap_active){')
b=source.index('  }else{',a)
step='static void connection_step(void){\n'+source[a:b]+'  }\n}\n'
connection_stub=r'''
#define HZ 100
static int station_up,ap_active,attempting,attempt,current,start_index,was_up,calls,order[20];
static unsigned now,last_try;
static void tls_wifi_disconnect(void){}
static int enter_ap(void){ap_active=1;return 1;}
static int tls_wifi_connect(u8 *ssid,unsigned len,u8 *pass,unsigned plen){order[calls++]=!strcmp((char*)ssid,"backup")?1:0;return 0;}
'''
test=r'''
int main(void){
 assert(provision_page_request("GET / HTTP/1.1\r\n"));
 assert(provision_page_request("GET /?setup=1 HTTP/1.1\r\n"));
 assert(provision_page_request("GET /generate_204 HTTP/1.1\r\n"));
 assert(provision_page_request("GET /hotspot-detect.html HTTP/1.1\r\n"));
 assert(!provision_page_request("POST /save HTTP/1.1\r\n"));
 wifi_record input={0},first,second;
 memset(flash,255,sizeof(flash));input.count=2;
 strcpy(input.items[0].ssid,"first<&'");strcpy(input.items[0].password,"secret123");
 strcpy(input.items[1].ssid,"backup");strcpy(input.items[1].password,"backup123");
 assert(save_profiles(&input));assert(active_slot==0&&profiles.count==2);
 first=profiles;assert(wifi_record_valid(&first));
 assert(strstr(make_page(),"first&lt;&amp;&#39;"));assert(!strstr(page_multi,"secret123"));
 assert(strstr(page_multi,"ssid4"));
 input.count=1;fail_write=1;assert(!save_profiles(&input));
 assert(!memcmp(&profiles,&first,sizeof(first)));assert(active_slot==0);
 memcpy(&second,flash+4096,sizeof(second));assert(!wifi_record_valid(&second));
 fail_write=0;assert(save_profiles(&input));assert(active_slot==1&&wifi_newer(profiles.generation,first.generation));
 second=profiles;second.items[0].password[0]^=1;assert(!wifi_record_valid(&second));
 memset(&input,0,sizeof(input));assert(save_profiles(&input));assert(wifi_record_valid(&profiles)&&profiles.count==0);
 assert(wifi_next(5,3,0)==3&&wifi_next(5,3,2)==0&&wifi_next(0,0,0)==-1);
 assert(wifi_newer(0,0xffffffffu));
 profiles=first;station_up=ap_active=attempting=attempt=current=start_index=was_up=calls=0;now=0;
 connection_step();assert(calls==1&&order[0]==0);
 now=14*HZ;connection_step();assert(calls==1);
 now=15*HZ;connection_step();assert(calls==2&&order[1]==1);
 station_up=1;connection_step();now=100*HZ;connection_step();assert(calls==2&&!ap_active);
 station_up=0;connection_step();assert(calls==3&&order[2]==1);
 now+=15*HZ;connection_step();assert(calls==4&&order[3]==0);
 now+=15*HZ;connection_step();assert(ap_active&&profiles.count==2);
 puts("Wi-Fi tests passed: five slots, escaping, no password echo, interrupted writes preserve old record, CRC, empty list, fallback order, wrap");return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text('#include "'+(app/'wifi_profiles.h').as_posix()+'"\n#include "'+(app/'provision_form.h').as_posix()+'"\n'+stub+save+page+connection_stub+step+test,encoding='utf-8')
 subprocess.run(['C:/MinGW/bin/gcc.exe','-std=c99',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
