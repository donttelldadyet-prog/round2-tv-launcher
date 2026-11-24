# PS4 tile – new TV screens (design, 24 Nov 2026)

From Karen (design team). **Design notes and pictures only. No code changed.** This is waiting for the owner to build
on his own branch (`ps4-tile`), and to make live only after both owners have tried it.

Full membership spec (phone page, pricing, crew card, motion): round2sg-home `design/membership/HANDOFF-membership-design.md`,
clickable phone prototype: round2sg-home `design/membership/prototype.html`. Concerns list: round2sg-home HANDOFF
"DESIGN → FUNCTION CONCERNS" (membership block, 24 Nov).

Pictures (mockups, 1920x1080 scaled down):
- `img/tile-design/scan_vale.jpg` – scan screen, Vale colours
- `img/tile-design/scan_kaeden.jpg` – scan screen, Kaeden colours (same layout, room colour from rooms.json)
- `img/tile-design/signed_bella.jpg` – signed-in screen, Bella colours (mockup shows a player name; use the crew name)

## Scan screen (replaces "Scan to log in")
- Three columns on the same top and bottom lines: words left, card stack middle, QR right.
- Left: `ROUND2 MEMBERS` / **`Pick up where you left off.`** / `Save your game here and carry on from any Round2 room.` /
  bottom line `After your plan, keep going at $10 a month.`
- Middle: stacked tier cards (Standard back, Gold, Platinum front). Each card's top strip shows tier, months and price
  (Standard 2 months $20; Gold 3 months and Platinum 6 months, prices still to be set by the owners).
- Right: QR with room-colour corner brackets; under it `Members scan to log in` / `New guests scan to join`.
- Footer: `Not saving today? Press [PS] to play.` and `book.round2.sg/m`.
- Remove the version text ("tile 0.13 id file") from the guest view.
- All tile text stays plain ASCII.

## Signed-in screen (new)
- `<TIER> CREW` / `Welcome back, <crew>.` / `Your saves are loading onto this PS4. Please don't start a game yet.`
- The crew card in the middle (crew name where a bank card has the cardholder name, `CREW SINCE mm/yy`, `VALID TO mm/yy`).
- Profiles list on the right (room profile first, then players: Ready / Loading / Next), progress bar underneath.
- Needs crew name, tier and expiry sent to the tile (not stored today).

## Look
- Background: room-colour glow behind the cards (smaller one behind the QR), fine pixel-dot texture inside the glow,
  darker edges. No character art. Room colour from round2sg-home `design/rooms.json`.
- Cards: muted matte. Standard = charcoal grey, Gold = deep bronze-gold, Platinum = near-black. Pixel dot grid fading in
  from one corner, thin inner edge, outline chip, small Round2 logo. No shiny gradients or tilt.
  If drawing these in code is too heavy, pre-rendered PNGs per tier are fine.

## Motion (calm ease-out, no bounce)
- Scan screen (~1.6 s): top line draws, words rise (0.08 s apart), cards deal in back to front (0.12 s apart),
  QR corners snap in, one light sweep across the front card.
- Signed in (~2 s, the high point): crew card rises in, tier name settles, dot grid lights up left to right,
  one sweep, then the "Welcome back" text.

## Still open
- Gold and Platinum prices; whether every tier renews at $10/month.
- Ties in with the existing "tile phase 2" request (wording and timings from a `design/tile.json`) so design can
  tweak these words later without a code change.
