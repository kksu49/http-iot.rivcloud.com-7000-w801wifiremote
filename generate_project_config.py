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
allpins=b['buttons']+b['receiver_outputs']+[b['receiver_rx'],b['receiver_tx'],c['provisioning']['button_gpio'],role_pin]
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
