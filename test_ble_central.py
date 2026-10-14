"""Host tests of actual BLE admission and scheduling code, no peripherals needed."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).parent;app=root/'w801-sdk/wm_sdk_w80x_20211115/app'
sender=(app/'remote_sender.c').read_text(encoding='utf-8')
admission=sender[sender.index('int remote_sender_submit_ble'):sender.index('static int success(cJSON *root);')]
ble=(app/'remote_ble.c').read_text()
peer=ble[ble.index('typedef struct {'):ble.index('static const char *names')]
tick=ble[ble.index('static void tick('):ble.index('static void entry(')]
subscribe=ble[ble.index('static int subscribed('):ble.index('static int gap_event(struct ble_gap_event *event,void *arg){')]
stub=r'''
#include <assert.h>
#include <stdio.h>
#define HZ 100
#define REMOTE_BLE_MAX 7
#define REMOTE_BLE_SCAN_OFF_SECONDS 25
#define REMOTE_BLE_SCAN_ON_SECONDS 5
#define BLE_OWN_ADDR_PUBLIC 0
typedef unsigned u32;
typedef struct {unsigned char type,val[6];} ble_addr_t;
struct ble_gap_event {int dummy;};
struct ble_npl_event {int dummy;};
struct ble_npl_callout {int dummy;};
struct ble_gap_conn_params {int itvl_min,itvl_max,latency,supervision_timeout;};
struct ble_gap_disc_params {int itvl,window,filter_duplicates;};
static unsigned time_now;
static int ip_ready,platform_ready,authenticated,device_ready,serial_sender,ble_activity;
static unsigned queue_head,queue_tail,command_time[32];
static char commands[32];static int *network_wake;
static int wake_count,connect_count,scan_count,cancel_count,scheduled,interval;
static unsigned tls_os_get_time(void){return time_now;}
static u32 tls_os_set_critical(void){return 0;}
static void tls_os_release_critical(u32 lock){}
static int tls_os_sem_release(int *s){wake_count++;return 0;}
static int missing_command,missing_cccd,write_error,write_count,last_cccd;
struct ble_gatt_error {int status;};
struct ble_gatt_attr {int dummy;};
struct peer {uint16_t conn_handle;};
struct peer_chr {struct {unsigned char properties;uint16_t val_handle;} chr;};
struct peer_dsc {struct {uint16_t handle;} dsc;};
typedef struct {unsigned short value;} ble_uuid_t;
#define BLE_UUID16_DECLARE(v) (&(ble_uuid_t){v})
#define REMOTE_BLE_SERVICE_UUID16 0xfff0
#define REMOTE_BLE_COMMAND_UUID16 0xfff1
#define REMOTE_BLE_STATUS_UUID16 0xfff2
static struct peer_chr chr={{0x10,44}},feedback_chr={{0x04,42}};
static struct peer_dsc dsc={{45}};
static const struct peer_chr *peer_chr_find_uuid(const struct peer *p,const ble_uuid_t *svc,const ble_uuid_t *u){assert(svc->value==0xfff0);return u->value==0xfff1?(missing_command?0:&chr):&feedback_chr;}
static const struct peer_dsc *peer_dsc_find_uuid(const struct peer *p,const ble_uuid_t *svc,const ble_uuid_t *u,const ble_uuid_t *d){assert(d->value==0x2902);return missing_cccd?0:&dsc;}
static int ble_gattc_write_flat(uint16_t conn,uint16_t handle,const void *data,int length,int (*fn)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg){assert(conn==7&&handle==45&&length==2);last_cccd=*(const unsigned char*)data;write_count++;return write_error;}
static int scanning;static unsigned scan_since;
static struct ble_npl_callout poll_callout;
static int ble_hs_synced(void){return 1;}
static void ble_gap_init_conn_params(struct ble_gap_conn_params *p){memset(p,0,sizeof(*p));}
static int ble_gap_disc_cancel(void){cancel_count++;return 0;}
static int gap_event(struct ble_gap_event *event,void *arg){return 0;}
static int ble_gap_connect(int own,ble_addr_t *addr,int timeout,struct ble_gap_conn_params *p,int (*fn)(struct ble_gap_event*,void*),void *arg){assert(timeout==30000);interval=p->itvl_min;connect_count++;return 0;}
static int ble_gap_disc(int own,int duration,struct ble_gap_disc_params *p,int (*fn)(struct ble_gap_event*,void*),void *arg){assert(duration==5000&&p->window<p->itvl);scan_count++;return 0;}
static int ble_npl_time_ms_to_ticks32(int ms){return ms/10;}
static int ble_npl_callout_reset(struct ble_npl_callout *c,int ticks){assert(ticks==HZ);scheduled++;return 0;}
'''
close=r'''
static void close_peer(int i){peers[i].state=6;}
static int find_conn(uint16_t conn){for(int i=0;i<7;i++)if(peers[i].state>=3&&peers[i].conn==conn)return i;return -1;}
'''
test=r'''
int main(void){
 unsigned char adv[]={12,9,'W','8','0','1','-','B','L','E','-','0','1',0};
 int sem=0,i;
 assert(ble_action((const unsigned char*)"a",1)=='a');
 assert(ble_action((const unsigned char*)"j\r\n",3)=='j');
 assert(ble_action((const unsigned char*)"ab",2)==-1);
 assert(ble_action((const unsigned char*)"k",1)==-1);
 assert(ble_action((const unsigned char*)"",0)==-1);
 assert(ble_name_match(adv,sizeof(adv),"W801-BLE-01"));
 assert(!ble_name_match(adv,sizeof(adv),"W801-BLE-02"));
 assert(!ble_name_match(adv,3,"W801-BLE-01"));
 assert(remote_sender_submit_ble('a')==1&&queue_head==0);
 ip_ready=platform_ready=authenticated=device_ready=1;network_wake=&sem;
 for(i=0;i<27;i++)assert(remote_sender_submit_ble('a')==0);
 assert(wake_count==27&&ble_activity);
 assert(remote_sender_submit_ble('b')==2&&queue_head==27);
 assert(remote_sender_submit_ble('z')==3);
 serial_sender=1;assert(remote_sender_submit_ble('a')==1);
 serial_sender=0;queue_tail=1;assert(remote_sender_submit_ble('j')==0&&commands[27]=='j');
 time_now=25*HZ;tick(0);assert(scan_count==1&&scanning);
 peers[0].state=1;tick(0);assert(connect_count==1&&cancel_count==1&&peers[0].state==2);
 peers[1].state=1;tick(0);assert(connect_count==1); /* connect sequentially */
 peers[0].state=5;tick(0);assert(connect_count==2&&interval==0x30);
 for(i=0;i<7;i++)peers[i].state=5;
 time_now+=30*HZ;tick(0);assert(connect_count==2&&scan_count==1); /* capacity */
 peers[3].state=0;tick(0);assert(scan_count==2); /* resume for a vacant slot */
 peers[2].state=3;peers[2].since=time_now-30*HZ;tick(0);assert(peers[2].state==6);
 struct peer p={7};struct ble_gatt_error ok={0},failed={1};
 memset(peers,0,sizeof(peers));peers[0].state=3;peers[0].conn=7;
 discovered(&p,0,0);assert(peers[0].state==4&&last_cccd==1&&peers[0].command==44&&peers[0].status==42);
 subscribed(7,&ok,0,0);assert(peers[0].state==5);
 chr.chr.properties=0x20;peers[0].state=3;discovered(&p,0,0);assert(last_cccd==2);
 subscribed(7,&failed,0,0);assert(peers[0].state==6);
 missing_command=1;peers[0].state=3;discovered(&p,0,0);assert(peers[0].state==6);
 missing_command=0;write_error=1;peers[0].state=3;discovered(&p,0,0);assert(peers[0].state==6);
 puts("BLE tests passed: commands, bounded advertising, offline/busy admission, physical queue reserve, wake, sequential connect, seven slots, timeout recovery");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text('#include "'+(app/'ble_command.h').as_posix()+'"\n'+stub+admission+peer+close+subscribe+tick+test)
 subprocess.run(['C:/MinGW/bin/gcc.exe','-std=c99',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
