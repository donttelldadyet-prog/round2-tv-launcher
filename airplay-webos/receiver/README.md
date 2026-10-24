<p align="center"><img src="assets/uxair-icon-256.png" width="128" alt="UxAir receiver icon"></p>

<h1 align="center">UxAir</h1>
<p align="center"><strong>AirPlay Receiver for webOS</strong></p>

UxAir is a native AirPlay mirroring receiver for compatible rooted LG webOS TVs, based on the open-source UxPlay project. It is an independent community project and is not affiliated with or endorsed by Apple or LG. UxAir is compatible with AirPlay; AirPlay is an Apple trademark.

## Status

UxAir 1.0.0 is publicly released; version 1.1.0 has completed hardware testing and is being prepared for its official release. Tested on LG OLED B7 (2017) running rooted webOS 3.9. Other LG webOS models may be compatible but have not yet been verified. HDR10/Main10 is not supported. Native 4K output-plane resolution and displayed FPS are not measured by this application.

## Features

- H.264 AirPlay mirroring up to 1080p/60, tested on the B7.
- HEVC/H.265 mirroring up to 4K/60, tested on the B7.
- 30 and 60 FPS profiles, including the B7-tested H.264 720p/1080p and HEVC 4K modes.
- Portrait and landscape adaptation.
- Photos and videos displayed through the tested mirroring workflows.
- Starfish hardware-accelerated video playback with stereo audio during mirroring.
- Standalone AirPlay music/audio-only playback, confirmed on the B7 in the tested workflow; individual audio codec variants were not tested separately.
- Configurable codec, resolution, frame rate, audio delay, and diagnostics.
- Magic Remote pointer, arrow, OK, and Back navigation.
- Large on-screen AirPlay PIN pairing, saved configuration, session history, and automatic decoder recovery/fallback.
- Lightweight diagnostics separating advertised rate, incoming rate, accepted Feed rate, and decoder callbacks. Decoder callback rate does not claim displayed FPS.

The iPhone, iPad, or Mac selects the actual AirPlay codec and rate. Selecting HEVC advertises HEVC alongside H.264; it cannot force a sender that chooses H.264. Direct Photos playback is distinct from Photos or video shown by mirroring. Direct Photos/video, HLS, AirPlay 2, HDR/Main10, and native 4K scanout are not supported or verified.

Starting with UxAir 1.1.0, a fresh installation defaults to H.264 at 1920×1080 and 30 FPS. Valid saved profiles from earlier UxAir builds are retained across upgrades. Automatic decoder recovery is temporary for that run and does not replace the saved preference.

## Screenshots

### UxAir 1.1 interface

![UxAir promotional image](screenshots/uxair-marketing.png)

![UxAir 1.1 video settings](screenshots/uxair-video.png)

![UxAir 1.1 history dashboard](screenshots/uxair-history.png)


## Hardware and requirements

- A compatible rooted LG webOS TV with native application development enabled.
- A TV and sender on the same local network with multicast/mDNS discovery available.
- Tested on LG OLED B7 (2017) running rooted webOS 3.9. Other LG webOS models may be compatible but have not yet been verified.
- The ARMv7 build uses the pinned webOS Brew toolchain. CI performs host tests and builds/validates the ARMv7 IPK.

## Install and use

### Download UxAir

**UxAir 1.0.0 is officially released.** Download the ARMv7 installer directly: **[Download UxAir 1.0.0 (.ipk)](https://github.com/depius/uxair-webos/releases/download/v1.0.0/com.github.depius.uxair_1.0.0_arm.ipk)**. For release notes, source code and SHA-256 checksums, see the [UxAir 1.0.0 release page](https://github.com/depius/uxair-webos/releases/tag/v1.0.0).

Install UxAir on a compatible rooted LG webOS TV with native app installation enabled. Keep the TV and Apple device on the same local network with multicast/mDNS discovery available.

### Method 1 — webOS Dev Manager (Recommended)

1. Install [webOS Dev Manager](https://github.com/webosbrew/dev-manager).
2. Connect a compatible rooted LG webOS TV.
3. Download the UxAir `.ipk` package.
4. Use webOS Dev Manager's application installation functionality to select the downloaded IPK.
5. Install and launch UxAir on the television.
6. Select UxAir from the AirPlay / Screen Mirroring menu on an Apple device.
7. Enter the pairing PIN if requested.

For standalone music playback, select UxAir as the AirPlay audio output.

### Method 2 — webOS CLI

Use this method if you already have the webOS CLI installed and `YOUR_TV` configured as a target device:

```sh
ares-install -d YOUR_TV com.github.depius.uxair_1.0.0_arm.ipk
ares-launch -d YOUR_TV com.github.depius.uxair
```

Open UxAir, select it from Screen Mirroring, and enter the pairing PIN when prompted. Pairing credentials and settings are private to the UxAir app ID.

### Migrating from UxPlay webOS development builds

UxAir uses the new ID `com.github.depius.uxair`. It installs alongside the existing UxPlay webOS development app and does not overwrite or uninstall it. The new ID has separate pairing keys, receiver identity, and settings; you will pair again and configure UxAir separately. You may keep the old development app installed while verifying UxAir on your own TV. No state is migrated or removed automatically.

## Remote and configuration

- Left/Right changes the Video, Audio, History, Diagnostics, and About sections.
- Up/Down focuses rows; OK opens or confirms a choice; Back cancels a dialog or pairing attempt, returns from playback to the UI, or exits from the idle screen.
- During playback, OK opens diagnostics and Back returns to video.
- Set audio delay before connecting; positive values delay the TV audio.
- Choose 30 or 60 FPS as the advertised maximum. The sender may use a lower rate.
- HEVC capability does not force the sender's codec selection.

## Build from source

UxAir is primarily written in C++. ARMv7 IPK builds require a compatible cross-compilation environment. The verified target-build path uses Apple Silicon macOS with the pinned webOS toolchain and webOS CLI. Native regression tests can also run on Ubuntu Linux with the development dependencies listed in [the full development guide](docs/DEVELOPMENT.md).

The following commands are for a prepared environment with the required platform-specific dependencies; they are not expected to work on every operating system without setup:

```sh
python3 tools/validate-metadata.py
tools/test.sh
tools/build.sh
python3 tools/validate-ipk.py
tools/source-package.py
```

The development guide documents the pinned toolchain, ARM32 soft-float ABI, GLIBC compatibility, ELF validation, source packaging, reproducibility, and GPL corresponding-source obligations. The source archive includes the exact application source and pinned upstream/dependency sources. CI validation does not replace physical TV testing.

## Known limitations and troubleshooting

- The tested standalone music workflow does not validate each negotiated audio codec separately. Protected media and unsupported AirPlay protocols are not decrypted or altered.
- An AirPlay sender may select a different codec or frame rate than the requested maximum.
- Incoming/Feed rates and Starfish `FRAMEREADY` callbacks are not proof of displayed FPS. Native panel output resolution is unmeasured.
- HDR10/Main10, direct Photos/video, AirPlay 2, startup at boot, broad TV compatibility, and macOS extended-display use are not supported or certified.
- Starfish buffering on webOS 3.9 can add latency.
- If the receiver does not appear, check same-network connectivity and multicast/mDNS. If a codec fails, reconnect and choose a supported profile. Review `uxair.log`; logs can contain sender names and local network details.

## Acknowledgements and license

This project builds on the excellent work of:

- **UxPlay by FDH2** — the original open-source AirPlay mirroring server.
- [**uxplay-webOS by Daniel Filho (@danlflh)**](https://github.com/danlflh/uxplay-webos) — the original webOS port.

UxAir extends this foundation with additional features, performance improvements, and a redesigned interface. Development and adaptation: [@depius](https://github.com/depius). This repository is [depius/uxair-webos](https://github.com/depius/uxair-webos) (formerly `uxplay-webos`). UxAir is licensed under [GPL-3.0-or-later](LICENSE); corresponding sources, upstream attribution, and third-party notices are preserved in [THIRD_PARTY.md](THIRD_PARTY.md).

## Support development

<a href="https://buymeacoffee.com/depius"><img src="assets/buy-me-a-coffee.svg" alt="Buy me a coffee"></a>

UxAir is free and open source. If you enjoy using it, you can support its development by buying me a coffee.

https://buymeacoffee.com/depius

Donations are optional and do not unlock features or affect the license. The app displays a bundled offline QR code for the same support URL.
