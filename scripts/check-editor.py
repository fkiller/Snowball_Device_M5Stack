"""Explicit disposable input QA on the real firmware; never sends a prompt.

Exercises the same editor/key handlers used by A/C and FACES. Refuses an
existing draft or editor. These checks do not certify physical keypresses.
"""
import argparse
import json
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
args = parser.parse_args()
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, .15
port.dtr = port.rts = False
port.open()
buffer = b''

def send(kind, **fields):
    port.write((json.dumps({'type': kind, **fields}) + '\n').encode())

def inspect():
    global buffer
    send('inspect')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        buffer += port.read(4096)
        while b'\n' in buffer:
            line, buffer = buffer.split(b'\n', 1)
            try:
                state = json.loads(line)
            except ValueError:
                continue
            if state.get('type') == 'inspection':
                return state
    raise RuntimeError('No actual firmware inspection')

def settled():
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        state = inspect()
        if not state['pending'] and (state.get('view') or {}).get('connected'):
            assert state['layoutOk'], 'Actual font/layout overflow'
            return state
        time.sleep(.15)
    raise RuntimeError('Real middleware/device request did not settle')

def action(kind, **fields):
    send(kind, **fields)
    time.sleep(.15)
    return settled()

def expect(state, text, caret):
    assert state['page'] == 2, 'Prompt Edit not active'
    assert state['draft'] == text, 'Unexpected input; stopped without clearing'
    assert state['editCaret'] == caret, 'Incorrect UTF-8 cursor position'

try:
    initial = settled()
    if initial['draft'] or initial['page'] in (2, 3, 5):
        raise RuntimeError('Existing user draft/editor; no QA actions were sent')
    action('nav-check', action='content')
    action('nav-check', action='end')
    expect(action('nav-check', action='down'), '', 0)
    expect(action('ime-check', korean=True, keys='gksrmf'), '한글', 6)
    expect(action('editor-check', action='left'), '한글', 3)
    action('editor-check', action='key', key=9)
    expect(action('editor-check', action='key', key=ord('X')), '한X글', 4)
    expect(action('editor-check', action='key', key=8), '한글', 3)
    expect(action('editor-check', action='home'), '한글', 0)
    expect(action('editor-check', action='key', key=ord('A')), 'A한글', 1)
    expect(action('editor-check', action='end'), 'A한글', 7)
    expect(action('editor-check', action='hold-left'), 'A한글', 4)
    expect(action('editor-check', action='hold-left'), 'A한글', 1)
    expect(action('editor-check', action='hold-right'), 'A한글', 4)
    expect(action('editor-check', action='end'), 'A한글', 7)
    expect(action('editor-check', action='key', key=ord('Z')), 'A한글Z', 8)
    action('clear-check')
    # A navigation letter typed while reading must become literal input.
    expect(action('editor-check', action='key', key=ord('W')), 'W', 1)
    action('clear-check')
    action('ime-check', korean=initial['korean'], keys='')
    final = action('clear-check')
    assert not final['draft'] and final['page'] == 0
    assert final['korean'] == initial['korean']
    assert final['view']['sessionKey'] == initial['view']['sessionKey']
    print(json.dumps({'endEntersEditor': True, 'readingKeyEntersEditor': True,
                     'utf8Cursor': True, 'holdRepeat': True, 'homeEnd': True,
                     'languageRestored': True, 'nativePromptsSent': 0}))
finally:
    port.close()
