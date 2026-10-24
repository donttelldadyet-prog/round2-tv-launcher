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
# Other apps the rooted Round2 TVs use, pinned to a checked release (update the version and SHA-256 by hand)
UX = 'https://github.com/depius/uxair-webos'
UX_ICON = 'https://raw.githubusercontent.com/depius/uxair-webos/v1.1.0/packaging/icon.png'
UX_DESC = 'AirPlay screen mirroring for older LG TVs without built-in AirPlay (UxAir by depius, open source).'
extras = [{
    'id': 'com.github.depius.uxair', 'title': 'UxAir (AirPlay)', 'iconUri': UX_ICON, 'shortDescription': UX_DESC,
    'manifest': {
        'id': 'com.github.depius.uxair', 'title': 'UxAir', 'version': '1.1.0', 'type': 'native',
        'appDescription': UX_DESC, 'iconUri': UX_ICON, 'sourceUrl': UX, 'rootRequired': True,
        'ipkUrl': f'{UX}/releases/download/v1.1.0/com.github.depius.uxair_1.1.0_arm.ipk',
        'ipkHash': {'sha256': '669e8883d04ad2f6ed6711a543fdc1ef6be322c666f1259a61c785886084bda2'},
    },
}]
print(json.dumps({'packages': [{
    'id': 'sg.round2.tvhome', 'title': 'Round2 Home', 'iconUri': ICON,
    'shortDescription': desc, 'manifest': manifest,
}] + extras}, indent=2))
