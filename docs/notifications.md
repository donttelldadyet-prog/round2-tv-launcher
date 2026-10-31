# TV notifications: timers and priorities (start here)

One page for everything that pops up on a room's TV: guest messages, Round2 messages, the session-end reminders, the
"+N CREDITS ADDED" banner and the food alerts. Owner's rules as of **31 Oct 2026**. Details and history are in
[guest-messages.md](guest-messages.md); this page is the summary another Claude should read first.

Words the owner uses: **big banner** = on the Round2 home screen; **small banner** = over an app (Netflix, YouTube,
PlayStation/HDMI). "Needs OK" = must be acknowledged.

## 1 · Who gets the TV, and how a banner behaves

![Who gets the TV](img/notif-1-who-gets-the-tv.png)

```mermaid
flowchart TD

  subgraph S["1 · WHO GETS THE TV (one message slot per TV)"]
    G["Guest message<br/>birthday · note · anniversary"]
    R["Round2 message<br/>scheduled or Send now"]
    C["+N CREDITS ADDED<br/>(In session)"]
    F["Food: Order locked in /<br/>Loot dropped"]
    R --> RB{"Guest message on the TV,<br/>not OK'd yet?"}
    RB -- yes --> RW["Waits, checked every 2 min"] --> RB
    RB -- no --> SLOT
    C --> CB{"Any message on the TV not OK'd?<br/>(10-min / wrap / older credit don't count)"}
    CB -- yes --> CW["Waits in line, every 30 s, up to 1 h"] --> CB
    CB -- no --> SLOT
    G --> SLOT["Goes on the TV"]
    F --> FS["Own card, comes before messages<br/>(a message waits 30 s while Loot dropped is up)"]
  end

  SLOT --> W{"Where is the guest?"}
  W -- "TV off" --> OFF["Waits"]
  W -- "Round2 home screen" --> BIG["BIG banner"]
  W -- "Netflix / YouTube / PS / HDMI" --> SMALL["SMALL banner over the app"]
  OFF -. "TV switched on: straight away" .-> W
  BIG -- "they open an app" --> SMALL
  SMALL -- "they press Home" --> BIG
  SMALL -- "they switch app (Netflix button)" --> SMALL

  BIG --> A{"Needs OK?<br/>(same rule for big and small)"}
  SMALL --> A
  A -- no --> AUTO["Closes by itself<br/>big 1 min · small 45 s<br/>never shown again"]
  A -- yes --> STAY["Stays until OK<br/>Back / other buttons only shake it"]
  STAY -- "OK pressed" --> DONE["Done · control centre shows Acknowledged"]
  STAY -- "closed some other way" --> AGAIN["Back within 5 min<br/>(Home / app switch: straight away)"] --> STAY
  STAY -- "cut-off reached" --> CUT["Gone, never shown again"]
```

Each TV has **one message slot** (Firebase `/r2/<TV_KEY>/<room>/msg`; OK goes to `msgAck`). Priority:
**guest message > Round2 message > +N CREDITS ADDED**. Food alerts have their own card and come before messages.

## 2 · Session end and extensions

![Session end](img/notif-2-session-end.png)

```mermaid
flowchart LR
  subgraph E["2 · SESSION END (example: session ends 8:00pm)"]
    T1["7:50 · About 10 minutes to go<br/>needs OK · gone 7:54"] --> Q{"OK pressed?"}
    Q -- yes --> T3
    Q -- "no, and still on the TV" --> T2["7:55 · About 5 minutes to go<br/>needs OK · gone 7:59:30"] --> T3
    T3["8:00 · That's a wrap!<br/>needs OK · gone 8:15 OK or not"] --> CL["8:00–8:15 cleaning"]
  end
  subgraph X["Guest extends (In session +1h)"]
    X1["EXT note(s) in SimplyBook<br/>+ SYSTEM BLOCK in Vale/Atlas twin"] --> X2["10-min + wrap move to the new end<br/>(added if there were none)"] --> X3["+1 CREDIT ADDED on the TV<br/>(last in line)"]
  end
```

## 3 · Cut-offs (gone at the latest, OK or not)

| Message | Cut-off |
|---|---|
| Guest message (birthday, note, anniversary, celebrate) | booking start + 3 h (follows the booking if it moves) |
| Round2 message (control centre) | show time + 3 h |
| About 10 minutes to go | + 4 min |
| About 5 minutes to go (only if the 10-min one wasn't OK'd) | 30 s before That's a wrap |
| That's a wrap! | + 15 min (guests usually just read it and leave) |
| +N CREDITS ADDED | 20 min if it needs OK, 5 min if not; waits in line up to 1 h |
| Order locked in | 20 s |
| Loot dropped | until OK; back every 5 min if not on screen |
| Test bench | 1 h |

No OK needed: big banner 1 min, small banner 45 s, then never again.

## Where each rule lives (code map)

| Rule | Where |
|---|---|
| Queue, push 1 min before, Round2 waits for an unread guest message (every 2 min) | round2sg-home `worker/round2-book.js` `qRun` |
| Messages follow their booking (moved/cancelled) | same file, `follow`, `/sb/notify/<key>` |
| 5-minute reminder | same file, `nudgeAdd` / `nudgeRun` |
| Credit banner, waits in line (every 30 s, 1 h) | same file, `sessBanner`, `tvBusy`, `bqRun` |
| In session (+1/+2/+3 h, EXT notes, Vale/Atlas SYSTEM BLOCK, undo) | same file, `insess`, `extend`, `extUndo`; control centre `manager/index.html` `Sess` |
| Cut-off values written into each message | Make GUEST MESSAGE LINKS (`end`/`until`), `manager/index.html` send-now |
| Android small banner, OK/shake, cut-off, re-show, Home → big banner | `app/.../FoodAlerts.java` (`msgTick`, `showMsgPage`, `menuInFront`, `menuLeft`) |
| Android big banner hand-off, page still loading | `app/.../MainActivity.java` `showMsg`, `pendingMsg` |
| Big banner on the home screen (Android + LG), cut-off, coin animation | `web/index.html` `Msg` |
| LG small banner (Round2 Alerts app) | `webos/alerts/index.html` |
| LG helper: when to pop, card.up, Home / app switch / power on | `webos/service/round2d.sh` `watch_apps`, `msg_tick`, `card_up`, `food_again` |

## Tested / not yet tested on real TVs (31 Oct 2026)

- Tested by the owner: small banner over Netflix on Bella (LG); Home → big banner bug found and fixed (v1.0.187).
- Fixed, waiting for a TV test: big banner closing at its cut-off (v1.0.189, Max); app-to-app switch on LG;
  5-minute reminder; credit banner waiting in line.

Redraw the pictures from the mermaid blocks above (`mmdc -i x.mmd -o x.png -b '#06050A'`) when a rule changes.
