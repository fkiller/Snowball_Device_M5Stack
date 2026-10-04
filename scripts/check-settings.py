"""Verify actual device settings, saved Wi-Fi reuse, and authenticated connection.

No harness prompt is submitted. Refuses existing editor/draft; diagnostics call
native handlers. --screens captures actual EN/KO framebuffers, not mockups.
Credentials never leave the device. Display and input defaults are restored.
"""
import argparse,base64,json,time,secrets
from pathlib import Path
import serial
from framebuffer import decode_framebuffer
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--screens',type=Path)
parser.add_argument('--capture-names',nargs='+',help='Capture only named screens; all native checks still run')
parser.add_argument('--display-language',choices=['en','ko'],help='Use this actual display locale during connection/password QA; restores the original')
parser.add_argument('--connect',action='store_true',help='Explicitly reconnect the last successful network')
parser.add_argument('--expect-middleware-failure',action='store_true',help='Observe real unavailable gateway; no fake responses')
parser.add_argument('--capture-connection',action='store_true',help='Capture actual connecting and success frames (capture can extend hold)')
parser.add_argument('--saved-password',action='store_true',help='Scan and select the actual last saved AP; never reads its password')
parser.add_argument('--wifi-failure',action='store_true',help='Attempt a random unregistered SSID using the real radio, then restore saved Wi-Fi')
args=parser.parse_args()
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.05;port.dtr=port.rts=False;port.open()
buffer=b''
def send(kind,**fields):port.write((json.dumps({'type':kind,**fields})+'\n').encode())
def wait(kind,seconds=10):
    global buffer
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        buffer+=port.read(4096)
        while b'\n' in buffer:
            line,buffer=buffer.split(b'\n',1)
            try:value=json.loads(line)
            except ValueError:continue
            if value.get('type')==kind:return value
    raise RuntimeError('Actual firmware response missing: '+kind)
def inspect():send('inspect');return wait('inspection')
def status():send('connection-inspect');return wait('connection-state')
def setting(action,**fields):send('settings-check',action=action,**fields);time.sleep(.15);return inspect()
def capture(locale,name,phase=None):
    if not args.screens or (args.capture_names and name not in args.capture_names):return
    send('screenshot',raw=True,**({'connectionPhase':phase} if phase is not None else {}));begin=wait('screen_begin',30);rows={}
    assert begin['displayLanguage']==locale
    if phase is not None:assert begin['connectionPhase']==phase and begin['page']==10
    for _ in range(begin['height']):
        row=wait('screen_row');rows[row['y']]=base64.b64decode(row['data'],validate=True)
    wait('screen_end');path=args.screens/locale/(name+'.png');path.parent.mkdir(parents=True,exist_ok=True)
    decode_framebuffer(rows,begin['format']).save(path)
    print('Actual framebuffer: '+str(path),flush=True)
    return begin
def settled():
    deadline=time.monotonic()+12
    state=inspect()
    while state['pending']:
        if time.monotonic()>deadline:raise RuntimeError('Native request did not settle')
        time.sleep(.1);state=inspect()
    return state

def clearDisposable(expected):
    # Periodic authenticated polling can begin between inspection and clear.
    # Retry only this local diagnostic draft and stop on any human edit.
    deadline=time.monotonic()+12
    while time.monotonic()<deadline:
        state=inspect()
        if state['page']==0 and not state['draft']:return state
        if state['page']!=2 or state['draft']!=expected:raise RuntimeError('Human editor change; disposable cleanup stopped')
        if not state['pending']:send('clear-check')
        time.sleep(.15)
    raise RuntimeError('Disposable draft could not be cleared')
def connect():
    send('settings-check',action='connect-saved')
    phases=set();connectedAt=None;deadline=time.monotonic()+35;captureDone=False
    if args.capture_connection:
        begin=capture(args.display_language or initial['displayLanguage'],'connected',phase=3)
        assert begin is not None,'Connection capture requires --screens'
        phases.add(3);connectedAt=begin['connectionChangedAt'];captureDone=True
        deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        state=status();phases.add(state['phase'])
        if state['phase']==3:
            assert state['wifi'] and state['middleware'],'Success requires real Wi-Fi and authenticated middleware'
            connectedAt=state['changedAt']
        if state['page']==0 and connectedAt is not None:
            elapsed=state['uptimeMs']-connectedAt
            assert elapsed>=1000,elapsed
            # USB observation follows the synchronous native render/serial
            # flush. Exact 1000ms admission is asserted in connection.cpp.
            if not captureDone:assert elapsed<2000,elapsed
            print(json.dumps({'savedWifiReconnect':True,'actualPhases':sorted(phases),'observedReturnMs':elapsed,'nativePromptsSent':0}),flush=True)
            return
        if state['page']==11 and args.expect_middleware_failure:
            assert state['wifi'] and 2 in phases
            print(json.dumps({'actualMiddlewareFailureReturnedToSearch':True,'actualPhases':sorted(phases),'nativePromptsSent':0}),flush=True);return
        if state['page'] in (5,11):raise RuntimeError('Actual connection failed; page='+str(state['page']))
        time.sleep(.06)
    raise RuntimeError('Actual connection did not reach a terminal view')
initial=inspect();originalInput=initial['korean'];originalDisplay=initial['displayLanguage']=='ko'
if initial['draft'] or initial['page'] in (2,3,5):port.close();raise RuntimeError('Existing user draft/editor; QA refused')
try:
    if args.display_language:
        setting('display',korean=args.display_language=='ko');settled()
    if args.wifi_failure:
        settled();send('wifi-provision',ssid='Snowball-QA-'+secrets.token_hex(6),password=secrets.token_hex(8))
        phases=set();deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            state=status();phases.add(state['phase'])
            if state['page']==5:break
            time.sleep(.1)
        assert state['page']==5 and 1 in phases and not state['wifi'],state
        print(json.dumps({'actualRadioTimeoutReturnedToWifi':True,'savedCredentialsPreserved':True,'nativePromptsSent':0}),flush=True)
        send('wifi-key-check',key=27);time.sleep(.2);connect()
    elif args.saved_password:
        send('wifi-scan');scan=wait('wifi-networks',16);assert scan['ok'] and scan['total']>0
        state=setting('saved-network')
        assert state['page']==5 and state['knownNetwork'] and state['passwordLength']>0
        length=state['passwordLength'];assert not state['passwordVisible']
        send('wifi-key-check',key=9);state=inspect();assert state['passwordVisible'] and state['passwordLength']==length
        capture(state['displayLanguage'],'saved-password') # native diagnostic masks it
        send('wifi-scan');refused=wait('wifi-networks');assert not refused['ok'] and refused['code']=='editing'
        send('wifi-key-check',key=9);state=inspect();assert not state['passwordVisible'] and state['passwordLength']==length
        send('wifi-key-check',key=27);time.sleep(.2)
        print(json.dumps({'actualSavedApPrefilled':True,'passwordMasking':True,'editingProtected':True,'nativePromptsSent':0}),flush=True)
    elif args.connect:
        # Exercise reconnection only; caller controls the real gateway's availability.
        connect()
    else:
        for locale in ['en','ko']:
            state=setting('display',korean=locale=='ko');state=settled()
            assert state['displayLanguage']==locale and state['korean']==originalInput
            state=setting('settings');assert state['layoutOk'];capture(locale,'settings')
            state=setting('display-menu');assert state['layoutOk'];capture(locale,'display-language')
            state=setting('input-menu');assert state['layoutOk'];capture(locale,'input-settings')
            for name,korean,keys,expected in [('english-input',False,'Hello Snowball','Hello Snowball'),('korean-input',True,'dkssudgktpdy gksrmf','안녕하세요 한글')]:
                state=setting('input',korean=korean);assert state['displayLanguage']==locale and state['korean']==korean
                settled();send('ime-check',korean=korean,keys=keys);time.sleep(.15);state=inspect()
                assert state['draft']==expected and state['page']==2 and state['layoutOk']
                capture(locale,name)
                state=settled();assert state['draft']==expected
                clearDisposable(expected)
            setting('input',korean=originalInput)
        print(json.dumps({'displayInputIndependent':True,'nativeLocales':['en','ko'],'actualFramebuffers':bool(args.screens),'nativePromptsSent':0}),flush=True)
finally:
    state=inspect()
    if not state['draft'] and state['page'] not in (2,3,5):
        setting('input',korean=originalInput);setting('display',korean=originalDisplay);send('nav-check',action='content')
    port.close()
