# Fake PS4 for ss/r2ss.c: UDP 9870 (DDP) + TCP 9970 (Second Screen) with a test RSA key, AES-128-CBC both ways.
# Run from ps4/: python3 ss/r2ss_test.py  (needs /tmp/r2ss_host built from ss/r2ss.c + ss/r2ss_testmain.c with -DR2SS_HOST)
import socket, struct, subprocess, threading, hashlib, os, sys, time
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
priv = rsa.generate_private_key(public_exponent=65537, key_size=2048)
open('/tmp/r2ss_pub.pem', 'wb').write(priv.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
ACCT = '52326d61696e3031'; PIN = '12345678'
CRED = hashlib.sha256(str(int.from_bytes(bytes.fromhex(ACCT), 'little')).encode()).hexdigest()
seen = {'ddp': [], 'pkts': [], 'login': None}
registered = set()
def udp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', 9870))
    while True:
        d, a = s.recvfrom(1000); seen['ddp'].append(d.decode().split('\n')[0] + ' cred=' + ('ok' if CRED in d.decode() else 'BAD'))
def recvn(c, n):
    b = b''
    while len(b) < n:
        x = c.recv(n - len(b))
        if not x: raise EOFError
        b += x
    return b
def tcp():
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('127.0.0.1', 9970)); s.listen(5)
    while True:
        c, _ = s.accept()
        try:
            h = recvn(c, 28); assert struct.unpack('<II', h[:8]) == (28, 0x6f636370), 'chello'
            seed = os.urandom(16)
            c.sendall(struct.pack('<IIII', 36, 0x6f636370, 0x20000, 0) + b'\0' * 4 + seed)
            hs = recvn(c, 280); assert struct.unpack('<II', hs[:8]) == (280, 32), 'handshake'
            key = priv.decrypt(hs[8:264], padding.OAEP(mgf=padding.MGF1(hashes.SHA1()), algorithm=hashes.SHA1(), label=None))
            assert hs[264:280] == seed, 'seed echo'
            dec = Cipher(algorithms.AES(key), modes.CBC(seed)).decryptor(); enc = Cipher(algorithms.AES(key), modes.CBC(seed)).encryptor()
            def rd():
                b = dec.update(recvn(c, 16)); L, T = struct.unpack('<II', b[:8]); m = (L + 15) & ~15
                if m > 16: b += dec.update(recvn(c, m - 16))
                return T, b[:L]
            def wr(T, body):
                p = struct.pack('<II', 8 + len(body), T) + body; p += b'\0' * ((-len(p)) % 16); c.sendall(enc.update(p))
            T, lg = rd(); assert T == 30, 'login type'
            cred = lg[16:80].decode(); pin = lg[368:384].rstrip(b'\0').decode(); model = lg[352:368].rstrip(b'\0').decode()
            seen['login'] = dict(cred_ok=cred == CRED, pin=pin, model=model, magic=struct.unpack('<I', lg[12:16])[0])
            wr(20, struct.pack('<I', 0))  # a status packet first, like a real PS4 may send
            if cred != CRED: wr(7, struct.pack('<I', 999)); continue
            if cred not in registered:
                if pin != PIN: wr(7, struct.pack('<I', 20)); continue
                registered.add(cred)
            wr(7, struct.pack('<I', 0))
            c.settimeout(3)
            while True:
                try: T, b = rd()
                except Exception: break
                if T == 28: seen['pkts'].append('rc %d' % struct.unpack('<I', b[8:12])[0])
                elif T == 10: seen['pkts'].append('boot ' + b[8:24].rstrip(b'\0').decode()); wr(11, struct.pack('<I', 0))
                else: seen['pkts'].append('type %d' % T)
                if T == 4: break
        except Exception as e:
            seen['pkts'].append('server error: %r' % e)
        finally: c.close()
threading.Thread(target=udp, daemon=True).start(); threading.Thread(target=tcp, daemon=True).start(); time.sleep(0.3)
def run(pin, ops, wake='0'):
    for k in ('ddp', 'pkts'): seen[k] = []
    seen['login'] = None
    out = subprocess.run(['/tmp/r2ss_host', '/tmp/r2ss_pub.pem', ACCT, pin, ops, wake], capture_output=True, text=True, timeout=60).stdout
    time.sleep(0.5); return out, dict(seen)
bad = 0
def check(label, cond, info):
    global bad; bad += not cond; print('ok  ' if cond else 'FAIL', label, '|', info)
o, s = run('', '')
check('not registered + no PIN -> PS4 asks for PIN (20)', 'RET 20' in o, o.split('\n')[1])
check('credential = sha256(decimal LE account id)', s['login'] and s['login']['cred_ok'], s['login'])
check('LAUNCH sent with credential before TCP', any(x.startswith('LAUNCH') and x.endswith('ok') for x in s['ddp']), s['ddp'])
o, s = run('12345678', 'ps')
check('PIN login registers, returns 0', 'RET 0' in o, o.split('\n')[1])
check('PS press = open RC, PS, close RC, bye', s['pkts'][:4] == ['type 20', 'rc 1024', 'rc 128', 'rc 2048'] and 'type 4' in s['pkts'], s['pkts'])
o, s = run('', 'enter,boot-CUSA02168', '1')
check('later login without PIN works', 'RET 0' in o, o.split('\n')[1])
check('enter = key + KEY_OFF; boot answered', 'rc 16' in s['pkts'] and 'rc 256' in s['pkts'] and 'boot CUSA02168' in s['pkts'] and 'boot CUSA02168: 11/0' in o, (s['pkts'], o.split('\n')[1]))
check('wake first sends WAKEUP x3 type C', sum(x.startswith('WAKEUP') for x in s['ddp']) == 3, s['ddp'])
print('ALL OK' if not bad else '%d FAILED' % bad); sys.exit(1 if bad else 0)
