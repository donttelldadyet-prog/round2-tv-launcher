# Fake DNS server on 127.0.0.1:5353 + the cases r2dns.c must handle. Run from ps4/: python3 tls/r2dns_test.py
import socket, struct, subprocess, threading, sys
def name(q, p):
    out = []
    while q[p]: l = q[p]; out.append(q[p+1:p+1+l].decode()); p += 1 + l
    return '.'.join(out), p + 1
def serve():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', 5353))
    while True:
        q, who = s.recvfrom(600); host, p = name(q, 12); qs = q[12:p+4]; i = q[:2]
        def rep(flags, answers, id_=i): return id_ + struct.pack('>HHHHH', flags, 1, len(answers), 0, 0) + qs + b''.join(answers)
        A = lambda ip: b'\xc0\x0c' + struct.pack('>HHIH', 1, 1, 60, 4) + socket.inet_aton(ip)
        if host == 'ok.test': s.sendto(rep(0x8180, [A('104.21.5.9')]), who)
        elif host == 'cname.test': s.sendto(rep(0x8180, [b'\xc0\x0c' + struct.pack('>HHIH', 5, 1, 60, 7) + b'\x04edge\xc0\x0c', A('172.67.1.2')]), who)
        elif host == 'blocked.test': s.sendto(rep(0x8180, [A('0.0.0.0')]), who)
        elif host == 'nx.test': s.sendto(rep(0x8183, []), who)
        elif host == 'wrongid.test': s.sendto(rep(0x8180, [A('6.6.6.6')], b'\x00\x01'), who); s.sendto(rep(0x8180, [A('10.9.8.7')]), who)
        elif host == 'junk.test': s.sendto(b'\x01\x02\x03', who); s.sendto(i + b'\x81\x80\x00\x01\x00\x05\x00\x00\x00\x00' + qs + b'\xc0', who)
        elif host == 'silent.test': pass
threading.Thread(target=serve, daemon=True).start()
import time; time.sleep(0.3)
def run(*a): r, ip, t = subprocess.run(['/tmp/r2dns_test', *a], capture_output=True, text=True, timeout=30).stdout.split(); return int(r), ip, float(t)
bad = 0
def want(label, got, r, ip, tmax, tmin=0):
    global bad; ok = got[0] == r and (ip is None or got[1] == ip) and tmin <= got[2] <= tmax
    bad += not ok; print('ok  ' if ok else 'FAIL', label, got)
want('plain answer', run('ok.test'), 0, '104.21.5.9', 0.5)
want('CNAME then address', run('cname.test'), 0, '172.67.1.2', 0.5)
want('numbers come straight back', run('1.2.3.4'), 0, '1.2.3.4', 0.1)
want('wrong id ignored, right one used', run('wrongid.test'), 0, '10.9.8.7', 0.5)
want('own servers: blocked (0.0.0.0) = not found', run('blocked.test', '127.0.0.1', 'own'), -1, None, 0.5)
want('own servers: no such name', run('nx.test', '127.0.0.1', 'own'), -1, None, 0.5)
want('own servers: no answer gives up in ~2.5 s', run('silent.test', '127.0.0.1', 'own'), -1, None, 3.2, 2.3)
want('own servers: junk replies do not crash or hang', run('junk.test', '127.0.0.1', 'own'), -1, None, 3.2)
want('all servers silent/unreachable: gives up within ~11 s', run('silent.test', '127.0.0.9'), -1, None, 11.5, 2.3)
want('too-long name refused', run('a' * 70 + '.test', '127.0.0.1', 'own'), -1, None, 0.2)
print('ALL OK' if not bad else '%d FAILED' % bad); sys.exit(1 if bad else 0)
