# UxAir third-party components and attribution

UxAir is distributed under GPL-3.0-or-later. It is an adaptation of UxPlay and the existing webOS port, not a clean-room or from-scratch receiver. Exact source revisions and archive checksums are in `dependencies.lock.json`.

## Project credits

- UxAir development and adaptation: Matej Skuhala / [@depius](https://github.com/depius).
- Original UxPlay AirPlay/RAOP project: FDH2 / [FDH2/UxPlay](https://github.com/FDH2/UxPlay).
- Original webOS port and prior project maintenance: Daniel Filho / [@danlflh](https://github.com/danlflh).
- The pinned UxPlay core is built from its recorded upstream revision. UxAir supplies the webOS Starfish/ACB integration, settings, UI, diagnostics, packaging, and focused instrumentation; `tools/instrument-uxplay.py` applies bounded development diagnostics patches to generated upstream source when that build mode is selected.

These credits are consistent with the repository history (the current adaptation history is authored by the `depius` account) and the existing port notices. Original copyright and license files remain in place.

| Component | Use | License |
| --- | --- | --- |
| FDH2/UxPlay 1.74 | AirPlay/RAOP, pairing, mDNS; unchanged upstream core | GPL-3.0-or-later; library files also carry LGPL notices |
| llhttp (within UxPlay) | HTTP parser | MIT |
| OpenSSL 3.5.8 | Statically linked cryptography | Apache-2.0 |
| libplist 2.7.0 | Statically linked property lists | LGPL-2.1-or-later |
| FFmpeg 8.1.2 | Statically linked AAC/ALAC/PCM decoding and resampling | LGPL-2.1-or-later (no GPL/nonfree options enabled) |
| GCC 12.2 libstdc++ | Private shared C++ runtime from the webOS Brew native toolchain | GPL-3.0 with GCC Runtime Library Exception 3.1 |

The build uses GCC, an eligible compilation process for the Runtime Library Exception. The runtime is private to this application and does not replace system libraries. GCC source: https://gcc.gnu.org/pub/gcc/releases/gcc-12.2.0/ . Runtime exception: https://www.gnu.org/licenses/gcc-exception-3.1.html .

The webOS Brew native toolchain and samples provide the build environment, headers, link stubs, and technical reference for the port; the samples are not bundled. The native backend was also informed by `mariotaku/ss4s` PCM payload documentation. SDL2, SDL2_ttf, LG fonts, glibc, Starfish Media Pipeline, and ACB are supplied by the TV and are not redistributed.

Daniel Filho ([@danlflh](https://github.com/danlflh)) created the original webOS port. UxAir is maintained and adapted by Matej Skuhala ([@depius](https://github.com/depius)). Run `tools/source-package.py` to create a corresponding-source archive containing this application's source, the pinned UxPlay core, the original dependency source archives, and build instructions. All static libraries can be rebuilt and the app relinked using `tools/build.sh`; installing the resulting `.ipk` on the rooted target uses `tools/tv.sh install`.
