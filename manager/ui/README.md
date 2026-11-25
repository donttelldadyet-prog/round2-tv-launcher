# Round2 UI – the design language for staff screens

Started 25 Nov 2026 from the Mac board v9 (`manager/widget/round2-board.jsx`), the first design the owner called
"a huge improvement to our design". Owner: *"make a branch for our design into React UI elements … roll out to staff
facing apps first before improving, so lets get the design feel and language consistent."*

Open `index.html` (served at `staff.round2.sg/ui/` once merged) to see every piece in dark and light.

## The feel – from the logo

The owner's words: *"our design style can be felt from our Round2 logo … we only have white on black and vice versa,
colours are our product but our main feel is monochrome."* What that means on screen:

- **Monochrome.** Dark = white on black, light = black on white (the logo turns black). Every screen supports both, plus
  "auto" (follows the device). No gradients, glass, glow, sun or grid – those were tried (v5, Vaporwave) and rejected.
- **Drawn like the logo:** thin 1px lines, rounded ends, small round corners (6px chips, 12px cards, 18px panels),
  outlines instead of fills, wide letter-spacing. Not boxy, nothing sharp (v3 was "too boxy, too many sharp elements").
- **Colour is the product:** each room's colour, and four state colours (OK green · waiting orange · needs-someone red ·
  cleaning yellow). Nothing else is coloured. States are a **small dot**, never a coloured block.

## Type

| Font | Used for | Never for |
|---|---|---|
| **Quicksand** (`--r2-text`) – thin, rounded, like the logo's strokes | all text; 300 for big numbers, 500 text, 600 names/times | – |
| **Press Start 2P** (`--r2-pixel`) – our game font | small labels (TONIGHT, UP NEXT, LEFT), tabs, **room names** | sentences, numbers, buttons |

Files: `manager/widget/fonts/` (both OFL – free to use).

## Rules that keep it consistent

1. **The six rooms are the main view.** Everything else goes in the quiet strip underneath (columns split by thin
   lines, no boxes) – "putting extra info to the right clutters the flow".
2. **Inside a room tile, everything follows the room's colour** – name, icon, status box, the outline while guests are
   in, the ring. "Ending" fills the status box lightly in the room colour; it does not turn orange.
3. **The big number is always a countdown, and says what it counts to:** `left` (guests in, white) · `cleaning left` ·
   `till arrival` (both grey). Never a number without words.
4. **The ring has one meaning: guests are in.** It fills as the booking is used and the middle says how long they booked.
   Not used for cleaning, arrivals or anything else. "Extended" is its own line under the guest's name.
5. **One solid button per screen** – the main action. Everything else is an outline; "quiet" for cancel/back.
6. **Same clock everywhere:** times are Singapore time from the engine (`r2Now()` / `clockOff`), never the device.

## Files

| File | What | Use in |
|---|---|---|
| `r2-ui.css` | tokens (colours, fonts, shapes; dark/light/auto) + every piece as a class (`r2-card`, `r2-chip`, `r2-ring` …) | any page: `<link rel="stylesheet" href="ui/r2-ui.css">` and `<html data-theme="auto">` |
| `r2-ui.js` | rooms + colours + pixel icons, `pixSvg()`, `ringSvg()`, time helpers, `setTheme()` | plain pages (the control centre): `<script type="module">` – also `window.R2UI` |
| `r2-ui.jsx` | the same pieces as React components (`RoomTile`, `Chip`, `Ring`, `Countdown`, `Button`, `Toggle`, `Tabs` …) | the Mac board and anything new built in React |
| `index.html` | the style guide – every piece, dark/light | looking, checking, showing the design team |
| `r2-logo.png` | the real Round2 logo, white, unaltered (light mode inverts it to black with CSS) | headers |

Pieces: Panel · Header · RoomName · RoomGrid · RoomTile · Card · Chip · Countdown · Ring · Then · Line · Pix · Kick ·
Strip/Col · Button · Field · Toggle · Tabs · List · Notice · ThemeSwitch.

## Rolling it out

1. This branch (`r2-ui`) – nothing goes live from a branch (the website and TV builds only publish from `main`).
2. Owner approves the style guide → merge, then move the control centre's screens over **one at a time** (Tonight first),
   with the System check (Light) after each – the control centre itself can't touch bookings.
3. The iPhone widget (Scriptable, no CSS) copies the tokens by hand: same colours, Quicksand + Press Start 2P.
4. Guest-facing screens (TV menu, member page, website) come later – the owner wants staff screens first, and the design
   team (round2sg-home `design/`) owns the guest look.
