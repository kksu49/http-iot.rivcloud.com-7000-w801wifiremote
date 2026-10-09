"""Read, back up and align RS232 business UART with the current W801 build.

Run without --apply to inspect only. Use --apply after installing a 3.3V
TTL/RS232 converter and wiring DTU 03/04 to W801 PA13/PA12 via it.
"""
import argparse, json, re, urllib.request, urllib.parse
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apply',action='store_true')
args=parser.parse_args()
c=json.loads(Path('project_config.json').read_text(encoding='utf-8'))
i=c['iot'];token=i['token']
if not token.startswith('Bearer '):token='Bearer '+token
def call(endpoint,params,post=False):
    req=urllib.request.Request(i['management_base_url']+'/reach/ctrl/device/serialport/'+endpoint+'?'+urllib.parse.urlencode(params),data=b'' if post else None,headers={'Authorization':token})
    with urllib.request.urlopen(req,timeout=35) as response:result=json.load(response)
    if result.get('code')!=200:raise RuntimeError('Platform rejected '+endpoint+' code='+str(result.get('code')))
    return result
params={'endpoint':str(i['imei'])}
before=call('getSerialPortInfo',params)
data=before['data']
def number(name):
    match=re.search(r':\s*(\d+)\s*$',str(data.get(name,'')))
    if not match:raise RuntimeError('Unexpected '+name+' format')
    return int(match.group(1))
mode,baud,bits,parity=[number(k) for k in ('transUartMode','baudrate','dataBit','parity')]
target=c['board']['receiver_baud']
print('Current: mode='+str(mode)+' baud='+str(baud)+' dataBitEnum='+str(bits)+' parityEnum='+str(parity))
print('Target: RS232 mode=2, baud='+str(target)+'; external level converter required')
if not args.apply:
    print('INSPECTION_ONLY: no device settings changed')
else:
    if mode!=2 or bits!=3 or parity!=0:raise RuntimeError('Only an existing RS232 8N1 configuration can be aligned by this script')
    backup=Path('backups/dtu-business-uart');backup.mkdir(parents=True,exist_ok=True)
    # Preserve the first snapshot so re-runs do not overwrite the original baud rate.
    original=backup/'before.json'
    if not original.exists():original.write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf-8')
    if baud!=target:call('setBaudrate',dict(params,baudrate=target),True)
    after=call('getSerialPortInfo',params)
    (backup/'after.json').write_text(json.dumps(after,ensure_ascii=False,indent=2),encoding='utf-8')
    data=after['data']
    if number('transUartMode')!=2 or number('baudrate')!=target or number('dataBit')!=3 or number('parity')!=0:
        raise RuntimeError('Readback mismatch; original settings saved in backups/dtu-business-uart/before.json')
    print('READBACK_VERIFIED: RS232 baud matches W801; end-to-end uplink still requires testing')
