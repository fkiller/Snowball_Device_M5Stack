"""Verify the real LCD/menu/Fn handlers without submitting a native prompt.

Captures contain a disposable keyboard draft; native content captures stay in
ignored artifacts. Refuses an existing draft and stops on human edits.
"""
import argparse,base64,json,time,urllib.request
from pathlib import Path
import serial
from framebuffer import decode_framebuffer
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--screens',type=Path)
args=parser.parse_args()
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.15;port.dtr=port.rts=False;port.open()
buffer=b''
def send(kind,**fields):port.write((json.dumps({'type':kind,**fields})+'\n').encode())
def wait(kind,seconds=10):
    global buffer
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        buffer+=port.read(4096)
        while b'\n' in buffer:
            raw,buffer=buffer.split(b'\n',1)
            try:value=json.loads(raw)
            except ValueError:continue
            if value.get('type')==kind:return value
    raise RuntimeError('Missing actual firmware response: '+kind)
def inspect():send('inspect');return wait('inspection')
def settled():
    deadline=time.monotonic()+55
    while time.monotonic()<deadline:
        state=inspect()
        if not state['pending'] and not state['view'].get('catalogPending'):return state
        time.sleep(.15)
    raise RuntimeError('Actual native operation did not settle')
def action(kind,name,**fields):send(kind,action=name,**fields);time.sleep(.25);return settled()
def nav(name):return action('nav-check',name)
def editor(name,**fields):return action('editor-check',name,**fields)
def setting(name,**fields):return action('settings-check',name,**fields)
def clearDisposable(expected):
    deadline=time.monotonic()+12
    while time.monotonic()<deadline:
        state=inspect()
        if state['page']==0 and not state['draft']:return state
        if state['page']!=2 or state['draft']!=expected:raise RuntimeError('Human editor change; disposable cleanup stopped')
        if not state['pending']:send('clear-check')
        time.sleep(.15)
    raise RuntimeError('Disposable draft could not be cleared')
def capture(directory,name):
    if directory is None:return
    directory.mkdir(parents=True,exist_ok=True)
    send('screenshot',raw=True);begin=wait('screen_begin');rows={}
    for _ in range(240):
        row=wait('screen_row');rows[row['y']]=base64.b64decode(row['data'],validate=True)
    wait('screen_end');assert set(rows)==set(range(240))
    decode_framebuffer(rows,begin['format']).save(directory/(name+'.png'))
    print('Captured actual LCD: '+name,flush=True)
    # UART framebuffer output pauses the loop long enough to expire the PC
    # TCP idle timeout. Require a fresh real middleware receipt afterward,
    # not the connected flag from a cached pre-capture view.
    started=time.monotonic();deadline=started+20
    while time.monotonic()<deadline:
        send('connection-inspect');state=wait('connection-state')
        if state['middleware'] and state['responseAgeMs']<4000:
            print(json.dumps({'capture':name,'authenticatedRecoveryMs':round((time.monotonic()-started)*1000)}),flush=True)
            return
        time.sleep(.2)
    raise RuntimeError('No fresh authenticated middleware response after framebuffer capture')
def api(route,body=None):
    req=urllib.request.Request('http://127.0.0.1:8765'+route,data=None if body is None else json.dumps(body).encode(),headers={'Origin':'http://127.0.0.1:8765','X-Snowball-Controller':'ctl_16cd785043c0fd4e','Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=15) as response:return json.load(response)
initial=inspect()
if initial['draft'] or initial['page'] in (3,5,14):raise RuntimeError('Existing user editor/draft; QA refused')
if initial['page']==2:
    if initial['editCaret']!=0:raise RuntimeError('Unexpected nonempty editor caret')
    state=editor('left');assert state['page']==0 and not state['draft']
initialController=api('/v1/controller');before=api('/v1/snapshot');draft='Hello Snowball'
Path('artifacts/fn-controller-restore.json').write_text(json.dumps(initialController),encoding='utf-8')
try:
    for locale in ['en','ko']:
        setting('input',korean=initial['korean'])
        setting('display-menu');nav('home')
        if locale=='ko':nav('down')
        state=nav('select');assert state['page']==8 and state['cursor']==3 and state['displayLanguage']==locale
        assert state['korean']==initial['korean']
        state=nav('home');state=nav('up');assert state['page']==8 and state['focus']=='top' and state['crumb']==0
        capture(args.screens/locale if args.screens else None,'menu-focus')
        state=nav('select');assert state['page']==8 and state['focus']=='content'
        capture(args.screens/locale if args.screens else None,'settings')
        state=nav('content');state=nav('end');assert state['cursor']==max(0,state['view']['contentTotal']-12)
        deadline=time.monotonic()+10
        while state['view']['contentOffset']!=state['cursor']:
            if time.monotonic()>deadline:raise RuntimeError('Displayed content offset did not converge')
            time.sleep(.2);state=settled()
        capture(Path('artifacts/fn-ui')/locale,'content-end')
        nav('home');capture(Path('artifacts/fn-ui')/locale,'content-home')
        for attempt in range(30):
            state=settled();send('ime-check',korean=False,keys=draft);time.sleep(.2);state=inspect()
            if state['page']==2:break
        assert state['page']==2 and state['draft']==draft
        state=editor('key',key=0xba);assert state['fnMenu'] and state['draft']==draft
        capture(args.screens/locale if args.screens else None,'fn-controls')
        state=editor('key',key=0xba);assert not state['fnMenu'] and state['page']==2
        for mode,button in [('model',0),('effort',1),('access',2)]:
            state=editor('fn');assert state['fnMenu']
            state=editor(mode);assert state['page']==14 and state['popupButton']==button
            assert state['layoutOk'] and state['draft']==draft
            kind={'model':'models','effort':'efforts','access':'access'}[mode]
            assert state['view']['menuKind']==kind
            if mode=='access':assert state['view']['menuTotal']>0,'Installed Codex schema should advertise its real policies'
            state=editor('end');assert state['cursor']==max(0,state['view']['menuTotal']-1)
            state=editor('home');assert state['cursor']==0
            capture(args.screens/locale if args.screens else None,'fn-'+mode)
            state=editor('fn');assert state['page']==2 and state['draft']==draft and not state['fnMenu']
        capture(args.screens/locale if args.screens else None,'english-input')
        state=settled();assert state['draft']==draft
        clearDisposable(draft)
    after=api('/v1/snapshot')
    assert [c['commandId'] for c in before['commands']]==[c['commandId'] for c in after['commands']]
    state=api('/v1/controller');assert state['selection']==initialController['selection']
    for key in ['model','effort','access']:assert state['preferences'].get(key,'')==initialController['preferences'].get(key,'')
    print(json.dumps({'displayReturnsToMenu':True,'menuContentPreserved':True,'actualReaderRows':12,'fnCancelPreservesDraft':True,'actualPopupGeometry':True,'nativePromptsSent':0}),flush=True)
finally:
    current=inspect()
    if not current['draft'] and current['page'] not in (2,3,5,14):
        setting('display',korean=initial['displayLanguage']=='ko');setting('input',korean=initial['korean']);nav('content')
        action('nav-check','reader',index=initialController['preferences'].get('scroll',0))
        if initial['page']==2:editor('key',key=21)
    port.close()
