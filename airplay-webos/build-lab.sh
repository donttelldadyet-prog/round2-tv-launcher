#!/bin/sh
# Builds the AirPlay lab test app for LG webOS 5+ TVs: dist/sg.round2.lab.ndl_<ver>_arm.ipk
#
#   airplay-webos/build-lab.sh [toolchain dir]
#
# Needs: the openlgtv buildroot NDK (downloaded and checked here if no dir is given), ffmpeg
# with libx264, python3. Runs on Linux x86_64 (GitHub Actions ubuntu-latest).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/dist"; WORK="$HERE/.work"
VERSION=0.1.0
APP=sg.round2.lab.ndl
TC_URL=https://github.com/openlgtv/buildroot-nc4/releases/download/2026.08-webos/arm-webos-linux-gnueabi_sdk-buildroot-x86_64.tar.gz
TC_SHA=2230a6351e5cce6fcb9d4fdcb4697335f223995401ef969c81744aaeb14c5344

mkdir -p "$OUT" "$WORK"
TC="${1:-$WORK/arm-webos-linux-gnueabi_sdk-buildroot}"
if [ ! -x "$TC/bin/arm-webos-linux-gnueabi-gcc" ]; then
  echo "downloading the webOS toolchain"
  curl -fsSL --retry 3 -o "$WORK/tc.tar.gz" "$TC_URL"
  echo "$TC_SHA  $WORK/tc.tar.gz" | sha256sum -c -
  tar -xzf "$WORK/tc.tar.gz" -C "$WORK" && rm "$WORK/tc.tar.gz"
  (cd "$TC" && ./relocate-sdk.sh >/dev/null)
fi
CC="$TC/bin/arm-webos-linux-gnueabi-gcc"
SYS="$TC/arm-webos-linux-gnueabi/sysroot"

# test clip: a moving test pattern with ROUND2 PICTURE TEST, and a 440 Hz tone
FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
[ -f "$FONT" ] && FONTOPT="fontfile=$FONT:" || FONTOPT=""
ffmpeg -loglevel error -y \
  -f lavfi -i "testsrc2=size=1920x1080:rate=30,drawtext=${FONTOPT}text='ROUND2 PICTURE TEST':fontsize=110:fontcolor=white:box=1:boxcolor=0xff2d6f@0.9:boxborderw=30:x=(w-tw)/2:y=(h-th)/2" \
  -t 12 -an -c:v libx264 -profile:v main -pix_fmt yuv420p \
  -x264-params keyint=30:min-keyint=30:scenecut=0:bframes=0:aud=1:repeat-headers=1 \
  -bsf:v h264_mp4toannexb -f h264 "$WORK/test.h264"
ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:sample_rate=48000" -t 12 \
  -ac 2 -ar 48000 -c:a pcm_s16le -f s16le "$WORK/test.pcm"

STAGE="$WORK/stage-$APP"; rm -rf "$STAGE"; mkdir -p "$STAGE"
"$CC" -O2 -Wall -o "$STAGE/ndl_lab" "$HERE/lab/ndl_lab.c" \
  -I"$SYS/usr/include/libndl-media" -I"$SYS/usr/include/SDL2" \
  -lNDL_directmedia -lSDL2 -Wl,-rpath,'$ORIGIN'
cp "$WORK/test.h264" "$WORK/test.pcm" "$STAGE/"
cp "$HERE/lab/icon.png" "$STAGE/icon.png"
cat > "$STAGE/appinfo.json" <<EOF
{
  "id": "$APP",
  "version": "$VERSION",
  "vendor": "Round2",
  "type": "native",
  "main": "ndl_lab",
  "title": "Round2 AirPlay lab",
  "icon": "icon.png",
  "iconColor": "#ff2d6f",
  "bgColor": "#ff2d6f"
}
EOF
python3 "$HERE/tools/make-ipk.py" "$STAGE" "$OUT/${APP}_${VERSION}_arm.ipk"
"$TC/bin/arm-webos-linux-gnueabi-readelf" -d "$STAGE/ndl_lab" | grep NEEDED
