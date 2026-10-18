#!/usr/bin/env python3
"""Build the native scheduling unit with the same headers as the Bionic ICD.

No APK, display, GPU or device modification. The resulting console test uses
mock XCB/gralloc and does not replace real WSI/game acceptance.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
for name in ('ndk', 'native-prefix', 'generated', 'headers', 'protocol-include', 'output'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--sanitize', action='store_true', help='Use Android AddressSanitizer/UBSan; stage the matching NDK ASan runtime to run')
p.add_argument('--address-only', action='store_true', help='Use AddressSanitizer without UBSan')
p.add_argument('--host', action='store_true', help='Build with host clang++/g++ and mocked XCB for sanitizer checks')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
cc = a.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++'
if a.host:
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('Install host clang++ or g++ first')
    cc = Path(compiler)
includes = [a.generated, a.native_prefix / 'include', a.protocol_include, a.headers,
            root / 'hybris/include', root / 'hybris/platforms/common', root / 'hybris/libsync/include']
if a.host:
    includes.remove(a.native_prefix / 'include')
flags = ['-O2', '-fPIC', '-D_GNU_SOURCE', '-DANDROID_BUILD', '-DHYBRIS_NO_SERVER_SIDE_BUFFERS',
         '-std=gnu++17', '-ffunction-sections', '-fdata-sections']
flags += ['-I' + str(i.resolve()) for i in includes]
if a.sanitize or a.address_only:
    flags += ['-fsanitize=' + ('address' if a.address_only else 'address,undefined'),
              '-fno-omit-frame-pointer']
    if not a.host or 'clang' in cc.name:
        flags += ['-fno-sanitize-trap=all']
subprocess.run([str(cc), *flags, '-c', str(root / 'hybris/platforms/common/nativewindowbase.cpp'),
                '-o', str(out / 'nativewindowbase.o')], check=True)
test = out / 'async-present-test'
subprocess.run([str(cc), *flags, '-static-libstdc++', str(root / 'tests/x11/async_present.cpp'),
                str(out / 'nativewindowbase.o'), '-Wl,--gc-sections,-z,max-page-size=16384',
                '-o', str(test)], check=True)
print(test)
