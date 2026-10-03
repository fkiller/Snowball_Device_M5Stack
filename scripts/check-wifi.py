"""Check real ESP32 AP scans without printing SSIDs or changing credentials.

This uses the same scan handler as the Wi-Fi menu. It refuses to interrupt
password entry or send confirmation; no native harness prompt is sent.
"""
import argparse
import json
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--rounds', type=int, default=1, choices=range(1, 6))
parser.add_argument('--require-aps', action='store_true')
parser.add_argument('--exercise-input', action='store_true',
                    help='Use disposable local keys to test password UI; never connects or submits.')
parser.add_argument('--password-screen', help='Save an actual masked password framebuffer during input QA.')
args = parser.parse_args()
if args.password_screen and not args.exercise_input:
    parser.error('--password-screen requires --exercise-input')
port = serial.Serial()
port.port, port.baudrate, port.timeout = args.port, 115200, .15
port.dtr = port.rts = False
port.open()
pending = b''

def send(kind, **fields):
    port.write((json.dumps({'type': kind, **fields}) + '\n').encode())

def wait(kind, timeout=15):
    global pending
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pending += port.read(4096)
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            try:
                value = json.loads(line)
            except ValueError:
                continue
            if value.get('type') == kind:
                return value
    raise RuntimeError('No real firmware response: ' + kind)

def verify_editing_guard():
    send('wifi-scan')
    refused = wait('wifi-networks', 6)
    assert refused['ok'] is False and refused['code'] == 'editing'
    send('inspect')
    retained = wait('inspection', 6)
    assert retained['page'] == 5, 'Scan interrupted password entry'
    print(json.dumps({'editingProtected': True, 'inputQaSkipped': 'existing user entry'}), flush=True)

try:
    for index in range(args.rounds):
        send('inspect')
        before = wait('inspection', 6)
        if before['page'] == 5:
            verify_editing_guard()
            raise SystemExit(0)
        if before['page'] == 3:
            raise RuntimeError('Device is editing or confirming a send; scan was not requested.')
        started = time.monotonic()
        send('wifi-scan')
        time.sleep(.15)
        # Repeated Scan while busy must preserve the same scan job.
        send('wifi-scan')
        result = wait('wifi-networks')
        elapsed = round(time.monotonic() - started, 3)
        if not result.get('ok') or result['code'] < 0:
            raise RuntimeError('Real scan failed or was refused: ' + str(result.get('code')))
        count = len(result['networks'])
        if args.require_aps and not count:
            raise RuntimeError('Real scan completed with zero visible APs.')
        send('inspect')
        after = wait('inspection', 6)
        assert not after['scanRunning'] and after['scanCount'] == count
        assert after['scanTotal'] == result['total']
        time.sleep(4)
        send('inspect')
        stable = wait('inspection', 6)
        assert stable['scanCount'] == count, 'AP snapshot changed without an explicit rescan'
        if stable['page'] == 4:
            assert stable['scanMessage'] == after['scanMessage'], 'Local scan status was overwritten'
        print(json.dumps({'round': index + 1, 'seconds': elapsed, 'deviceMilliseconds': result['durationMs'],
                          'visible': count, 'total': result['total'],
                          'attempts': result['attempts'], 'snapshotStable': True}), flush=True)
        # A physical user may have started entering a password during this check.
        if stable['page'] in (3, 5):
            print('Stopped additional scans because the device is editing.', flush=True)
            break
    if args.exercise_input:
        send('inspect')
        state = wait('inspection', 6)
        if state['page'] == 5:
            verify_editing_guard()
            raise SystemExit(0)
        if state['page'] != 4 or state['scanCount'] < 1:
            raise RuntimeError('Input QA requires the AP list; existing user input was left untouched.')
        # Select an actual scanned AP; the diagnostic never sends Enter in its
        # password screen, so it cannot connect or replace NVS credentials.
        send('wifi-key-check', key=13)
        send('inspect')
        state = wait('inspection', 6)
        if state['page'] != 5 or state['passwordLength'] != 0:
            raise RuntimeError('No empty password screen; user input was left untouched.')
        for key in 'qa-only-pass':
            send('wifi-key-check', key=ord(key))
        send('wifi-key-check', key=9)
        send('inspect')
        shown = wait('inspection', 6)
        assert shown['passwordVisible'] and shown['passwordLength'] == 12
        send('wifi-scan')
        refused = wait('wifi-networks', 6)
        assert refused['ok'] is False and refused['code'] == 'editing'
        time.sleep(4)
        send('inspect')
        retained = wait('inspection', 6)
        assert retained['page'] == 5 and retained['passwordLength'] == 12 and retained['passwordVisible']
        send('wifi-key-check', key=9)
        send('wifi-key-check', key=8)
        send('inspect')
        hidden = wait('inspection', 6)
        assert not hidden['passwordVisible'] and hidden['passwordLength'] == 11
        if args.password_screen:
            import base64
            from pathlib import Path
            from PIL import Image
            send('screenshot')
            rows = {}
            for _ in range(240):
                row = wait('screen_row', 8)
                raw = base64.b64decode(row['data'], validate=True)
                assert len(raw) == 960
                rows[row['y']] = raw
            assert set(rows) == set(range(240))
            wait('screen_end', 8)
            screen = Image.frombytes('RGB', (320, 240), b''.join(rows[y] for y in range(240)))
            out = Path(args.password_screen)
            out.parent.mkdir(parents=True, exist_ok=True)
            screen.save(out)
        send('wifi-key-check', key=27)
        print(json.dumps({'passwordShowHide': True, 'editingProtected': True,
                          'backspace': True, 'nativePromptsSent': 0}), flush=True)
finally:
    port.close()
