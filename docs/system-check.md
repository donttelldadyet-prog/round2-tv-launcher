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

## What it covers (2 Nov 2026)
Engine up + keys · SimplyBook login and the 7 rooms · public holidays · booking page price = checkout price · food menu read +
price check · guest message link full life (make → open → write → timer → cancel) · TVs reporting in (msgShown/msgAck) ·
background jobs heartbeat (cron `cronLast`) · Late session note → In session + Upcoming list · +1h on a late note (stretched,
text rewritten, no EXT) · Undo · Change (room move) · Vale ↔ Atlas twin + clash · "↓ AFTER 12AM" labels · cancel cleans up ·
today's wrap-up messages not doubled / not orphaned · GitHub through the engine · public pages up.

## How it stays safe
Test sessions are named `ZZTEST`, go in at 3–6am on a free night about 6 weeks ahead (so no TV message, no +credits banner,
no guest), and are always removed at the end, also when a step fails. A guest-link test uses a date 40 days ahead and is
cancelled. Nothing is paid, nothing goes to Telegram or a phone.

## Not covered (test by hand, in daylight)
Real payments (Test bench / Test an order use the HitPay sandbox), the kitchen Telegram slip and "Loot dropped" (pings the
Madd Pizza group), phone push alerts (Alerts › Test), +1h on a real SimplyBook booking (EXT-note route), what each TV
actually shows on screen.
