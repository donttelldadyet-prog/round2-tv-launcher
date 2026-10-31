# Round2 Home

The home screen for the TVs in the Round2 rooms at 20 Haji Lane. It uses the same game-menu look as the round2.sg homepage ([donttelldadyet-prog/round2sg-home](https://github.com/donttelldadyet-prog/round2sg-home)) and is driven entirely by the TV remote.

Main menu: **Inputs** (PlayStation and guest HDMI ports), **AirPlay** (if PhairPlay is installed), **YouTube** (opens TizenTube, the ad-free YouTube, when installed), **Netflix**, **Prime Video**, **HBO Max** (Deluxe and VIP rooms only), **More apps** and **Options** (background, transition, home animation, menu sounds, rain ambience). There is no "Continue" item on purpose, so a new guest can't reopen the previous guest's app.

## Where things live

| Path | What it is |
| --- | --- |
| `web/index.html` | The menu screen itself. Edit this to change the look, menu items or text. The settings block near the top of the script sets HDMI port names. |
| `app/` | The small Android app that shows the menu full-screen and opens apps and HDMI inputs for it. |
| `.github/workflows/build.yml` | Builds the app on every push to `main` and publishes it as a GitHub release (`round2-home.apk` for Android TVs, `round2-home.ipk` for LG). |
| `webos/` | The LG version's extras: its app info and icons, and a small background service that downloads event backgrounds. `tools/build-webos.sh` packages it. |
| `tv-tools/round2tv.ps1` | Windows toolkit that connects to a TV with adb, backs it up, cleans it up, installs the app and undoes everything. |
| `tv-tools/cleanup.txt` | The approved list of ad and bloat apps to disable on the TCL TVs. |
| `signing/round2-tv.jks` | The app's signing key, so each new build installs over the previous one. |
| `manager/systems.json` | The control centre's **Systems** list: every Round2 system and its stage (live / testing / building / planned / on the shelf / off / broken). **Update it whenever a system changes stage.** |
| `manager/msg/` | Guest messages: the guest page (`?c=CODE`) and the staff page. See `docs/guest-messages.md`. |
| `docs/notifications.md` | **TV notifications at a glance**: flow chart of every timer and priority (guest / Round2 / 10-5-wrap / credit banner / food), cut-offs, code map. Read before touching any banner. |

## Set up a TV (Windows)

Run these from the `tv-tools` folder in PowerShell. Every change is recorded in `undo.ps1`.

```
powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 start
powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 cleanup .\cleanup.txt
powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 tweaks
powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 launcher
powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 homeguard
powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 room vale
```

`start` connects and backs up without changing anything. `launcher` installs the latest release of this repo and makes it the home screen; on Google TV (TCL), then run `homeguard` so the remote's Home button opens Round2 Home. Never use `launcher-force` on Google TV: disabling its home screen starts a recovery loop. On TCL TVs `homeguard` also grants TCL's auto-start permission (`APP_AUTO_START`), without which TCL blocks the Home-button helper. If Home still opens Google TV after `homeguard`, go to TV Settings > Device Preferences > Accessibility > **Round2 Home button** and switch it off and on once. On first start the app asks which room the TV is in; `room <name>` changes it later.

To put everything back: `.\round2tv.ps1 undo`. When finished with a TV: `.\round2tv.ps1 lockdown` turns network debugging off.

## Change the menu

1. Edit `web/index.html` on GitHub (pencil icon) and commit to `main`.
2. Wait for the **Build Round2 Home** action to finish (Actions tab). It publishes a new release.
3. Run `.\round2tv.ps1 launcher` again on each TV to install the update.

To preview the menu on a computer, open `web/index.html` in a browser and use the arrow keys and Enter.

## Backgrounds

Options > Background on the TV lists the room's backgrounds with a live preview: the room's scene loops first, then Round2 Lounge. The choice is saved per TV.

To add a scene loop (e.g. for another room):
1. Add a line to `web/media/sources.txt`: a file name and the video's download link (an 8 s seamless loop works best).
2. Add an entry under `SCENES` in `web/index.html` with that file name. Use `layout: 'low'` when the logo sits where the description card normally is, and set `focus` to the logo's position so the launch transition dives into it.
3. Commit. The build downloads, compresses and thumbnails the video and puts it in the app.

The Kai loops (Neon Summit) were made in OpenArt: still images with Nano Banana 2.1 from the Round2 logo plus style references, animated with Veo 3.1 using the same first and last frame so they loop.

## Other TV brands

- **TCL, Sony and other Google TV / Android TV sets:** this app, as above.
- **Samsung (Tizen):** the menu already has Samsung app IDs, but it must be packaged as a signed Tizen web app. See [CleanFrame](https://github.com/nthnclrk/CleanFrame), which this project uses as its reference for remote navigation and device handling.
- **LG (webOS):** `round2-home.ipk`, built from the same menu with every release. See [LG TVs](#lg-tvs-webos) below.

## LG TVs (webOS)

Scarlett, Vale and Bella have LG TVs. They run the same menu as the other rooms, packaged as an LG app. LG doesn't let an app replace its home screen, so Round2 Home is an app you open (see "Getting guests to it" below).

**Rooted LG TVs (recommended):** if the TV has Homebrew Channel (rooted), skip the Developer Mode setup below and use [Rooted LG TVs](#rooted-lg-tvs) instead: no timer, no PC, and the TV opens Round2 Home by itself.

**One-time setup per TV, not rooted** (about 20 minutes):

1. On a computer, create a free account at [webostv.developer.lge.com](https://webostv.developer.lge.com) (Sign in > Create account). **Each LG TV needs its own account**: signing a second TV into the same account switches Developer Mode off on the first one (and removes Round2 Home from it). If LG accepts it, Gmail's `+` trick gives you separate addresses, e.g. `you+scarlett@gmail.com`.
2. On the TV, open **LG Content Store**, search **Developer Mode**, install it and open it. Sign in with that account, switch **Dev Mode Status** on (the TV restarts), open the app again and switch **Key Server** on. Leave this screen up: it shows the TV's IP address and a passphrase.
3. On the Windows PC, install **webOS Dev Manager** from [its releases page](https://github.com/webosbrew/dev-manager-desktop/releases) (the `.msi` file). Open it, choose **Add device**, and type the TV's IP address and the passphrase.
4. Download `round2-home.ipk` from this repo's [latest release](https://github.com/donttelldadyet-prog/round2-tv-launcher/releases/latest). In Dev Manager, open **Apps**, click **Install**, and pick the file.
5. On the TV, open **Round2 Home** from the apps row and pick the room.

**Keep Developer Mode alive.** LG switches Developer Mode off when its timer runs out (1000 hours, about six weeks), and that removes Round2 Home from the TV. Dev Manager shows the time left on its Info page and has an **Extend** button (the Developer Mode app on the TV has one too). Extend it when you update the TVs, or at least once a month. If it does run out, repeat steps 2 to 5.

**Updating:** after a change to the menu, download the new `round2-home.ipk` and install it again from Dev Manager (step 4). Settings and the room choice are kept.

**Getting guests to it:** on LG's Home screen, move Round2 Home to the front of the apps row (press and hold OK on it, then **Move**). After opening Netflix or an HDMI input from Round2 Home, the remote's **Back** button returns to it. The **Home** button always opens LG's own home bar.

**What's different on LG:** HDMI ports open LG's own input apps; More apps lists the apps the TV allows Round2 Home to see; AirPlay is built into most LG TVs from 2018 on, so the AirPlay item only explains how to mirror. Event backgrounds download in the background like on the other TVs.

### Rooted LG TVs

With Homebrew Channel on the TV, Round2 Home installs and updates from the TV remote, has no Developer Mode timer, and a small helper makes the TV behave like the other rooms:

- switching the TV on opens Round2 Home (also with Quick Start+ on), and the guest's Options changes are reset;
- pressing **Home** in Netflix, an HDMI input or any other app opens Round2 Home;
- pressing **Home** while Round2 Home is showing opens LG's own home screen (for staff: LG settings, the app list, Homebrew Channel).

**Setup, once per TV (with the remote):**

1. LG Settings > Support > Software Update: switch **Auto Update** off. An LG update can remove root and every homebrew app.
2. Open **Homebrew Channel** > Settings (gear, top right):
   - switch **Block system updates** on;
   - switch **Telnet** off, and leave **SSH Server** off unless you're using it. Both give full control of the TV to anyone on the shop Wi-Fi (SSH uses the password `alpine` until a key is added), and Round2 Home needs neither.
   - **Add repository**, type `https://donttelldadyet-prog.github.io/round2-tv-launcher/lg.json` and confirm.
3. Back in Homebrew Channel, pick **Round2 Home** > **Install**, then **Launch**, and pick the room. The helper sets itself up on that first start.
4. Unplug the TV for 10 seconds and plug it back in, so the start-up hook runs once.

Don't install LG's Developer Mode app on a rooted TV.

**Home button:** opens Round2 Home's main menu from anywhere, including from inside Round2 Home. Press **Home 3 times quickly** to get LG's own home screen (for staff, or if the menu freezes).

For this to be reliable, also install **LG Input Hook** from Homebrew Channel and open it once. Round2 Home then sets button `773` (Home) in it to run `sh /var/lib/webosbrew/round2/round2d.sh key`, so the TV never receives Home and LG's home bar can't take over. Without LG Input Hook, the helper only catches LG's home bar after it has opened, which isn't always reliable. If Round2 Home is removed, that setting falls back to opening LG's home. To undo, delete the `773` entry in LG Input Hook.

**Switches:** Options > Staff mode (PIN) > **Open when TV turns on** and **Home button** (OPENS ROUND2 / LG HOME). **Update app** in the same list installs the newest version from GitHub through Homebrew Channel; the menu closes and reopens by itself. Homebrew Channel also shows updates.

**To undo:** switch both off in Options, or delete `/var/lib/webosbrew/init.d/round2` over SSH and restart the TV. The helper's log is `/var/lib/webosbrew/round2/log.txt`.

How it works: `webos/service/round2d.sh` watches LG's power state and which app is in front (Luna `getPowerState` and `getForegroundAppInfo`). Round2 Home writes the start-up hook and switches through Homebrew Channel's root service (`org.webosbrew.hbchannel.service/exec`). The build publishes `manager/lg.json` (the Homebrew Channel listing) with every release.

## Notes

- The signing key is in this repo on purpose so builds stay installable over each other. Anyone with the key could build an update, but installing it still needs adb access to the TV, which `lockdown` turns off.
- Background videos and fonts are bundled in the app, so the menu looks right without internet. Rain and menu sounds are generated on the TV, not downloaded.
- Useful toolkit commands: `tizentube` (installs TizenTube from its GitHub releases), `airplay` (PhairPlay), `perf` (measures how smooth the menu is), `status`, `monitor`, `undo`.

## Working on this with Claude

Point Claude at this repo and the website repo. A good opening message: "Read the README in donttelldadyet-prog/round2-tv-launcher. I want to change …".

> **Booking times:** a SimplyBook booking ends 15 min after the guest's session (cleaning). Guest-facing timing (wrap-up / time's up messages) uses the session end. See [docs/booking-times.md](docs/booking-times.md).

## How Round2's automation fits together (read first) – updated 30 Oct 2026

**Rule: Cloudflare first, Make.com last.** Make has a hard monthly budget (10,000 operations, shared by everything), so
nothing that runs often, polls, or waits may live in Make. Anything fast or frequent runs in the **Round2 engine on
Cloudflare** (free, instant). Make only does the few jobs that need its Google connections, and only when something real
happened (a paid order). Pages must never poll Make, and Make webhook addresses must not appear in public pages – they
go through the engine.

**The Round2 engine** = Cloudflare Worker `round2-book` (`https://round2-book.donttelldadyet.workers.dev`), source
`worker/round2-book.js` in **round2sg-home** (branch `own-booking` until merged). One Durable Object (`HoldBook`) holds its
state (SQLite) and wakes itself with alarms. Deploy = upload that file through the Cloudflare API with `keep_bindings`
(see round2sg-home `HANDOFF.md`). Secrets live only in Cloudflare (names: `HITPAY_KEY`, `HITPAY_SALT`, `HITPAY_API`,
`SB_COMPANY`, `SB_USER`, `SB_PASS`, `GM_STAFF`, `GM_HOOK_URL`, `MENU_MAKE_K`, `TV_KEY`, `TG_BOT_TOKEN`, `STAFF_CHAT`).

| Job | Where it runs | Notes |
|---|---|---|
| Booking page (`slots/`): free times, prices, holds, HitPay, writing into SimplyBook | Engine | SimplyBook is the calendar. Bookings end 15 min after the session (cleaning) – `docs/booking-times.md` |
| Room food menu (`menu/`): pricing check, HitPay payment, payment check | Engine | Same HitPay setting as bookings (sandbox → live by changing the 3 HitPay secrets) |
| Food order → room TV alert + kitchen Telegram slip | Engine | Instantly after payment |
| Food order → accounting sheet + Drive slip (kitchen phone prints it) | Make `[BACKEND] ROOM MENU PAID ORDER` | Called once per paid order by the engine |
| Telegram pickup button, Madd nightly settlement | Make (`[CONTROL] MADD PAYOUT TELEGRAM`, `[BACKEND] MADD NIGHTLY SETTLEMENT LOG`) | |
| Guest/Round2 TV messages: every page talks to the engine's `/gm` front door | Engine → Make `[BACKEND] GUEST MESSAGE LINKS` | Engine checks codes, caches, caps edits; Make keeps the message records |
| Message timer (send each message to its TV on time) | Engine (alarm, 1 min before) | Make's old 15-min timer is OFF |
| Vale ↔ Atlas mirror bookings | Make `[BACKEND] SYSTEM BLOCKS MIRROR ROOMS` | Runs only on SimplyBook changes |
| TVs ⇄ control centre (messages, OKs, food alerts, website stats) | Firebase Realtime Database | Pages listen live (EventSource); no Make |

Docs: round2-tv-launcher `docs/guest-messages.md` (front door, timer), `docs/booking-times.md`, `docs/site-stats.md`;
round2sg-home `HANDOFF.md`, `docs/own-booking-plan.md`.
