#!/usr/bin/env bash
# Packages the LG webOS version (round2-home.ipk) from web-assets/ (run tools/build-web.sh first).
# Usage: tools/build-webos.sh [version]   e.g. tools/build-webos.sh 1.0.57
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-1.0.0}"
[ -f web-assets/index.html ] || { echo "web-assets/ missing: run tools/build-web.sh first"; exit 1; }

STAGE=build-webos
rm -rf "$STAGE" && mkdir -p "$STAGE/app" "$STAGE/out"
cp -r web-assets/. "$STAGE/app/"
cp webos/app/icon.png webos/app/largeIcon.png "$STAGE/app/"
cp -r webos/service "$STAGE/service"

# the app's version, in appinfo.json and on the boot screen
python3 - "$VERSION" "$STAGE" <<'EOF'
import json, sys
version, stage = sys.argv[1], sys.argv[2]
info = json.load(open('webos/app/appinfo.json'))
info['version'] = version
json.dump(info, open(f'{stage}/app/appinfo.json', 'w'), indent=2)
svc = json.load(open(f'{stage}/service/package.json'))
svc['version'] = version
json.dump(svc, open(f'{stage}/service/package.json', 'w'), indent=2)
page = open(f'{stage}/app/index.html', encoding='utf-8').read()
page = page.replace('<meta charset="utf-8">', f'<meta charset="utf-8">\n<script>window.R2_VERSION = {json.dumps(version)};</script>', 1)
open(f'{stage}/app/index.html', 'w', encoding='utf-8').write(page)
EOF

ARES="${ARES_PACKAGE:-npx --yes --package=@webos-tools/cli ares-package}"
$ARES --no-minify -o "$STAGE/out" "$STAGE/app" "$STAGE/service"
cp "$STAGE"/out/*.ipk round2-home.ipk
echo "round2-home.ipk ready ($(du -h round2-home.ipk | cut -f1), version $VERSION)"
