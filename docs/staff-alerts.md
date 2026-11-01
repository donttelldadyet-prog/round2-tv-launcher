# Staff alerts (push notifications on staff phones) – built 1 Nov 2026

The control centre (https://staff.round2.sg, saved to the Home Screen) can show real notifications. The Cloudflare
engine `round2-book` sends them itself (Web Push: RFC 8291 encryption + RFC 8292 VAPID, WebCrypto). No Make, no
extra service.

## For staff
1. iPhone: Safari › staff.round2.sg › Share › **Add to Home Screen**, then open it **from that icon** (iOS 16.4+).
2. Tests & systems › **Alerts** › tick what you want › **Turn on alerts** › Allow.
3. Use the Test buttons (plain test, a sample booking alert, 1 hour before for the next booking and for the next calendar note – sent to this phone only, marked TEST).
Needs the guest messages staff code on the phone (Keys).

## Alerts
| Kind | When | Example |
|---|---|---|
| `booking` | SimplyBook callback (`/sb/notify/<key>`): create / change / cancel | **⛵ ATLAS · Clarence Tia** (+ CHANGED / CANCELLED / in N min) · Sat 14 Nov, 11am–2pm · 3h · S$35 paid (EB+peak) · 2 pax |
| `soon` | Cron every 10 min, 9:45am–2am SG only: a session 50–62 min away (not if keyed in within the hour), bookings **and** calendar notes | **🎀 VALE · Indra · in 1 hour** · Arrives 11am · 3h (till 2pm) · 2 pax (pax decides room set-up and controllers; from the booking field, or "4 pax" written in a note) |

- Room icon = the character's icon from the booking page, in the room colour where an emoji exists: 🎀 Vale, 🍃 Kaeden,
  🪐 Scarlett, 🏔️ Kai, 🔮 Bella, ⛵ Atlas, 🏎️ Max (iPhone always shows the app icon itself).
- Rate: Peak / Off-peak / EB / EB+peak (Early Bird on a peak day), from the SimplyBook service.
- Money: our own booking page's record ("paid online"), else SimplyBook's invoice ("paid" / "NOT PAID"), else the
  room's price for that day and length ("price").
- Skipped: the Make mirror's SYSTEM BLOCK bookings/notes (twin room), EXT notes (extensions), notes not in a room.
- Same booking again (changed) replaces its earlier alert (tag `bk<id>`). Tapping an alert opens the control centre
  (1-hour and today's bookings → In session).

## Engine (round2sg-home `worker/round2-book.js`)
- Secret `VAPID_JWK` (private signing key, generated in Cloudflare, never shown). Public half: `GET /push/key`.
- `POST /push/sub|state|unsub|test|preview` with `{staff, …}` (staff = GM_STAFF). Body is JSON sent as text (no CORS preflight).
  `preview` shows what an alert would say (`{kind:'soon'}` or `{bid}`), sends nothing.
- Phones are kept in the HoldBook Durable Object (`push` table); one the push service says is gone (404/410) is dropped.
- Cron trigger `*/10 * * * *` → `scheduled()` → `soonRun`: asks SimplyBook only 9:45am–2am SG, only when a phone wants `soon`, today only (tomorrow near midnight); table `soon` stops repeats. Booking alerts are pushed by SimplyBook 24/7.
- `/status` shows `pushAlerts: true` when the key is set.

## Control centre (`manager/`)
- `sw.js`: `push` → showNotification, `notificationclick` → focus/open with `?tab=…`.
- `index.html` › `Alerts` module (Tests & systems › Alerts).

## Next alert ideas
Food order paid / food at pickup; Loot dropped not OK'd after 10 min; TV offline or Homebrew failsafe in opening hours;
guest message not shown; Madd payout approval. Add the kind to `PUSH_KINDS` (engine) and `KINDS` (control centre).
