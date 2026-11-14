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

## How an alert travels (in plain words – owner asked 14 Nov)
**A booking alert is sent when the booking is in the SimplyBook calendar – not at the moment of payment.**
1. *Online booking:* guest pays at HitPay → HitPay tells the engine → the engine checks the payment is real and writes the
   booking into SimplyBook → SimplyBook calls the engine back ("booking created") → the engine reads that booking and sends
   the alert. A few seconds in all. So an alert always means "this is really in the calendar", and "S$… paid online" in it
   comes from our own payment record.
2. *Booking made any other way* (in SimplyBook, by staff, changed or cancelled there): the same last two steps – SimplyBook
   calls the engine, the engine sends the alert. No payment is involved; the money line says what SimplyBook's invoice says.
3. *A move done through Round2* (guest's own rescheduling page, or Move a booking): one "🔁 RESCHEDULED" alert from the
   engine itself; the plain "changed" alert SimplyBook would cause is skipped.
4. *1 hour before:* not caused by anything happening – the engine's 5-minute timer looks at today's sessions and sends the
   reminder for each one that starts in an hour (once).
5. *Noise-rule clash, "needs a human":* sent by the engine when it notices them.
Then the road to the phone is the same for all of them: engine → Apple's (or Google's) push service → the phone shows it and
tells the engine "shown". A phone that hasn't said so gets the alert again after ~50 s and then about every minute for a few
tries (see "Why alerts sometimes come late").

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
**Since 12 Nov – real booking first:** with the guest's MOBILE (email optional), a daytime session (start 11am–8:15pm, guest out by
11:15pm, 3–6 whole hours, nothing in the way) becomes a REAL SimplyBook booking instead (engine `lateBooking`): its own contact,
pax, twin/noise/Auto wrap-up, and a guest booking link (`book.round2.sg/b/<code>`, also written in the booking's SimplyBook comment)
shown with Copy link / WhatsApp it. With an email the guest also gets SimplyBook's confirmation email. Everything else stays a note
(Check says why). Upcoming lists these bookings with their link; change them in Bookings › Move a booking. Full flow:
round2sg-home HANDOFF.md (top).

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
- **1 hour before:** line 1 `5:30pm–8:30pm · 3h · 2 pax`; line 2 only when there's something to do (owner: "prev out" was
  hard to read): `🧹 Last group leaves 4:45pm` when the previous group (this room or its twin) is still in at reminder time,
  and the guest's TV message for this room + start: `🎂 Message 7:15pm` (💞 anniversary, 🎉 celebrate, 💬 note) when
  written, `🔗 Message not written yet` when only the link was sent. Notes keep their text on the last line.
- Engine: `soonMsg`, `soonExtra`, `bookingAlert`; `/push/preview {kind:'soon', key, day}` shows one session's reminder
  (System check step "1-hour reminder shows the guest message").


## Why alerts sometimes come late, and in twos or threes (looked into 14 Nov 2026)
What the engine's delivery log shows (KV `diag:push`, each phone reports back when it has shown an alert):
- Every alert leaves the engine within seconds and Apple accepts it. Most phones show it in 1–3 s.
- Any of the iPhones is sometimes late (never the same one), mostly when it has been lying locked for a while.
- **Nearly every late alert appeared within 2 s of one of our re-sends** (then every 5 min). So a push that reaches an idle
  iPhone can sit undelivered, and the next push to that phone releases it – together with every copy sent meanwhile. That
  is where "late, and 3 at once" came from: the original + two re-sends, all arriving in the same second.
- This is on Apple's side (others report the same on iOS 18.7 / 26 for locked, idle phones – Apple forum thread 802239);
  the control centre being open, in the background or closed makes no difference.

What we changed (engine c9958b6 + `manager/sw.js`, 11 and 14 Nov):
1. **First re-send after ~50 s**, then about every minute (~2, ~3, ~4½, ~9 min; 5 at most – `PUSH_RETRY_GAPS`) instead of
   waiting for the 5-minute timer. (First real result, 14 Nov 1:15pm: one phone released at +57 s, one only at +235 s.) It runs on the
   hold timer's alarm (DO kv `pushchk`, `pushChk()`, `pushRetry`); the 5-minute timer is the backstop. Only to phones that
   haven't reported showing it, and only phones that reported anything in the last 3 days.
2. **`Topic` header** = the alert's tag: while a phone hasn't taken an alert, a newer copy replaces it at Apple instead of
   queueing behind it. If a push service refuses the header (HTTP 400) the alert is sent again without it.
3. **The phone closes an older alert with the same tag** before showing a new one (`sw.js`) – iPhones don't do that by
   themselves. A phone gets this the next time the control centre is opened on it.
4. **One signed pass per push service, reused for 2 hours** (`vapidAuth`, kept in KV `vapid:<service>`). Apple's rule is
   "don't refresh your JWT more than once per hour"; until now every alert to every phone carried a fresh one. Whether this
   was slowing delivery isn't known – it now follows the rule.
5. **Alerts are sent from inside the engine's store** (the Durable Object: `pushAll` → `pushRun`), 14 Nov 10:25am. That morning
   two of four 10:00 reminders failed for every phone – the 5-minute timer's run had used up the free plan's 50 outside
   requests (SimplyBook work + pushes share one allowance). The ~50 s re-send delivered them 1–2 minutes late. Sent from the
   store, each alert has its own allowance.
6. **A sign-up that has shown nothing for a week is removed** (engine 774e2cf, 14 Nov 2:08pm; owner: yes). An old sign-up left
   behind when the app was re-added never goes "gone" at Apple, so it stayed listed for good. Hourly (`pushPrune`, minute 25):
   a phone counts as alive from the day it signed up / last saved its choices and every time it reports showing any alert,
   the Test alert included (DO kv `pshown`). Nothing is removed unless **another** phone reported in the last 2 days, so a
   quiet week or broken reporting can't empty the list. The removed sign-up's choices are kept 60 days (`pushDropped`): if
   that phone opens the control centre again (and notifications are still allowed on it) it signs itself up again with the
   same choices and says so (`Alerts.wake` at start-up, `/push/state` → `dropped`). "Did the alerts arrive?" counts down the
   days for a silent phone and names the ones removed in the last 14 days. System check step: "Phone alerts: a sign-up
   silent for a week is cleared".
To check afterwards: `diag:push` – late alerts should now show `+50s`-ish instead of `+300s` / `+600s`.
In the control centre: Tests & systems › Phone alert setup › **Did the alerts arrive?** (gm `pushsubs`, reading only) – each of
the last alerts and how long each phone took; a phone that hasn't reported for 3+ days is marked as an old sign-up.
Telegram is not a backup for these – see the next section.

## Declarative Web Push – trial on the owner's phone (14 Nov 2026, 3:45pm)
**Why:** 3:30pm, Scarlett's 1-hour reminder: sent 15:30:16; phones showed it at +3 s, +53 s (first re-send) and the owner's at
+115 s (second re-send) – as **3 copies at once**. So the "one copy" claim above (items 2 and 3) was wrong for copies that
arrive together:
- iPhones ignore the alert's `tag` (WebKit bug 258922, open since 2023) – nothing is ever replaced by the system;
- `close()` on an alert is refused in its first ~30 s, and for a burst of pushes `getNotifications()` is stale – so the
  worker's "close the older copy" only works when copies come apart;
- Apple accepted the `Topic` header but the three copies still all arrived – they were probably already on the phone, waiting
  for the control centre's background worker to be started. That waiting looks like the real cause of the lateness: a locked,
  idle iPhone puts off starting a web app's worker.

**What it is:** since iOS 18.4 the alert itself can say what to show (`{"web_push": 8030, "notification": {title, body,
navigate, tag, data}, "mutable": true}`), and the iPhone shows it like a normal app's alert – no worker needed. With
`mutable` the worker is still asked first (so it shows it itself and reports "shown" to the engine as before); if the worker
can't run, the phone shows the alert anyway. No "silent push" penalty. Browsers that don't know it hand the same data to the
worker, which shows it (sw.js `r2Alert`, worker version `SWV = 2`).

**How it is switched (no deploy):** KV `HOLDS` › `push:decl` = `{"ids": ["GGAmdj"]}` (first 6 letters of the phone, as in
"Did the alerts arrive?") or `{"all": true}`; delete the key = everything as before. A listed phone only gets it once its
worker has reported version 2+ (`/push/seen` field `v` → KV `push:swv`) – i.e. after the control centre was opened on it once
(new worker) and it showed one alert (Test alert will do). Such a phone gets **one** re-send at most. `navigate` is
`https://staff.round2.sg` + the alert's page – before `all`, check no phone is still signed up from the old github.io address.

**Reading the trial** ("Did the alerts arrive?", the owner's phone marked "new delivery (trial)"):
- shows in 1–3 s every time, also where the other phones needed a re-send → it works: switch to `all`, drop the quick re-sends.
- not reported ("—") but the owner saw it on time, with one copy a minute later → the phone shows it by itself and the worker
  still starts late: switch re-sends off for declarative phones (nothing to report), on-time and single.
- still late, 2 copies → the hold is before the phone (Apple / power saving); nothing in the alert's shape changes that –
  go back (delete the key) and decide between fewer re-sends (fewer copies, later) and today's (faster, more copies).

**Tidying copies:** when the control centre is opened or brought to the front it keeps the newest alert of each tag and
closes the rest (`r2TidyAlerts`, index.html).

## Telegram: only the food workflow with Madd Pizza – never bookings (owner's rule, 14 Nov 2026)
The Telegram chat (the kitchen bot's group, `KITCHEN_CHAT` in the engine) is for the **Madd Pizza ↔ Round2 food workflow
only**: the kitchen slip for a paid food order and the "Food is at the pickup point" button. Madd Pizza's people are in it.
**Nothing about bookings goes to Telegram** – no new-booking, reminder, noise-rule, reschedule or "needs a human" alerts, no
guest names or numbers. Madd Pizza doesn't need to know them.

- The engine used to have a second, unused road (`staffAlert` → a staff group named by secret `STAFF_CHAT`, never set, so
  nothing was ever sent). Since engine ed9d0be (14 Nov) `staffAlert` never sends, whatever is set. Don't revive it, and
  don't add booking messages to the kitchen chat.
- Booking alerts reach staff through the control centre's phone alerts only (this document).
- What may be added to the Telegram chat later is food-side only, and depends on what is agreed with Madd Pizza (still being
  discussed, 14 Nov): e.g. kitchen opening hours / "kitchen closed" notices, an order not picked up, payout messages
  (today: Make's [CONTROL] MADD PAYOUT TELEGRAM). Ask the owner before adding any message there.
- If the owner's phone alerts stay late after the 14 Nov changes above, the next thing to try is on the phone side
  (re-adding the Home Screen app, iOS update), not Telegram.
