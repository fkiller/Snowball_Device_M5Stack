"""Verify that PlatformIO actually sees the detected physical flash capacity."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import struct
import sys
import subprocess
import re
from unittest.mock import patch
from platformio.project.config import ProjectConfig

source = Path(__file__).resolve().parents[1] / 'scripts' / 'install_device.py'
spec = importlib.util.spec_from_file_location('install_device', source)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
sys.path.insert(0, str(source.parent))
from os_wifi import profile_credentials

class FlashConfig(unittest.TestCase):
    @staticmethod
    def same_wifi_append():
        # NVS-format fixture only: no USB response, device or flash is simulated.
        before = bytearray(b'\xff' * 0x5000)
        before[:4] = struct.pack('<I', 0xfffffffe)
        entry = struct.pack('<BBBBI16sQ', 6, 4, 1, 255, 0x12345678, b'WIFI_STA_DEF', 7)
        before[64:96] = entry
        before[32] = 0xfe  # entry 0 written; remaining entries empty
        after = bytearray(before)
        after[32] = 0xf8  # entry 0 erased; entry 1 written
        after[96:128] = entry
        return before, after

    def test_nvs_accepts_exact_bytes_and_one_identical_wifi_driver_append(self):
        before, after = self.same_wifi_append()
        self.assertTrue(installer.nvs_preserved(before, before))
        self.assertTrue(installer.nvs_preserved(before, after))
        self.assertFalse(installer.nvs_preserved(after, before), 'not a forward append')

    def test_nvs_rejects_value_key_namespace_header_or_unrelated_byte_changes(self):
        before, after = self.same_wifi_append()
        for offset in (96, 97, 98, 100, 104, 120, 124, 4, 63, 4096, 160):
            with self.subTest(offset=offset):
                changed = bytearray(after)
                changed[offset] ^= 1
                self.assertFalse(installer.nvs_preserved(before, changed))
        for size in (0, 4096, 0x5000-1, 0x5000+1):
            self.assertFalse(installer.nvs_preserved(before, b'\xff'*size))

    def test_nvs_rejects_added_deleted_or_other_key_rewrites(self):
        before, after = self.same_wifi_append()
        added = bytearray(after); added[32] = 0xfa  # keep both entries written
        self.assertFalse(installer.nvs_preserved(before, added))
        removed = bytearray(after); removed[96:128] = b'\xff'*32; removed[32] = 0xfc
        self.assertFalse(installer.nvs_preserved(before, removed))
        other_before, other_after = bytearray(before), bytearray(after)
        other_before[72:88] = b'password' + b'\0'*8
        other_after[72:88] = other_after[104:120] = other_before[72:88]
        self.assertFalse(installer.nvs_preserved(other_before, other_after))

    def test_chunk_assembly_retries_only_failed_range_at_lower_baud_and_keeps_progress_monotonic(self):
        # Filesystem/assembler fault injection only; no device acceptance claim.
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / 'backup.bin'
            total, events, calls = 2 * 1024 * 1024 + 4096, [], []
            def reader(args, capture=False, on_output=None):
                self.assertIn('read_flash', args)
                self.assertEqual(args[args.index('--before') + 1], 'no_reset')
                self.assertEqual(args[args.index('--after') + 1], 'no_reset')
                offset, length = map(int, args[-3:-1])
                baud = args[args.index('--baud') + 1]
                calls.append((offset, length, baud))
                on_output(f'{length} (100 %)')
                if offset == 1024 * 1024 and baud == '460800':
                    raise subprocess.CalledProcessError(2, args, output='Corrupt data')
                Path(args[-1]).write_bytes(bytes([offset // (1024 * 1024)]) * length)
            with patch.object(installer, 'run', side_effect=reader):
                installer.read_flash_backup('metadata-only', backup, total, lambda phase, **data: events.append(data))
            self.assertEqual(calls, [(0, 1048576, '460800'), (1048576, 1048576, '460800'), (1048576, 1048576, '115200'), (2097152, 4096, '460800')])
            self.assertEqual(backup.read_bytes(), b'\0' * 1048576 + b'\1' * 1048576 + b'\2' * 4096)
            self.assertFalse(backup.with_suffix('.partial').exists())
            self.assertFalse(backup.with_suffix('.chunk').exists())
            counts = [event['completedBytes'] for event in events]
            self.assertEqual(counts, sorted(counts))
            self.assertEqual(counts[-1], total)

    def test_prewrite_nvs_check_requires_exact_backup_bytes_and_removes_private_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / 'backup.bin'
            original = b'\xff' * 0x9000 + bytes(range(256)) * 80
            def reader(args, **kwargs):
                self.assertEqual(args[args.index('--before')+1], 'no_reset')
                self.assertEqual(args[args.index('--after')+1], 'no_reset')
                self.assertIn('read_flash', args)
                Path(args[-1]).write_bytes(original[0x9000:0xe000])
            with patch.object(installer, 'run', side_effect=reader):
                installer.verify_nvs_before_flash('metadata-only', backup, original, 0x9000, 0x5000)
            self.assertFalse(backup.with_suffix('.nvs-before-write').exists())

    def test_prewrite_changed_nvs_reboots_existing_image_without_any_flash_write(self):
        # File/command-plan fault injection, never a simulated USB acceptance.
        with tempfile.TemporaryDirectory() as directory:
            backup, calls = Path(directory) / 'backup.bin', []
            original = b'\xff' * 0xe000
            def child(args, **kwargs):
                calls.append(args)
                if 'read_flash' in args:
                    changed = bytearray(original[0x9000:0xe000]); changed[0] ^= 1
                    Path(args[-1]).write_bytes(changed)
            with patch.object(installer, 'run', side_effect=child), patch.object(installer, 'read_hello') as hello:
                with self.assertRaisesRegex(RuntimeError, 'nvs_changed_before_flash_no_write'):
                    installer.verify_nvs_before_flash('metadata-only', backup, original, 0x9000, 0x5000)
            hello.assert_called_once_with('metadata-only', timeout=8, require_faces=False)
            self.assertEqual(len(calls), 2)
            self.assertIn('hard_reset', calls[-1])
            self.assertFalse(any('write_flash' in args for args in calls))
            self.assertFalse(backup.with_suffix('.nvs-before-write').exists())

    def test_prewrite_mismatch_does_not_claim_restart_when_native_hello_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / 'backup.bin'
            def child(args, **kwargs):
                if 'read_flash' in args:
                    Path(args[-1]).write_bytes(b'\0' * 0x5000)
                self.assertNotIn('write_flash', args)
            with patch.object(installer, 'run', side_effect=child), patch.object(installer, 'read_hello', side_effect=RuntimeError('no hello')):
                with self.assertRaisesRegex(RuntimeError, 'nvs_changed_before_flash_reboot_unconfirmed'):
                    installer.verify_nvs_before_flash('metadata-only', backup, b'\xff'*0xe000, 0x9000, 0x5000)
            self.assertFalse(backup.with_suffix('.nvs-before-write').exists())

    def test_nvs_still_rejects_deletion_of_last_wifi_driver_entry(self):
        before, _ = self.same_wifi_append()
        after = bytearray(before); after[32] = 0xfc
        self.assertFalse(installer.nvs_preserved(before, after))

    def test_exhausted_reads_never_publish_full_backup_or_start_firmware_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            backup, calls = Path(directory) / 'backup.bin', []
            def failing_child(args, **kwargs):
                calls.append(args)
                raise subprocess.CalledProcessError(2, args, output='Corrupt data')
            with patch.object(installer, 'run', side_effect=failing_child):
                with self.assertRaisesRegex(RuntimeError, 'backup_read_failed_reboot_unconfirmed'):
                    installer.read_flash_backup('metadata-only', backup, 1048576, lambda *args, **kwargs: None)
            self.assertFalse(backup.exists())
            self.assertFalse(any('write_flash' in args for args in calls))
            self.assertEqual(len([args for args in calls if 'read_flash' in args]), 2)
            self.assertIn('hard_reset', calls[-1])

    def test_backup_progress_parses_split_esptool_backspace_counters(self):
        events = []
        read = installer.BackupProgress(16777216, lambda phase, **data: events.append((phase, data)))
        for chunk in ['private tool header\n16777', '2 (0 %)\b\b\b', '167772 (1 %)\b', '335544 (1 %)\b', '335545 (2 %)\b', '16777216 (100 %)\n']:
            read(chunk)
        self.assertEqual([value['completedBytes'] for _, value in events], [167772, 335544, 335545, 16777216])
        self.assertTrue(all(phase == 'backup' and set(value) == {'completedBytes', 'bytes', 'elapsedSeconds'} for phase, value in events))
        self.assertTrue(all(value['bytes'] == 16777216 and value['elapsedSeconds'] >= 0 for _, value in events))

    def test_backup_progress_rejects_inconsistent_overflow_and_regressing_values(self):
        events = []
        read = installer.BackupProgress(1000, lambda phase, **data: events.append(data))
        read('100 (10 %)\b')
        read('90 (9 %)\b1001 (100 %)\b200 (99 %)\b')
        read('secret profile material' * 1000)
        self.assertLessEqual(len(read.buffer), 128)
        read('200 (20 %)\b')
        self.assertEqual([value['completedBytes'] for value in events], [100, 200])

    def test_stream_runner_drains_actual_child_output_without_forwarding_it(self):
        chunks = []
        result = installer.run(['-c', "import sys;sys.stdout.write('100 (10 %)\\b');sys.stdout.flush();sys.stderr.write('diagnostic')"], capture=True, on_output=chunks.append)
        self.assertIn('100 (10 %)', ''.join(chunks))
        self.assertIn('diagnostic', result)

    def test_stream_runner_propagates_real_child_failure(self):
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            installer.run(['-c', 'import sys;sys.exit(7)'], capture=True, on_output=lambda chunk: None)
        self.assertEqual(raised.exception.returncode, 7)

    def test_actual_platformio_config_for_each_supported_board(self):
        for size in (4, 16):
            with self.subTest(size=size), tempfile.TemporaryDirectory() as directory:
                file = Path(directory) / 'platformio.ini'
                file.write_text(installer.build_config(size), encoding='utf-8')
                parsed = ProjectConfig(str(file))
                self.assertEqual(parsed.get('env:m5stack-core-esp32', 'board_upload.flash_size'), f'{size}MB')
                self.assertEqual(parsed.get('env:m5stack-core-esp32', 'board'), 'm5stack-core-esp32')
                self.assertEqual(parsed.get('env:m5stack-core-esp32', 'board_build.partitions'), 'huge_app.csv')

    def test_unsupported_capacity_never_creates_a_flash_profile(self):
        with self.assertRaises(ValueError):
            installer.build_config(8)

    def test_partition_parser_reads_the_real_nvs_shape(self):
        table = struct.pack('<HBBII16sI', 0x50aa, 1, 2, 0x9000, 0x5000, b'nvs', 0)
        self.assertEqual(installer.nvs_partition(table), (0x9000, 0x5000))
        self.assertIsNone(installer.nvs_partition(b'\xff' * 4096))

    def test_actual_built_plan_preserves_nvs_and_rejects_a_changed_layout(self):
        file = source.parents[1] / '.pio/build/m5stack-core-esp32/flash-plan.json'
        if not file.exists():
            self.skipTest('Run the real firmware build first')
        plan = json.loads(file.read_text(encoding='utf-8'))
        original = bytearray(4 * 1024 * 1024)
        table = next(Path(i['path']).read_bytes() for i in plan['images'] if i['offset'] == 0x8000)
        original[0x8000:0x8000 + len(table)] = table
        self.assertEqual(installer.validate_flash_plan(plan, original, len(original)), (0x9000, 0x5000))
        original[0x8004:0x8008] = struct.pack('<I', 0xa000)
        with self.assertRaisesRegex(RuntimeError, 'nvs_layout_incompatible'):
            installer.validate_flash_plan(plan, original, len(original))
        changed = json.loads(json.dumps(plan))
        changed['images'][0]['offset'] = 0x9000
        with self.assertRaises(RuntimeError):
            installer.validate_flash_plan(changed, original, len(original))

    def test_os_profile_refuses_encrypted_or_enterprise_keys_and_wrong_ssid(self):
        xml = '<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1"><SSIDConfig><SSID><name>fixture</name></SSID></SSIDConfig><MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>fixture-only</keyMaterial></sharedKey></security></MSM></WLANProfile>'
        self.assertEqual(profile_credentials(xml, 'fixture'), 'fixture-only')
        self.assertEqual(profile_credentials(xml.replace('>fixture-only<', '> fixture-only <'), 'fixture'), ' fixture-only ')
        for invalid in (xml.replace('<protected>false', '<protected>true'), xml.replace('<useOneX>false', '<useOneX>true')):
            with self.assertRaises(RuntimeError):
                profile_credentials(invalid, 'fixture')
        with self.assertRaises(RuntimeError):
            profile_credentials(xml, 'different')

    def test_release_and_compiled_header_agree(self):
        root = source.parents[1]
        release = json.loads((root / 'firmware/release.json').read_text(encoding='utf-8'))
        self.assertIn('"' + release['version'] + '"', (root / 'firmware/version.h').read_text(encoding='utf-8'))

    def test_real_compiled_pairing_path_has_loop_stack_headroom(self):
        root = source.parents[1]
        report = root / '.pio/build/m5stack-core-esp32/src/main.cpp.su'
        if not report.exists():
            self.skipTest('Run the real firmware build with stack-usage output first')
        # Compiler frames, rather than guessed source-local sizes. Diagnostic
        # navigation is the deepest UI entry; reserve 4 KiB for SDK/crypto calls
        # not represented in this translation unit. Hardware high-water QA is
        # still required; this guards the reproduced 8 KiB regression.
        frames = {}
        for line in report.read_text(encoding='utf-8').splitlines():
            name, size, kind = line.rsplit('\t', 2)
            frames[name] = (int(size), kind)
        path = ['void loop()', 'void handleSerial(', 'void select()',
                'bool pairCandidate(', 'bool pairingHttp(',
                'bool snowball::LanKeyExchange::decrypt(']
        used = 0
        for name in path:
            found = [value for signature, value in frames.items() if name in signature]
            self.assertEqual(len(found), 1, name)
            self.assertEqual(found[0][1], 'static', name)
            used += found[0][0]
        for capacity in (4, 16):
            config = installer.build_config(capacity)
            match = re.search(r'-DARDUINO_LOOP_STACK_SIZE=(\d+)', config)
            self.assertIsNotNone(match, 'The real install build must retain the stack budget')
            self.assertLessEqual(used + 4096, int(match.group(1)))

if __name__ == '__main__':
    unittest.main()
