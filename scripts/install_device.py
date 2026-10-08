"""Install on a real original ESP32 M5Stack, retaining a full private flash backup."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
import webbrowser
import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1]

def build_config(size_mb):
    if size_mb not in (4, 16):
        raise ValueError('Unsupported original Core flash size')
    # extra_configs is applied after its parent INI, so importing the 16MB
    # profile would overwrite a 4MB override. Materialize the original profile.
    original = (ROOT / 'platformio.ini').read_text(encoding='utf-8')
    result, count = re.subn(r'^board_upload\.flash_size\s*=.*$', f'board_upload.flash_size = {size_mb}MB', original, flags=re.M)
    if count != 1:
        raise RuntimeError('Expected one reviewed flash-size setting')
    return result

def run(args, capture=False):
    return subprocess.run([sys.executable, *args], cwd=ROOT, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None).stdout

def select_port(explicit):
    if explicit:
        return explicit
    # VID/PID is only a candidate filter. Interrogate the ESP32 and check the
    # installed firmware hello below; never choose the first of several UARTs.
    candidates = [p.device for p in list_ports.comports() if p.vid == 0x10c4 and p.pid == 0xea60]
    if not candidates and sys.platform == 'win32':
        print('No CP210x UART is visible. Connect M5Stack by USB; if Windows needs a driver, install the official Silicon Labs VCP driver from the opened page.')
        webbrowser.open('https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers')
        input('After connecting the board/installing its driver, press Enter to detect again: ')
        candidates = [p.device for p in list_ports.comports() if p.vid == 0x10c4 and p.pid == 0xea60]
    if len(candidates) != 1:
        raise RuntimeError('Connect exactly one original M5Stack USB UART, or supply --port COMx (/dev/ttyUSBx). Install the Silicon Labs CP210x driver if no UART appears.')
    return candidates[0]

def read_hello(port, timeout=25):
    device = serial.Serial()
    device.port, device.baudrate, device.timeout = port, 115200, 1
    device.dtr = device.rts = False
    device.open()
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            device.write(b'{"type":"probe"}\n')
            line = device.readline(32769)
            try:
                data = json.loads(line)
                if data.get('type') == 'hello' and data.get('board') == 'M5Stack' and re.fullmatch(r'm5-[a-f0-9]{12}', data.get('deviceId', '')):
                    if not data.get('faces'):
                        raise RuntimeError('The real FACES keyboard was not detected. Check its connection.')
                    return data
            except (ValueError, UnicodeDecodeError):
                pass
    finally:
        device.close()
    raise RuntimeError('No M5Stack firmware hello received. Device installation is incomplete.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--no-flash', action='store_true', help='Require an already installed, working Snowball firmware')
    args = parser.parse_args()
    port = select_port(args.port)
    backup = None
    if not args.no_flash:
        probe = run(['-m', 'esptool', '--port', port, '--chip', 'esp32', 'flash_id'], capture=True)
        match = re.search(r'Detected flash size:\s*(4|16)MB', probe)
        mac = re.search(r'MAC:\s*([0-9a-f:]{17})', probe, re.I)
        if not match or not mac or 'ESP32' not in probe:
            raise RuntimeError('This installer supports verified original ESP32 Core boards with 4MB or 16MB flash only.')
        size_mb = int(match[1])
        directory = ROOT / 'artifacts' / 'backups'
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        backup = directory / (mac[1].replace(':', '') + '-' + uuid.uuid4().hex + '.bin')
        print(f'Backing up all {size_mb}MB from {port} before writing firmware...')
        run(['-m', 'esptool', '--port', port, '--chip', 'esp32', 'read_flash', '0', str(size_mb * 1024 * 1024), str(backup)])
        if backup.stat().st_size != size_mb * 1024 * 1024:
            raise RuntimeError('Incomplete flash backup; firmware was not written.')
        backup.with_suffix('.json').write_text(json.dumps({'mac': mac[1], 'flashBytes': backup.stat().st_size,
            'sha256': hashlib.sha256(backup.read_bytes()).hexdigest()}, indent=2) + '\n', encoding='utf-8')
        # Generate a local override; do not modify the tracked PlatformIO profile.
        config = ROOT / '.local' / 'install-platformio.ini'
        config.parent.mkdir(exist_ok=True, mode=0o700)
        config.write_text(build_config(size_mb), encoding='utf-8')
        run(['-m', 'platformio', 'run', '--project-conf', str(config)])
        run(['-m', 'platformio', 'run', '--project-conf', str(config), '--target', 'upload', '--upload-port', port])
    hello = read_hello(port)
    if hello.get('firmware') != '0.3.0':
        raise RuntimeError('Snowball firmware 0.3.0 is required for multiple PCs. Rerun without --no-flash to install it; a full flash backup is made first.')
    args.output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    args.output.write_text(json.dumps({'port': port, 'deviceId': hello['deviceId'], 'flashBytes': hello.get('flashBytes'),
        'faces': True, 'backup': str(backup) if backup else None}) + '\n', encoding='utf-8')
    print(f"Verified physical M5Stack {hello['deviceId']} with FACES keyboard on {port}.")

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, serial.SerialException) as error:
        sys.exit(str(error))
