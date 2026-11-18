#!/bin/sh
# Builds the round2_input GoldHEN plugin (controller detector, helper 1.1.62+). Output: ps4/out/round2_input.prx
# Needs: OpenOrbis PS4 Toolchain v0.5.2 ($OO_PS4_TOOLCHAIN) and the GoldHEN Plugins SDK built with `make PRINTF=1` ($GOLDHEN_SDK).
# The Makefile comes from the GoldHEN Plugins Repository (GPL-3.0) - it expects to sit two folders below a "common" dir,
# so the build runs inside a copy of that repository: GH_REPO=/path/to/GoldHEN_Plugins_Repository
set -e
cd "$(dirname "$0")"
: "${OO_PS4_TOOLCHAIN:?set OO_PS4_TOOLCHAIN}" "${GOLDHEN_SDK:?set GOLDHEN_SDK}" "${GH_REPO:?set GH_REPO}"
D="$GH_REPO/plugin_src/round2_input"; mkdir -p "$D"
cp -r round2_input/source round2_input/include round2_input/Makefile "$D/"
mkdir -p "$GH_REPO/bin/plugins/elf_final" "$GH_REPO/bin/plugins/prx_final"
make -C "$D" clean >/dev/null 2>&1 || true
make -C "$D"
mkdir -p ../out; cp "$GH_REPO/bin/plugins/prx_final/round2_input.prx" ../out/
sha256sum ../out/round2_input.prx; stat -c %s ../out/round2_input.prx
