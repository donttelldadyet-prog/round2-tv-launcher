# Event backgrounds (themes)

Push a background to the TVs for an event (Halloween, Christmas, CNY, a tournament night) without reinstalling anything.

**Easiest: the Theme manager** at https://donttelldadyet-prog.github.io/round2-tv-launcher/ (Windows, Mac, phone; can be installed as an app). It does everything below with buttons. The rest of this page is for editing by hand.

## Add one

1. Make an 8-second seamless loop (OpenArt: same first and last frame works well). Either copy its download link, or upload the video file into this `themes` folder (GitHub: **Add file > Upload files**, under 25 MB).
2. Add an entry to `themes/themes.json` and commit:

```json
{
  "themes": [
    {
      "id": "halloween-2026",
      "name": "Halloween Night",
      "source": "https://...link to the video... (or the file name in this folder, e.g. halloween.mp4)",
      "rooms": ["all"],
      "from": "2026-10-25",
      "until": "2026-11-01",
      "takeover": true
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `id` | Short name: lowercase letters, numbers, dashes. Change it to publish a new version. |
| `name` | Shown in Options > Background. |
| `source` | Video link, or the file name of a video uploaded to this folder. |
| `rooms` | `["all"]`, or some rooms: `["kai", "bella", "maximillion"]`. |
| `from` / `until` | Dates it's live (both days included). Leave out for always. |
| `takeover` | `true`: becomes the room's background while live, and goes back afterwards by itself. `false`: only added to the Background list. |
| `cards` | Where the TV's description card sits so the logo stays visible: `right`, `low` (bottom right), `top` (top centre), `middle`, `bottom` (bottom centre). |
| `focus` | Optional: where the launch transition zooms (e.g. `"50% 40%"`). |

3. The **Publish themes** action (Actions tab) encodes the video for TVs and publishes it, in a few minutes.

TVs check for new themes when they start, every 3 hours, and when someone returns to the home screen (at most every 30 minutes). Videos download up to 14 days before `from`, so they're ready on the day; ended themes are deleted from the TVs. To push one to a TV right away: `.\round2tv.ps1 themes`.

Staff can still pick another background in Options during an event; that room then keeps its choice until the event ends.

To remove a theme early, delete its entry and commit.
