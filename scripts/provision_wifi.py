"""Provision a physically connected M5Stack from a matching Windows WLAN profile.
No credentials are printed, logged, or written to a plaintext staging file.
"""
import argparse,json,re,subprocess,time,sys
import serial
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
args=p.parse_args()
def netsh(*args):
    result=subprocess.run(['netsh','wlan',*args],capture_output=True,check=True)
    return result.stdout.decode('mbcs',errors='replace')
interfaces=netsh('show','interfaces')
match=re.search(r'^\s*Profile\s*:\s*(.+?)\s*$',interfaces,re.M)
if not match:raise RuntimeError('No active Windows WLAN profile; configure Wi-Fi on the device.')
current=match.group(1)
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.8;port.dtr=False;port.rts=False;port.open()
def send(obj):port.write((json.dumps(obj,ensure_ascii=False)+'\n').encode())
def wait(kind,seconds):
    deadline=time.monotonic()+seconds;pending=b''
    while time.monotonic()<deadline:
        pending+=port.read(4096)
        while b'\n' in pending:
            line,pending=pending.split(b'\n',1)
            try:value=json.loads(line)
            except ValueError:continue
            if value.get('type')==kind:return value
    raise RuntimeError('Device response timed out: '+kind)
send({'type':'wifi-scan'})
networks=wait('wifi-networks',15)['networks']
profiles=re.findall(r'^\s*All User Profile\s*:\s*(.+?)\s*$',netsh('show','profiles'),re.M)
available=sorted([n for n in networks if n['ssid'] in profiles],key=lambda n:(n['ssid']!=current,-n['rssi']))
if not available:raise RuntimeError('No visible 2.4GHz SSID matches a saved Windows WLAN profile.')
selected=available[0]
profile=netsh('show','profile','name='+selected['ssid'],'key=clear')
key_match=re.search(r'^\s*Key Content\s*:\s*(.*?)\s*$',profile,re.M)
if selected['secure'] and not key_match:raise RuntimeError('Windows could not supply this profile key; configure on-device.')
password=key_match.group(1) if key_match else ''
send({'type':'wifi-provision','ssid':selected['ssid'],'password':password})
password=profile=''
deadline=time.monotonic()+27
while time.monotonic()<deadline:
    send({'type':'inspect'})
    observation=wait('inspection',5)
    if observation.get('wifi'):
        print(json.dumps({'connected':True,'ssid':selected['ssid'],'ip':observation['ip']},ensure_ascii=False));break
    time.sleep(1)
else:raise RuntimeError('Real Wi-Fi association failed; last working NVS credentials were preserved.')
port.close()
