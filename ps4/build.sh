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
ls -la out
