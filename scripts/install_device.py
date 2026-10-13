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

def run(args, capture=False, on_output=None):
    if on_output is not None:
        # esptool 4 emits byte counts separated by backspaces, not lines.
        # Drain available chunks while it runs; never expose its raw output.
        with subprocess.Popen([sys.executable, '-u', *args], cwd=ROOT,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0) as child:
            tail = ''
            while chunk := child.stdout.read1(4096):
                text = chunk.decode('utf-8', errors='replace')
                tail = (tail + text)[-65536:]
                on_output(text)
            code = child.wait()
            if code:
                raise subprocess.CalledProcessError(code, child.args, output=tail)
            return tail
    return subprocess.run([sys.executable, *args], cwd=ROOT, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None,
                          creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0).stdout


class BackupProgress:
    """Export only validated, monotonic esptool read counters, once per percent."""
    def __init__(self, total, progress):
        self.total, self.progress = total, progress
        self.buffer, self.completed, self.percent = '', 0, -1
        self.started = time.monotonic()

    def __call__(self, chunk):
        self.buffer += chunk
        matches = list(re.finditer(r'(\d+)\s*\(\s*(\d+)\s*%\s*\)', self.buffer))
        for match in matches:
            completed, percent = map(int, match.groups())
            if not self.completed <= completed <= self.total or percent != completed * 100 // self.total:
                continue
            self.completed = completed
            if percent > self.percent:
                self.percent = percent
                self.progress('backup', completedBytes=completed, bytes=self.total,
                              elapsedSeconds=int(time.monotonic() - self.started))
        self.buffer = self.buffer[matches[-1].end():][-128:] if matches else self.buffer[-128:]


def read_flash_backup(port, backup, total, progress):
    """Read bounded, esptool-digest-checked chunks before publishing a full backup."""
    partial, chunk = backup.with_suffix('.partial'), backup.with_suffix('.chunk')
    started, reported = time.monotonic(), 0
    def report(completed):
        nonlocal reported
        reported = max(reported, completed)
        progress('backup', completedBytes=reported, bytes=total,
                 elapsedSeconds=int(time.monotonic() - started))
    try:
        with partial.open('xb') as output:
            for offset in range(0, total, 1024 * 1024):
                length = min(1024 * 1024, total - offset)
                for baud in (460800, 115200):
                    chunk.unlink(missing_ok=True)
                    counters = BackupProgress(length, lambda phase, **data: report(offset + data['completedBytes']))
                    try:
                        run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--baud', str(baud),
                             '--after', 'no_reset', 'read_flash', str(offset), str(length), str(chunk)],
                            capture=True, on_output=counters)
                    except subprocess.CalledProcessError:
                        if baud == 115200:
                            raise
                        continue
                    if chunk.stat().st_size != length:
                        raise RuntimeError('backup_incomplete')
                    output.write(chunk.read_bytes())
                    report(offset + length)
                    break
        if partial.stat().st_size != total:
            raise RuntimeError('backup_incomplete')
        partial.replace(backup)
    except Exception as error:
        # No write can have started here. Restore the existing runtime after a
        # failed read so the next reviewed attempt can obtain its real hello.
        try:
            run(['-m', 'esptool', '--port', port, '--chip', 'esp32', '--after', 'hard_reset', 'run'], capture=True)
            read_hello(port, timeout=8, require_faces=False)
        except Exception:
            raise RuntimeError('backup_read_failed_reboot_unconfirmed') from error
        raise RuntimeError('backup_read_failed') from error
    finally:
        chunk.unlink(missing_ok=True)

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

def nvs_preserved(before, after):
    """Retain every byte, except one proven identical SDK Wi-Fi append.

    ESP-IDF can append WIFI_STA_DEF and mark its identical previous entry
    erased. This changes storage bookkeeping without changing any value.
    Do not accept general compaction, new keys, changed values or blob moves.
    """
    if len(before) != 0x5000 or len(after) != len(before):
        return False
    if before == after:
        return True
    removed, added = [], []
    for page in range(5):
        old, new = before[page*4096:(page+1)*4096], after[page*4096:(page+1)*4096]
        if old == new:
            continue
        if old[:32] != new[:32] or int.from_bytes(old[:4], 'little') not in (0xfffffffe, 0xfffffffc) or (old[63] & 0xf0) != (new[63] & 0xf0):
            return False
        for index in range(126):
            start = 64 + index*32
            a, b = old[start:start+32], new[start:start+32]
            sa = (old[32+index//4] >> (2*(index % 4))) & 3
            sb = (new[32+index//4] >> (2*(index % 4))) & 3
            if sa == sb and a == b:
                continue
            if (sa, sb) == (2, 0) and a == b:
                removed.append((page, index, a))
            elif (sa, sb) == (3, 2) and a == b'\xff'*32:
                added.append((page, index, b))
            else:
                return False
    if len(removed) != 1 or len(added) != 1:
        return False
    page, index, entry = removed[0]
    return (added[0][0] == page and added[0][1] > index and added[0][2] == entry
            and 0 < entry[0] < 255 and entry[1:3] == b'\x04\x01'
            and entry[8:24].split(b'\0', 1)[0] == b'WIFI_STA_DEF')


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
    progress('backup', backup=str(backup), bytes=size_mb * 1024 * 1024, completedBytes=0, elapsedSeconds=0)
    read_flash_backup(port, backup, size_mb * 1024 * 1024, progress)
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
        if not nvs_preserved(original[nvs_start:nvs_start + nvs_size], retained.read_bytes()):
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
    included = json.loads((ROOT / 'firmware/release.json').read_text(encoding='utf-8'))['version']
    if hello.get('firmware') not in ('0.3.0', '0.4.0', '0.5.0', included):
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
