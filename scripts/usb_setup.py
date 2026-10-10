"""Private JSON-line bridge for the native USB setup window. No raw UART logs."""
import argparse
import json
from pathlib import Path
import re
import sys
import time
import serial
from serial.tools import list_ports
from install_device import install_firmware, read_hello, ROOT
from os_wifi import current_wifi

def emit(event, **data):
    print(json.dumps({'event': event, **data}, ensure_ascii=False), flush=True)

def public_hello(hello):
    keys = ('deviceId', 'firmware', 'faces', 'flashBytes', 'lanPairing', 'usbSetup', 'wifiConfigured', 'wifiConnected', 'wifiSsid')
    return {key: hello[key] for key in keys if key in hello}

def candidates():
    return [{'port': p.device, 'label': (p.description or 'CP210x USB UART')[:128], 'usbIdentity': p.serial_number or ''}
            for p in list_ports.comports() if p.vid == 0x10c4 and p.pid == 0xea60]

def migrate(port, approved, expected_device):
    hello = read_hello(port, timeout=4, require_faces=False)
    if hello.get('deviceId') != expected_device:
        raise RuntimeError('usb_device_changed')
    if hello.get('usbSetup') is not True or hello.get('wifiConfigured') is not False:
        raise RuntimeError('wifi_already_saved_or_upgrade_required')
    network = current_wifi(secret=True)
    if network['ssid'] != approved:
        raise RuntimeError('wifi_changed_review_again')
    password = network.get('password')
    if password is None or len(password.encode('utf-8')) > 63:
        raise RuntimeError('wifi_key_unavailable')
    port_handle = serial.Serial()
    port_handle.port, port_handle.baudrate, port_handle.timeout = port, 115200, 0.5
    port_handle.dtr = port_handle.rts = False
    port_handle.open()
    try:
        # No secret in argv, stdout, progress, or desktop state. Persisted only
        # by the device after the real association reaches GOT_IP.
        port_handle.write((json.dumps({'type': 'wifi-provision', 'onlyIfEmpty': True,
            'ssid': approved, 'password': password}) + '\n').encode('utf-8'))
        del password, network
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            port_handle.write(b'{"type":"probe"}\n')
            try:
                reply = json.loads(port_handle.readline(32769))
                if reply.get('type') == 'wifi-provision-result' and reply.get('ok') is False:
                    raise RuntimeError('wifi_already_saved')
                if reply.get('type') == 'hello' and reply.get('deviceId') == hello['deviceId'] and reply.get('wifiConfigured') is True and reply.get('wifiConnected') is True and reply.get('wifiSsid') == approved:
                    return public_hello(reply)
            except (ValueError, UnicodeDecodeError):
                pass
        raise RuntimeError('wifi_connection_unconfirmed')
    finally:
        port_handle.close()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--command', choices=('list', 'probe', 'install', 'wifi', 'migrate'), required=True)
    parser.add_argument('--port')
    parser.add_argument('--backup-dir', type=Path)
    args = parser.parse_args()
    if args.command == 'list':
        emit('ports', ports=candidates())
        return
    if args.command == 'wifi':
        try:
            emit('wifi', network=current_wifi())
        except Exception:
            emit('wifi', network=None, code='wifi_unavailable_or_permission_required')
        return
    if not args.port or not any(p['port'] == args.port for p in candidates()):
        raise RuntimeError('usb_device_disconnected')
    if args.command == 'probe':
        try:
            hello = read_hello(args.port, timeout=3, require_faces=False)
        except RuntimeError:
            emit('device', status='unrecognized', hello=None)
        else:
            emit('device', status='snowball', hello=public_hello(hello))
    elif args.command == 'install':
        request = json.loads(sys.stdin.readline(4097))
        release = json.loads((ROOT / 'firmware/release.json').read_text(encoding='utf-8'))
        if request.get('version') != release['version'] or request.get('confirmBoard') is not True or not args.backup_dir or not args.backup_dir.is_absolute():
            raise RuntimeError('firmware_review_required')
        selected = next(p for p in candidates() if p['port'] == args.port)
        if request.get('usbIdentity') and request['usbIdentity'] != selected['usbIdentity']:
            raise RuntimeError('usb_device_changed')
        if request.get('deviceId'):
            before = read_hello(args.port, timeout=4, require_faces=False)
            if before['deviceId'] != request['deviceId']:
                raise RuntimeError('usb_device_changed')
        backup = install_firmware(args.port, args.backup_dir, lambda phase, **details: emit('progress', phase=phase, **details))
        hello = read_hello(args.port, require_faces=False)
        if hello.get('firmware') != release['version']:
            raise RuntimeError('firmware_boot_unconfirmed')
        emit('device', status='snowball', hello=public_hello(hello), backup=str(backup))
    else:
        request = json.loads(sys.stdin.readline(4097))
        if not isinstance(request.get('ssid'), str):
            raise RuntimeError('wifi_review_required')
        emit('device', status='snowball', hello=migrate(args.port, request['ssid'], request.get('deviceId')))

if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Driver output and exception values can contain profile material.
        # Export only reviewed error identifiers, never raw external messages.
        code = str(error)
        emit('error', code=code if re.fullmatch(r'[a-z][a-z0-9_]{1,79}', code) else 'usb_operation_failed')
        sys.exit(1)
