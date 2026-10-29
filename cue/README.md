# cue.round2.sg

Short, branded address for guest message links: `https://cue.round2.sg/CODE`.

These files are the whole site. They're kept here for reference and are published from their own repo
(`round2-cue`, GitHub Pages, custom domain `cue.round2.sg`), because a custom domain applies to a whole repo,
and putting one on `round2-tv-launcher` would move the control centre too (and lose the codes saved in each browser).

- `index.html` / `404.html` (identical): read the code from the path, `?c=`, `?` or `#`, and show
  `round2-tv-launcher/msg/?c=CODE` full screen in an iframe. No code → round2.sg.
- `CNAME`: `cue.round2.sg`.
- DNS (where round2.sg is managed, Wix › Domains › Manage DNS): CNAME, host `cue`, value `donttelldadyet-prog.github.io`.

The staff page (`manager/msg/staff.html`, `PUBLIC`/`shareUrl`) switches to `https://cue.round2.sg/CODE` once this is live.
