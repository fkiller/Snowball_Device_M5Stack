"""Install on a real original ESP32 M5Stack, retaining a full private flash backup."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
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
                          stderr=subprocess.STDOUT if capture else None,
                          creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0).stdout

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

def read_hello(port, timeout=25, require_faces=True):
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
                    if require_faces and not data.get('faces'):
                        raise RuntimeError('The real FACES keyboard was not detected. Check its connection.')
                    return data
            except (ValueError, UnicodeDecodeError):
                pass
    finally:
        device.close()
    raise RuntimeError('No M5Stack firmware hello received. Device installation is incomplete.')

def nvs_partition(table):
    for offset in range(0, min(len(table), 0xc00), 32):
        record = table[offset:offset + 32]
        if len(record) != 32 or record[:2] != b'\xaa\x50':
            break
        magic, kind, subtype, start, size, label, flags = struct.unpack('<HBBII16sI', record)
        if kind == 1 and subtype == 2 and label.rstrip(b'\0') == b'nvs':
            return start, size
    return None

def validate_flash_plan(plan, original, capacity):
    images = plan.get('images', [])
    if len(images) != 4 or sorted(i['offset'] for i in images) != [0x1000, 0x8000, 0xe000, 0x10000]:
        raise RuntimeError('firmware_layout_unreviewed')
    partitions = next(Path(i['path']).read_bytes() for i in images if i['offset'] == 0x8000)
    saved = nvs_partition(original[0x8000:0x9000])
    target = nvs_partition(partitions)
    if target != (0x9000, 0x5000) or (saved is not None and saved != target):
        raise RuntimeError('nvs_layout_incompatible')
    ranges = []
    for image in images:
        start, size = image['offset'], Path(image['path']).stat().st_size
        end = start + ((size + 4095) // 4096) * 4096
        if size < 1 or start % 4096 or end > capacity or (start < target[0] + target[1] and end > target[0]):
            raise RuntimeError('firmware_storage_overlap')
        if any(start < b and end > a for a, b in ranges):
            raise RuntimeError('firmware_storage_overlap')
        ranges.append((start, end))
    return target

def install_firmware(port, backup_directory, progress=lambda phase, **details: None):
    # Nothing below is invoked by detection. The UI/CLI must explicitly approve.
    progress('identify')
    probe = run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--after', 'no_reset', 'flash_id'], capture=True)
    match = re.search(r'Detected flash size:\s*(4|16)MB', probe)
    mac = re.search(r'MAC:\s*([0-9a-f:]{17})', probe, re.I)
    if not match or not mac or 'ESP32' not in probe:
        raise RuntimeError('original_core_required')
    size_mb = int(match[1])
    backup_directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    backup = backup_directory / (mac[1].replace(':', '') + '-' + uuid.uuid4().hex + '.bin')
    progress('backup', backup=str(backup), bytes=size_mb * 1024 * 1024)
    run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--baud', '460800', '--after', 'no_reset', 'read_flash', '0', str(size_mb * 1024 * 1024), str(backup)], capture=True)
    original = backup.read_bytes()
    if len(original) != size_mb * 1024 * 1024:
        raise RuntimeError('backup_incomplete')
    backup.with_suffix('.json').write_text(json.dumps({'mac': mac[1], 'flashBytes': len(original),
        'sha256': hashlib.sha256(original).hexdigest()}, indent=2) + '\n', encoding='utf-8')
    backup.chmod(0o600)
    backup.with_suffix('.json').chmod(0o600)
    progress('backup-verified', backup=str(backup))
    progress('build')
    config = ROOT / '.local' / 'install-platformio.ini'
    config.parent.mkdir(exist_ok=True, mode=0o700)
    config.write_text(build_config(size_mb), encoding='utf-8')
    run(['-m', 'platformio', 'run', '--project-conf', str(config)], capture=True)
    plan = json.loads((ROOT / '.pio/build/m5stack-core-esp32/flash-plan.json').read_text(encoding='utf-8'))
    nvs_start, nvs_size = validate_flash_plan(plan, original, len(original))
    progress('flash')
    images = [value for image in sorted(plan['images'], key=lambda i: i['offset'])
              for value in (hex(image['offset']), image['path'])]
    # Keep the app stopped until the retained NVS bytes have been verified.
    run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--baud', '460800', '--after', 'no_reset',
         'write_flash', '--flash_size', f'{size_mb}MB', *images], capture=True)
    progress('verify-nvs')
    retained = backup.with_suffix('.nvs-check')
    try:
        run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--baud', '460800', '--after', 'no_reset', 'read_flash', hex(nvs_start), str(nvs_size), str(retained)], capture=True)
        if retained.read_bytes() != original[nvs_start:nvs_start + nvs_size]:
            raise RuntimeError('nvs_preservation_failed_restore_backup')
    finally:
        retained.unlink(missing_ok=True)
    progress('reboot')
    run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--after', 'hard_reset', 'run'], capture=True)
    return backup

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--no-flash', action='store_true', help='Require an already installed, working Snowball firmware')
    args = parser.parse_args()
    port = select_port(args.port)
    backup = None
    if not args.no_flash:
        backup = install_firmware(port, ROOT / 'artifacts/backups', lambda phase, **details: print('Firmware: ' + phase, flush=True))
    hello = read_hello(port)
    if hello.get('firmware') not in ('0.3.0', '0.4.0', '0.5.0'):
        raise RuntimeError('Unsupported Snowball firmware. LAN pairing requires 0.4.0 or later; desktop Wi-Fi migration requires 0.5.0.')
    args.output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    args.output.write_text(json.dumps({'port': port, 'deviceId': hello['deviceId'], 'flashBytes': hello.get('flashBytes'),
        'faces': True, 'firmware': hello.get('firmware'), 'lanPairing': hello.get('lanPairing') is True,
        'backup': str(backup) if backup else None}) + '\n', encoding='utf-8')
    print(f"Verified physical M5Stack {hello['deviceId']} with FACES keyboard on {port}.")

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, serial.SerialException) as error:
        sys.exit(str(error))
