#!/usr/bin/env python3
"""Pack a staged native webOS app folder into an .ipk (what ares-package makes), without Node.

Usage: make-ipk.py <staged app folder with appinfo.json> <output.ipk>

An ipk is an `ar` archive of debian-binary, control.tar.gz and data.tar.gz, with the app under
usr/palm/applications/<id>/ and a packageinfo.json under usr/palm/packages/<id>/.
"""
import io, json, os, sys, tarfile, time

src, out = sys.argv[1], sys.argv[2]
info = json.load(open(os.path.join(src, 'appinfo.json')))
app_id, version = info['id'], info['version']
now = int(time.time())

def tar_gz(add):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w:gz', format=tarfile.GNU_FORMAT) as t:
        add(t)
    return buf.getvalue()

def add_dir(t, name):
    ti = tarfile.TarInfo(name); ti.type = tarfile.DIRTYPE; ti.mode = 0o755; ti.mtime = now
    t.addfile(ti)

def add_bytes(t, name, data, mode=0o644):
    ti = tarfile.TarInfo(name); ti.size = len(data); ti.mode = mode; ti.mtime = now
    t.addfile(ti, io.BytesIO(data))

size = 0
def data(t):
    global size
    for d in ('usr', 'usr/palm', 'usr/palm/applications', 'usr/palm/packages',
              f'usr/palm/applications/{app_id}', f'usr/palm/packages/{app_id}'):
        add_dir(t, d)
    for root, dirs, files in os.walk(src):
        dirs.sort()
        rel = os.path.relpath(root, src)
        base = f'usr/palm/applications/{app_id}' + ('' if rel == '.' else '/' + rel)
        if rel != '.':
            add_dir(t, base)
        for f in sorted(files):
            p = os.path.join(root, f)
            b = open(p, 'rb').read(); size += len(b)
            add_bytes(t, f'{base}/{f}', b, 0o755 if os.access(p, os.X_OK) else 0o644)
    add_bytes(t, f'usr/palm/packages/{app_id}/packageinfo.json',
              json.dumps({'id': app_id, 'version': version, 'app': app_id}, indent=2).encode())

data_tgz = tar_gz(data)
control = (f'Package: {app_id}\nVersion: {version}\nSection: misc\nPriority: optional\n'
           f'Architecture: arm\nInstalled-Size: {size}\nMaintainer: Round2 <noreply@round2.sg>\n'
           f'Description: {info.get("title", app_id)}\nwebOS-Package-Format-Version: 2\n'
           f'webOS-Packager-Version: x.y.x\n').encode()
control_tgz = tar_gz(lambda t: add_bytes(t, 'control', control))

def ar_member(name, b):
    hdr = (f'{name:<16}{now:<12}{0:<6}{0:<6}' + f'{"100644":<8}{len(b):<10}`\n').encode()
    assert len(hdr) == 60
    return hdr + b + (b'\n' if len(b) % 2 else b'')

with open(out, 'wb') as f:
    f.write(b'!<arch>\n')
    f.write(ar_member('debian-binary', b'2.0\n'))
    f.write(ar_member('control.tar.gz', control_tgz))
    f.write(ar_member('data.tar.gz', data_tgz))
print(f'{out}: {app_id} {version}, {size // 1024} KB')
