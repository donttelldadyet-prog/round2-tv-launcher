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
# Round2's own AirPlay receiver for LG webOS 5 TVs without AirPlay (Bella), pinned to a checked build
# from the airplay-lab release (update the version and SHA-256 by hand when the receiver changes).
# UxAir and UxPlay webOS were listed before; they need libAcbAPI, which webOS 5 doesn't have.
RX_VER = '0.2.5'
RX_SHA = '5b8c5a7ea1f8d29a485dbb8e927b6b6a14a5493cbca64ec810cefedfbd27b660'
RX_ICON = 'https://raw.githubusercontent.com/donttelldadyet-prog/round2-tv-launcher/main/airplay-webos/receiver/packaging/icon.png'
RX_DESC = 'Round2 AirPlay: iPhone, iPad and Mac screen mirroring for LG TVs without built-in AirPlay (based on UxAir, GPL-3).'
extras = [{
    'id': 'sg.round2.airplay', 'title': 'Round2 AirPlay', 'iconUri': RX_ICON, 'shortDescription': RX_DESC,
    'manifest': {
        'id': 'sg.round2.airplay', 'title': 'Round2 AirPlay', 'version': RX_VER, 'type': 'native',
        'appDescription': RX_DESC, 'iconUri': RX_ICON, 'sourceUrl': f'{REPO}/tree/main/airplay-webos/receiver', 'rootRequired': True,
        'ipkUrl': f'{REPO}/releases/download/airplay-lab/sg.round2.airplay_{RX_VER}_arm.ipk',
        'ipkHash': {'sha256': RX_SHA},
    },
}, {
    # Ad-free YouTube for the LG rooms (RF1705/youtube-webos-cobalt-adfree, open source; stable channel), pinned
    # to a checked release. It uses LG YouTube's own app id, so it replaces the official YouTube app (keeps sign-in
    # and phone pairing). Round2 Home shows "NO ADS" on YouTube once it's installed.
    'id': 'youtube.leanback.v4', 'title': 'YouTube Cobalt AdFree',
    'iconUri': 'https://raw.githubusercontent.com/RF1705/youtube-webos-cobalt-adfree/main/assets/largeIcon.png',
    'shortDescription': 'YouTube without ads, with SponsorBlock (RF1705, open source). Replaces LG\'s YouTube app.',
    'manifest': {
        'id': 'youtube.leanback.v4', 'title': 'YouTube Cobalt AdFree', 'version': '1.2.5', 'type': 'native',
        'appDescription': 'YouTube for LG TVs with ad blocking and SponsorBlock (RF1705/youtube-webos-cobalt-adfree).',
        'iconUri': 'https://raw.githubusercontent.com/RF1705/youtube-webos-cobalt-adfree/main/assets/largeIcon.png',
        'sourceUrl': 'https://github.com/RF1705/youtube-webos-cobalt-adfree', 'rootRequired': False,
        'ipkUrl': 'https://github.com/RF1705/youtube-webos-cobalt-adfree/releases/download/v1.2.5/youtube.leanback.v4_1.2.5_arm.ipk',
        'ipkHash': {'sha256': '189af7d46617a511b191dc472a9fcfec3d0f44caf70107fb1da9adde3d39f2de'},
    },
}]
print(json.dumps({'packages': [{
    'id': 'sg.round2.tvhome', 'title': 'Round2 Home', 'iconUri': ICON,
    'shortDescription': desc, 'manifest': manifest,
}] + extras}, indent=2))
