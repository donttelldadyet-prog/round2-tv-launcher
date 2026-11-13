# Round2 PS4 helper – current release

Only the newest files, replaced for each version (built from branch `ps4-helper`, folder `ps4/`):

- `round2d.elf` – the helper. The booking engine reads `round2d.json` (`v`, `sha256`, `size`) and hands this file to the room
  PS4s (`/ps4/app`) after checking its SHA-256. PS4s with the starter update themselves in quiet time or on
  control centre › TVs › PS4s › Update now.
- `round2-boot.elf` – the starter, installed once per PS4 (GoldHEN AutoRun). It doesn't update itself.

The TV apps never look at this branch. To release: build (`ps4/build.sh`), copy both ELFs here, write round2d.json, commit, push.
