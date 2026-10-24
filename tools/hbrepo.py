#!/usr/bin/env python3
"""Writes the Homebrew Channel listing for rooted LG TVs (manager/lg.json, served on GitHub Pages).

On a rooted LG TV: Homebrew Channel > Settings > Add repository >
  https://donttelldadyet-prog.github.io/round2-tv-launcher/lg.json
then Round2 Home appears in Homebrew Channel and installs (and updates) from the TV remote.

Usage: tools/hbrepo.py <version> <path to round2-home.ipk>
"""
import hashlib, json, sys

version, ipk = sys.argv[1], sys.argv[2]
sha = hashlib.sha256(open(ipk, 'rb').read()).hexdigest()
REPO = 'https://github.com/donttelldadyet-prog/round2-tv-launcher'
ICON = 'https://donttelldadyet-prog.github.io/round2-tv-launcher/icon-192.png'
desc = 'The Round2 room menu: inputs, streaming apps and event backgrounds, driven by the remote.'
manifest = {
    'id': 'sg.round2.tvhome', 'title': 'Round2 Home', 'version': version, 'type': 'web',
    'appDescription': desc, 'iconUri': ICON, 'sourceUrl': REPO, 'rootRequired': False,
    'ipkUrl': f'{REPO}/releases/download/v{version}/round2-home.ipk', 'ipkHash': {'sha256': sha},
}
print(json.dumps({'packages': [{
    'id': 'sg.round2.tvhome', 'title': 'Round2 Home', 'iconUri': ICON,
    'shortDescription': desc, 'manifest': manifest,
}]}, indent=2))
