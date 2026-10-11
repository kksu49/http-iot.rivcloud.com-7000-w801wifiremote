"""Exercise the actual modem-sleep policy without hardware."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).parent
source=(root/'w801-sdk/wm_sdk_w80x_20211115/app/remote_power.c').read_text()
stub='''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define FALSE 0
#define HZ 100
static unsigned tick;static int ps=-1,calls;
static unsigned tls_os_get_time(void){return tick;}
static void tls_wifi_set_psm_chipsleep_flag(unsigned flag){assert(flag==0);}
static void tls_wifi_set_psflag(int enabled,int persistent){assert(!persistent);ps=enabled;calls++;}
'''
main='''
int main(void){
 tick=100;remote_power_start();assert(ps==0);
 tick=399;remote_power_update(1,0);assert(ps==0);
 tick=400;remote_power_update(1,0);assert(ps==REMOTE_WIFI_POWER_SAVE);
 int before=calls;remote_power_update(1,0);assert(calls==before);
 remote_power_activity();assert(ps==0);
 tick=700;remote_power_update(1,1);assert(ps==0);
 remote_power_update(1,0);assert(ps==REMOTE_WIFI_POWER_SAVE);
 remote_power_update(0,0);assert(ps==0);
 tick=0xffffff00u;remote_power_activity();tick+=300;
 remote_power_update(1,0);assert(ps==REMOTE_WIFI_POWER_SAVE);
 puts("Power policy passed: idle, immediate button wake, busy, offline/AP/serial, disabled setting, rollover, no flash writes");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'wm_include.h').write_text(stub);(p/'remote_power.h').write_text('')
 (p/'test.c').write_text(source+main)
 for enabled in (0,1):
  (p/'remote_board.h').write_text(f'#define REMOTE_WIFI_POWER_SAVE {enabled}\n#define REMOTE_POWER_AWAKE_MS 3000\n')
  executable=p/f'test-{enabled}.exe'
  subprocess.run(['C:/MinGW/bin/gcc.exe','-std=c99','-Wall',str(p/'test.c'),'-o',str(executable)],check=True)
  subprocess.run([str(executable)],check=True)
