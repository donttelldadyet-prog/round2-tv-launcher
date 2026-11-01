# Round2 – notes for Claude

- **Run the System check before and after any change** (control centre › Tests & systems › System check). Add a step for
  every new feature that touches another part. Why and how: `docs/system-check.md`. This is the owner's standing rule.
- The owner describes behaviour; he doesn't read code. Explain results in plain words, and keep `manager/systems.json`
  (All systems / Planned) up to date when something changes stage.
- Times are Singapore time everywhere. The shop's "day" runs until staff sleep: a session starting 12am–11am belongs to
  the night before. Guest end = SimplyBook end − 15 min cleaning. Booking times go in 15-minute steps.
- The booking engine lives in the other repo: round2sg-home `worker/round2-book.js` (deployed to Cloudflare `round2-book`).
- **Kaeden is under renovation (since ~30 Oct 2026, open-ended).** Its long "room blocked" note in SimplyBook stays – don't
  remove it or "fix" Kaeden having no free slots. Its TV should still run and report in like the others, ready for reopening.
