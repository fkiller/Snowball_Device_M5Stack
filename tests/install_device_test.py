"""Verify that PlatformIO actually sees the detected physical flash capacity."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from platformio.project.config import ProjectConfig

source = Path(__file__).resolve().parents[1] / 'scripts' / 'install_device.py'
spec = importlib.util.spec_from_file_location('install_device', source)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

class FlashConfig(unittest.TestCase):
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

if __name__ == '__main__':
    unittest.main()
