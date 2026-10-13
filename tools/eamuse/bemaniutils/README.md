# pop'n music 9 for bemaniutils

`popn9-python.patch` adds pop'n music 9, as it runs on the Konami Python 1 and in HIRAKU, to a
[bemaniutils](https://github.com/DragonMinded/bemaniutils) server. Cabinets then keep their cards,
player data, KONAMI IDs and rankings on that server. docs/EAMUSE3.md describes the protocol.

> **Not for submission to bemaniutils.** The patch was written with an AI model (Claude).
> bemaniutils does not accept AI or LLM contributions in any form (its README, since commit
> 1e66fcd), and will reject or remove them. It is kept here for server owners to apply to
> their own install if they choose. Please don't open a pull request, issue or review with it,
> or with code derived from it. bemaniutils is public domain, so running a patched copy is fine.

## Applying it

Against bemaniutils commit `1e66fcd` (newer commits may need small fixes):

```bash
cd bemaniutils
git apply /path/to/HIRAKU/tools/eamuse/bemaniutils/popn9-python.patch
```

Then restart the services. Nothing new is needed in the database or the config.

## What it changes

| File | Change |
|---|---|
| `bemani/backend/popn/popn9.py` | new: the game's backend (all of its calls) |
| `bemani/backend/popn/stubs.py`, `factory.py` | `PopnMusic9` is now that backend; game code `C00` is routed to it |
| `bemani/backend/dispatch.py` | a `<call>` with several requests (pop'n 9 bundles its `posevent` calls) is answered with one element each, in order; before, it was refused |
| `bemani/protocol/xml.py`, `protocol.py` | a request without an XML declaration is answered without one (pop'n 9 rejects any declaration with an `encoding`), and text inside an element with no `__type` is kept (the game's `userdata.write` chunks) |

Other games' traffic is unchanged: their requests carry declarations and types. bemaniutils'
protocol and XML tests pass with the patch, and so do the rest of the tests that run without
the extra packages they need.

## How pop'n 9 maps onto bemaniutils

- **Cards:** the server numbers new cards (`numbering.assign`: `200301` and 10 digits, Luhn
  checked) and creates the account at once. The PIN is taken from the player data at the first
  save, so the player can claim the account on the web with card number and PIN. These are
  decimal numbers, not E004 IDs.
- **Player data:** `userdata` is kept per card, in the account's pop'n 9 profile (`cards`), with
  the player's name (`name`) decoded from it.
- **KONAMI ID:** the account's username and password, so the ID made on the cabinet also logs
  in to the web frontend. `adduser` sets them on the card's account; `addcard` moves a card
  without an ID (and its player data) to the ID's account.
- **Rankings:** each player's best per course (achievements `course`) and per HITASURA course
  (`hitasura`); the Gambler Z board per shop, in the machine's data (`popn9_gamblerz`). The
  attract mode and the INTERNET RANKING screen show them.
- **Shop and prefecture:** as the cabinet sends them (hex Shift-JIS name, 0-based JIS area).

## Setting up a cabinet

- `server.address` and `server.port` must be what the cabinet reaches. The game speaks plain
  HTTP, and `services.get` always hands out `http://` whatever `server.https` says.
- With `enforce_pcbid`, add the cabinet's PCBID (its `srcid`, 20 hex digits; HIRAKU's is
  `01401401000100045FEF`, or the one given with `-p`) as a machine first.
- In HIRAKU: the launcher's Settings > e-AMUSEMENT, as spice2x's: EA Service URL
  `your-server:port`, and PCBID if wanted (or `-url your-server:port -p PCBID`). The game's test
  mode must have e-AMUSEMENT on.
- **A player's own card number:** with Player 1 Card (`-card0 E004...`), a new card takes that
  number instead of one from `numbering.assign`, so an e-AMUSEMENT PASS already on an account
  (from other games) saves pop'n 9 to that account.

## Checked

With bemaniutils at 1e66fcd on MariaDB 11, and HIRAKU's pop'n 9 online against it:
- `tools/eamuse/check.py http://server:port/`: all corpus calls and the card-life scenario pass.
- **A new card:** numbered, written, played, saved (`popn9-card-new.his`). Then logged in again
  with the PIN (`popn9-card-again.his`): the player's data and character came back.
- **A KONAMI ID** made on the cabinet (`popn9-konamiid-new.his`): the account got the username
  and a hashed password. An ID already taken gets 113, and the game sends the player back to
  the menu.
- **Rankings:** an EXPERT course (`rank.register`), a HITASURA run (`register_h`), and a Gambler
  Z credit (`register_gz`, `prog="4" parts="00084003"` kept with the machine); `get`, `get_h`
  and `get_gz` at start-up.

## Not supported, or not checked

- **The title-screen message** (`package` "message"): bemaniutils answers every GET with its
  health check, so the patch offers no package.
- **Web frontend pages** for pop'n 9 aren't added, and the frontend wasn't run. Accounts,
  usernames and PINs work there as for any game; the decimal card numbers may not display.
- **INTERNET RANKING with a KONAMI ID:** once the card has an ID, the screen after a course
  shows "? 位 ? 人中" instead of the place. HIRAKU's own server gets the same, so this is the
  game's behaviour (cause not found), not the patch's.
- `alter`, `aflag` and the meanings of statuses 600/601 and 700/701 (docs/EAMUSE3.md).
