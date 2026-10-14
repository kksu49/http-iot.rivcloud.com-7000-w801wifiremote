"""Check platform response classification and consecutive-failure policy."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).parent
header=root/'w801-sdk/wm_sdk_w80x_20211115/app/platform_health.h'
test=r'''
#include <assert.h>
int main(void){
 unsigned failures=0;
 assert(platform_response_reachable(200,1));
 assert(!platform_response_reachable(200,0)); /* captive/login HTML */
 assert(!platform_response_reachable(302,0));
 assert(!platform_response_reachable(511,0));
 assert(platform_response_reachable(401,0));
 assert(platform_response_reachable(403,0));
 assert(platform_response_reachable(503,0)); /* server problem, not WiFi */
 assert(!platform_health_update(&failures,0)&&failures==1);
 assert(!platform_health_update(&failures,0)&&failures==2);
 assert(!platform_health_update(&failures,1)&&failures==0);
 assert(!platform_health_update(&failures,0));
 assert(!platform_health_update(&failures,0));
 assert(platform_health_update(&failures,0)&&failures==3);
 assert(platform_health_update(&failures,0)&&failures==3);
 assert(!platform_health_update(&failures,1)&&failures==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text('#include "'+header.as_posix()+'"\n'+test)
 subprocess.run(['C:/MinGW/bin/gcc.exe','-std=c99',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
print('Platform health tests passed: JSON, captive portal, auth/server errors, failure threshold and recovery')
