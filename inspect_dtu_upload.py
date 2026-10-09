"""Read DTU settings and recent uplinks; never log platform credentials."""
import json, urllib.request, urllib.parse
from pathlib import Path
c=json.loads(Path('project_config.json').read_text(encoding='utf-8'))['iot']
base=c['management_base_url']; token=c['token']
if not token.startswith('Bearer '): token='Bearer '+token
def request(path,params=None,post=False):
    global token
    url=base+path+('?' + urllib.parse.urlencode(params) if params else '')
    req=urllib.request.Request(url,data=b'' if post else None,headers={'Authorization':token})
    with urllib.request.urlopen(req,timeout=35) as r: return json.load(r)
results={}
for path in ['serialport/getSerialPortInfo','modbus/getModbusInfo','workmode/getDeviceConfig','common/getBhuHeart']:
    try:
        r=request('/reach/ctrl/device/'+path,{'endpoint':str(c['imei'])})
        results[path]=r
        print(path,'code',r.get('code'))
        data=r.get('data') or {}
        if isinstance(data,dict):
            print({k:v for k,v in data.items() if not any(x in k.lower() for x in ['secret','password','token','key'])})
    except Exception as e: print(path,type(e).__name__,str(e))
r=request('/reach/dmp/deviceData/list/',{'pageNum':1,'pageSize':100,'imei':c['imei'],'productId':c['product_id'],'recType':'uplink'})
rows=r.get('rows') or []
print('uplink rows',len(rows),'non-heartbeat',[(x.get('id'),x.get('dataText')) for x in rows if not str(x.get('dataText','')).startswith('HB,')])
# Keep complete settings local; .gitignore excludes dtu-*.json.
Path('dtu-upload-diagnostic.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
