#!/usr/bin/env python3
"""Fetch pinned sources and the Linux x86_64 webOS cross-toolchain; verify before extracting.

Round2 build: set WEBOS_NDK to an already extracted toolchain to reuse it."""
import os
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / '.cache'
LOCK = json.loads((ROOT / 'dependencies.lock.json').read_text())

def run(*args, **kw):
    subprocess.run(args, check=True, **kw)

def main():
    CACHE.mkdir(exist_ok=True)
    ndk = os.environ.get('WEBOS_NDK')
    if ndk and not (CACHE / LOCK['toolchain']['directory']).exists():
        (CACHE / LOCK['toolchain']['directory']).symlink_to(ndk)
    for name, item in LOCK.items():
        if name == 'uxplay':
            target = ROOT / 'vendor' / 'uxplay'
            embedded = ROOT / 'upstream-uxplay-source.tar'
            if embedded.exists() and not (target / '.git').exists():
                if not target.exists():
                    target.parent.mkdir(exist_ok=True)
                    run('tar', '-xf', str(embedded), '-C', str(target.parent))
                continue
            if not (target / '.git').exists():
                target.parent.mkdir(exist_ok=True)
                run('git', 'clone', item['repository'], str(target))
            # Round2: the only local changes are our patches (patches/uxplay), re-applied every time
            run('git', '-C', str(target), 'reset', '-q', '--hard')
            run('git', '-C', str(target), 'checkout', '-q', '--detach', item['commit'])
            for patch in sorted((ROOT / 'patches' / 'uxplay').glob('*.patch')):
                run('git', '-C', str(target), 'apply', str(patch))
                print(f'UxPlay patch applied: {patch.name}')
            continue
        if (CACHE / item['directory']).exists():   # already extracted (or supplied)
            continue
        archive = CACHE / item['archive']
        if not archive.exists():
            run('curl', '-fL', '--retry', '3', item['url'], '-o', str(archive))
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != item['sha256']:
            sys.exit(f'Checksum mismatch: {archive}. Remove this archive and retry.')
        target = CACHE / item['directory']
        if name == 'toolchain' and target.exists():
            continue
        if not target.exists():
            run('tar', '-xf', str(archive), '-C', str(CACHE))
            if name == 'toolchain':
                run('bash', './relocate-sdk.sh', cwd=str(target))
    print('Pinned inputs verified.')

if __name__ == '__main__':
    main()
