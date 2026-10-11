"""Verify that PlatformIO actually sees the detected physical flash capacity."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import struct
import sys
import subprocess
from platformio.project.config import ProjectConfig

source = Path(__file__).resolve().parents[1] / 'scripts' / 'install_device.py'
spec = importlib.util.spec_from_file_location('install_device', source)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
sys.path.insert(0, str(source.parent))
from os_wifi import profile_credentials

class FlashConfig(unittest.TestCase):
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

if __name__ == '__main__':
    unittest.main()
