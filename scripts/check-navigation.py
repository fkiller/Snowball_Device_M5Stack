"""Check the real firmware focus/navigation using read-only diagnostic actions.

Exercises the actual UI state machine and live source lists, not physical
keypresses. Never composes, connects a network, or sends a harness prompt.
"""
import argparse
import base64
import json
import time
from pathlib import Path
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--screens',type=Path)
parser.add_argument('--capture-names',nargs='+',help='Capture only named screens; all navigation checks still run')
parser.add_argument('--display-language',choices=['en','ko'],help='Use an actual display locale for captures; restore it afterward')
parser.add_argument('--input-screens',action='store_true',help='Explicit disposable English/Korean rendering QA; refuses an existing draft')
args=parser.parse_args()
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.15;port.dtr=port.rts=False;port.open()
buffer=b''
def send(kind,**fields):port.write((json.dumps({'type':kind,**fields})+'\n').encode())
def wait(kind,seconds=8):
    global buffer
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        buffer+=port.read(4096)
        while b'\n' in buffer:
            line,buffer=buffer.split(b'\n',1)
            try:value=json.loads(line)
            except ValueError:continue
            if value.get('type')==kind:return value
    raise RuntimeError('No actual firmware response: '+kind)
def inspect():
    # A framebuffer transfer can overlap a LAN reconnect/read timeout. Only
    # retry observation, never a selection, input, or a mutating command.
    for attempt in range(3):
        send('inspect')
        try:return wait('inspection')
        except RuntimeError:
            if attempt==2:raise
def action(name,settle=True):
    send('nav-check',action=name)
    time.sleep(.2)
    deadline=time.monotonic()+10
    while True:
        state=inspect()
        if state['page'] in (2,3,5):raise RuntimeError('Device is editing; navigation QA stopped.')
        if not settle or not state['pending']:return state
        if time.monotonic()>deadline:raise RuntimeError('Native navigation request did not settle')
        time.sleep(.15)
def capture(name):
    if not args.screens or (args.capture_names and name not in args.capture_names):return
    from framebuffer import decode_framebuffer
    send('screenshot',raw=True);begin=wait('screen_begin');pixel_format=begin['format']
    rows={}
    for _ in range(240):
        row=wait('screen_row');raw=base64.b64decode(row['data'],validate=True);rows[row['y']]=raw
    wait('screen_end');assert set(rows)==set(range(240))
    args.screens.mkdir(parents=True,exist_ok=True)
    decode_framebuffer(rows,pixel_format).save(args.screens/(name+'.png'))
    print('Captured real framebuffer: '+name,flush=True)
def check(state,**expected):
    for key,value in expected.items():assert state[key]==value,(key,state[key],value)
    assert state['layoutOk'],'Actual font/layout overflow'

try:
    initial=inspect()
    if initial['page'] in (2,3,5):raise RuntimeError('Device is editing; no QA actions were sent.')
    deadline=time.monotonic()+20
    while True:
        send('connection-inspect');live=wait('connection-state')
        if live['middleware'] and live['responseAgeMs']<4000:break
        if time.monotonic()>deadline:raise RuntimeError('Actual middleware is unavailable')
        time.sleep(.2);initial=inspect()
    if args.display_language:
        send('settings-check',action='display',korean=args.display_language=='ko');time.sleep(.2)
        changed=inspect();assert changed['displayLanguage']==args.display_language
    state=action('content');check(state,page=0,focus='content')
    assert state['view'].get('connected'),'Actual middleware is unavailable'
    state=action('end');assert state['cursor']==max(0,state['view']['contentTotal']-12)
    state=action('home');assert state['cursor']==0
    capture('session-content')
    state=action('home');state=action('up');check(state,page=0,focus='top',crumb=4)
    state=action('select');check(state,page=1,focus='content');assert state['view']['menuKind']=='sessions'
    selected=state['view']['menuIndex'];total=state['view']['menuTotal'];assert total>0
    assert state['cursor']==selected and state['viewportStart']==state['view']['menuOffset']
    capture('session-list')
    state=action('end');assert state['cursor']==total-1
    assert state['viewportStart']==max(0,total-7)
    state=action('home');assert state['cursor']==0 and state['viewportStart']==0
    if total>7:
        state=action('pgdn');assert state['cursor']==7
        state=action('pgup');assert state['cursor']==0
    state=action('up');check(state,page=0,focus='top',crumb=4)
    state=action('up');check(state,page=0,focus='top',crumb=3)
    state=action('select');check(state,page=1,focus='content');assert state['view']['menuKind']=='projects'
    projects=state['view']['menuTotal'];assert projects>0
    capture('project-list')
    state=action('home');state=action('up');check(state,page=0,focus='top',crumb=3)
    state=action('up');check(state,page=0,focus='top',crumb=2)
    capture('harness-breadcrumb')
    state=action('select');check(state,page=1,focus='content');assert state['view']['menuKind']=='harnesses'
    harnesses=state['view']['menuTotal'];assert harnesses>0
    assert all(icon['size']==16 for icon in state['view']['itemIcons'] if icon)
    capture('harness-list')
    state=action('home');state=action('up');check(state,page=0,focus='top',crumb=2)
    state=action('up');check(state,page=0,focus='top',crumb=1)
    capture('machine-breadcrumb')
    state=action('select');check(state,page=1,focus='content');assert state['view']['menuKind']=='machines'
    assert state['view']['menuTotal']==1 # This loopback gateway observes one real host.
    state=action('select');assert state['view']['menuKind']=='harnesses'
    state=action('home');state=action('up');state=action('home');check(state,page=0,focus='top',crumb=0)
    state=action('select');check(state,page=8,focus='content')
    capture('settings')
    state=action('content');check(state,page=0,focus='content')
    if args.input_screens:
        for name,korean,keys,expected in [('english-input',False,'Hello Snowball','Hello Snowball'),('korean-input',True,'dkssudgktpdy gksrmf','안녕하세요 한글')]:
            before=inspect()
            if before['draft'] or before['page'] in (2,3,5):raise RuntimeError('User draft/editor detected; input QA stopped.')
            for attempt in range(20):
                if before['pending']:time.sleep(.15);before=inspect();continue
                send('ime-check',korean=korean,keys=keys);time.sleep(.15);before=inspect()
                if before['page']==2:break
            check(before,page=2,focus='content');assert before['draft']==expected and before['korean']==korean
            capture(name)
            after=inspect()
            if after['draft']!=expected or after['page']!=2:raise RuntimeError('Human input changed the draft; leaving it untouched.')
            while after['pending']:time.sleep(.15);after=inspect()
            send('clear-check');time.sleep(.15)
        send('ime-check',korean=initial['korean'],keys='');time.sleep(.15)
        restored=inspect()
        if restored['draft']:raise RuntimeError('Human input detected; leaving it untouched.')
        while restored['pending']:time.sleep(.15);restored=inspect()
        send('clear-check');time.sleep(.15)
        state=action('content');check(state,page=0,focus='content')
    print(json.dumps({'actualSessionCount':total,'actualHarnessCount':harnesses,
                      'focusHierarchy':True,'homeEndPaging':True,'fontLayout':True,
                      'nativePromptsSent':0}),flush=True)
finally:
    if args.display_language and 'initial' in globals():
        current=inspect()
        if not current['draft'] and current['page'] not in (2,3,5):
            send('settings-check',action='display',korean=initial['displayLanguage']=='ko')
            time.sleep(.2)
    port.close()
