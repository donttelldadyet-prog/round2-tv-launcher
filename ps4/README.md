# Round2 PS4 code

Small programs ("payloads") that run on the room PS4s after the jailbreak, started by GoldHEN (PayLoader LaunchPad /
AutoRun). Built with [ps4-payload-dev/sdk](https://github.com/ps4-payload-dev/sdk) (clang 18). GPL-3.0.

| Folder | What |
|---|---|
| `nanodns/nanodns.ini` | Settings for nanoDNS (drakmor/nanoDNS v0.4) on every PS4: block every address except Round2 (round2.sg, *.round2.sg, the booking engine) and Google time (GoldHEN's clock update uses time1.google.com). PS4 DNS = 127.0.0.1 / 127.0.0.1, so nothing works until nanoDNS runs (fails closed). |
| `tls/` | Round2's own HTTPS (mbedTLS 3.6.2, TLS 1.2, Mozilla root certificates built in, entropy from the PS4's hardware RNG). The PS4's own sceHttp/sceSsl failed on Kai (0x8095F00C), this works (Vale 13 Nov: round2.sg 200 / engine 200). |
| `probe/` | `round2-probe.elf` – read-only check of a room PS4 (firmware, temperature, disk, Wi-Fi, profiles + account IDs, games, saves, game update downloads, captures, controllers, play history, Sony blocked, HTTPS, speed) + a 30-minute background test. Writes `/data/round2/probe.txt` and `heartbeat.txt`. |

The full plan, decisions and room-by-room results: Claude project doc **ps4-game-saves.md** and `manager/systems.json`
(Members & game saves).

## Rules (from the owner)
- **Never touch PS Vue (CUSA00960) or its half-downloaded update** – it keeps the Vue jailbreak working.
- **Never delete installed game updates** – many are backported; the firmware number in them is not a "broken" sign.
- Keep the 0-byte `/update/PS4UPDATE.PUP` files – they block system updates.
- Guests' screenshots/clips are deleted at clean slate (never kept or uploaded).
- All times use the engine's clock (the PS4 clocks have been wrong for long stretches).

## Build
`./build.sh` (needs git, clang-18, lld-18, python3; fetches the SDK, mbedTLS 3.6.2 and SQLite into `ps4/.deps`).
