#!/bin/sh
# Builds the Round2 AirPlay receiver for LG webOS 5+ (UxAir 1.1.0 with an NDL video layer):
#   receiver/dist/sg.round2.airplay_<ver>_arm.ipk
#
#   airplay-webos/build-receiver.sh [toolchain dir]
#
# Fetches and checks the pinned sources (UxPlay, OpenSSL, libplist, FFmpeg, the openlgtv NDK),
# cross-compiles them, builds the receiver and packs it. Linux x86_64 (GitHub Actions ubuntu-latest).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE/receiver"
[ -n "${1:-}" ] && export WEBOS_NDK="$1"
python3 tools/bootstrap.py
bash tools/build-deps.sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.cache/arm-webos-linux-gnueabi_sdk-buildroot/share/buildroot/toolchainfile.cmake" \
  -DBUILD_RECEIVER=ON -DBUILD_DEVELOPMENT_PACKAGE=OFF
cmake --build build --parallel "$(nproc 2>/dev/null || echo 4)"
printf 'production\n' > build/package-variant.txt
python3 tools/package.py
