# System check – read this before changing anything (owner's standing rule, 2 Nov 2026)

**To every future Claude (and anyone else) working on Round2:** the systems here talk to each other – the booking engine,
SimplyBook, the message timer, Auto wrap-up, Late session, +1h, the SimplyBook labels, the TVs, the food menu, the control
centre. A new feature can quietly break an older one. The owner works by describing the behaviour he wants ("vibe coding")
and relies on us to keep everything consistent, so:

1. **Before you change something, run the System check** (control centre › Tests & systems › System check › Run full check,
   or the same steps by hand). Note what passes.
2. **After your change, run it again.** Anything that went from ✅ to ❌/⚠️ is your bug until proven otherwise.
3. **Add a step for your feature** to the check (`SysCheck` in `manager/index.html`) whenever it talks to another part –
   e.g. "if X happens, Y follows". The check is the living list of how the parts are meant to work together.
4. **Tell the owner in plain words** what the check showed. Its purpose is a real-world test that new work fits the old
   code – not a formality.

## One clock (owner, 2 Nov 2026)
Everything time-based runs on the booking engine's clock (Cloudflare `/time`), never a device's own: the LG web app
(`r2Now()`), the LG helper (`snow`), the Android app (`Clock.now()`), the control centre (`clockOff`). Each measures the
difference every 30 min and adds it. New time-based code must use these, not `Date.now()` / `System.currentTimeMillis()`.

## Screens follow the 7am rule (owner, 2 Nov 2026)
Owner, after In session's "Extensions today" kept last night's +1h until 24 hours had passed: *"how did we not catch that
… as the system gets more complicated we need to catch these sort of interactions."* The engine was tested; the screens
weren't. So now:

- **`manager/r2day.js` is the only place a control-centre screen works out a day:** `R2Day.now()` (engine clock),
  `R2Day.night(ms)` (before 7am = the night before), `R2Day.tonight()`, `R2Day.addDays()`. Every page loads it.
- **Every list that files things under a day, or shows "today / tonight", registers** with `R2Day.list(name, { night,
  today })`, passing the exact functions it draws with. Registered now: TV themes › today, Round2 messages, In session ›
  Extensions today, In session › History, Guest messages, Late session › date.
- **System check › "Screens follow the 7am rule"** opens Guest messages and Late session in hidden frames, collects every
  registered list and runs it at 6:59am, 7:00am, 12:01am, 11:59pm, noon and New Year: each must land on the right night.
  A list in its `NEED` array that stops registering fails the step. It also checks all three pages share one clock.
- **System check › "Watchdog"** reads the code of the control centre, Guest messages, Late session and the TV page
  (published as `src/tv-web.txt` by pages.yml) and flags "24 hours" maths, calendar dates without the 7am shift, the
  device's own calendar/clock, and "today from Date.now()". A line that's right on purpose carries a `day-ok: why` note
  (on it or the line above). It would have flagged the old Extensions-today filter.
- **New list?** Use `R2Day`, register it, and add its name to `NEED`. New page? Load `r2day.js` and add it to the
  watchdog's `FILES` (and to the frames if it has lists).

## What it covers (2 Nov 2026, + 5 Nov)
The tool "Late session" is called **Add a session** on screen since 5 Nov (address still /late/; its internal list name
`Late session › date` and the engine's `late*` actions are unchanged on purpose – the check looks for them). Its notes say
"· till 7pm ·", and "· late till 1am ·" only when the times are late (from 11pm or past 11:15pm); the engine reads both.

Engine up + keys · SimplyBook login and the 7 rooms · public holidays · booking page price = checkout price · food menu read +
price check · guest message link full life (make → open → write → timer → cancel) · TVs reporting in (msgShown/msgAck) ·
background jobs heartbeat (cron `cronLast`) · Late session note → In session + Upcoming list · +1h on a late note (stretched,
text rewritten, no EXT) · Undo · daytime session keyed in says "till 9am", not "late till" (5 Nov), +1h/undo still find it · Change (room move) · Vale ↔ Atlas twin + clash · "↓ AFTER 12AM" labels · cancel cleans up ·
today's wrap-up messages not doubled / not orphaned · GitHub through the engine · public pages up.

## SimplyBook's daily limit (2 Nov 2026)
SimplyBook's shared servers allow about **5,000 API requests a day** (more is "high load"; at most 2 at once, 5 a second).
The engine counts every request it makes (`sbn:<day>` in its kv; `/gm action=auto` → `sbToday`, `sbYesterday`) and the check
shows it ("SimplyBook requests today"; ⚠️ over 3,500, ❌ over 4,800). Measured 2 Nov: background ≈ 90 an hour; **one full
System check ≈ 260**. Keep it lean: logins are reused for 20 min, one timer tick reads each day once for all its jobs, and
the 1-hour reminder only runs on the 10-minute ticks plus :15 and :45.

## How it stays safe
Test sessions are named `ZZTEST`, go in at 3–6am on a free night about 6 weeks ahead (so no TV message, no +credits banner,
no guest), and are always removed at the end, also when a step fails. A guest-link test uses a date 40 days ahead and is
cancelled. Nothing is paid, nothing goes to Telegram or a phone.

## Not covered (test by hand, in daylight)
Real payments (Test bench / Test an order use the HitPay sandbox), the kitchen Telegram slip and "Loot dropped" (pings the
Madd Pizza group), phone push alerts (Alerts › Test), +1h on a real SimplyBook booking (EXT-note route), what each TV
actually shows on screen.
