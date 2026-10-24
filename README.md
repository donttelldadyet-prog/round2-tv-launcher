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

**One-time setup per TV** (about 20 minutes):

1. On a computer, create a free account at [webostv.developer.lge.com](https://webostv.developer.lge.com) (Sign in > Create account). **Each LG TV needs its own account**: signing a second TV into the same account switches Developer Mode off on the first one (and removes Round2 Home from it). If LG accepts it, Gmail's `+` trick gives you separate addresses, e.g. `you+scarlett@gmail.com`.
2. On the TV, open **LG Content Store**, search **Developer Mode**, install it and open it. Sign in with that account, switch **Dev Mode Status** on (the TV restarts), open the app again and switch **Key Server** on. Leave this screen up: it shows the TV's IP address and a passphrase.
3. On the Windows PC, install **webOS Dev Manager** from [its releases page](https://github.com/webosbrew/dev-manager-desktop/releases) (the `.msi` file). Open it, choose **Add device**, and type the TV's IP address and the passphrase.
4. Download `round2-home.ipk` from this repo's [latest release](https://github.com/donttelldadyet-prog/round2-tv-launcher/releases/latest). In Dev Manager, open **Apps**, click **Install**, and pick the file.
5. On the TV, open **Round2 Home** from the apps row and pick the room.

**Keep Developer Mode alive.** LG switches Developer Mode off when its timer runs out (1000 hours, about six weeks), and that removes Round2 Home from the TV. Dev Manager shows the time left on its Info page and has an **Extend** button (the Developer Mode app on the TV has one too). Extend it when you update the TVs, or at least once a month. If it does run out, repeat steps 2 to 5.

**Updating:** after a change to the menu, download the new `round2-home.ipk` and install it again from Dev Manager (step 4). Settings and the room choice are kept.

**Getting guests to it:** on LG's Home screen, move Round2 Home to the front of the apps row (press and hold OK on it, then **Move**). After opening Netflix or an HDMI input from Round2 Home, the remote's **Back** button returns to it. The **Home** button always opens LG's own home bar.

**What's different on LG:** HDMI ports open LG's own input apps; More apps lists the apps the TV allows Round2 Home to see; AirPlay is built into most LG TVs from 2018 on, so the AirPlay item only explains how to mirror. Event backgrounds download in the background like on the other TVs.

## Notes

- The signing key is in this repo on purpose so builds stay installable over each other. Anyone with the key could build an update, but installing it still needs adb access to the TV, which `lockdown` turns off.
- Background videos and fonts are bundled in the app, so the menu looks right without internet. Rain and menu sounds are generated on the TV, not downloaded.
- Useful toolkit commands: `tizentube` (installs TizenTube from its GitHub releases), `airplay` (PhairPlay), `perf` (measures how smooth the menu is), `status`, `monitor`, `undo`.

## Working on this with Claude

Point Claude at this repo and the website repo. A good opening message: "Read the README in donttelldadyet-prog/round2-tv-launcher. I want to change …".
