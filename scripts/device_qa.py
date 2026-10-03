"""Inspect the real firmware and save its displayed framebuffer over USB."""
import argparse,base64,json,time,sys
from pathlib import Path
import serial
from PIL import Image
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
p.add_argument('--output',required=True)
p.add_argument('--korean',action='store_true')
p.add_argument('--keys')
p.add_argument('--inspect',action='store_true')
args=p.parse_args()
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.5;port.dtr=False;port.rts=False;port.open()
def send(obj):port.write((json.dumps(obj,ensure_ascii=False)+'\n').encode())
time.sleep(.5)
if args.keys is not None:send({'type':'ime-check','korean':args.korean,'keys':args.keys});time.sleep(.5)
send({'type':'inspect'})
deadline=time.monotonic()+7
inspection=None
while time.monotonic()<deadline:
    try:
        obj=json.loads(port.readline())
        if obj.get('type')=='inspection':inspection=obj;break
    except ValueError:pass
if inspection is None:raise RuntimeError('No actual firmware inspection response')
print(json.dumps(inspection,ensure_ascii=False))
send({'type':'screenshot'})
rows={};deadline=time.monotonic()+70
while time.monotonic()<deadline:
    try:obj=json.loads(port.readline())
    except ValueError:continue
    if obj.get('type')=='screen_row':
        row=base64.b64decode(obj['data'],validate=True)
        if len(row)!=960:raise RuntimeError('Invalid RGB framebuffer row')
        rows[obj['y']]=row
    elif obj.get('type')=='screen_end':break
if set(rows)!=set(range(240)):raise RuntimeError(f'Incomplete framebuffer: {len(rows)}/240 rows')
image=Image.frombytes('RGB',(320,240),b''.join(rows[y] for y in range(240)))
out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True);image.save(out)
if args.keys is not None:send({'type':'clear-check'})
port.close()
print(f'Saved actual firmware framebuffer: {out.resolve()}')
