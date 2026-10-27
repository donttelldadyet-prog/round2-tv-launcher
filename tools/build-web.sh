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
# private part of the food alert topic names (GitHub secret FOOD_ALERT_KEY); left empty, the alerts stay off
FOOD_KEY="$(printf '%s' "${FOOD_ALERT_KEY:-}" | tr -cd 'A-Za-z0-9')"
sed -i "s/__FOOD_ALERT_KEY__/${FOOD_KEY}/" web-assets/index.html
mkdir -p web-assets/media
# Scene loops: download each listed source, compress for TVs (1080p H.264, no audio, fast start) and make a thumbnail
if [ -f web/media/sources.txt ] && command -v ffmpeg >/dev/null; then
  while read -r name url; do
    case "$name" in ''|'#'*) continue ;; esac
    tmp="$(mktemp)"
    curl -fsSL "$url" -o "$tmp"
    ffmpeg -nostdin -loglevel error -y -i "$tmp" -an -c:v libx264 -preset slow -crf 21 -profile:v main -level 4.0 -g 24 -keyint_min 24 -sc_threshold 0 -bf 2 -pix_fmt yuv420p -vf "scale=1920:-2,fps=24" -movflags +faststart "web-assets/media/$name"
    ffmpeg -nostdin -loglevel error -y -ss 0.5 -i "web-assets/media/$name" -frames:v 1 -vf "scale=320:-2" -q:v 3 "web-assets/media/${name%.mp4}.jpg"
    rm -f "$tmp"
    echo "media: $name $(du -h "web-assets/media/$name" | cut -f1)"
  done < web/media/sources.txt
fi
python3 tools/bundle-fonts.py web-assets/index.html || echo "fonts: kept online links"
echo "web-assets ready"
