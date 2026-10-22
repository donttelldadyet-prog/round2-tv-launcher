# Round2 Home

The home screen for the TVs in the Round2 rooms at 20 Haji Lane. It uses the same game-menu look as the round2.sg homepage ([donttelldadyet-prog/round2sg-home](https://github.com/donttelldadyet-prog/round2sg-home)) and is driven entirely by the TV remote.

Main menu: **Continue** (last app), **Inputs** (PlayStation and guest HDMI ports), **YouTube**, **Netflix**, **Prime Video**, **HBO Max** (Deluxe and VIP rooms only) and **More apps**.

## Where things live

| Path | What it is |
| --- | --- |
| `web/index.html` | The menu screen itself. Edit this to change the look, menu items or text. The settings block near the top of the script sets HDMI port names. |
| `app/` | The small Android app that shows the menu full-screen and opens apps and HDMI inputs for it. |
| `.github/workflows/build.yml` | Builds the app on every push to `main` and publishes it as a GitHub release (`round2-home.apk`). |
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

## Other TV brands

- **TCL, Sony and other Google TV / Android TV sets:** this app, as above.
- **Samsung (Tizen):** the menu already has Samsung app IDs, but it must be packaged as a signed Tizen web app. See [CleanFrame](https://github.com/nthnclrk/CleanFrame), which this project uses as its reference for remote navigation and device handling.
- **LG (webOS):** needs packaging as a webOS app installed through LG's Developer Mode app. Not done yet.

## Notes

- The signing key is in this repo on purpose so builds stay installable over each other. Anyone with the key could build an update, but installing it still needs adb access to the TV, which `lockdown` turns off.
- Room photos, character art, logo and background video load from the Round2 Wix media library, so the TVs need internet for the full look.

## Working on this with Claude

Point Claude at this repo and the website repo. A good opening message: "Read the README in donttelldadyet-prog/round2-tv-launcher. I want to change …".
