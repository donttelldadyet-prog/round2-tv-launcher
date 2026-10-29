#!/usr/bin/env bash
# Packages Round2 Alerts for rooted LG TVs (sg.round2.alerts.ipk): the Round2 pop-up card drawn over other apps
# in a webOS "overlay" window. The LG helper (round2d.sh, in Round2 Home) opens it for food alerts and messages.
# Usage: tools/build-alerts.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-1.0.0}"
ARES="${ARES_PACKAGE:-npx --yes --package=@webos-tools/cli ares-package}"
S=build-alerts; rm -rf "$S" && mkdir -p "$S/app" "$S/out"
cp webos/alerts/index.html webos/alerts/icon.png webos/alerts/largeIcon.png "$S/app/"
python3 - "$VERSION" "$S/app/appinfo.json" <<'EOF'
import json, sys
v, out = sys.argv[1:]
json.dump({
  "id": "sg.round2.alerts", "version": v, "vendor": "Round2", "type": "web", "main": "index.html",
  "title": "Round2 Alerts", "icon": "icon.png", "largeIcon": "largeIcon.png",
  "resolution": "1920x1080", "transparent": True, "defaultWindowType": "overlay",
  "disableBackHistoryAPI": True, "handlesRelaunch": True, "noSplashOnLaunch": True, "spinnerOnLaunch": False,
}, open(out, 'w'), indent=2)
EOF
$ARES --no-minify -o "$S/out" "$S/app"
cp "$S"/out/*.ipk sg.round2.alerts.ipk
echo "sg.round2.alerts.ipk ready (version $VERSION)"
