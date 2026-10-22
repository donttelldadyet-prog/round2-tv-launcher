#!/usr/bin/env bash
# Builds web-assets/ for the app from web/index.html (the same file used for the browser preview):
# full HTML page, media files, and (when online) the Google fonts saved locally so the TV never waits for them.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p web-assets
{
  printf '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n'
  cat web/index.html
} > web-assets/index.html
if [ -d web/media ]; then cp -r web/media web-assets/; fi
python3 tools/bundle-fonts.py web-assets/index.html || echo "fonts: kept online links"
echo "web-assets ready"
