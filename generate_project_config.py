"""Generate build headers from local project_config.json without logging secrets."""
import json,re
from pathlib import Path
root=Path(__file__).parent
c=json.loads((root/'project_config.json').read_text(encoding='utf-8'))
w,i,b=c['wifi'],c['iot'],c['board'];app=root/'w801-sdk/wm_sdk_w80x_20211115/app'
def pin(v):
    if not re.fullmatch(r'P[AB]\d{1,2}',v):raise ValueError('Invalid GPIO')
    n=int(v[2:])
    if n>(15 if v[1]=='A' else 31):raise ValueError('Invalid GPIO range')
    return 'WM_IO_P'+v[1]+'_'+str(n).zfill(2)
role_pin=b.get('transport_select_gpio','PA11')
battery=c.get('battery',{})
battery_enabled=battery.get('enabled',False)
battery_pins=([battery.get('adc_gpio')]+battery.get('led_pins',[])) if battery_enabled else []
allpins=battery_pins+b['buttons']+b['receiver_outputs']+[b['receiver_rx'],b['receiver_tx'],c['provisioning']['button_gpio'],role_pin]
if len(b['buttons'])!=10 or len(b['receiver_outputs'])!=10 or len(set(allpins))!=len(allpins):raise ValueError('GPIO conflict or invalid channel count')
if {'PB19','PB20'}&set(allpins):raise ValueError('Download UART pins reserved')
if b['receiver_uart']!=5 or b['receiver_data_bits']!=8 or b['receiver_stop_bits']!=1 or b['receiver_parity']!='none':raise ValueError('This adapter supports UART5 8N1')
if b['receiver_tx'] not in ('PA12','PA8','PB18') or b['receiver_rx'] not in ('PA13','PA9','PB17'):raise ValueError('Unsupported UART5 pins')
if not 1<=b['pulse_ms']<=60000 or b['on_level'] not in (0,1):raise ValueError('Invalid output configuration')
if i['management_base_url']!='http://iot.rivcloud.com:7000/prod-api' or i['protocol']!='tcp':raise ValueError('Network adapter currently fixed to Rivcloud HTTP/TCP')
if len(w['ssid'].encode())>32 or not w['ssid'] or len(w['password'].encode())>64:raise ValueError('Invalid Wi-Fi configuration')
token=i['token'];token=token if token.startswith('Bearer ') else 'Bearer '+token
if len(token)>=2048 or '\r' in token or '\n' in token:raise ValueError('Invalid Token')
def write(name,values):
    (app/name).write_text('/* Generated from project_config.json. */\n'+''.join('#define '+k+' '+json.dumps(v)+'\n' for k,v in values.items()),encoding='utf-8')
ap_password=c['provisioning']['ap_password']
if not 8<=len(ap_password.encode())<=63:raise ValueError('AP password must be 8 to 63 bytes')
write('remote_config.h',{'REMOTE_SSID':w['ssid'],'REMOTE_WIFI_PASSWORD':w['password'],'REMOTE_USER':i['account'],'REMOTE_PASSWORD_MD5':i['password_md5'],'REMOTE_AP_PASSWORD':ap_password})
write('remote_session.h',{'DEPLOY_TOKEN':token,'DEPLOY_IMEI':str(i['imei']),'DEPLOY_DEVICE_ID':int(i['device_id']),'DEPLOY_PRODUCT_ID':int(i['product_id']),'DEPLOY_PRODUCT_KEY':i['product_key']})
board={'REMOTE_OUTPUT_PINS':'{'+','.join(pin(p) for p in b['receiver_outputs'])+'}','REMOTE_BUTTON_PINS':'{'+','.join(pin(p) for p in b['buttons'])+'}','REMOTE_UART_RX_PIN':pin(b['receiver_rx']),'REMOTE_UART_TX_PIN':pin(b['receiver_tx']),'REMOTE_UART_BAUD':str(b['receiver_baud']),'REMOTE_OUTPUT_ON':str(b['on_level']),'REMOTE_PULSE_MS':str(b['pulse_ms'])}
(app/'remote_board.h').write_text('/* Generated from project_config.json. */\n'+''.join('#define '+k+' '+v+'\n' for k,v in board.items()),encoding='utf-8')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:f.write('#define REMOTE_PROVISION_PIN '+pin(c['provisioning']['button_gpio'])+'\n')
heartbeat=b.get('serial_heartbeat_interval_ms',0)
if not isinstance(heartbeat,int) or heartbeat not in (0,) and not 1000<=heartbeat<=3600000:raise ValueError('Invalid serial heartbeat interval')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:f.write('#define REMOTE_SERIAL_HEARTBEAT_MS '+str(heartbeat)+'\n')
levels=b.get('output_on_levels',[b['on_level']]*10)
if len(levels)!=10 or any(v not in (0,1) for v in levels):raise ValueError('Invalid per-channel output levels')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:f.write('#define REMOTE_OUTPUT_LEVELS {'+','.join(str(v) for v in levels)+'}\n')
print('Firmware configuration generated; secrets omitted')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:f.write('#define REMOTE_TRANSPORT_SELECT_PIN '+pin(role_pin)+'\n')
power=c.get('power_save',{})
enabled=power.get('wifi_modem_sleep',True);hold=power.get('awake_after_button_ms',3000)
if not isinstance(enabled,bool) or not isinstance(hold,int) or isinstance(hold,bool) or not 1000<=hold<=30000:raise ValueError('Invalid power-save configuration')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:
    f.write('#define REMOTE_WIFI_POWER_SAVE '+str(int(enabled))+'\n#define REMOTE_POWER_AWAKE_MS '+str(hold)+'\n')

if battery_enabled:
    if battery['adc_gpio']!='PA1' or battery['adc_channel']!=0 or len(battery['led_pins'])!=4:raise ValueError('Battery requires PA1 ADC0 and four distinct LEDs')
    t=battery['thresholds_mv']
    if len(t)!=4 or any(not isinstance(v,int) for v in t) or not 3000<=t[0]<t[1]<t[2]<t[3]<=4200:raise ValueError('Invalid battery thresholds')
    for k in ['divider_top_ohms','divider_bottom_ohms']:
        if not isinstance(battery[k],int) or not 1000<=battery[k]<=1000000:raise ValueError('Invalid divider')
    if 4300*battery['divider_bottom_ohms']/(battery['divider_top_ohms']+battery['divider_bottom_ohms'])>2200:raise ValueError('ADC divider exceeds safe design input')
    if not 900<=battery['calibration_permille']<=1100 or not 1000<=battery['display_ms']<=30000:raise ValueError('Invalid battery calibration/display')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:
    f.write('#define REMOTE_BATTERY_ENABLED '+str(int(battery_enabled))+'\n')
    if battery_enabled:
        f.write('#define REMOTE_BATTERY_LEDS {'+','.join(pin(p) for p in battery['led_pins'])+'}\n')
        for k in ['divider_top_ohms','divider_bottom_ohms','calibration_permille','display_ms']:
            f.write('#define BATTERY_'+k.upper()+' '+str(battery[k])+'\n')
        f.write('#define BATTERY_THRESHOLDS {'+','.join(map(str,t))+'}\n')

networks=w.get('networks',[{'ssid':w['ssid'],'password':w['password']}])
if not isinstance(networks,list) or not 1<=len(networks)<=5:raise ValueError('Require one to five Wi-Fi networks')
seen=set()
for n in networks:
    name,secret=n['ssid'],n['password']
    if not isinstance(name,str) or not isinstance(secret,str) or not 1<=len(name.encode())<=32 or len(secret.encode())>64 or (secret and len(secret.encode())<8) or name in seen:raise ValueError('Invalid or duplicate Wi-Fi network')
    seen.add(name)
with (app/'remote_config.h').open('a',encoding='utf-8') as f:
    f.write('#define REMOTE_WIFI_DEFAULT_COUNT '+str(len(networks))+'\n')
    f.write('#define REMOTE_WIFI_DEFAULTS {'+','.join('{'+json.dumps(n['ssid'])+','+json.dumps(n['password'])+'}' for n in networks)+'}\n')

ble=c.get('ble_central',{})
be=ble.get('enabled',False);bn=ble.get('max_connections',7);names=ble.get('peer_names',[])
if not isinstance(be,bool) or not isinstance(bn,int) or not 1<=bn<=7 or (be and (len(names)!=bn or len(set(names))!=bn or any(not isinstance(n,str) or not 1<=len(n.encode())<=20 for n in names))):raise ValueError('Invalid BLE central configuration')
for key,lo,hi in [('service_uuid16',1,65535),('command_uuid16',1,65535),('status_uuid16',1,65535),('minimum_command_interval_ms',30,10000),('scan_on_seconds',1,30),('scan_off_seconds',1,300)]:
    if be and (not isinstance(ble[key],int) or not lo<=ble[key]<=hi):raise ValueError('Invalid BLE parameter')
with (app/'remote_board.h').open('a',encoding='utf-8') as f:
    f.write('#define REMOTE_BLE_ENABLED '+str(int(be))+'\n#define REMOTE_BLE_MAX '+str(bn)+'\n')
    if be:
        f.write('#define REMOTE_BLE_NAMES {'+','.join(json.dumps(n) for n in names)+'}\n')
        for key in ['service_uuid16','command_uuid16','status_uuid16','minimum_command_interval_ms','scan_on_seconds','scan_off_seconds']:
            f.write('#define REMOTE_BLE_'+key.upper()+' '+str(ble[key])+'\n')
