#include "wm_include.h"
#include "wm_params.h"
#include "wm_watchdog.h"
#include "lwip/sockets.h"
#include "remote_config.h"
#include "remote_board.h"
#include "wifi_provision.h"
#include "provision_form.h"
#include "wifi_profiles.h"
#include "wm_internal_flash.h"
#include "wm_flash_map.h"
#include <stdlib.h>
static OS_STK stack[2048];static tls_os_task_t task;
static volatile int station_up,ap_active;
static wifi_record profiles;
static int active_slot=-1;
static const wifi_profile defaults[]=REMOTE_WIFI_DEFAULTS;
static char page_multi[6000];

int wifi_provision_active(void){return ap_active;}
void wifi_provision_station_status(int connected){station_up=connected;}
static int storage_safe(void){return TLS_FLASH_PARAM_DEFAULT>=USER_ADDR_START+0x2000 && TLS_FLASH_END_ADDR>=USER_ADDR_START+0x1fff;}
static int save_profiles(const wifi_record *input){
 wifi_record next=*input,verify;int target=active_slot==0?1:0;
 if(!storage_safe())return 0;
 next.magic=0x57494635u;next.version=1;next.generation=profiles.generation+1;
 next.crc=wifi_crc(&next,sizeof(next)-sizeof(next.crc));
 if(!wifi_record_valid(&next)||tls_fls_write(USER_ADDR_START+target*0x1000,(u8*)&next,sizeof(next))!=TLS_FLS_STATUS_OK || tls_fls_read(USER_ADDR_START+target*0x1000,(u8*)&verify,sizeof(verify))!=TLS_FLS_STATUS_OK ||memcmp(&next,&verify,sizeof(next)))return 0;
 profiles=next;active_slot=target;return 1;
}
static void load_wifi(void){
 wifi_record records[2];int good[2]={0,0},i;struct tls_param_ssid s;struct tls_param_key k;
 memset(&profiles,0,sizeof(profiles));
 if(storage_safe())for(i=0;i<2;i++)good[i]=tls_fls_read(USER_ADDR_START+i*0x1000,(u8*)&records[i],sizeof(records[i]))==TLS_FLS_STATUS_OK&&wifi_record_valid(&records[i]);
 if(good[0]||good[1]){active_slot=good[1]&&(!good[0]||wifi_newer(records[1].generation,records[0].generation))?1:0;profiles=records[active_slot];}
 else{
  memset(&s,0,sizeof(s));memset(&k,0,sizeof(k));tls_param_get(TLS_PARAM_ID_SSID,&s,TRUE);tls_param_get(TLS_PARAM_ID_KEY,&k,TRUE);
  if(k.res==0xa5&&s.ssid_len<=32&&k.key_length<=64){if(s.ssid_len){profiles.count=1;memcpy(profiles.items[0].ssid,s.ssid,s.ssid_len);memcpy(profiles.items[0].password,k.psk,k.key_length);}}
  else{profiles.count=REMOTE_WIFI_DEFAULT_COUNT;memcpy(profiles.items,defaults,sizeof(defaults));}
 }
 printf("WIFI_PROFILES_LOADED count=%lu storage=%s\n",(unsigned long)profiles.count,active_slot>=0?"flash":"legacy/default");
}
static int enter_ap(void){
 struct tls_softap_info_t ap;struct tls_ip_info_t ip;u8 mac[6],mode=TLS_PARAM_IEEE80211_SOFTAP;
 memset(&ap,0,sizeof(ap));memset(&ip,0,sizeof(ip));
 ap_active=1;station_up=0;tls_wifi_disconnect();
 tls_param_set(TLS_PARAM_ID_WPROTOCOL,&mode,FALSE);tls_wifi_set_oneshot_flag(0);
 tls_get_mac_addr(mac);snprintf((char*)ap.ssid,sizeof(ap.ssid),"W801-Setup-%02X%02X%02X",mac[3],mac[4],mac[5]);
 ap.encrypt=IEEE80211_ENCRYT_CCMP_WPA2;ap.channel=6;ap.keyinfo.format=1;ap.keyinfo.index=1;
 ap.keyinfo.key_len=strlen(REMOTE_AP_PASSWORD);memcpy(ap.keyinfo.key,REMOTE_AP_PASSWORD,ap.keyinfo.key_len);
 ip.ip_addr[0]=192;ip.ip_addr[1]=168;ip.ip_addr[2]=4;ip.ip_addr[3]=1;
 ip.netmask[0]=ip.netmask[1]=ip.netmask[2]=255;strcpy((char*)ip.dnsname,"w801.local");
 if(tls_wifi_softap_create(&ap,&ip)!=WM_SUCCESS){ap_active=0;printf("PROVISION_AP_FAILED\n");return 0;}
 printf("PROVISION_AP_READY ssid=%s url=http://192.168.4.1:8080\n",ap.ssid);return 1;
}
static void escaped(const char *src,char *dst,unsigned size){
 unsigned used=0;while(*src){const char *entity=0;char c=*src++;
  if(c=='&')entity="&amp;";else if(c=='<')entity="&lt;";else if(c=='>')entity="&gt;";else if(c=='\"')entity="&quot;";else if(c=='\'')entity="&#39;";
  if(entity){unsigned n=strlen(entity);if(used+n>=size)break;memcpy(dst+used,entity,n);used+=n;}else{if(used+1>=size)break;dst[used++]=c;}
 }dst[used]=0;
}
static const char *make_page(void){
 int i,used;char name[200];
 used=snprintf(page_multi,sizeof(page_multi),"<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Wi-Fi 配网</title><body><h2>最多保存5组2.4GHz Wi-Fi</h2><p>按顺序连接。密码不回显，同名网络留空保留原密码。名称留空删除该组。</p><form method='post' action='/save'>");
 for(i=0;i<5;i++){
  escaped(i<(int)profiles.count?profiles.items[i].ssid:"",name,sizeof(name));
  used+=snprintf(page_multi+used,sizeof(page_multi)-used,"<fieldset><legend>网络%d</legend><p>名称 <input name='ssid%d' maxlength='32' value='%s'></p><p>密码 <input type='password' name='password%d' maxlength='64' autocomplete='new-password'></p><label><input type='checkbox' name='open%d' value='1'>开放网络（清除密码）</label></fieldset>",i+1,i,name,i,i);
 }
 snprintf(page_multi+used,sizeof(page_multi)-used,"<p>清空全部名称并保存即可清空列表。</p><button>保存并连接</button></form></body></html>");return page_multi;
}
static int respond(int fd,const char *status,const char *body){
 char header[256];int n,offset=0,length;
 snprintf(header,sizeof(header),"HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",status,(unsigned)strlen(body));
 length=strlen(header);while(offset<length){n=send(fd,header+offset,length-offset,0);if(n<=0)return 0;offset+=n;}
 offset=0;length=strlen(body);while(offset<length){n=send(fd,body+offset,length-offset,0);if(n<=0)return 0;offset+=n;}return 1;
}
static void serve(int fd){
 char buffer[3800],name[33],pass[65],key[24],open[4],*body,*length_header;int n,total=0,wanted=0,i,j;
 wifi_record next;memset(&next,0,sizeof(next));
 struct timeval timeout={2,0};
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 for(;;){n=recv(fd,buffer+total,sizeof(buffer)-1-total,0);if(n<=0){closesocket(fd);return;}total+=n;buffer[total]=0;
  body=strstr(buffer,"\r\n\r\n");if(body){body+=4;
   if(!strncmp(buffer,"GET / ",7)){respond(fd,"200 OK",make_page());closesocket(fd);return;}
   if(strncmp(buffer,"POST /save ",11)){respond(fd,"404 Not Found","Not found");closesocket(fd);return;}
   length_header=strstr(buffer,"\r\nContent-Length: ");if(!length_header)length_header=strstr(buffer,"\r\ncontent-length: ");
   if(!length_header){respond(fd,"411 Length Required","Missing length");closesocket(fd);return;}
   wanted=atoi(length_header+18);
   if(wanted<=0||wanted>3000){respond(fd,"400 Bad Request","Invalid length");closesocket(fd);return;}
   if(total-(body-buffer)>=wanted)break;
  }
  if(total>=sizeof(buffer)-1){respond(fd,"413 Payload Too Large","Too large");closesocket(fd);return;}
 }
 body[wanted]=0;
 for(i=0;i<5;i++){
  snprintf(key,sizeof(key),"ssid%d",i);if(!form_value(body,key,name,sizeof(name)))goto invalid;
  snprintf(key,sizeof(key),"password%d",i);if(!form_value(body,key,pass,sizeof(pass)))goto invalid;
  if(!name[0])continue;
  snprintf(key,sizeof(key),"open%d",i);
  if(form_value(body,key,open,sizeof(open))&&!strcmp(open,"1"))pass[0]=0;
  else if(!pass[0])for(j=0;j<(int)profiles.count;j++)if(!strcmp(name,profiles.items[j].ssid)){strcpy(pass,profiles.items[j].password);break;}
  if(pass[0]&&strlen(pass)<8)goto invalid;
  for(j=0;j<(int)next.count;j++)if(!strcmp(name,next.items[j].ssid))goto invalid;
  strcpy(next.items[next.count].ssid,name);strcpy(next.items[next.count].password,pass);next.count++;
 }
 if(!save_profiles(&next)){respond(fd,"500 Internal Server Error","保存失败，原配置保留，请重试。");closesocket(fd);return;}
 respond(fd,"200 OK","已保存，即将重启。所有网络连接失败后会再次开启热点。");
 closesocket(fd);tls_os_time_delay(HZ);tls_sys_reset();return;
 invalid:respond(fd,"400 Bad Request","名称最多32字节且不能重复；密码为空或8至64字节，请返回修改。");closesocket(fd);
}

static void provision_entry(void *arg){
 int listener=-1,attempting=0,attempt=0,current=0,start_index=0,was_up=0;unsigned last_try=0,held=0;int pressed=0,latch=0;
 u8 mode=TLS_PARAM_IEEE80211_INFRA;
 tls_gpio_cfg(REMOTE_PROVISION_PIN,WM_GPIO_DIR_INPUT,WM_GPIO_ATTR_PULLHIGH);
 tls_param_set(TLS_PARAM_ID_WPROTOCOL,&mode,FALSE);
 for(;;){
  unsigned now=tls_os_get_time();int low=!tls_gpio_read(REMOTE_PROVISION_PIN);
  if(low){if(!pressed){pressed=1;held=now;}if(!latch&&(unsigned)(now-held)>=5*HZ){latch=1;if(listener>=0){closesocket(listener);listener=-1;}enter_ap();}}
  else{pressed=latch=0;}
  if(!ap_active){
   if(station_up){was_up=1;attempting=0;attempt=0;start_index=current;}
   else{
    if(was_up){was_up=0;attempt=0;attempting=0;start_index=current;printf("WIFI_FAILOVER_START\n");}
    if(attempting&&(unsigned)(now-last_try)>=15*HZ){tls_wifi_disconnect();attempting=0;attempt++;}
    if(!attempting){
     if(!profiles.count||attempt>=(int)profiles.count){enter_ap();attempt=0;}
     else{
      current=wifi_next(profiles.count,start_index,attempt);last_try=now;attempting=1;
      printf("WIFI_PROFILE_TRY slot=%d total=%lu\n",current+1,(unsigned long)profiles.count);
      tls_wifi_connect((u8*)profiles.items[current].ssid,strlen(profiles.items[current].ssid),(u8*)profiles.items[current].password,strlen(profiles.items[current].password));
     }
    }
   }
  }else{
   if(listener<0){struct sockaddr_in addr;int reuse=1;
    listener=socket(AF_INET,SOCK_STREAM,0);memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(8080);addr.sin_addr.s_addr=htonl(0xc0a80401);
    if(listener>=0)setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    if(listener<0||bind(listener,(struct sockaddr*)&addr,sizeof(addr))<0||listen(listener,2)<0){if(listener>=0)closesocket(listener);listener=-1;printf("PROVISION_WEB_RETRY\n");tls_os_time_delay(HZ);continue;}
   }
   {fd_set reads;struct timeval wait={0,100000};FD_ZERO(&reads);FD_SET(listener,&reads);
    if(select(listener+1,&reads,0,0,&wait)>0){int client=accept(listener,0,0);if(client>=0)serve(client);}
   }
  }
  tls_os_time_delay(HZ/10?HZ/10:1);
 }
}
void wifi_provision_start(void){
 load_wifi();
 if(tls_os_task_create(&task,"wifi_setup",provision_entry,0,(u8*)stack,sizeof(stack),35,0)!=WM_SUCCESS)printf("PROVISION_TASK_FAILED\n");
}
