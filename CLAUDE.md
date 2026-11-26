# Round2 – notes for Claude

- **Before every release (owner, 27 Nov 2026 3:04am: "everytime a new version is pushed, check design if there are updates"):** before
  building/pushing a new tile, helper, member page or engine version, pull round2sg-home and read `git log` of `design/` + `index.html`
  and HANDOFF "DESIGN → FUNCTION CONCERNS" since the last release – build in anything new for the screens you touch (e.g. a new
  `design/tile/save-screen-v*.html` mockup → the tile), or say in HANDOFF why not.
- **Run the System check before and after any change** (control centre › Tests & systems › System check). Add a step for
  every new feature that touches another part. Why and how: `docs/system-check.md`. This is the owner's standing rule.
  **Two sizes (owner, 16 Nov 2026):** the **Light check** for changes that can't touch bookings (PS4 helper, TV app, the
  control centre itself) – it makes NO SimplyBook requests; the **Full check** for anything touching bookings, notes,
  messages, payments or the engine's booking code – it costs ~650 SimplyBook requests a run (guideline ~5,000 a day, hard
  stop ~10,000; 16 Nov: 8 full runs in a day helped push it past 10,000). Look at "SimplyBook requests today" before a full run.
- The owner describes behaviour; he doesn't read code. Explain results in plain words, and keep `manager/systems.json`
  (All systems / Planned) up to date when something changes stage.
- **One clock:** all time-based code uses the engine's time (Cloudflare `/time`) via `r2Now()` (TV web), `snow` (LG helper),
  `Clock.now()` (Android), `clockOff` (control centre) – never the device's own clock. Times are Singapore time everywhere. The shop night: a session starting before 7am belongs to the night before, and every screen
  switches to the new day at 7am (owner, 2 Nov: staff wake ~10am and want today's list on the way in). Guest end = SimplyBook end − 15 min cleaning. Booking times go in 15-minute steps.
- **Screens:** every control-centre day/“today” list uses `manager/r2day.js` (`R2Day.night`, `R2Day.now`) and registers with
  `R2Day.list(...)`; the System check tests each one at 6:59am/7am and a watchdog flags day maths outside it
  (`docs/system-check.md` "Screens follow the 7am rule").
- The booking engine lives in the other repo: round2sg-home `worker/round2-book.js` (deployed to Cloudflare `round2-book`).
- **Kaeden is under renovation (since ~30 Oct 2026, open-ended).** Its long "room blocked" note in SimplyBook stays – don't
  remove it or "fix" Kaeden having no free slots. Its TV should still run and report in like the others, ready for reopening.
- **Each room's TV behaves differently** (TCL Safety Guard and volume on Kai, Sony running Netflix outside Android on Max,
  Prism+ on Atlas, rooted LGs): read README "Each room's TV: what's different" before changing the TV app or the LG helper.
  Diagnose with the TV's own records first (`tv-tools/round2tv.ps1 diag`) – guessed fixes cost the owner an update each.
- Every push that touches the app (`web/`, `app/`, `webos/`, `tools/`) publishes a new TV version that every TV tile then
  offers. Notes, the control centre and the PC toolkit don't (build.yml `paths-ignore`) – keep it that way.
- **Telegram is only for the Madd Pizza ↔ Round2 food workflow.** Never send anything about bookings to Telegram (owner, 14 Nov).
