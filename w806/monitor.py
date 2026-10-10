"""Read-only USB UART0 log capture; does not inject DTU UART5 commands."""
import argparse,time,serial
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',default='COM10');p.add_argument('--seconds',type=int,default=20);p.add_argument('--reset',action='store_true')
a=p.parse_args()
print('Opening',a.port,flush=True)
s=serial.Serial();s.port=a.port;s.baudrate=115200;s.timeout=0;s.dtr=False;s.rts=False;s.open()
if a.reset:
 s.rts=True;time.sleep(.1);s.rts=False
print('Listening; reset board to capture startup',flush=True)
data=bytearray();end=time.monotonic()+a.seconds
try:
 while time.monotonic()<end:
  chunk=s.read(s.in_waiting);data.extend(chunk)
  if chunk:print(chunk.decode('utf-8',errors='replace'),end='',flush=True)
  time.sleep(.02)
finally:
 s.close();out=Path(__file__).parent/'build';out.mkdir(exist_ok=True);(out/'com10-boot.log').write_bytes(data)
