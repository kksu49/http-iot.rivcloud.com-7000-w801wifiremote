#include "wm_include.h"
#include "remote_board.h"
#include "remote_power.h"
static unsigned awake_since;
static int last_mode=-1;
static void mode(int enabled){
 if(enabled==last_mode)return;
 /* Modem sleep only: preserve CPU, UART, timers and RTOS GPIO scanning.
    Do not persist transient state to flash on each button press. */
 tls_wifi_set_psm_chipsleep_flag(0);
 tls_wifi_set_psflag(enabled,FALSE);
 last_mode=enabled;
 printf("POWER_WIFI %s chip_sleep=0\n",enabled?"SAVE":"AWAKE");
}
void remote_power_start(void){awake_since=tls_os_get_time();mode(0);}
void remote_power_activity(void){awake_since=tls_os_get_time();mode(0);}
void remote_power_update(int online,int busy){
 unsigned elapsed=(unsigned)(tls_os_get_time()-awake_since);
 unsigned hold=(REMOTE_POWER_AWAKE_MS*HZ+999)/1000;
 mode(REMOTE_WIFI_POWER_SAVE&&online&&!busy&&elapsed>=hold);
}
