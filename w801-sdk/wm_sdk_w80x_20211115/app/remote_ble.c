#include "wm_include.h"
#include "remote_board.h"
#include "remote_ble.h"
#include "ble_command.h"
#if REMOTE_BLE_ENABLED
#include "wm_bt_config.h"
#if !NIMBLE_FTR
#error This adapter requires the current NimBLE SDK configuration
#endif
/* The vendor NPL header omits these OSAL declarations. Match SDK definitions. */
extern u8 tls_os_task_schedule_state(void);
extern tls_os_task_t tls_os_task_id(void);
extern u8 tls_os_queue_is_empty(tls_os_queue_t *queue);
extern u8 tls_os_timer_active(tls_os_timer_t *timer);
extern u32 tls_os_timer_expirytime(tls_os_timer_t *timer);
extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "../src/app/bleapp/wm_ble_client_util.h"

extern int tls_bt_init(uint8_t uart_index);
typedef struct {
    ble_addr_t address;
    int state; /* 0 idle, 1 seen, 2 connecting, 3 discovery, 4 subscribe, 5 ready, 6 closing */
    uint16_t conn,command,status;
    unsigned since,last_command,cooldown;
    int rate_seen;
} peer_t;
static peer_t peers[REMOTE_BLE_MAX];
static const char *names[]=REMOTE_BLE_NAMES;
static OS_STK stack[1024];static tls_os_task_t task;
static unsigned scan_since;
static int scanning;
static struct ble_npl_callout poll_callout;
static int gap_event(struct ble_gap_event *event,void *arg);
static int find_conn(uint16_t conn){int i;for(i=0;i<REMOTE_BLE_MAX;i++)if(peers[i].state>=3&&peers[i].conn==conn)return i;return -1;}
static int pending(void){int i;for(i=0;i<REMOTE_BLE_MAX;i++)if(peers[i].state==2)return i;return -1;}
static void close_peer(int i){
    if(i<0)return;
    peers[i].state=6;peers[i].since=tls_os_get_time();
    ble_gap_terminate(peers[i].conn,BLE_ERR_REM_USER_CONN_TERM);
}
static int subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
    int i=find_conn(conn);if(i<0)return 0;
    if(error->status){printf("BLE_SUBSCRIBE_FAILED slot=%d status=%d\n",i+1,error->status);close_peer(i);return 0;}
    peers[i].state=5;printf("BLE_SUBSCRIBED slot=%d conn=%u\n",i+1,conn);return 0;
}
static void discovered(const struct peer *peer,int status,void *arg){
    const struct peer_chr *command,*feedback;const struct peer_dsc *cccd;
    uint8_t value[2]={0,0};int rc,i=find_conn(peer->conn_handle);
    if(i<0)return;
    if(status){close_peer(i);return;}
    command=peer_chr_find_uuid(peer,BLE_UUID16_DECLARE(REMOTE_BLE_SERVICE_UUID16),BLE_UUID16_DECLARE(REMOTE_BLE_COMMAND_UUID16));
    cccd=peer_dsc_find_uuid(peer,BLE_UUID16_DECLARE(REMOTE_BLE_SERVICE_UUID16),BLE_UUID16_DECLARE(REMOTE_BLE_COMMAND_UUID16),BLE_UUID16_DECLARE(0x2902));
    feedback=peer_chr_find_uuid(peer,BLE_UUID16_DECLARE(REMOTE_BLE_SERVICE_UUID16),BLE_UUID16_DECLARE(REMOTE_BLE_STATUS_UUID16));
    if(!command||!cccd||!(command->chr.properties&0x30)){printf("BLE_PROTOCOL_MISMATCH slot=%d\n",i+1);close_peer(i);return;}
    peers[i].command=command->chr.val_handle;
    peers[i].status=feedback&&(feedback->chr.properties&0x04)?feedback->chr.val_handle:0;
    value[0]=(command->chr.properties&0x10)?1:2;peers[i].state=4;
    rc=ble_gattc_write_flat(peer->conn_handle,cccd->dsc.handle,value,2,subscribed,0);
    if(rc){printf("BLE_CCCD_WRITE_FAILED slot=%d rc=%d\n",i+1,rc);close_peer(i);}
}
static int gap_event(struct ble_gap_event *event,void *arg){
    int i,j,rc,action,result;unsigned now=tls_os_get_time();
    switch(event->type){
    case BLE_GAP_EVENT_DISC:
        if(!scanning||pending()>=0)return 0;
        for(i=0;i<REMOTE_BLE_MAX;i++){
            if(peers[i].state||(peers[i].cooldown&&(unsigned)(now-peers[i].cooldown)<30*HZ))continue;
            if(!ble_name_match(event->disc.data,event->disc.length_data,names[i]))continue;
            for(j=0;j<REMOTE_BLE_MAX;j++)if(peers[j].state&& !memcmp(&peers[j].address,&event->disc.addr,sizeof(ble_addr_t)))break;
            if(j<REMOTE_BLE_MAX)return 0;
            peers[i].address=event->disc.addr;peers[i].conn=0xffff;peers[i].since=now;peers[i].state=1;
            printf("BLE_PEER_FOUND slot=%d rssi=%d\n",i+1,event->disc.rssi);break;
        }return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:scanning=0;scan_since=now;return 0;
    case BLE_GAP_EVENT_CONNECT:
        i=pending();if(i<0){if(!event->connect.status)ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);return 0;}
        if(event->connect.status){peers[i].state=0;peers[i].cooldown=now;printf("BLE_CONNECT_FAILED slot=%d status=%d\n",i+1,event->connect.status);return 0;}
        peers[i].conn=event->connect.conn_handle;peers[i].since=now;peers[i].state=3;
        printf("BLE_CONNECTED slot=%d conn=%u\n",i+1,peers[i].conn);
        rc=peer_add(peers[i].conn);
        if(!rc)rc=peer_disc_all(peers[i].conn,discovered,0);
        if(rc){printf("BLE_DISCOVERY_FAILED slot=%d rc=%d\n",i+1,rc);close_peer(i);}
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        i=find_conn(event->disconnect.conn.conn_handle);
        peer_delete(event->disconnect.conn.conn_handle);
        if(i>=0){peers[i].state=0;peers[i].cooldown=now;peers[i].rate_seen=0;printf("BLE_DISCONNECTED slot=%d reason=%d\n",i+1,event->disconnect.reason);}return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:{
        unsigned char data[3],feedback[2];unsigned length=OS_MBUF_PKTLEN(event->notify_rx.om);
        i=find_conn(event->notify_rx.conn_handle);
        if(i<0||peers[i].state!=5||event->notify_rx.attr_handle!=peers[i].command)return 0;
        if(!length||length>sizeof(data)||os_mbuf_copydata(event->notify_rx.om,0,length,data))return 0;
        action=ble_action(data,length);if(action<0){printf("BLE_INVALID slot=%d len=%u\n",i+1,length);return 0;}
        if(peers[i].rate_seen&&(unsigned)(now-peers[i].last_command)<(REMOTE_BLE_MINIMUM_COMMAND_INTERVAL_MS*HZ+999)/1000)result=4;
        else{peers[i].rate_seen=1;peers[i].last_command=now;result=remote_sender_submit_ble((char)action);}
        printf("BLE_COMMAND slot=%d action=%c result=%d\n",i+1,action,result);
        if(peers[i].status){feedback[0]=action;feedback[1]=result;ble_gattc_write_no_rsp_flat(peers[i].conn,peers[i].status,feedback,2);}
        return 0;
    }
    default:return 0;
    }
}
/* Runs on the BLE host queue, as do GAP/GATT callbacks: no cross-task peer races. */
static void tick(struct ble_npl_event *event){
    int i,waiting=0,free_slot=0,rc;unsigned now=tls_os_get_time();
    if(ble_hs_synced()){
        for(i=0;i<REMOTE_BLE_MAX;i++){
            if(peers[i].state==0||peers[i].state==1)free_slot=1;
            if(peers[i].state>=2&&peers[i].state<=4){
                waiting=1;
                if(peers[i].state>=3&&(unsigned)(now-peers[i].since)>=30*HZ){printf("BLE_SETUP_TIMEOUT slot=%d\n",i+1);close_peer(i);}
            }
        }
        if(!waiting){
            for(i=0;i<REMOTE_BLE_MAX;i++)if(peers[i].state==1)break;
            if(i<REMOTE_BLE_MAX){
                struct ble_gap_conn_params params;
                if(scanning){ble_gap_disc_cancel();scanning=0;scan_since=now;}
                ble_gap_init_conn_params(&params);params.itvl_min=0x20+i*16;params.itvl_max=params.itvl_min+2;
                params.latency=0;params.supervision_timeout=500;
                peers[i].state=2;peers[i].since=now;
                rc=ble_gap_connect(BLE_OWN_ADDR_PUBLIC,&peers[i].address,30000,&params,gap_event,0);
                if(rc){peers[i].state=0;peers[i].cooldown=now;printf("BLE_CONNECT_START_FAILED slot=%d rc=%d\n",i+1,rc);}
            }else if(!scanning&&free_slot&&(unsigned)(now-scan_since)>=REMOTE_BLE_SCAN_OFF_SECONDS*HZ){
                struct ble_gap_disc_params params={0};params.itvl=0x80;params.window=0x10;params.filter_duplicates=1;
                /* Active scanning also reads a name advertised in scan response. */
                rc=ble_gap_disc(BLE_OWN_ADDR_PUBLIC,REMOTE_BLE_SCAN_ON_SECONDS*1000,&params,gap_event,0);
                scanning=rc==0;scan_since=now;if(rc)printf("BLE_SCAN_FAILED rc=%d\n",rc);
            }
        }
    }
    if(ble_npl_callout_reset(&poll_callout,ble_npl_time_ms_to_ticks32(1000))!=0)printf("BLE_SCHEDULER_FAILED\n");
}
static void entry(void *arg){
    int rc;unsigned started;
    while(!remote_sender_wifi_ready())tls_os_time_delay(HZ);
    /* WiFi owns initialization first; do not contend with its heap bootstrap. */
    tls_os_time_delay(3*HZ);
    rc=tls_bt_init(1);printf("BLE_ENABLE result=%d\n",rc);
    if(rc&&rc!=BLE_HS_EALREADY)goto idle;
    started=tls_os_get_time();
    while(!ble_hs_synced()){
        if((unsigned)(tls_os_get_time()-started)>=30*HZ){printf("BLE_SYNC_TIMEOUT (WiFi retained)\n");goto idle;}
        tls_os_time_delay(HZ/10?HZ/10:1);
    }
    rc=peer_init(REMOTE_BLE_MAX,32,64,32);
    if(rc){printf("BLE_PEER_MEMORY_FAILED rc=%d (WiFi retained)\n",rc);goto idle;}
    scan_since=tls_os_get_time()-REMOTE_BLE_SCAN_OFF_SECONDS*HZ;
    printf("BLE_CENTRAL_READY max=%d service=%04x\n",REMOTE_BLE_MAX,REMOTE_BLE_SERVICE_UUID16);
    ble_npl_callout_init(&poll_callout,nimble_port_get_dflt_eventq(),tick,0);
    if(ble_npl_callout_reset(&poll_callout,ble_npl_time_ms_to_ticks32(1000))!=0)printf("BLE_SCHEDULER_FAILED\n");
 idle:
    /* RTOS task entry functions must never return to the sentinel LR. */
    for(;;)tls_os_time_delay(60*HZ);
}
#endif
void remote_ble_start(int wifi_role){
#if REMOTE_BLE_ENABLED
    if(wifi_role&&tls_os_task_create(&task,"ble_central",entry,0,(u8*)stack,sizeof(stack),38,0)!=WM_SUCCESS)printf("BLE_TASK_FAILED (WiFi retained)\n");
#endif
}
