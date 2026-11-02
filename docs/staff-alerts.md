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
| `booking` | SimplyBook callback (`/sb/notify/<key>`): create / change / cancel | Bold: **⛵ ATLAS · Clarence Tia**. Below – new: (⏰ Starts in N min) Sat 14 Nov, 11am–2pm · 3h · S$35 paid (EB+peak) · 2 pax; cancelled: ❌ CANCELLED + the freed time; moved: 🔁 MOVED · was … · now … (engine remembers each booking's last times, table `bkseen`; `/push/seed` filled it for existing bookings); otherwise ✏️ CHANGED + new details |
| `soon` | Cron every 5 min (since 2 Nov; other jobs stay on 10), 9:45am–7am SG: a session **exactly 1 hour** away (55–61 min window, lands on the quarter-hour start, same time as SimplyBook's own 1-hour reminder email) (not if keyed in within the hour), bookings **and** calendar notes | **🎀 VALE · Indra · in 1 hour** · Arrives 11am · 3h (till 2pm) · 2 pax (pax decides room set-up and controllers; from the booking field, or "4 pax" written in a note) |

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

## Late session (staff.round2.sg/late/, also In session › Late session)
Standalone page with its own Home Screen button ("Late session"); the control centre tab shows the same page (`?embed=1`).
Room, date, arrives, guest leaves, name, pax → Check (engine `/gm` action `late`, dry run + clash list) → Add to SimplyBook:
ONE note from arrival to guest end + 15 min cleaning (past midnight too), text `<name> <pax> pax · late till 1am · control
centre`; Vale/Atlas twin gets a SYSTEM BLOCK. Needs the guest messages staff code (asked once on that phone).

## Auto wrap-up (TV messages › Auto wrap-up) – built 1 Nov 2026, starts OFF
Switch (engine mode `autoEnd`, staff code, `/gm` action `auto`). When on, every session today (bookings + calendar notes,
late sessions; last night's too until 6am) gets "About 10 minutes to go" (guest end − 10) and "That's a wrap!" (guest end)
as soon as the engine sees it. Kept in sync every 10 min (cron) and straight away on every SimplyBook callback: new →
added, moved/extended → moved, gone → cancelled (only messages the auto made; more than 4 at once → nothing cancelled,
logged). Messages already set (+1h, Late session, Round2 messages) are adopted, never doubled. Per-session "Auto off".
Skips SYSTEM BLOCKs and stray EXT notes. Last change in kv `autoLast`.
No Make at all (owner, 1 Nov): the engine keeps its own schedule (table `autoq`) and its alarm writes the message straight
to the room's TV in Firebase (id `A…`), guest message not OK'd yet goes first, 5-minute reminder still follows. While the
switch is on, +1h and Late session don't add Make messages (existing ones still move). These auto messages don't appear
in Round2 messages (that list is Make's); the Auto wrap-up tab is where to see them.

## Next alert ideas
Food order paid / food at pickup; Loot dropped not OK'd after 10 min; TV offline or Homebrew failsafe in opening hours;
guest message not shown; Madd payout approval. Add the kind to `PUSH_KINDS` (engine) and `KINDS` (control centre).

## Compact layout (owner, 2 Nov 2026)
Little room in an iPhone banner, so details are one line and extras go at the end, only when they matter:
- **New booking:** `Sun 15 Nov, 11am–2pm · 3h · S$35 paid (EB+peak) · 2 pax · 13d ahead` – the end is `⚡ in 3h` (or `in 45m`)
  for a same-day booking, `Nd ahead` otherwise (none when the "⏰ Starts in N min" line is already there).
- **1 hour before:** `5:30pm–8:30pm · 3h · 2 pax` + `🧹 prev out 4:45pm` when the previous group (this room or its twin)
  is still in at reminder time + the guest's TV message for this room + start: `🎂 7:15pm` (💞 anniversary, 🎉 celebrate,
  💬 note) when written, `🔗 msg not written` when only the link was sent. Notes keep their text on the next line.
- Engine: `soonMsg`, `soonExtra`, `bookingAlert`; `/push/preview {kind:'soon', key, day}` shows one session's reminder
  (System check step "1-hour reminder shows the guest message").

