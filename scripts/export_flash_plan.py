"""Export the real pinned PlatformIO image addresses for the trusted updater."""
import json
from pathlib import Path
Import('env')

def export_plan(source, target, env):
    directory = Path(env.subst('$BUILD_DIR'))
    images = [{'offset': int(str(offset), 0), 'path': str(Path(env.subst(str(file))).resolve())}
              for offset, file in env.get('FLASH_EXTRA_IMAGES', [])]
    images.append({'offset': int(env.subst('$ESP32_APP_OFFSET'), 0),
                   'path': str((directory / (env.subst('$PROGNAME') + '.bin')).resolve())})
    (directory / 'flash-plan.json').write_text(json.dumps({'images': images}), encoding='utf-8')

env.AddPostAction('buildprog', export_plan)
