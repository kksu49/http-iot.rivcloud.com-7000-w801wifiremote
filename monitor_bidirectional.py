"""Capture USB debug logs without writing data to either board's UART."""
import argparse, serial, time
from contextlib import ExitStack
parser=argparse.ArgumentParser()
parser.add_argument('--ports',nargs='+',default=['COM4','COM5'])
parser.add_argument('--seconds',type=int,default=60)
args=parser.parse_args()
with ExitStack() as stack:
    ports=[]
    for name in args.ports:
        port=serial.Serial();port.port=name;port.baudrate=115200;port.timeout=0;port.dtr=False;port.rts=False
        port.open();stack.callback(port.close);ports.append((name,port))
    log=stack.enter_context(open('bidirectional-live.log','a',encoding='utf-8'))
    end=time.monotonic()+args.seconds
    while time.monotonic()<end:
        for name,port in ports:
            data=port.read(port.in_waiting)
            if data:
                value='['+name+'] '+data.decode('utf-8',errors='replace')
                print(value,end='',flush=True);log.write(value);log.flush()
        time.sleep(.02)
