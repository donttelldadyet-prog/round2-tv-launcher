# Round2 tile (PS4 app, fPKG RTWO00001)

Owner 19 Nov 2026: the tile opens the member page (QR), shows progress, and keeps guests from interrupting save prep.
v0.01 = QR to `book.round2.sg/m?room=<room>` + live room status from the engine (`GET /ps4/tile?id=<PS4 id>`).

Build (Linux): OpenOrbis toolchain (release `toolchain-llvm-18`) + .NET Core 3.1 + OpenSSL 1.1 for PkgTool.Core
(`/opt/oo/env.sh` on the build box), then `make` here → `IV0000-RTWO00001_00-ROUND2MEMBERS000.pkg`.
Third-party: qrcodegen (Project Nayuki, MIT), Chakra Petch + Press Start 2P fonts (SIL OFL, assets/fonts), OpenOrbis
samples/_common Scene2D (GPL-3.0, linked from the toolchain).
