#include "wm_include.h"
#include "wm_params.h"
#include "wm_watchdog.h"
#include "lwip/sockets.h"
#include "remote_config.h"
#include "remote_board.h"
#include "wifi_provision.h"
#include "provision_form.h"
#include <stdlib.h>
static OS_STK stack[2048];static tls_os_task_t task;
static volatile int station_up,ap_active;
static char ssid[33],password[65];
static const char page[]= "<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>W801 Wi-Fi 配网</title><body style='font-family:sans-serif;max-width:420px;margin:40px auto;padding:20px'><h2>Wi-Fi 配网</h2><form method='post' action='/save'><p>Wi-Fi 名称</p><input name='ssid' maxlength='32' required style='width:95%;padding:12px'><p>Wi-Fi 密码</p><input type='password' name='password' maxlength='64' style='width:95%;padding:12px' autocomplete='new-password'><p>仅支持2.4GHz Wi-Fi。开放网络可留空密码。</p><button style='padding:12px'>保存并连接</button></form></body></html>";
int wifi_provision_active(void){return ap_active;}
void wifi_provision_station_status(int connected){station_up=connected;}
static int save_wifi(const char *name,const char *pass){
 struct tls_param_ssid s;struct tls_param_key k;
 memset(&s,0,sizeof(s));memset(&k,0,sizeof(k));
 s.ssid_len=strlen(name);memcpy(s.ssid,name,s.ssid_len);
 k.key_length=strlen(pass);memcpy(k.psk,pass,k.key_length);k.key_format=1;
 /* SDK-reserved byte records that provisioning owns this configuration,
    including an intentionally cleared SSID. No custom flash sectors used. */
 k.res=0xa5;
 if(tls_param_set(TLS_PARAM_ID_KEY,&k,TRUE)!=WM_SUCCESS)return 0;
 if(tls_param_set(TLS_PARAM_ID_SSID,&s,TRUE)!=WM_SUCCESS)return 0;
 return 1;
}
static void load_wifi(void){
 struct tls_param_ssid s;struct tls_param_key k;
 memset(&s,0,sizeof(s));memset(&k,0,sizeof(k));
 tls_param_get(TLS_PARAM_ID_SSID,&s,TRUE);tls_param_get(TLS_PARAM_ID_KEY,&k,TRUE);
 if(k.res==0xa5&&s.ssid_len<=32&&k.key_length<=64){memcpy(ssid,s.ssid,s.ssid_len);memcpy(password,k.psk,k.key_length);}
 else{strcpy(ssid,REMOTE_SSID);strcpy(password,REMOTE_WIFI_PASSWORD);}
}
static int enter_ap(void){
 struct tls_softap_info_t ap;struct tls_ip_info_t ip;u8 mac[6],mode=TLS_PARAM_IEEE80211_SOFTAP;
 memset(&ap,0,sizeof(ap));memset(&ip,0,sizeof(ip));
 if(!save_wifi("","")){printf("PROVISION_CLEAR_FAILED\n");return 0;}
 ssid[0]=password[0]=0;ap_active=1;station_up=0;tls_wifi_disconnect();
 tls_param_set(TLS_PARAM_ID_WPROTOCOL,&mode,FALSE);tls_wifi_set_oneshot_flag(0);
 tls_get_mac_addr(mac);snprintf((char*)ap.ssid,sizeof(ap.ssid),"W801-Setup-%02X%02X%02X",mac[3],mac[4],mac[5]);
 ap.encrypt=IEEE80211_ENCRYT_CCMP_WPA2;ap.channel=6;ap.keyinfo.format=1;ap.keyinfo.index=1;
 ap.keyinfo.key_len=strlen(REMOTE_AP_PASSWORD);memcpy(ap.keyinfo.key,REMOTE_AP_PASSWORD,ap.keyinfo.key_len);
 ip.ip_addr[0]=192;ip.ip_addr[1]=168;ip.ip_addr[2]=4;ip.ip_addr[3]=1;
 ip.netmask[0]=ip.netmask[1]=ip.netmask[2]=255;strcpy((char*)ip.dnsname,"w801.local");
 if(tls_wifi_softap_create(&ap,&ip)!=WM_SUCCESS){ap_active=0;printf("PROVISION_AP_FAILED\n");return 0;}
 printf("PROVISION_AP_READY ssid=%s url=http://192.168.4.1:8080\n",ap.ssid);return 1;
}
static int respond(int fd,const char *status,const char *body){
 char header[256];int n,offset=0,length;
 snprintf(header,sizeof(header),"HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",status,(unsigned)strlen(body));
 length=strlen(header);while(offset<length){n=send(fd,header+offset,length-offset,0);if(n<=0)return 0;offset+=n;}
 offset=0;length=strlen(body);while(offset<length){n=send(fd,body+offset,length-offset,0);if(n<=0)return 0;offset+=n;}return 1;
}
static void serve(int fd){
 char buffer[1400],name[33],pass[65],*body,*length_header;int n,total=0,wanted=0;
 struct timeval timeout={2,0};
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 for(;;){n=recv(fd,buffer+total,sizeof(buffer)-1-total,0);if(n<=0){closesocket(fd);return;}total+=n;buffer[total]=0;
  body=strstr(buffer,"\r\n\r\n");if(body){body+=4;
   if(!strncmp(buffer,"GET / ",7)){respond(fd,"200 OK",page);closesocket(fd);return;}
   if(strncmp(buffer,"POST /save ",11)){respond(fd,"404 Not Found","Not found");closesocket(fd);return;}
   length_header=strstr(buffer,"\r\nContent-Length: ");if(!length_header)length_header=strstr(buffer,"\r\ncontent-length: ");
   if(!length_header){respond(fd,"411 Length Required","Missing length");closesocket(fd);return;}
   wanted=atoi(length_header+18);
   if(wanted<=0||wanted>1024){respond(fd,"400 Bad Request","Invalid length");closesocket(fd);return;}
   if(total-(body-buffer)>=wanted)break;
  }
  if(total>=sizeof(buffer)-1){respond(fd,"413 Payload Too Large","Too large");closesocket(fd);return;}
 }
 body[wanted]=0;
 if(!form_value(body,"ssid",name,sizeof(name))||!form_value(body,"password",pass,sizeof(pass))||!name[0]||(strlen(pass)>0&&strlen(pass)<8)){
  respond(fd,"400 Bad Request","Wi-Fi名称需1至32字节；密码为空或8至64字节。请返回重新填写。");closesocket(fd);return;
 }
 if(!save_wifi(name,pass)){respond(fd,"500 Internal Server Error","保存失败，请重试。");closesocket(fd);return;}
 respond(fd,"200 OK","已保存，即将重启连接Wi-Fi。连接失败会再次开启热点。请关闭此页。");
 closesocket(fd);tls_os_time_delay(HZ);tls_sys_reset();
}
static void provision_entry(void *arg){
 int listener=-1;unsigned started=tls_os_get_time(),last_try=0,held=0;int pressed=0,latch=0;
 u8 mode=TLS_PARAM_IEEE80211_INFRA;
 tls_gpio_cfg(REMOTE_PROVISION_PIN,WM_GPIO_DIR_INPUT,WM_GPIO_ATTR_PULLHIGH);
 tls_param_set(TLS_PARAM_ID_WPROTOCOL,&mode,FALSE);
 for(;;){
  unsigned now=tls_os_get_time();int low=!tls_gpio_read(REMOTE_PROVISION_PIN);
  if(low){if(!pressed){pressed=1;held=now;}if(!latch&&(unsigned)(now-held)>=5*HZ){latch=1;if(listener>=0){closesocket(listener);listener=-1;}enter_ap();}}
  else{pressed=latch=0;}
  if(!ap_active){
   if(station_up){started=now;last_try=0;}
   else if(!ssid[0]||(unsigned)(now-started)>=45*HZ){enter_ap();started=now;}
   else if(!last_try||(unsigned)(now-last_try)>=15*HZ){tls_wifi_connect((u8*)ssid,strlen(ssid),(u8*)password,strlen(password));last_try=now?now:1;}
  }else{
   if(listener<0){struct sockaddr_in addr;int reuse=1;
    listener=socket(AF_INET,SOCK_STREAM,0);memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(8080);addr.sin_addr.s_addr=htonl(0xc0a80401);
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
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
