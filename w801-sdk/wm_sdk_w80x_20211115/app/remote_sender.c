#include "wm_include.h"
#include "wm_regs.h"
#include <string.h>
#include <stdlib.h>
#include "random.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "wm_http_client.h"
#include "wm_netif.h"
#include "cJSON.h"
#include "remote_config.h"
#include "remote_session.h"
#include "remote_board.h"
#include "wifi_provision.h"

#define BASE "http://iot.rivcloud.com:7000/prod-api"
#define IMEI DEPLOY_IMEI
static OS_STK net_stack[4096], key_stack[512],ack_stack[4096];
static tls_os_task_t net_task, key_task,ack_task;
static volatile int ip_ready, authenticated, device_ready, busy;
static char target_key[96]=DEPLOY_PRODUCT_KEY,target_imei[24]=DEPLOY_IMEI;
static int target_id=DEPLOY_DEVICE_ID,target_product=DEPLOY_PRODUCT_ID;
static char commands[32];
static unsigned command_time[32];
static volatile unsigned queue_head,queue_tail;
static char ack_reply[18000];
static char token[2048]=DEPLOY_TOKEN, reply[18000];
static unsigned auth_generation;
static unsigned sequence;
static unsigned session_id;
static char pending_id[10][64],pending_action[10];
static unsigned pending_since[10];
static volatile unsigned connection_epoch;
static const enum tls_io_name keys[]=REMOTE_BUTTON_PINS;
static void expire_auth(unsigned generation) {
    u32 lock=tls_os_set_critical();
    if(generation==auth_generation){authenticated=0;device_ready=0;queue_tail=queue_head;memset(pending_action,0,sizeof(pending_action));}
    tls_os_release_critical(lock);
}

static void status_event(u8 status) {
    if(status==NETIF_IP_NET_UP&&!wifi_provision_active()) {ip_ready=1;wifi_provision_station_status(1); printf("WIFI_IP_READY\n");}
    if(status==NETIF_WIFI_DISCONNECTED) {
        u32 lock=tls_os_set_critical();
        ip_ready=0;connection_epoch++;queue_tail=queue_head;
        wifi_provision_station_status(0);
        memset(pending_action,0,sizeof(pending_action));
        tls_os_release_critical(lock);
        printf("WIFI_DISCONNECTED (token retained; old commands cleared)\n");
    }
}
static int connect_bounded(int fd,struct sockaddr_in *addr) {
    unsigned long nonblocking=1;fd_set writes;struct timeval wait={5,0};
    int error=0; socklen_t size=sizeof(error);
    if(ioctlsocket(fd,FIONBIO,&nonblocking)<0)return 0;
    if(connect(fd,(struct sockaddr*)addr,sizeof(*addr))<0) {
        FD_ZERO(&writes);FD_SET(fd,&writes);
        if(select(fd+1,0,&writes,0,&wait)<=0)return 0;
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0||error)return 0;
    }
    nonblocking=0;return ioctlsocket(fd,FIONBIO,&nonblocking)==0;
}
static cJSON *request_buffer(char *url,int post,int auth,char *reply,unsigned capacity) {
    char header[3000],auth_token[2048],*body,*cursor,*end,*out;
    struct hostent *host;struct sockaddr_in addr;struct timeval timeout={10,0};
    int fd,n,sent=0,length,total=0,status=0,chunked=0;unsigned chunk,started=tls_os_get_time(),epoch=connection_epoch;
    cJSON *root=0;
    unsigned generation;u32 lock=tls_os_set_critical();generation=auth_generation;
    if(auth)strcpy(auth_token,token);else auth_token[0]=0;
    tls_os_release_critical(lock);
    host=gethostbyname("iot.rivcloud.com");if(!host)return 0;
    fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 0;
    memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(7000);
    memcpy(&addr.sin_addr,host->h_addr_list[0],4);
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))<0||setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))<0||!connect_bounded(fd,&addr)){closesocket(fd);return 0;}
    length=snprintf(header,sizeof(header),"%s %s HTTP/1.1\r\nHost: iot.rivcloud.com:7000\r\nConnection: close\r\nAccept-Encoding: identity\r\n%s%s%sContent-Length: 0\r\n\r\n",post?"POST":"GET",url+strlen("http://iot.rivcloud.com:7000"),auth?"Authorization: ":"",auth?auth_token:"",auth?"\r\n":"");
    if(length<0||length>=sizeof(header)){closesocket(fd);return 0;}
    length=strlen(header);
    while(sent<length){if(!ip_ready||epoch!=connection_epoch||(unsigned)(tls_os_get_time()-started)>20*HZ){closesocket(fd);return 0;}n=send(fd,header+sent,length-sent,0);if(n<=0){closesocket(fd);return 0;}sent+=n;}
    while(total<capacity-1){if(!ip_ready||epoch!=connection_epoch||(unsigned)(tls_os_get_time()-started)>20*HZ){closesocket(fd);return 0;}n=recv(fd,reply+total,capacity-1-total,0);if(n<=0)break;total+=n;}
    closesocket(fd);reply[total]=0;
    if(n<0||total==capacity-1)return 0;
    cursor=strchr(reply,' ');if(cursor)status=atoi(cursor+1);
    body=strstr(reply,"\r\n\r\n");if(!body)return 0;
    *body=0;chunked=strstr(reply,"Transfer-Encoding: chunked")!=0||strstr(reply,"transfer-encoding: chunked")!=0;
    body+=4;
    if(auth&&status==401){expire_auth(generation);printf("TOKEN_EXPIRED\n");}
    if(status!=200){printf("HTTP_STATUS %d\n",status);return 0;}
    if(chunked){
        cursor=body;out=body;end=reply+total;
        for(;;){
            char *line=strstr(cursor,"\r\n"),*hex_end;if(!line)return 0;
            if(!((*cursor>='0'&&*cursor<='9')||(*cursor>='a'&&*cursor<='f')||(*cursor>='A'&&*cursor<='F')))return 0;
            chunk=strtoul(cursor,&hex_end,16);
            if(hex_end!=line&&*hex_end!=';')return 0;
            cursor=line+2;if(chunk==0){if(end-cursor<2)return 0;*out=0;break;}
            if(chunk>(unsigned)(end-cursor)||end-cursor-(int)chunk<2)return 0;
            memmove(out,cursor,chunk);out+=chunk;cursor+=chunk;
            if(cursor[0]!='\r'||cursor[1]!='\n')return 0;
            cursor+=2;
        }
    }
    root=cJSON_Parse(body);
    if(!root)printf("HTTP_JSON_PARSE_FAILED\n");
    if(auth&&root){
        cJSON *code=cJSON_GetObjectItem(root,"code");
        int value=code?(code->valuestring?atoi(code->valuestring):code->valueint):0;
        if(value==401){expire_auth(generation);printf("TOKEN_EXPIRED\n");}
    }
    return root;
}
static cJSON *request(char *url,int post,int auth) {
    return request_buffer(url,post,auth,reply,sizeof(reply));
}
static int renew_auth(void) {
    char url[512],user[192];const unsigned char *p=(const unsigned char*)REMOTE_USER;
    unsigned j=0,epoch=connection_epoch;int length,ok=0;cJSON *root,*access;
    for(;*p;p++){
        if(j+3>=sizeof(user))return 0;
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-')user[j++]=*p;
        else {snprintf(user+j,4,"%%%02X",*p);j+=3;}
    }
    user[j]=0;
    length=snprintf(url,sizeof(url),BASE "/reach/dmp/user/login?username=%s&password=%s",user,REMOTE_PASSWORD_MD5);
    if(length<0||length>=sizeof(url))return 0;
    printf("AUTH_RENEW_START\n");root=request(url,1,0);
    access=root?cJSON_GetObjectItem(root,"access_token"):0;
    if(access&&access->valuestring&&access->valuestring[0]&&strlen(access->valuestring)+8<=sizeof(token)&&!strchr(access->valuestring,'\r')&&!strchr(access->valuestring,'\n')){
        u32 lock=tls_os_set_critical();
        if(ip_ready&&epoch==connection_epoch){
            snprintf(token,sizeof(token),"Bearer %s",access->valuestring);auth_generation++;
            queue_tail=queue_head;memset(pending_action,0,sizeof(pending_action));device_ready=0;authenticated=1;ok=1;
        }
        tls_os_release_critical(lock);
    }
    cJSON_Delete(root);printf(ok?"AUTH_RENEW_OK\n":"AUTH_RENEW_FAILED\n");return ok;
}
static int success(cJSON *root) {
    cJSON *code=root?cJSON_GetObjectItem(root,"code"):0;
    return code&&code->valueint==200;
}
static int load_device(void) {
    char url[128];cJSON *root;
    snprintf(url,sizeof(url),BASE "/reach/dmp/device/%d",DEPLOY_DEVICE_ID);
    root=request(url,0,1);
    cJSON *data=root?cJSON_GetObjectItem(root,"data"):0;
    cJSON *imei=data?cJSON_GetObjectItem(data,"imei"):0;
    cJSON *key=data?cJSON_GetObjectItem(data,"productKey"):0;
    cJSON *id=data?cJSON_GetObjectItem(data,"id"):0;
    cJSON *product=data?cJSON_GetObjectItem(data,"productId"):0;
    cJSON *protocol=data?cJSON_GetObjectItem(data,"commProtocol"):0;
    device_ready=0;
    if(success(root)&&imei&&imei->valuestring&&!strcmp(imei->valuestring,IMEI)&&key&&key->valuestring&&strlen(key->valuestring)<sizeof(target_key)&&id&&id->valueint==DEPLOY_DEVICE_ID&&product&&product->valueint>0&&protocol&&((protocol->valuestring&&!strcmp(protocol->valuestring,"3"))||(!protocol->valuestring&&protocol->valueint==3))) {
        /* Only URL-safe platform identifiers are accepted. */
        const char *p=key->valuestring;
        for(;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))break;
        if(!*p&&key->valuestring[0]) {
            strcpy(target_imei,imei->valuestring);strcpy(target_key,key->valuestring);
            target_id=id->valueint;target_product=product->valueint;device_ready=1;
        }
    }
    cJSON_Delete(root);
    if(device_ready)printf("DEVICE_READY id=%d imei=%s productId=%d key=%s TCP\n",target_id,target_imei,target_product,target_key);
    else printf("DEVICE_QUERY_FAILED (sending disabled)\n");
    return device_ready;
}
static int ack_matches(cJSON *root,char *id,char action) {
    cJSON *rows,*row,*data,*type,*device;char prefix[100],expected[140];int i,index=action-'a';
    if(!success(root))return 0;
    rows=cJSON_GetObjectItem(root,"rows");
    snprintf(prefix,sizeof(prefix),"ACK,%s,%c,",id,action);
    for(i=0;rows&&i<cJSON_GetArraySize(rows);i++) {
        row=cJSON_GetArrayItem(rows,i);data=cJSON_GetObjectItem(row,"dataText");
        type=cJSON_GetObjectItem(row,"recType");device=cJSON_GetObjectItem(row,"imei");
        if(!type||!type->valuestring||strcmp(type->valuestring,"uplink"))continue;
        if(device&&device->valuestring&&strcmp(device->valuestring,IMEI))continue;
        if(!data||!data->valuestring||strncmp(data->valuestring,prefix,strlen(prefix)))continue;
        snprintf(expected,sizeof(expected),"%sTIMER,%d,DONE",prefix,index+1);
        if(!strcmp(data->valuestring,expected)||(!strncmp(data->valuestring,expected,strlen(expected))&&!strcmp(data->valuestring+strlen(expected),"\n")))return 1;
        snprintf(expected,sizeof(expected),"%sLATCH,%d,%s",prefix,index/2+1,index%2?"OFF":"ON");
        if(!strcmp(data->valuestring,expected)||(!strncmp(data->valuestring,expected,strlen(expected))&&!strcmp(data->valuestring+strlen(expected),"\n")))return 1;
    }
    return 0;
}
static void send_action(char action) {
    char url[512],id[64];cJSON *root;int i,accepted;
    /* MAC + tick + sequence distinguish concurrent button transactions. */
    u8 mac[6]; tls_get_mac_addr(mac);
    snprintf(id,sizeof(id),"%02x%02x%02x%02x%02x%02x-%08lx-%08lx-%u",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],(unsigned long)session_id,(unsigned long)tls_os_get_time(),++sequence);
    snprintf(url,sizeof(url),BASE "/reach/tcp/devices/downlink?imei=%s&productKey=%s&sendData=CMD%%2C%s%%2C%c%%0A&sendDataType=text",target_imei,target_key,id,action);
    printf("TX_ACTION gpio=PA%d ascii=%d action=%c id=%s\n",action-'a',(int)action,action,id);
    root=request(url,1,1);accepted=success(root);cJSON_Delete(root);
    if(!accepted){printf("SEND_NOT_CONFIRMED %c (no automatic retry; execution may be unknown)\n",action);return;}
    printf("PLATFORM_ACCEPTED %c id=%s\n",action,id);
    {
        u32 lock=tls_os_set_critical();
        for(i=0;i<10&&pending_action[i];i++);
        if(i<10){strcpy(pending_id[i],id);pending_since[i]=tls_os_get_time();pending_action[i]=action;}
        tls_os_release_critical(lock);
        if(i==10)printf("ACK_TRACKING_FULL (command was sent)\n");
    }
}
static void poll_pending(void) {
    char url[256];cJSON *root;int i,any=0;
    for(i=0;i<10;i++)if(pending_action[i])any=1;
    if(!any)return;
    snprintf(url,sizeof(url),BASE "/reach/dmp/deviceData/list/?pageNum=1&pageSize=20&imei=%s&productId=%d&recType=uplink",target_imei,target_product);
    root=request_buffer(url,0,1,ack_reply,sizeof(ack_reply));
    for(i=0;i<10;i++)if(pending_action[i]) {
        char id[64],action;unsigned since;int matched,expired,cleared=0;
        u32 lock=tls_os_set_critical();
        strcpy(id,pending_id[i]);action=pending_action[i];since=pending_since[i];
        tls_os_release_critical(lock);
        if(!action)continue;
        matched=ack_matches(root,id,action);expired=(unsigned)(tls_os_get_time()-since)>20*HZ;
        if(matched||expired) {
            lock=tls_os_set_critical();
            if(pending_action[i]==action&&!strcmp(id,pending_id[i])){pending_action[i]=0;cleared=1;}
            tls_os_release_critical(lock);
            if(matched&&cleared)printf("EXECUTION_CONFIRMED %c id=%s\n",action,id);
        }
    }
    cJSON_Delete(root);
}
static void ack_entry(void *arg) {
    for(;;){
        if(ip_ready&&authenticated&&device_ready)poll_pending();
        tls_os_time_delay(2*HZ);
    }
}
static void network_entry(void *arg) {
    unsigned stamp,last_probe=0,retry_seconds=5;char action;
    tls_netif_add_status_event(status_event);
    for(;;) {
        if(!ip_ready) {
            u32 lock=tls_os_set_critical();queue_tail=queue_head;tls_os_release_critical(lock);
            tls_os_time_delay(HZ);continue;
        }
        if(!authenticated) {
            u32 lock=tls_os_set_critical();queue_tail=queue_head;tls_os_release_critical(lock);
            if(!renew_auth()){printf("AUTH_RETRY_IN %u seconds\n",retry_seconds);tls_os_time_delay(retry_seconds*HZ);retry_seconds=retry_seconds<30?retry_seconds*2:60;continue;}
            retry_seconds=5;
        }
        if(!device_ready){if(!load_device()){tls_os_time_delay(10*HZ);continue;}}
        if(queue_tail==queue_head&&(unsigned)(tls_os_get_time()-last_probe)>=60*HZ){
            last_probe=tls_os_get_time();if(!load_device())continue;
        }
        if(!session_id){if(random_get_bytes(&session_id,sizeof(session_id))!=0||!session_id){printf("SESSION_RANDOM_RETRY\n");tls_os_time_delay(HZ);continue;}}
        action=0;
        {u32 lock=tls_os_set_critical();if(queue_tail!=queue_head){action=commands[queue_tail];stamp=command_time[queue_tail];queue_tail=(queue_tail+1)%32;}tls_os_release_critical(lock);}
        if(action){
            if((unsigned)(tls_os_get_time()-stamp)>5*HZ)printf("KEY %c STALE_NOT_SENT\n",action);
            else {busy=1;send_action(action);busy=0;}
        }
        tls_os_time_delay(1);
    }
}
static void keys_entry(void *arg) {
    int count[10]={0},release[10]={0},armed[10]={0},i,low;
    unsigned last_report=0;int mask;
    for(i=0;i<10;i++){
        tls_gpio_cfg(keys[i],WM_GPIO_DIR_INPUT,WM_GPIO_ATTR_PULLHIGH);
        printf("KEY_MAP index=%d enum=%d action=%c\n",i,(int)keys[i],'a'+i);
    }
    for(;;) {
        for(i=0;i<10;i++) {
            low=!tls_gpio_read(keys[i]);
            if(!low){count[i]=0;if(release[i]<3)release[i]++;if(release[i]>=3)armed[i]=1;continue;}
            release[i]=0;
            if(!armed[i])continue;
            if(++count[i]<3)continue;
            armed[i]=0;
            if(!ip_ready||!authenticated||!device_ready)printf("KEY %c REJECTED_OFFLINE\n",'a'+i);
            else {
                int added=0;u32 lock=tls_os_set_critical();
                if(ip_ready&&authenticated&&device_ready&&(queue_head+1)%32!=queue_tail){commands[queue_head]='a'+i;command_time[queue_head]=tls_os_get_time();queue_head=(queue_head+1)%32;added=1;}
                tls_os_release_critical(lock);
                printf(added?"KEY %c QUEUED\n":"KEY %c NOT_QUEUED (offline or full)\n",'a'+i);
            }
        }
        if((unsigned)(tls_os_get_time()-last_report)>30*HZ) {
            mask=0;for(i=0;i<10;i++)if(!tls_gpio_read(keys[i]))mask|=1<<i;
            printf("GPIO_LOW_MASK=%03x busy=%d (bit0=PA0 bit9=PA9)\n",mask,busy);
            printf("GPIOA_CONFIG afsel=%08lx dir=%08lx pull=%08lx\n",(unsigned long)tls_reg_read32(HR_GPIO_AF_SEL),(unsigned long)tls_reg_read32(HR_GPIO_DIR),(unsigned long)tls_reg_read32(HR_GPIO_PULLUP_EN));
            last_report=tls_os_get_time();
        }
        tls_os_time_delay(HZ/100?HZ/100:1);
    }
}
void remote_sender_start(void) {
    authenticated=1;
    printf("\nREMOTE_W801_SENDER_V0.8: PA0..PA9 => a..j; optional ACK\n");
    tls_os_task_create(&net_task,"remote_net",network_entry,0,(u8*)net_stack,sizeof(net_stack),33,0);
    tls_os_task_create(&key_task,"remote_keys",keys_entry,0,(u8*)key_stack,sizeof(key_stack),32,0);
    tls_os_task_create(&ack_task,"remote_ack",ack_entry,0,(u8*)ack_stack,sizeof(ack_stack),34,0);
}
