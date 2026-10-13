# e-AMUSE3 for pop'n music 9 (Konami Python 1)

pop'n music 9 (2003, `C00:J:A:A`, Konami Python 1) was the first pop'n on e-AMUSEMENT. Konami's
servers for it closed in August 2003. HIRAKU plays the cabinet's card reader/writer, its 10-key,
its network, and the server, so the game's e-AMUSEMENT works again: cards, player data, KONAMI ID
and rankings.

This page is for two kinds of readers:
- **HIRAKU players:** how to run the server, offline or online (first section).
- **Server owners:** how to add pop'n 9 to an e-AMUSEMENT server, such as bemaniutils or one of
  your own. Everything the game asks and accepts is below, found from the game's own code (the
  EE program's addresses are given) and checked in HIRAKU where marked.

`tools/eamuse/check.py` checks a server against all of it.

## Running it

**In the game:** test mode > e-AMUSEMENT OPTIONS > e-AMUSEMENT **ON**, SAVE AND EXIT, then restart.
It is OFF by default, and the game plays the same without it.

**Controls:**
- **Insert:** puts the card in the slot, or takes it back when the reader has given it out.
- **The numeric keypad:** the 10-key (0–9, Enter, `.`; Backspace is Clear).

**The card** is `card.bin` in the save folder (`~/.local/share/hiraku/popn9`): 128 bytes, a fresh
pop'n 9 card until the game writes it.

**Three ways to run the server:**

| | How | What for |
|---|---|---|
| **Offline** (default) | nothing to do: `hiraku-popn9 --window` | one cabinet; data in the save folder's `eamuse/`; runs are reproducible |
| **Online, HIRAKU's server** | on the server machine, `hiraku-eamuse --port 8080 --data DIR`; on each cabinet, `hiraku-popn9 --window --eamuse http://server:8080/` | several cabinets sharing cards, players and rankings |
| **Online, another server** | `hiraku-popn9 --window --eamuse http://host[:port][/path]` | a server that implements this page |

- **In online mode** the game's requests go out unchanged: encrypted as the game encrypts them,
  with the URL's path in front of the request path and the Host header set to the server. Your
  network sees plain HTTP. HIRAKU does no DNS for the game except for its own lookups (below).
- **`HK_PY_NET_LOG=FILE`** logs every socket command, and every call and answer decrypted.
- **`hiraku-eamuse` options:**
  - `--bind ADDR` (default 0.0.0.0);
  - `--clock "2003-06-01 12:00:00"`: the time `ntp.get` gives, running from there (default: the
    host's clock);
  - `--tz 9`: with the host's clock, the cabinet's time zone in hours from UTC (default 9, Japan);
  - `--quiet`: don't print the calls.

**What the HIRAKU server keeps** (the save folder's `eamuse/`, or `--data`), plain text, one record
a line, tab-separated `key=value` fields:
- `numbering`: the next card number;
- `userdata-CARD`: a card's player data, as the game wrote it;
- `users`: KONAMI IDs (user, pass, cards, active);
- `rank-score`, `rank-hitasura`, `rank-gamblerz`: each entrant's best;
- `eemall-CARD`: optional, ee'MALL item numbers for a card, one a line (the server owner writes these).

## What the cabinet does on the network

1. **The board:** the interface board (not the PS2) has the Ethernet port. It gets its address by
   DHCP, and the game reads it from the board along with the gateway, the DNS server and a
   **local domain** (in HIRAKU, `PYTHON1-00000001`).
2. **The first lookup:** the game resolves `services.<local domain>` and POSTs `services.get` to
   it, on port 80 (`http://services` plus the domain: `0x18cd38`).
3. **The service list:** the answer lists the other services' URLs. The game uses them as given
   (`http://host[:port]/path`), with its own DNS lookups for the names.
4. **The rest:** `pcbtracker.alive`, then the sales report (`posevent`), then `ntp.get`. After
   that e-AMUSEMENT counts as up: the title says "e-AMUSEMENT 接続中!" and "カードが使用できます!!".
5. **Afterwards:** a ping (ICMP echo, raw socket) goes to the gateway from time to time. Three
   failed rounds give ROUTER COMMUNICATION ERROR. In HIRAKU the virtual network answers it.

## Transport

- **HTTP/1.0, one request per connection:**

  ```
  POST / HTTP/1.0
  Host: services.PYTHON1-00000001
  User-Agent:EAMUSE.Httpac/1.0
  Content-length: 84
  X-Eamuse-Info: 1-3eda6962-4e60          (encrypted calls only)

  <body>
  ```

- **The answer:** `HTTP/1.0 200 ...`, a `Content-Length`, and `X-Eamuse-Info` on encrypted
  answers only. Then the body, then **close the connection**: the game reads until the end of
  the stream. Any status but 200 fails the call.
- **No compression and no binary XML:** the body is XML text. Unlike later games, there is no
  `X-Compress` and no KBinXML.

## Encryption

**Which calls are encrypted** depends on the call. **Answer in kind:**

| Encrypted (with `X-Eamuse-Info`) | In the clear (no `X-Eamuse-Info`) |
|---|---|
| services, pcbtracker, posevent, userid | ntp, numbering, package, userdata, rank, p_friend (eemall: not seen yet) |

- **The cipher:** RC4, with key = MD5(the 6 bytes of `X-Eamuse-Info: 1-xxxxxxxx-xxxx`, as hex,
  then the 26-byte shared secret `69d74627d985ee2187161570d08d93b12455035b6df0d8205df5`). This
  is the same scheme as later e-AMUSEMENT games; bemaniutils' protocol code has it.
- **Encrypted calls:** answer encrypted, with your own `X-Eamuse-Info` (any 1-xxxxxxxx-xxxx value).
- **Calls that come in the clear:** answer in the clear, **without** `X-Eamuse-Info`. The game
  compares the header's crypto version with the call's packet type, and drops an answer that
  differs before reading any XML (`0x190290`). This is the easiest thing to get wrong.

## The envelope

**A call:**

```xml
<call srcid="01401401000100045FEF" model="C00:J:A:A"><MODULE method="..." .../></call>
```

- **srcid:** the cabinet's PCB id. The same value comes as `gpcb_id` in some calls.
- **model:** the game; pop'n music 9 is `C00:J:A:A`.
- **Several calls in one:** a packet can carry more than one module element (the start-up's
  `posevent` sends income, sales and sale together).

**The answer:**

```xml
<response><MODULE status="0" ...>...</MODULE>...</response>
```

- **One module element per call, in order.**
- **Failure:** `status` other than 0, or a `fault` attribute, fails that call; `fault` on
  `<response>` fails them all. Each module's own status codes are listed below.
- **XML details:** no XML declaration is needed; double quotes, elements written out or empty
  (`<x/>`) both parse. A text payload goes in a child element (`userdata`'s `<b>`).
- **The game's parser** (`0x1910e0`) calls a start handler per element, a text handler, and an
  end handler. A call is done when its module element closes.

## The modules

**Legend:**
- **[game]:** sent by the game and answered in HIRAKU, the answer accepted;
- **[code]:** from the game's request format and response parser, not seen in play yet.

Each module's parser is listed as `parse 0x…`.

**services.get** [game] (parse `0x18a808`): the other modules' URLs.

```xml
<call ...><services method="get"/></call>
<response><services expire="600" status="0">
  <item name="posevent" url="http://host[:port]/"/>
  ... pcbtracker, numbering, package, userdata, userid, local ...
</services></response>
```

**Every name above is needed.** The rank, p_friend, eemall and ntp methods look up **`local`**,
not their own names (each method's entry names the item it uses; `0x1e81e8`…). A missing item
means that method is never sent. `expire` is seconds.

**pcbtracker.alive** [game] (`0x18b588`): `<pcbtracker status="0" expire="600"/>`; `expire` optional.

**posevent** [game] (`0x18abb0`, `0x18af68`, `0x18b308`): the coin and sales reports.
- **income** (`slot1="00000002"`, coins as 8 hex digits): answer `<posevent status="0"/>`.
- **sale** (`article1=... payment1=...`) and **sales** (`play= coin= service= article=`): the
  answer **needs `approve`** (hex), e.g. `<posevent status="0" approve="1" margin="0"/>`.
  `margin` is optional, capped at 1000.

**ntp.get** [game] (`0x19b740`):

```xml
<ntp status="0"><time year="2003" month="6" day="1" hour="12" min="0" sec="19"/></ntp>
```

- **The game won't start e-AMUSEMENT** until `ntp.get` answers or 40 s pass.
- **The clock:** the game sets its own clock to this time as given (`0x18a138` → `0x1a9598`) and
  stamps its times "+9". So send the cabinet's local time, Japan time for a Japanese cabinet,
  not UTC.

**package.list / package.intend** [game / code] (`0x197960`): downloads. An empty answer
`<package status="0"/>` means none. A list would be `<item url= name= desc= size= pkgtype= sumtype= sum= from= till=/>`;
offering packages isn't explored.

**numbering.assign** [game] (`0x197530`): a new card's number.

```xml
<numbering method="assign" label="cardv1" model="C00:J:A:A" format="card16m10"/>
<numbering status="0" number="2003010000000010"/>
```

- **The number:** 16 hex digits, written into the card as 8 bytes. The HIRAKU server gives
  decimal numbers with a Luhn check digit (its reading of `card16m10`). The game doesn't check
  the digit, but numbers must be unique.

**userdata.read** [game] (`0x197d80`; statuses `0x184ca0`):

```xml
<userdata method="read" card="2003010000000010" model="C00:J:A:A" label=""/>
<userdata status="0" time="2003-06-02 06:42:41+9"><b>BASE64...</b></userdata>
```

- **109:** not registered, so the game registers the player (name, then PIN).
- **110:** expired.
- **Otherwise:** send back exactly the `time` and the `<b>` elements from the last write.

**userdata.write** [game] (`0x197fc0`; answer `0x1981c0`): at the end of a credit.

```xml
<userdata method="write" card="..." time="2003-06-02 06:42:41+9" model="C00:J:A:A" label=""><b>BASE64...</b>...</userdata>
<userdata status="0"/>
```

- **The data:** base64, in `<b>` chunks of up to 0x200 bytes each.
- **The player data:** name, character, options, and the player's **PIN**. The game checks the
  PIN itself; the server only stores the blob.

**userid** (KONAMI ID: a user and password with cards; `0x198858`, `0x198650`; statuses `0x1863e8`, `0x186620`, `0x186818`):

| Call | Answer | Statuses |
|---|---|---|
| `ctou card=` [game] | `<userid status="0" user="NAME" active="1"/>` | 112: no KONAMI ID for the card; active 0 = provisional |
| `adduser model= user= pass= card=` [game] | `<userid status="0" alter=""/>` | 113: the user exists; 114: the card is someone's |
| `addcard user= pass= card= model=` [game] | `<userid status="0"/>` | 115: no such user; 116: wrong password; 114: the card is someone's |

- **`user`** is what the player typed: 8 to 32 characters from `a-z 0-9 . - _` (the screen adds
  "@konami"). **`pass`** is 4 digits from the 10-key.
- **`aflag="%d"`** is in the request builder (`0x1984f8`) but only sent when nonzero; the cabinet's
  flows never set it. **`alter`** (up to 40 characters) is read from a status-0 answer and kept;
  nothing seen shows it. Answering `alter=""` is enough.
- **What the game does with the status:** `adduser` 0: "「KONAMI ID」と「PASSWORD」が登録されました";
  113: back to the menu to try another ID; anything else: an error page, and the credit ends.
  `addcard` 0: "ポップンミュージックカードの登録が完了しました！"; 115 or 116: try again (three times
  in all); anything else: an error page. A failed call is retried three times first.

**rank** (all six: `local`, in the clear):

**`get` / `get_h` / `get_gz`** [game] (parse `0x19a080`, `0x19a5c8`, `0x19aaf0`): the
boards shown in attract mode and after play. The request carries `course_offset course_num
rank_offset rank_num`; `get_gz` has no courses.

```xml
<rank status="0">
  <item course="0" entry="2"/>                                                   (a header per course)
  <item rank="1" name="82A082A082A0" score="98765" cool="321" area="13" id="..."/>
  ...
</rank>
```

- **`get`:** score and cool.
- **`get_h`** (HITASURA): music and combo.
- **`get_gz`** (Gambler Z, by shop): `<item entry="N"/>`, then items with rank, prog, parts (hex),
  shop_name, area and id.
- **Fields:**
  - `name` and `shop_name` are hex of the Shift-JIS bytes;
  - `id` is at most 36 characters;
  - `area` is the prefecture number;
  - up to 20 courses, 100 entries each.

**`register`**, **`register_h`** and **`register_gz`** [game] (parse `0x1992e8`, `0x1997f0`, `0x199c60`; statuses `0x184f68`): a result.

```xml
<rank method="register" card_id= konami_id= gpcb_id= name= course= option= score= cool= great= good= bad= combo= shop_name= area_num=/>
<rank status="0"><item entry="12" rank="3" update="1" name=.. score=.. cool=.. area=.. id=../></rank>
```

- **The answer:** the player's place in the course, the number of entries, and whether the
  result is a new best.
- **`register_h`** sends `music combo`: the songs cleared in the HITASURA run and its combo, e.g.
  `course="0" music="3" combo="365"` for the first course (3 songs) cleared. A run that clears no
  song sends none.
- **`register_gz`** sends `gpcb_id prog parts shop_name area_num`, at the end of a credit in
  event phases 1–3, e.g. `prog="0" parts="00000000"` (parts: 8 hex digits).
  - **Gambler Z progress is the shop's, per cabinet.** The labyrinth itself lives in the cabinet's
    own settings; the server only ranks the shops (see below).
- **`get_h`** and **`get_gz`** come at start-up in phases MAX and 1–3.
- **Other statuses:** 700 and 701 make the game show "not ranked"-style results (meaning
  unconfirmed).
- **When it's sent:** a course result is registered when a card is in use (`0x1359e8`, from the
  result flow `0x129c68`), even for a failed course.
- **Seen from the game:**

  ```xml
  <rank method="register" card_id="2003010000000010" konami_id="" gpcb_id="01401401000100045FEF"
        name="82A082A082A0" course="0" option="0" score="918" cool="1" great="1" good="3" bad="36"
        combo="0" shop_name="" area_num="255"/>
  ```

  The name is hex of its Shift-JIS bytes ("あああ"); `area_num` 255 is no prefecture set.

**The Gambler Z labyrinth** [game] ("ギャンブラーZ", pop'n music internet event), for reference: it
is the cabinet's, not the server's. In event phases 1 and 2 (`0x143c98`), after a credit's results
(and its staff roll when all stages were cleared), the game runs the labyrinth (`0x19e188`):
- **Steps:** one per cleared stage, two for a stage whose song counts double (`0x1088c8`, "Walk %d").
  Three cleared stages walked "1階 Aフロア" three cells.
- **The walk** follows direction bits in the map's cells. At a junction the player chooses (buttons
  4 and 6, red to confirm, a 180-frame timer); a treasure cell gives an item; stairs change floor.
- **Bosses:** from floor index 10, a door cell starts a fight: a song from the floor's table (`0x19e628`),
  whose score deals 1–5 damage (70,000 / 80,000 / 90,000 / 95,000, `0x19e720`) to the boss's 25 HP
  (50 on floor index 15). Then the walk goes on.
- **Saved** in the settings record (battery RAM, mirrored like the rest of it; the file at +0x10):

  | Offset | Field |
  |---|---|
  | `0x18` | items (low 24 bits; 0xffffff = all) and 0x1000000 = cleared, u32 |
  | `0x1c`, `0x1e` | position, s16 pair (cells; three steps took `0x1c` from -1 to -4) |
  | `0x20` | floor, u8 |
  | `0x22` | the boss's HP left, u16 (0 = no fight under way) |

  The same record holds what every `rank.register*` sends as `area_num` (`0x35`, u8, 255 = not
  set) and `shop_name` (`0x37`, 25 bytes).

**p_friend.get / state / ask** [game, game, code] (`0x19b070`…): pop'n friends. The game sends
`card_id gpcb_id p_friend shop_name area_num`; status only, `<p_friend status="0"/>`.

**eemall.i_get** [code] (`0x19b558`): the card's ee'MALL items (bought on Konami's ee'MALL site,
delivered to the game): `<eemall status="0"><item num="N"/>...</eemall>`, up to 99.

## A card's life (as checked in HIRAKU)

**A new card:**
1. **The title:** with e-AMUSEMENT up, the title says cards can be used.
2. **Start:** coins, then Start; the INFORMATION screen asks for the pop'n card. The reader reads
   it: a fresh pop'n 9 card, "C00 NEW CARD".
3. **The calls:**

   ```
   name entry -> PIN twice on the 10-key ->
   numbering.assign -> (the game formats, writes and reads back the card) ->
   "カードセーブ完了" -> userid.ctou -> play ->
   p_friend.state, userdata.write at the end of the credit
   ```

**The card again:**

```
userdata.read (status 0) -> the PIN -> "name, character, options" ->
p_friend.get, userid.ctou -> play -> ... userdata.write
```

**A wrong PIN:** "パスワードが違います", and play without the card.

**A KONAMI ID, on the cabinet:** at the end of a credit played with a card that has none
(`ctou` 112), the game shows INFORMATION, "KONAMI IDのご案内" (`0x1a8678`), for two minutes:

| Item | Does |
|---|---|
| KONAMI IDとは？ | explains it |
| 新規発行 | a new ID: the ID on a character wheel (buttons 4/6 turn it, red picks; its last items are cursor left and right, delete, 決定 and back), the password twice on the 10-key -> `userid.adduser` |
| カードへ登録 | this card to an existing ID: the ID, the password once -> `userid.addcard` |
| 登録を中止する | (selected at first) leave |

Checked: a new ID `hiraku09` with the first card; then a second card, registered as new, added
to it. The server's `users` then lists both cards, and `ctou` gives the ID for either.

Scripts: `oracle/scripts/popn9-eamuse-on.his`, `popn9-card-new.his`, `popn9-card-again.his`,
`popn9-konamiid-new.his`, `popn9-konamiid-addcard.his`.

## The card

The card holds no player data, only an identity.
- **Layout:** 128 bytes; 0x41 used.
  - bytes 0–3, a header, and byte 4 its CRC-8 (reflected 0x31, initial 0xff, inverted; `0x1a3050`);
  - bytes 6–13, the number from `numbering`, in reverse byte order;
  - bytes 5–0x3e, data; 0x3f–0x40, its CRC-16 (reflected 0x1021, initial 0xffff, inverted;
    `0x1a30c0`).
- **Header types** (`0x1a2728`):

  | Header | Type |
  |---|---|
  | `28 23 41 01` | a new pop'n 9 card |
  | `68 23 41 01` | registered |
  | `6c 23 41 01` | "death" (dead) |
  | `18 23 41 01` / `58 23 41 01` | location test |
  | `08 1f 7d f1` | Konami common |
  | `08 20 82 01` | e-AMUSEMENT common |

## Not known yet

- `eemall`, `package.intend`, `p_friend.ask`; what `alter` and `aflag` are for.
- What 700/701 mean exactly.
- **The Gambler Z labyrinth:** how `prog` and `parts` grow (both stayed 0 over a credit on floor 1),
  the items, and what phase 3 does.
