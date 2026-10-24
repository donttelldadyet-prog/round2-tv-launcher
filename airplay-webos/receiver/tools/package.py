#!/usr/bin/env python3
import argparse
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
SDK = ROOT / '.cache/arm-webos-linux-gnueabi_sdk-buildroot'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--development', action='store_true', help='Package the separately identified development receiver')
    args = parser.parse_args()
    variant = 'development' if args.development else 'production'
    marker = ROOT / 'build/package-variant.txt'
    if marker.exists() and marker.read_text().strip() != variant:
        parser.error('package variant differs from the compiled receiver')
    stage = ROOT / 'build/app'
    # Staging is disposable; never package test sessions or local pairing keys.
    if stage.exists():
        shutil.rmtree(stage)
    (stage / 'lib').mkdir(parents=True)
    (stage / 'licenses').mkdir()
    app = json.loads((ROOT / 'packaging/appinfo.json').read_text())
    if args.development:
        app['id'] += '.dev'
        app['title'] += ' Dev'
        app['version'] = '1.1.0'
    (stage / 'appinfo.json').write_text(json.dumps(app, indent=2) + '\n')
    source_commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (stage / 'build-info.txt').write_text(f'version={app["version"]}\ncommit={source_commit}\n')
    shutil.copy2(ROOT / 'packaging/icon.png', stage / 'icon.png')
    shutil.copy2(ROOT / 'packaging/largeIcon.png', stage / 'largeIcon.png')
    shutil.copy2(ROOT / 'build/uxair', stage / 'uxair')
    (stage / 'uxair').chmod(0o755)
    shutil.copy2(ROOT / 'assets/bmc-qr.bmp', stage / 'bmc-qr.bmp')
    shutil.copy2(ROOT / 'assets/bmc-icon.bmp', stage / 'bmc-icon.bmp')
    shutil.copy2(SDK / 'arm-webos-linux-gnueabi/sysroot/usr/lib/libstdc++.so.6', stage / 'lib/libstdc++.so.6')
    # Round2: SDL2_ttf may be missing on webOS 5 firmware; ship the NDK's copy (its own deps are system libraries)
    shutil.copy2(SDK / 'arm-webos-linux-gnueabi/sysroot/usr/lib/libSDL2_ttf-2.0.so.0', stage / 'lib/libSDL2_ttf-2.0.so.0')
    for source, dest in [
        ('LICENSE', 'GPL-3.0.txt'),
        ('ROUND2.md', 'ROUND2.md'),
        ('.cache/openssl-3.5.8/LICENSE.txt', 'OpenSSL.txt'),
        ('.cache/libplist-2.7.0/COPYING', 'libplist.txt'),
        ('.cache/ffmpeg-8.1.2/COPYING.LGPLv2.1', 'FFmpeg.txt'),
        ('vendor/uxplay/lib/llhttp/LICENSE-MIT', 'llhttp.txt'),
        ('THIRD_PARTY.md', 'THIRD_PARTY.md'),
        ('packaging/GCC-RUNTIME-EXCEPTION.txt', 'GCC-RUNTIME-EXCEPTION.txt'),
    ]:
        shutil.copy2(ROOT / source, stage / 'licenses' / dest)
    shutil.copy2(ROOT / 'dependencies.lock.json', stage / 'dependencies.lock.json')
    strip = SDK / 'bin/arm-webos-linux-gnueabi-strip'
    subprocess.run([str(strip), str(stage / 'uxair')], check=True)
    subprocess.run(['python3', str(ROOT / 'tools/verify-elf.py'), str(stage)], check=True)
    (ROOT / 'dist').mkdir(exist_ok=True)
    (ROOT / 'dist').mkdir(exist_ok=True)
    subprocess.run(['python3', str(ROOT.parent / 'tools/make-ipk.py'), str(stage), str(ROOT / 'dist' / f"{app['id']}_{app['version']}_arm.ipk")], check=True)
    package = ROOT / 'dist' / f"{app['id']}_{app['version']}_arm.ipk"
    if not package.is_file() or not package.stat().st_size:
        raise SystemExit(f'Expected nonempty package was not produced: {package}')
    print(f'Packaged {package}')

if __name__ == '__main__':
    main()
