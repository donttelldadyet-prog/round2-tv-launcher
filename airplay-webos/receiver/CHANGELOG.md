# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - Pending publication

### Added

- Dedicated History page with calendar-month and calendar-year playback charts, playback distribution, recent sessions, storage reporting and confirmed reset.
- Bounded usage history with a 64 KiB storage limit, up to 100 recent sessions and 365 days of aggregates; backward-compatible V1 history migration.
- Bundled Buy Me a Coffee icon on About.

### Changed

- Simplified Video and Audio menus and consolidated technical information in Diagnostics.
- New installations default to H.264 at 1080p/30 FPS, retaining valid saved preferences on upgrade.
- Calendar-based chart labels, readable duration units, clipping and TV-safe spacing.
- About identifies Matej Skuhala (@depius), while preserving UxPlay and uxplay-webOS upstream credits.

## [1.0.0] - 2026-10-20

### Added

- Final UI preparation: full-bleed slate-blue launcher artwork, metadata-controlled tile color, a matching 130 px large icon, and reproducible PNG exports from the vector master.
- Public README screenshots placeholder and exact GitHub repository description/topics/rename instructions; no release was published and no repository settings were changed.
- RC5: runtime APPID/ACB/Starfish identity diagnostics, media-open failure-stage reporting, actionable playback failure status, and baseline payload/identity regression tests.
- RC5: exact 1080p H.264/30 payload equivalence coverage plus sanitizer tests for the startup error-message mapping.
- RC4: atomic pairing-key persistence outside the UI-shared receiver lock, RAOP callback quiescence before receiver-state reset, and bounded PIN-free pairing lifecycle diagnostics.
- RC4: sanitizer-backed regression coverage for cancellation followed by successful pairing and immediate UI-state dismissal.
- UxAir application identity, launcher branding, and 80/256/512 px icon assets, packaged under a new app ID to preserve existing UxPlay installations and pairing data.
- Standalone RAOP audio playback using the existing Starfish BUFFERSTREAM and PCM output path for negotiated AAC-LC, AAC-ELD, ALAC, and PCM streams.
- Audio session negotiation and lifecycle diagnostics, volume/mute handling, existing RAOP pause/flush/reconnect handling, and focused audio-mode tests.
- A large pairing PIN overlay, success/cancel/reset lifecycle state, a dedicated Buy Me a Coffee QR panel, and deterministic QR asset checks.
- Confirmed Buy Me a Coffee profile integration in the About page, README badge, and GitHub funding metadata.
- UxAir release documentation, source-package instructions, ARMv7 RC artifact workflow, checksum generation, and separate Audio session history.

### Changed

- Final branding consistency: launcher title UxAir, subtitle AirPlay Receiver for webOS, concise app description, and `@depius` UI attribution without a personal name.
- Verified HEVC and 60 FPS options no longer carry obsolete experimental labels; sender codec/rate selection and unmeasured display FPS remain accurately described.
- Replaced the legacy UxPlay launcher identity with `com.github.depius.uxair`; old development installations are retained as a separate app and are never removed automatically.
- Updated public metadata, executable naming, packaging, and CI artifact names for UxAir.

### Limitations

- Standalone AirPlay music/audio-only playback was confirmed on the LG OLED B7 in the tested workflow. Individual negotiated codec variants were not tested separately; protected streams and AirPlay 2 remain outside the verified scope.
- The original owner-provided QR image was not present in the RC2 attachment or workspace; the bundled offline image is generated for the confirmed profile URL.
- HDR10/Main10, native 4K scanout, and output/display FPS measurement remain unavailable.

## [Unreleased]

### Fixed

- Compose pairing and donation dialogs over the active UI in a single SDL frame; pairing Back restarts the RAOP listener to abandon the pending authentication attempt.
- Preserve pairing overlays across redraws and avoid repeated presentation while a pairing PIN remains unchanged.

### Changed

- Offer 720p, 1080p and 4K independently of the selected codec, with valid atomic H.264/HEVC profile transitions.
- Simplify codec labels and use the UxAir navy/blue theme, updated launcher artwork and RC3 package branding.

### Added

- Configurable advertised maximum of 30 or experimental 60 FPS for H.264 720p/1080p and HEVC up to 4K.
- Separate diagnostics for advertised maximum, incoming rate, PTS cadence, accepted Feed rate and decoder callbacks.

### Changed

- Persist the selected frame rate; remove the obsolete HDR preference while ignoring it when loading existing settings.
- Lower the advertised rate to 30 FPS after a 60 FPS decoder failure, with restoration of the previous receiver profile if recovery fails.

## [0.1.0] - 2026-09-07

### Added

- Experimental native AirPlay receiver for rooted webOS TVs.
- H.264 mirroring, stereo PCM/AAC/ALAC audio handling, PIN pairing, AirPlay discovery, and adjustable audio delay.
- Reproducible local build, ABI verification, source-package generation, sanitizer-backed host tests, and release documentation.

### Known limitations

- Validated only on the LG 32LJ600B-SA with firmware 06.10.60.
- DRM/HLS, HEVC/HDR, recording, startup at boot, and broad-device compatibility are unsupported or unverified.
