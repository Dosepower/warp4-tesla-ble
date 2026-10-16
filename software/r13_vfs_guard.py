# Read-only PlatformIO binding guard; no outputs, package edits, or cleanup.
from pathlib import Path
import sys
Import('env')
sys.path.insert(0, str(Path(env.subst('$PROJECT_DIR')) / 'tools'))
from r13_vfs_binding import BINDING, verified

platform = env.PioPlatform()
core = platform.get_package_dir('framework-arduinoespressif32')
sdk = platform.get_package_dir('framework-arduinoespressif32-libs')
if verified(core, sdk):
    env.Append(CPPDEFINES=[('WARP_BLUETOOTH_LITTLEFS_BINDING', hex(BINDING))])
else:
    print('R13: unverified LittleFS/VFS artifacts; Bluetooth master persistence unavailable')
