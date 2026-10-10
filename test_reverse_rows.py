"""Exercise actual firmware uplink consumer with vendor cJSON on the host."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).parent
app=root/'w801-sdk/wm_sdk_w80x_20211115/app'
source=(app/'remote_sender.c').read_text()
function=source[source.index('static void reverse_rows('):source.index('static void expire_auth(')]
matcher=source[source.index('static int ack_matches('):source.index('static void send_action(')]
prefix=r'''
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "short_frame.h"
#define IMEI "test-imei"
static unsigned connection_epoch;
static int received;
static char last[96];
static int success(cJSON *root){return cJSON_GetObjectItem(root,"code")->valueint==200;}
static int remote_link_enqueue(const char *frame,int serial){assert(!serial);received++;strcpy(last,frame);return 1;}
'''
main=r'''
static void poll(const char *json){cJSON *r=cJSON_Parse(json);assert(r);reverse_rows(r);cJSON_Delete(r);}
int main(void){
 poll("{\"code\":200,\"rows\":[{\"id\":10,\"recType\":\"uplink\",\"dataText\":\"REV,old,a\"}]}");assert(!received);
 poll("{\"code\":200,\"rows\":[{\"id\":11,\"recType\":\"uplink\",\"dataText\":\"REV,new,d\\n\"}]}");assert(received==1&&!strcmp(last,"CMD,new,d\n"));
 poll("{\"code\":200,\"rows\":[{\"id\":11,\"recType\":\"uplink\",\"dataText\":\"REV,new,d\\n\"}]}");assert(received==1);
 poll("{\"code\":200,\"rows\":[{\"id\":14,\"recType\":\"downlink\",\"dataText\":\"REV,wrong,a\"},{\"id\":13,\"recType\":\"uplink\",\"dataText\":\"HB,test\"},{\"id\":12,\"recType\":\"uplink\",\"dataText\":\"ACK,test,a,TIMER,1,DONE\"}]}");assert(received==1);
 connection_epoch++;
 poll("{\"code\":200,\"rows\":[{\"id\":15,\"recType\":\"uplink\",\"dataText\":\"REV,offline,a\"}]}");assert(received==1);
 poll("{\"code\":200,\"rows\":[{\"id\":16,\"recType\":\"uplink\",\"dataText\":\"REV,current,b\"}]}");assert(received==2&&!strcmp(last,"CMD,current,b\n"));
 poll("{\"code\":200,\"rows\":[{\"id\":17,\"recType\":\"uplink\",\"dataText\":\"RdABCDEFGHIJKLMNOPQRSTUVWXYZ\"}]}");assert(received==2);
 poll("{\"code\":200,\"rows\":[{\"id\":18,\"recType\":\"uplink\",\"dataText\":\"ReABCDEFGHIJKLMNOPQRS\\r\\n\"}]}");assert(received==3&&!strcmp(last,"CeABCDEFGHIJKLMNOPQRS\n"));
 poll("{\"code\":200,\"rows\":[{\"id\":18,\"recType\":\"uplink\",\"dataText\":\"ReABCDEFGHIJKLMNOPQRS\"}]}");assert(received==3);
 {
  cJSON *r=cJSON_Parse("{\"code\":200,\"rows\":[{\"recType\":\"uplink\",\"dataText\":\"KeABCDEFGHIJKLMNOPQRS\\r\\n\"}]}");
  assert(ack_matches(r,"ABCDEFGHIJKLMNOPQRS",'e'));assert(!ack_matches(r,"ABCDEFGHIJKLMNOPQRS",'d'));cJSON_Delete(r);
  r=cJSON_Parse("{\"code\":200,\"rows\":[{\"recType\":\"uplink\",\"dataText\":\"SeABCDEFGHIJKLMNOPQRS\"}]}");
  assert(!ack_matches(r,"ABCDEFGHIJKLMNOPQRS",'e'));cJSON_Delete(r);
  r=cJSON_Parse("{\"code\":200,\"rows\":[{\"recType\":\"uplink\",\"dataText\":\"ACK,legacy,e,TIMER,5,DONE\\n\"}]}");
  assert(ack_matches(r,"legacy",'e'));cJSON_Delete(r);
 }
 puts("Reverse polling tests passed: baseline, conversion, duplicate, direction, heartbeat/ACK, reconnect");return 0;
}
'''
vendor=root/'w801-sdk/wm_sdk_w80x_20211115/src/app/cJSON'
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.c').write_text(prefix+function+matcher+main)
    (path/'wm_mem.h').write_text('#include <stdlib.h>\n#define tls_mem_alloc malloc\n#define tls_mem_free free\n')
    (path/'utils.h').write_text('')
    subprocess.run(['C:/MinGW/bin/gcc.exe','-std=gnu99','-I',str(app),'-I',str(path),'-I',str(vendor),str(path/'test.c'),str(vendor/'cJSON.c'),'-lm','-o',str(path/'test.exe')],check=True)
    subprocess.run([str(path/'test.exe')],check=True)
