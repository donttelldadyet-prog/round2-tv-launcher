# Guest messages (test build)

Branch: `feature/guest-messages`. Not merged into `main`, so nothing here is live on the TVs, the control centre or GitHub Pages yet.

Guests write their own birthday, anniversary, celebration or note message for the room TV. It works for bookings from any channel (WhatsApp, Telegram, TikTok, SimplyBook, walk-in), because staff send a link instead of the guest messaging a bot.

## How it works

1. **Staff make a link, only after the booking is paid.** Staff page: pick the room, date, start time and length. Make creates an 8-character random code (e.g. `8CB92138`) tied to that room and slot.
2. **Staff paste the invite into whatever chat the guest booked in.** Link: `https://donttelldadyet-prog.github.io/round2-tv-launcher/msg/?c=CODE` (live once merged).
3. **The guest page is locked to the slot.** The room can't be changed. Show time is a button: start, +15, +30, +45 or +60 min. Make re-checks all of this when the guest sends, so editing the page can't get around it.
4. **Staff approve.** Approve writes the message to the room's TV (`/r2/<key>/<room>/msg`, the same slot the control centre's Messages tab uses), with `at` = the guest's time and `until` = the booking end. So the TV never shows it after the booking ends.
5. **Cancel** closes the link. If the message was already approved and is still the one waiting on that TV, it's removed from the TV too.

The guest can edit their message until it's approved. After that it's locked.

## Files

| Path | What it is |
| --- | --- |
| `manager/msg/index.html` | Guest page. Published at `…/round2-tv-launcher/msg/` once merged (Pages publishes `manager/`). |
| `manager/msg/staff.html` | Staff page: make links, approve, cancel. Kept separate from `manager/index.html` on purpose, so this branch doesn't clash with work on the main control centre. Fold it into the control centre's Notifications tab when merging. |
| `docs/make/guest-message-links.blueprint.json` | Copy of the Make scenario. The real staff code and TV messages key are replaced with `STAFF_CODE` and `TV_MESSAGES_KEY`, because this repo is public. |

## Make (team "My Team", eu1)

| Thing | ID / name |
| --- | --- |
| Scenario | `[TEST] GUEST MESSAGE LINKS`, id 8249444 |
| Webhook | `GUEST MESSAGE LINKS`, id 4000032, `https://hook.eu1.make.com/0vs25z4g3omjuvbdxwstdgofqhc3p2e4` |
| Data store | `GUEST_MSG_LINKS`, id 237603 (data structure 667731). Key = the code. |

One webhook, `action` decides the route. The pages POST form-encoded fields (no CORS preflight).

| action | who | needs | does |
| --- | --- | --- | --- |
| `create` | staff | `staff`, `room`, `start`, `end` (ms), `label` | new code; refuses wrong staff code, unknown room, end before start, over 12 h, already ended |
| `get` | guest | `code` | returns the link record; refuses unknown, cancelled, ended |
| `submit` | guest | `code`, `kind`, `to`, `from`, `title`, `text`, `at` | saves the message; refuses times outside the first hour, unknown occasion, empty headline, text over 600, names over 40, already approved |
| `list` | staff | `staff` | links whose booking ended less than 2 days ago or later. Make answers plain `Accepted` when there are none. |
| `approve` | staff | `staff`, `code` | only if submitted and not ended; writes to the TV |
| `cancel` | staff | `staff`, `code` | closes the link; takes the message off the TV if it's still the one there |

Records hold exactly the TV message fields (`id`, `kind`, `to`, `from`, `title`, `text`, `ack`, `at`, `until`) plus link fields (`room`, `start`, `end`, `status`, `label`, timestamps). Approve sends the whole record to the TV; the TV ignores fields it doesn't use.

The staff code is not in this repo. Ask the owners for it.

### Make gotchas found while building

- `datastore:GetRecord` must have `returnWrapped` set, or the run fails with a vague `BundleValidationError`.
- Mapping a whole bundle (`{{20}}`) into Transform to JSON gives the literal `20`. Use `returnWrapped: true` and map `{{20.data}}`.
- `replace(x; /\s+/g; " ")` didn't behave as a regex here (it spaced out every letter). Use `replace(x; newline; " ")`.
- `datastore:UpdateRecord` defaults to `upsert: true`, which would let a made-up code create a record. It's set to `false`.

## Known limits (test build)

- **One message per TV.** Approving replaces whatever message is waiting on that TV, the same rule as the control centre. Best to approve on the day of the booking. A later version could hold approved messages in Make and push each one to the TV shortly before its time.
- **Not tried on a real TV yet.** The Make side was tested end to end (create, get, submit, list, cancel, and every refusal). Approve writes the same shape the control centre writes, but hasn't been run against a live TV.
- The guest page receives the staff `label`. Keep it to a first name or channel, never a phone number.

## Ideas for later

- SimplyBook: when a booking is paid, Make creates the link and puts it in the confirmation.
- Telegram ping to staff when a guest submits.
- Fold the staff page into the control centre's Notifications tab.
