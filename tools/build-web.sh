#!/usr/bin/env bash
# Wraps web/index.html (the same file used for the browser preview) into a full page for the app.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p web-assets
{
  printf '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n'
  cat web/index.html
} > web-assets/index.html
echo "web-assets/index.html ready"
