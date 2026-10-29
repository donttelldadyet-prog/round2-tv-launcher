# Fix: black background after coming back from Netflix or YouTube (LG)

Branch: `fix/bg-after-app`. One change in `web/index.html` (`showWall` and a safety net just below it).

## What the Diagnostics showed (Scarlett, 29 Oct, 17:56)

- `17:56:06 start lounge`, then `17:56:07 start theme-black-hole-2026` twice, then a `playing` at 17:56:08.
- Yet `player: none`: there was no video element in the front background layer at all.
- The helper log shows the guest came back from Netflix at 17:56:06 (`closed netflix (back on the menu)`).

## Cause

Leaving an app on a rooted LG now triggers several "start the background again" calls within one or two seconds:

- the helper's relaunch with `r2: back`;
- the page becoming visible (`r2Resume`, 500 ms later);
- the themes list and `refreshLocal`, which were added for Vale's fix (1.0.153 to 1.0.157).

Each call puts a new player in the other layer and swaps. Every swap then emptied the whole old layer 300 ms later (`old.innerHTML = ''`). When three starts land inside about a second, the layer being emptied is often the one a newer start has just put its player in. The result is no player at all, a black background, and `player: none` in Diagnostics.

Vale got its extra restarts first. Scarlett only showed the problem once it was updated to the same version, which is why it "moved" from Vale to Scarlett.

## Change

1. Only the newest start may swap layers (`wallSeq`).
2. The clean-up removes only the player that swap replaced, never the whole layer.
3. A second start of the same background within 3 s is skipped while the first player is still there (logged as `skip repeat start`).
4. Safety net (LG only): every 8 s, if the menu is showing a video background but the front layer has no player, start it again (logged as `no background player: start again`).

Not yet tried on a TV. To check after updating: come back from Netflix and YouTube a few times. The background should keep moving, and Diagnostics should show `player: t=…` rather than `player: none`.
