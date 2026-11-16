#!/bin/sh
# Builds the Round2 PS4 payloads. Output: ps4/out/*.elf
set -e
cd "$(dirname "$0")"
D=$PWD/.deps; mkdir -p "$D" out
[ -d "$D/sdk" ] || git clone --depth 1 https://github.com/ps4-payload-dev/sdk "$D/sdk"
[ -d "$D/sdk-inst" ] || make -C "$D/sdk" -j8 DESTDIR="$D/sdk-inst" install
export PS4_PAYLOAD_SDK="$D/sdk-inst"; CC="$PS4_PAYLOAD_SDK/bin/orbis-clang"
[ -d "$D/mbedtls" ] || git clone --depth 1 -b mbedtls-3.6.2 https://github.com/Mbed-TLS/mbedtls "$D/mbedtls"
[ -d "$D/sqlite" ] || git clone --depth 1 https://github.com/azadkuh/sqlite-amalgamation "$D/sqlite"
if [ ! -f "$D/libmbedtls-ps4.a" ]; then
  mkdir -p "$D/mbo"
  for f in "$D"/mbedtls/library/*.c; do
    $CC -O2 -c -I"$D/mbedtls/include" -I"$D/mbedtls/library" -Itls -DMBEDTLS_USER_CONFIG_FILE='"r2_mbedtls_config.h"' "$f" -o "$D/mbo/$(basename "$f" .c).o"
  done
  llvm-ar rcs "$D/libmbedtls-ps4.a" "$D"/mbo/*.o
fi
[ -f "$D/sqlite3.o" ] || $CC -O2 -c "$D/sqlite/sqlite3.c" -o "$D/sqlite3.o" -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_SHARED_CACHE -DHAVE_USLEEP=1
# Mozilla root certificates (Debian/Ubuntu ca-certificates package)
python3 - <<'PY'
import glob
pems=''.join(open(f).read().strip()+'\n' for f in sorted(glob.glob('/usr/share/ca-certificates/mozilla/*.crt')))
assert pems.count('BEGIN CERTIFICATE')>100, 'Mozilla roots not found'
open('.deps/ca_bundle.c','w').write('const char r2_ca_pem[] =\n'+''.join('"%s\\n"\n'%l for l in pems.splitlines())+';\n')
PY
mkdir -p "$D/inc"; cp "$D/sqlite/sqlite3.h" "$D/inc/"
$CC -Wall -O2 -I"$D/mbedtls/include" -Itls -I"$D/inc" -DMBEDTLS_USER_CONFIG_FILE='"r2_mbedtls_config.h"' \
  -o out/round2-probe.elf probe/main.c tls/r2tls.c "$D/ca_bundle.c" "$D/libmbedtls-ps4.a" "$D/sqlite3.o" \
  -lSceUserService -lSceRandom -lSceRegMgr -lSceSystemService -lSceNetCtl -lScePad
# own-program test (helper 1.1.18): the test payload is built first and carried inside the helper; spawn/ = elfldr's method
$CC -Wall -O2 -o out/round2-own.elf own/main.c
python3 - <<'PY'
d=open('out/round2-own.elf','rb').read()
open('.deps/own_elf.c','w').write('const unsigned char r2_own_elf[] = {'+','.join(str(x) for x in d)+'};\nconst unsigned int r2_own_elf_len = %d;\n'%len(d))
PY
for f in elfldr pt r2spawn; do $CC -fPIC -Wall -O2 -mstackrealign -c spawn/$f.c -o "$D/sp-$f.o"; done
# Remote Play registration + wake (helper 1.1.21): rp/rpcrypt.c is chiaki's file (AGPL-3.0), built on our mbedTLS
# -mstackrealign (1.1.22): 1.1.21 died with signal 10 (SIGBUS) the moment '/_rpreg' ran on Kai – a payload's stack isn't
# always 16-byte aligned under GoldHEN's loader, and optimised code that keeps 16-byte values on the stack then faults.
# Every Round2 file of the helper is now built with it (the spawn files already were).
$CC -Wall -Wno-unused-variable -O2 -mstackrealign -DCHIAKI_LIB_ENABLE_MBEDTLS -Irp -I"$D/mbedtls/include" -Itls -DMBEDTLS_USER_CONFIG_FILE='"r2_mbedtls_config.h"' -c rp/rpcrypt.c -o "$D/rp-rpcrypt.o"
$CC -Wall -O2 -mstackrealign -Irp -c rp/r2rp.c -o "$D/rp-r2rp.o"
$CC -Wall -O2 -mstackrealign -I"$D/mbedtls/include" -Itls -DMBEDTLS_USER_CONFIG_FILE='"r2_mbedtls_config.h"' \
  -o out/round2d.elf helper/main.c tls/r2tls.c "$D/ca_bundle.c" "$D/own_elf.c" "$D/sp-elfldr.o" "$D/sp-pt.o" "$D/sp-r2spawn.o" "$D/rp-rpcrypt.o" "$D/rp-r2rp.o" "$D/libmbedtls-ps4.a" \
  -lSceUserService -lSceRandom -lSceRegMgr -lSceSystemService -lSceNetCtl
$CC -Wall -O2 -o out/round2-boot.elf boot/main.c   # the starter (AutoRun) – no network, no TLS
# Round2 build of the blocker (nanoDNS + settings reload without a restart) – see nanodns/main.c
$CC -Wall -Wextra -Werror -O2 -std=c11 -DPLATFORM_PS4=1 -o out/nanodns-r2.elf nanodns/main.c -lSceNet
ls -la out
