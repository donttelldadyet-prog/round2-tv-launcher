#!/usr/bin/env bash
# Packages Round2 Alerts for rooted LG TVs (sg.round2.alerts.ipk): the Round2 pop-up card drawn over other apps
# in a webOS "overlay" window. The LG helper (round2d.sh, in Round2 Home) opens it for food alerts and messages.
# Usage: tools/build-alerts.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-1.0.0}"
APPID="${2:-sg.round2.alerts}"; WTYPE="${3:-overlay}"   # test variants (owner, 4 Nov: Netflix): another id and window type
ARES="${ARES_PACKAGE:-npx --yes --package=@webos-tools/cli ares-package}"
S=build-alerts; rm -rf "$S" && mkdir -p "$S/app" "$S/out"
cp webos/alerts/index.html webos/alerts/icon.png webos/alerts/largeIcon.png "$S/app/"
# the menu's fonts (tools/build-web.sh saves them), so the card looks the same as the menu
[ -d web-assets/fonts-native ] && cp -r web-assets/fonts-native "$S/app/fonts"
python3 - "$VERSION" "$S/app/appinfo.json" "$APPID" "$WTYPE" <<'EOF'
import json, sys
v, out, aid, wt = sys.argv[1:]
json.dump({
  "id": aid, "version": v, "vendor": "Round2", "type": "web", "main": "index.html",
  "title": "Round2 Alerts" + ("" if aid == "sg.round2.alerts" else " (test " + wt + ")"), "icon": "icon.png", "largeIcon": "largeIcon.png",
  "resolution": "1920x1080", "transparent": True, "defaultWindowType": wt,
  "disableBackHistoryAPI": True, "handlesRelaunch": True, "noSplashOnLaunch": True, "spinnerOnLaunch": False,
}, open(out, 'w'), indent=2)
EOF
$ARES --no-minify -o "$S/out" "$S/app"
cp "$S"/out/*.ipk "$APPID.ipk"
echo "$APPID.ipk ready (version $VERSION, $WTYPE window)"
