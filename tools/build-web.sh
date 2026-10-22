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
mkdir -p web-assets/media
# Scene loops: download each listed source, compress for TVs (1080p H.264, no audio, fast start) and make a thumbnail
if [ -f web/media/sources.txt ] && command -v ffmpeg >/dev/null; then
  while read -r name url; do
    case "$name" in ''|'#'*) continue ;; esac
    tmp="$(mktemp)"
    curl -fsSL "$url" -o "$tmp"
    ffmpeg -loglevel error -y -i "$tmp" -an -c:v libx264 -preset slow -crf 21 -pix_fmt yuv420p -vf "scale=1920:-2" -movflags +faststart "web-assets/media/$name"
    ffmpeg -loglevel error -y -ss 0.5 -i "web-assets/media/$name" -frames:v 1 -vf "scale=320:-2" -q:v 3 "web-assets/media/${name%.mp4}.jpg"
    rm -f "$tmp"
    echo "media: $name $(du -h "web-assets/media/$name" | cut -f1)"
  done < web/media/sources.txt
fi
python3 tools/bundle-fonts.py web-assets/index.html || echo "fonts: kept online links"
echo "web-assets ready"
