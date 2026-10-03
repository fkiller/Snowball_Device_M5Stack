"""Trusted host USB broker; opens only an explicitly selected serial port."""
import argparse, json, sys, threading, time
import serial
sys.stdin.reconfigure(encoding='utf-8')
sys.stdout.reconfigure(encoding='utf-8')

p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
args=p.parse_args()
port=serial.Serial()
port.port=args.port
port.baudrate=115200
port.timeout=0.25
port.write_timeout=2
port.dtr=False
port.rts=False
port.open()
def outgoing():
    try:
        for line in sys.stdin:
            if len(line.encode()) <= 32768:
                port.write(line.encode())
    except Exception:
        port.close()
    finally:
        port.close()
threading.Thread(target=outgoing,daemon=True).start()
try:
    pending=b''
    while port.is_open:
        try: pending+=port.read(4096)
        except serial.SerialException: break
        if len(pending)>32768: pending=b'';continue
        while b'\n' in pending:
            line,pending=pending.split(b'\n',1)
            try:
                obj=json.loads(line)
                if isinstance(obj,dict):
                    print(json.dumps(obj,ensure_ascii=False),flush=True)
            except (ValueError,UnicodeDecodeError): pass
finally:
    port.close()
