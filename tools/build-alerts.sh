#!/usr/bin/env bash
# Packages the Round2 Alerts TEST apps for rooted LG TVs: the same pop-up card in three window types,
# to find which one each webOS version lets draw over Netflix without pausing it and still gets the remote.
#   A = overlay, B = popup, C = floating
# Usage: tools/build-alerts.sh [version]   → sg.round2.alerts.a.ipk, .b.ipk, .c.ipk
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-1.0.0}"
ARES="${ARES_PACKAGE:-npx --yes --package=@webos-tools/cli ares-package}"
for v in "A overlay" "B popup" "C floating"; do
  set -- $v; L=$1; T=$2; id="sg.round2.alerts.$(echo "$L" | tr A-Z a-z)"
  S="build-alerts/$id"; rm -rf "$S" && mkdir -p "$S/app" "$S/out"
  cp webos/alerts/icon.png webos/alerts/largeIcon.png "$S/app/"
  sed -e "s/__VARIANT__/$L/; s/__WINTYPE__/$T/; s/__APPID__/$id/" webos/alerts/index.html > "$S/app/index.html"
  python3 - "$id" "$VERSION" "$L" "$T" "$S/app/appinfo.json" <<'EOF'
import json, sys
i, v, l, t, out = sys.argv[1:]
json.dump({
  "id": i, "version": v, "vendor": "Round2", "type": "web", "main": "index.html",
  "title": f"Round2 pop-up test {l} ({t})", "icon": "icon.png", "largeIcon": "largeIcon.png",
  "resolution": "1920x1080", "transparent": True, "defaultWindowType": t,
  "disableBackHistoryAPI": True, "handlesRelaunch": True, "noSplashOnLaunch": True, "spinnerOnLaunch": False,
}, open(out, 'w'), indent=2)
EOF
  $ARES --no-minify -o "$S/out" "$S/app"
  cp "$S"/out/*.ipk "$id.ipk"
  echo "$id.ipk ready"
done
