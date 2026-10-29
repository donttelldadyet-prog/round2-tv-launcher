# Website visitor stats (round2.sg game menu → Firebase → control centre)

Branches: `site-stats` in **round2sg-home** (the tracker) and `site-stats` in **round2-tv-launcher** (the Website tab).
Built 29 Oct 2026 by the other owner's Claude session. Nothing is live until both are merged to `main` **and** the Firebase rule below is in.

## Why

round2.sg is a Wix site, but its homepage (and booking) is the game-style menu from `round2sg-home`, embedded full-screen
("Embed HTML → Website address"). Wix analytics only sees "someone loaded the homepage"; everything inside the menu
(rooms, booking form steps, WhatsApp clicks) is invisible to it. Wix also only reports yesterday and older, so there was
no "on the site right now". The tracker lives inside the menu page and writes counts to the same Firebase Realtime
Database the TVs use, under a new `/web` branch. No new accounts, no Make operations, free at this traffic.

## What is stored (numbers only)

```
/web/days/<YYYY-MM-DD, Singapore date>/<counter>   number, +1 per visit that did it (once per visit)
/web/live/<random visit id>                        { t: server time, p: screen }  refreshed every 30 s, deleted on leave
```

Counters: `visit`, `h00`…`h23` (hour the visit started, SG), `dev_m` / `dev_d` (phone / computer), `entry_<screen>`,
`rooms`, `room_<vale|kaeden|scarlett|bella|kai|atlas|maximillion>`, `gallery`, `loadout`, `how`, `find`, `info_<faq|rules|about|privacy>`,
`book` (booking popup opened), `sb_<step>` (SimplyBook form steps, from its `stepChanged` messages: `category`, then room type, add-ons,
room, time, contact details… whatever SimplyBook calls them), `out_<wa|tg|ig|tiktok|maps|email|phone|simplybook>` (links clicked).

A "visit" is one browser tab's session (`sessionStorage`), so refreshing doesn't double count. `?notrack` or `?sim=` turns it off for a load.
No names, contact details, IPs or cookies.

## Firebase rule (add once, in the Firebase console)

Firebase console › project **round2-tv** › Realtime Database › **Rules**. Inside the top-level `"rules": { … }`, add the
`"web"` block below next to the existing entries (keep everything that's already there; mind the comma between entries),
then **Publish**.

```json
"web": {
  ".read": true,
  "live": {
    "$sid": {
      ".write": "$sid.matches(/^[a-z0-9]+$/) && $sid.length <= 24",
      ".validate": "newData.hasChildren(['t']) && newData.child('t').val() === now",
      "t": { ".validate": true },
      "p": { ".validate": "newData.isString() && newData.val().length <= 24" },
      "$other": { ".validate": false }
    }
  },
  "days": {
    "$day": {
      "$k": {
        ".write": "$day.matches(/^20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/) && $k.matches(/^[a-z0-9_]+$/) && $k.length <= 32",
        ".validate": "newData.isNumber() && newData.val() === (data.exists() ? data.val() : 0) + 1"
      }
    }
  }
}
```

What it allows: anyone can **read** `/web` (it's only counts), add **+1** to a counter, and write/delete their own live entry.
Nothing else under `/web`, and nothing outside it changes (the TV data rules are untouched). Deleting a live entry is allowed
so the control centre can tidy entries older than 10 minutes.

Check: open the control centre › Website. If it shows "401", the rule isn't published yet.

## Files

| Repo / file | What |
| --- | --- |
| round2sg-home `index.html` | Last `<script>` before `</body>`: the tracker. It wraps `DCLogic.prototype.setState` (the runtime's component base) to see screen changes, listens for SimplyBook `stepChanged` postMessages, and catches outbound link clicks. Best-effort: any failure is ignored. |
| round2-tv-launcher `manager/index.html` | New main tab **Website** › Visitors (`Web` module): live count every 15 s, today / 7 / 28 days, booking funnel, busy hours and days, rooms looked at, contact clicks. |

## Notes for later

- Wix's own analytics (API, via the Wix connector) still has the history before this existed: sessions by hour/day, sources (Google, Instagram ads…), but not what happens inside the menu.
- If the menu's state names change (`menu`, `pick`, `book`, `gal`, `info` in the component's `this.state`), update `seen()` in the tracker.
