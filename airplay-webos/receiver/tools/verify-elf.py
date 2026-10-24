#!/usr/bin/env python3
"""Reject a mismatched ARM ABI or dependencies newer than this TV's libc/runtime.

This is a preflight, not proof that the proprietary media APIs work. The on-TV
loader check and playback probe are also required (see docs/VALIDATION.md).
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
READELF = ROOT / '.cache/arm-webos-linux-gnueabi_sdk-buildroot/bin/arm-webos-linux-gnueabi-readelf'

def output(*args):
    return subprocess.check_output([str(READELF), *map(str,args)],text=True)

def version(v):
    return tuple(map(int, v.split('.')))

def main():
    stage = pathlib.Path(sys.argv[1])
    binary = sys.argv[2] if len(sys.argv) > 2 else 'uxair'
    if binary not in ('uxair', 'hevc-probe'):
        sys.exit('Unsupported binary name')
    files = [stage / binary, stage / 'lib/libstdc++.so.6']
    exported = output('-V', files[1]).split('Version needs section')[0]
    max_glibc = (0, 0)
    for file in files:
        header = output('-h', file)
        if 'ELF32' not in header or not re.search(r'Machine:\s+ARM\b', header) or 'soft-float ABI' not in header:
            sys.exit(f'Wrong target ABI: {file}')
        versions = output('-V',file).split('Version needs section')[-1]
        for v in re.findall(r'Name: GLIBC_(\d+(?:\.\d+)+)', versions):
            if version(v)>version('2.28'):
                sys.exit(f'{file}: requires GLIBC_{v}, newer than webOS 5 (2.28)')
            max_glibc = max(max_glibc, version(v))
        for name in re.findall(r'Name: ((?:GLIBCXX|CXXABI)[_A-Z.0-9]+)',versions):
            if name not in exported:
                sys.exit(f'{file}: bundled C++ runtime lacks {name}')
    dynamic = output('-d',files[0])
    if '$ORIGIN/lib' not in dynamic:
        sys.exit('Missing private runtime search path')
    for bad in ['libcrypto.so', 'libplist', 'libavcodec', 'libX11']:
        if bad in dynamic:
            sys.exit(f'Unexpected dynamic dependency: {bad}')
    print(f'ELF preflight: ARM ELF32 soft-float ABI; max required GLIBC_{".".join(map(str, max_glibc))} <= 2.28; private C++ runtime resolves version requirements.')

if __name__ == '__main__':
    main()
