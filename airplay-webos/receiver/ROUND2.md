# Round2 AirPlay (webOS 5+) — based on UxAir

This folder is **UxAir 1.1.0** by Matej Skuhala ([@depius](https://github.com/depius)),
from [depius/uxair-webos](https://github.com/depius/uxair-webos) at tag `v1.1.0`, itself built on
[UxPlay](https://github.com/FDH2/UxPlay) by FDH2 and [uxplay-webOS](https://github.com/danlflh/uxplay-webos)
by Daniel Filho. It is licensed under GPL-3.0-or-later (see `LICENSE` and `THIRD_PARTY.md`), and so
are Round2's changes to it.

## Why it was changed

UxAir drives the TV's video plane through `libAcbAPI`, which LG removed in webOS 5. On Bella's
LG 75NANO80PTA (webOS 5, firmware 03.53.45) UxAir stops at start-up with
`libAcbAPI.so.1: cannot open shared object file`.

## What Round2 changed (October 2026)

- `src/media.cpp`: rewritten for **NDL DirectMedia v2** (`libNDL_directmedia`, webOS 5+), which owns
  the video plane itself. Timestamps go in as milliseconds and the screen area is left at its
  default; both were chosen from the AirPlay lab test on the 75NANO80PTA (`../lab/ndl_lab.c`).
- `src/main.cpp`: while mirroring, the app window is cleared to transparent every 250 ms. A single
  clear left the screen black on webOS 5.
- `src/media.h`, `src/media_open_failure.h`: Starfish/ACB members and messages removed or reworded.
- `src/identity.h`, `packaging/appinfo.json`: app ID `sg.round2.airplay`, AirPlay name "Round2 TV".
- `CMakeLists.txt`: links NDL instead of Starfish/ACB; modern C++ ABI; HEVC probe target removed.
- `dependencies.lock.json`, `tools/bootstrap.py`, `tools/build-deps.sh`: the openlgtv NDK for
  Linux x86_64 instead of the macOS toolchain, so GitHub Actions can build it.
- `tools/package.py`, `tools/verify-elf.py`: packed with `../tools/make-ipk.py` (no Node);
  glibc ceiling raised to webOS 5's 2.28.

Build: `airplay-webos/build-receiver.sh`. The source for every published package is this folder at
the commit named in the package's `build-info.txt`.

## 0.1.1 – 0.1.2

- `src/shell.cpp`, `src/main.cpp`, `src/receiver.cpp`: state and log go to the first writable folder
  (app folder, `$HOME`, `/tmp`); each start-up failure is named on screen and in the log.
- Low latency: video is handed to the TV as soon as it arrives (no schedule wait), and every
  buffer is stamped with the time since Load (ms), as moonlight-tv's NDL backend does. On Bella
  0.1.1 showed about a second of delay with the sender's own timestamps. Audio still follows
  its timestamps; the receiver's Audio delay setting adjusts lip-sync.

## 0.1.3

- Audio is also handed to the TV as it arrives; lip-sync is set by the Audio delay (−1000…+1000 ms),
  applied as a timestamp offset in the TV's player (`media.cpp av_offset_ms`): positive plays sound
  later, negative plays the picture later. ◀ ▶ change it in 25 ms steps on the waiting screen and
  during mirroring (a short note shows the value).
- The AirPlay name comes from the launch parameter `{"name": "..."}` (Round2 Home sends
  "<Room>'s AirPlay") and is remembered in `state/airplay-name`.

## 0.2.0

- **Timeline** (`src/media.cpp`): sender timestamps mapped onto the TV clock with the smallest recent
  delay plus a jitter cushion (p90 + 20 ms, 30–300 ms), after AirPlay Server's DelayTracker and
  TimelineBuffer (jqssun/android-airplay-server, GPL-3.0). Sound buffers are stamped back to back,
  never overlapping and never in the past. 0.1.3's arrival-time stamps overlapped and the TV
  dropped every other beep.
- **UxPlay fixes** (`patches/uxplay/`, applied by `tools/bootstrap.py`): from AirPlay Server,
  "deliver volume while audio stream paused" and "keep session on BLE discovery query"; and its
  pin-pairing proof-length fix, adapted to this UxPlay version (bounds check before the copy).
  Its HLS (AirPlay video) patches don't apply: this receiver doesn't play AirPlay video URLs.
- **Screens** (`src/round2_ui.cpp`, `src/shell.*`, `src/main.cpp`, `src/ui_navigation.cpp`): Round2
  look (Chakra Petch and Press Start 2P, SIL OFL, bundled in `fonts/`; the room's colour from the
  launch parameter `color`). Two pages instead of five: Home (how to connect, picture quality,
  smoothness, sound timing, More settings) and More settings (video format, info overlay, version,
  credits, licence). History, detailed diagnostics and the donation screen were removed. Lists
  scroll, layout scales with screen height. Left/right on Home changes sound timing.

## 0.2.1

- 0.2.0's clock mapping made mirroring ~10 s late on Bella and connections kept dropping.
  Back to 0.1.3's live timestamps (fast); kept only the fix for overlapping sound: sound buffers
  are laid back to back from the live time. The two AirPlay Server session/volume patches are
  removed again while that is retested; the pairing bounds check stays.

## 0.2.3

- Picture froze after connecting (0.2.1-0.2.2) and the receiver screen didn't come back: sound
  laid back to back could run up to 400 ms ahead of the picture and fill the TV's sound buffer.
  Now a sound piece more than 250 ms ahead is skipped, and when the sound buffer is full the
  receiver tries again a moment later instead of waiting inside the TV's call.
