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

`tools/eamuse/check.py` checks a server against all of it. For bemaniutils there is a ready
patch, `tools/eamuse/bemaniutils/popn9-python.patch`, checked against the game (its README says
how it maps the game onto bemaniutils). It was written with an AI model, so it is for server
owners' own installs only: bemaniutils accepts no AI-written contributions.

## Running it

**In the game:** test mode > e-AMUSEMENT OPTIONS > e-AMUSEMENT **ON**, SAVE AND EXIT, then restart.
It is OFF by default, and the game plays the same without it.

**Controls:**
- **Insert:** puts the card in the slot, or takes it back when the reader has given it out.
- **The numeric keypad:** the 10-key (0–9, Enter, `.`; Backspace is Clear).

**The card** is `card.bin` in the save folder (`~/.local/share/hiraku/popn9`): 128 bytes, a fresh
pop'n 9 card until the game writes it. With a card number (`-card0`, below) it is
`cards/NUMBER.bin` instead, one file per number.

**Three ways to run the server:**

| | How | What for |
|---|---|---|
| **Offline** (default) | nothing to do: `hiraku-popn9 --window` | one cabinet; data in the save folder's `eamuse/`; runs are reproducible |
| **Online, HIRAKU's server** | on the server machine, `hiraku-eamuse --port 8080 --data DIR`; on each cabinet, `hiraku-popn9 --window -url server:8080` | several cabinets sharing cards, players and rankings |
| **Online, another server** | `hiraku-popn9 --window -url host[:port][/path]` | a server that implements this page |

**spice2x's options:** the e-AMUSEMENT options have spice2x's names and meanings
(`runtime/shell/eaoptions.h`), so a setup made for spice2x carries over. Each takes one dash or two,
and the launcher's Settings > e-AMUSEMENT writes them, laid out like spicecfg's Network page.

| Option | spice2x's name | In HIRAKU |
|---|---|---|
| `-ea` | Basic Local EA Emulation | the server inside HIRAKU. It is also what runs without `-url`, and it is ignored with `-url`, as in spice2x. Unlike spice2x's, it is the whole server: cards and data are kept |
| `-url URL` | EA Service URL | the cabinet's server: `host:port[/path]` (spice2x's form) or `http://host[:port][/path]`. `--eamuse URL` is the older name |
| `-smartea` | Smart EA | with `-url`: if that server doesn't take a connection at start (3 s), the server inside HIRAKU |
| `-p PCBID` | PCBID | 20 hex digits, sent in place of the game's own (pop'n 9's is `01401401000100045FEF`) as `srcid` and `gpcb_id`, for a server that only takes the machines it knows |
| `-card0 NUMBER` | Player 1 Card | the card's number, 16 hex digits (`E004...`). Without it, `card0.txt` (the working directory's, then the save folder's), as spice2x reads it. `-card1` and `card1.txt` are player 2's; pop'n has one reader |
| `-autocard off\|p1\|p2\|both` | Auto Card Insert | the card goes in by itself: it is in the slot from the start, and back in it 8 s after the reader gives it out, so the game takes it whenever it opens the slot |

- **A card with a number:** these games number their cards themselves: a new card asks the server
  (`numbering.assign`), and the game writes the number into the card. With `-card0`, HIRAKU gives
  the game the card's number itself, without asking the server, so the card carries the number
  set. Then every call about the card (`userdata`, `rank`, `playerdata`, ...) uses it. A server
  that knows the number from other games (the player's e-AMUSEMENT PASS in spice2x) can then link
  the pop'n card to the same player. The bemaniutils patch does: a number already on an account
  (as an e-AMUSEMENT PASS played in spice2x) saves to that account.
- **In online mode** the game's requests go out as the game sends them: encrypted as the game
  encrypts them, with the URL's path in front of the request path and the Host header set to the
  server. `-p` and `-card0` change the calls' contents, as above. Your network sees plain HTTP.
  HIRAKU does no DNS for the game except for its own lookups (below).
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
- `eemall-CARD`: optional, ee'MALL item numbers for a card, one a line (the server owner writes these;
  pop'n 9 itself never asks, see `eemall.i_get`);
- `message`: optional, written by the server owner: one line (up to 255 bytes, ASCII or
  Shift-JIS) that every cabinet downloads at start-up and scrolls across its title screen (see
  `package`). Delete it to stop.

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
  `X-Compress` and no KBinXML. (pop'n 13 compresses one thing inside the XML: its player records.)

## Encryption

**Which calls are encrypted** depends on the call. **Answer in kind:**

| Encrypted (with `X-Eamuse-Info`) | In the clear (no `X-Eamuse-Info`) |
|---|---|
| services, pcbtracker, posevent, userid | ntp, numbering, package, userdata, rank, p_friend (eemall: never sent, below) |

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
- **XML details:** no XML declaration is needed, and **one with an `encoding` is refused**: with
  `<?xml version="1.0" encoding="shift-jis"?>` (or `SHIFT_JIS`) in front, the game asks for
  `services.get` again and again; `<?xml version="1.0"?>` or none is fine [game]. Double quotes,
  elements written out or empty (`<x/>`) both parse, attributes in any order, and extra ones
  (`dstid` on `<response>`, `__type` on `<b>`) are ignored [game]. A text payload goes in a child
  element (`userdata`'s `<b>`).
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

**package.list / package.intend** [game] (`0x197960`; the flow `0x184410`): downloads. pop'n 9
takes one package, **`message`**: a line of text it scrolls across the top of its title screen
(`0x1237c0`). At start-up, from the title:
1. `<package method="list" pkgtype="all" model=/>`. An empty answer `<package status="0"/>` means
   nothing to download. An offer is an item:
   `<item url="http://host/message" name="message" desc="" size="N" pkgtype="all" sumtype="" sum="" from="" till=""/>`
   (the game keeps url, name, desc, size, pkgtype, sumtype and sum; it looks only for the name
   `message`).
2. `<package method="intend" url="(the item's url)" model=/>`, answered `<package status="0"/>`.
3. A plain HTTP/1.0 `GET` of the url (no e-AMUSE envelope, no encryption); the game keeps at most
   `size` - 1 bytes of the body. ASCII and Shift-JIS (kana and kanji) both show.

Checked in HIRAKU, offline and online through hiraku-eamuse: "HELLO FROM THE SERVER" and
"ひらくへようこそ！ HIRAKU e-AMUSEMENT 稼動中" scrolled across the title. A server should give a url
the cabinet reaches like its services (hiraku-eamuse uses the Host the call came with).

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
  - `name` and `shop_name` are uppercase hex of the Shift-JIS bytes (a plain-text `shop_name`
    shows blank) [game];
  - `id` is at most 36 characters;
  - `area` is the prefecture: 0–46 in JIS order from 0 (0 北海道, 12 東京都, 13 神奈川県, 46 沖縄県),
    255 none [game];
  - up to 20 courses, 100 entries each.
- **Where they show** [game] (only while the cabinet's network check passed, `0x2571f8`):
  - attract mode, after each demo song: "popper's ranking", one page per course (course name,
    entries as "N poppers", then 順位, なまえ, 県名, SCORE, COOL数);
  - attract mode in event phases 1 and 2, after the labyrinth: "たたかえ！ギャンブラーZ お店対抗
    RANKING" (順位, お店のなまえ, 県名, and `parts` drawn as one picture per item);
  - after a course played with a card: INTERNET RANKING, from the `register` answer.

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
  event phases 1 and 2 [game], after that credit's labyrinth (its walk and any boss fight), while the
  item list shows:
  - `parts` is the labyrinth's items word (below) in 8 hex digits, `prog` the number of its bits set
    (0–25; `0x188248`). A save with two songs and the first boss sent `prog="4" parts="00084003"`;
    a credit that beat the first boss sent `prog="2" parts="00084000"` at its end.
  - Rank shops by `prog` (then `parts`): hiraku-eamuse does.
  - **Gambler Z progress is the shop's, per cabinet.** The labyrinth itself lives in the cabinet's
    own settings; the server only ranks the shops (see below).
- **`get_h`** and **`get_gz`** come at start-up in phases MAX and 1–3.
- **Phase 3** (GAMBLER Z DATA RELEASE) ends the event [game]: no labyrinth after a credit and no
  `register_gz`, but `get_gz` still comes at start-up, and attract mode shows the shop ranking
  (the final standings) after its demos, in place of the course pages.
- **Other statuses:** 700 and 701 make the game show "not ranked"-style results (meaning
  unconfirmed).
- **When it's sent:** a course result is registered when a card is in use (`0x1359e8`, from the
  result flow `0x129c68`), even for a failed course.
- **With a KONAMI ID** [game]: once `ctou` gives the card an ID, the game sends it as `konami_id`,
  and the INTERNET RANKING screen after a course shows "? 位 ? 人中" instead of the place the
  answer gives. The same with HIRAKU's server and with bemaniutils, whatever `id` and attribute
  order the answer has; without an ID the place shows. The rule wasn't found (the screen's tasks
  around `0x133900`).
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
  (50 on floor index 15). Then the walk goes on. A fight under way (HP saved, nonzero) resumes at
  the next credit's labyrinth. Seen with --autoplay, from saves set to floor index 10 ("5階 Kフロア")
  with HP 25, then 5: the boss's challenge, an EX STAGE (テクノポップ "321 STARS" EX), 100,000 and 5
  damage; at 25 HP the boss stays ("まだまだ…", 20 saved); at 5 it falls ("やーらーれーたー！"),
  INFORMATION "隠しコース 出現！ すわコース" (selectable in EXPERT from the next play), the bits
  0x84000 set, and the hero back on "1階 Aフロア" (floor 0, position 0, 0).
- **Saved** in the settings record (battery RAM, mirrored like the rest of it; the file at +0x10):

  | Offset | Field |
  |---|---|
  | `0x18` | items (low 24 bits; 0xffffff = all) and 0x1000000 = cleared, u32 |
  | `0x1c`, `0x1e` | position, s16 pair (cells; three steps took `0x1c` from -1 to -4) |
  | `0x20` | floor, u8 |
  | `0x22` | the boss's HP left, u16 (0 = no fight under way) |

  - **The bits:** 0–13 a song each, the treasures of floors 1–14 (ジャジー, ポジティブ (CS3);
    モーター5, ボッサ, プログレ (CS4); オイパンク (CS6); メランコリー, テラピー, スムース, モダン80,
    インダストリアル (CS7); ポップス・アンコール, ロックギター, ソフトロック from IIDX (REMIX)); bits
    14+k and 19+k the five bosses (an EX chart and an EXPERT course each: テクノポップ and すわ,
    ターバン and サナ, ピラミッド and パーキッツ, スカ and Des-ROW, オイパンク0 and クラシック);
    bit 24 the last boss, cleared. Read from the item list a save with every bit shows;
    `tools/unlock.py popn9` decodes a save.

  The same record holds the cabinet's shop: `area_num` (`0x35`, u8, 255 = not set) and the shop
  name (`0x37`, Shift-JIS, up to 12 characters), set in test mode > e-AMUSEMENT OPTIONS >
  e-AMUSEMENT SETTINGS (SHOP NAME SETTING, PREFECTURE). Every call that carries `shop_name` sends
  it as hex: "ひらく" and 東京都 went out as `shop_name="82D082E782AD" area_num="12"` [game].

**p_friend.get / ask / state** [game] (`0x19b070`…; the card flow `0x16da30`): pop'n friends.
`p_friend` is the player's character's place in the game's character table (`0x19ecf8`, 0–181).
The answers are status only, `<p_friend status="0"/>`.

| Call | When | Statuses |
|---|---|---|
| `get card_id gpcb_id p_friend shop_name area_num` | after the PIN, while the player data's friend flag (`+0x414`) is 0 | 0 sets the flag; 600: no, without retrying |
| `ask card_id p_friend` | after the PIN, once the flag is set | 0 keeps it; 601: no, without retrying, and the flag is cleared (the next credit sends `get` again) |
| `state card_id p_friend status` | at the end of the credit, before `userdata.write` | `status` is the game's own result (1 after a `get` or `ask` that passed, 0 otherwise) |

Anything else is retried, three times in all. The flag travels in the player data, so it is the
server's `userdata` that decides which of `get` and `ask` a card's next credit sends.

**eemall.i_get** [code] (`0x19b558`): the card's ee'MALL items (bought on Konami's ee'MALL site,
delivered to the game): `<eemall status="0"><item num="N"/>...</eemall>`, up to 99.
**pop'n 9 never sends it:** its only caller (`0x186ef8`) is called from nowhere, neither directly
nor through a pointer. A server may leave it out.

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
p_friend.get (or .ask, once a get passed), userid.ctou -> play -> ... p_friend.state, userdata.write
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
  | `28 23 49 01` / `28 24 45 11` | a new pop'n 10 / 11 card (their programs; "Later games") |
  | `68 23 41 01` | registered |
  | `6c 23 41 01` | "death" (dead) |
  | `18 23 41 01` / `58 23 41 01` | location test |
  | `08 1f 7d f1` | Konami common |
  | `08 20 82 01` | e-AMUSEMENT common |

## Later games: pop'n music 10–14

Each later pop'n speaks the same transport, cipher and envelope, with its own `model` (`C20:J:A:A`
pop'n 10, `D11:J:A:A` 11, `E12:J:A:B` 12, `E13:J:A:E` 13, `F14:J:A:B` 14). HIRAKU's server picks
the game by the model's first three characters; pop'n 9's answers are unchanged. Player data is
kept per game (`userdata-C20-CARD`, ...). Turning e-AMUSEMENT on: test mode > NETWORK OPTIONS >
e-AMUSEMENT OPTIONS, as in pop'n 9 (pop'n 13 and 14 start their menus' cursors on EXIT).

**pop'n music 10** (checked in HIRAKU: a new card registered, a whole credit saved, the card read
back with its PIN; `oracle/scripts/popn10-eamuse-on.his`, `popn10-card-new.his`,
`popn10-card-again.his`). The EE program's addresses are pop'n 10's.
- **As pop'n 9:** services (plus `eemall` and `shopinf`, looked up by those names), pcbtracker,
  posevent, numbering, package, userdata, userid, p_friend `state`/`ask`.
- **The card:** a new pop'n 10 card is `28 23 49 01` ("C20 NEW CARD", `0x1ad3c8` type 7; used
  `68 23 49 01`). A pop'n 9 card is offered for carrying over (name and clears; it then no longer
  works on pop'n 9): a pop'n 10 `userdata.read` of a card with no pop'n 10 data gets its pop'n 9
  data. When the player refuses, the game hands the card back with a card-unit **reset**.
- **p_friend.get** sends `card_num card_id name shop_name area_num` and reads `<p_friend
  chara_num rare_f/>` (`0x19e7e8`); no `chara_num` means none.
- **rank.exp** (pop'n 9's `rank.register`, the same fields): answer `<rank status="0" update entry
  rank hi_score hi_cool/>` on the element itself (`0x19dda8`).
- **rank.get** (no attributes): the rankings, base64 in `<b>` (`0x19e300`; the game appends one
  `<b>` after another, into a 16 KiB buffer it clears first), copied into a 0x1f30-byte table
  (`0x187be4`, at `0x18287c8`). Little-endian:
  - 15 EXPERT courses of 0x1e4 bytes each: a count (up to 20), then 20 entries of 0x18 bytes:
    0x00 the name (Shift-JIS, 13 bytes), 0x0d the place, 0x0e the prefecture (JIS code, from 1),
    0x10 the score (shown with the game's score display on), 0x14 the COOL count (`0x12b218`).
    The attract mode's INTERNET RANKING screens show them (event phase 1 and on).
  - Then (at 0x1c5c) HOSHI NI NEGAIO's shop board: a count (up to 20), then 20 entries of 0x24
    bytes: 0x00 the shop's name (25 bytes), 0x19 the place, 0x1a the prefecture, 0x1c the shop's
    granted wishes (64-bit; each a bucket icon, all 37 a special one) (`0x13afa0`). Shown as
    "お星様にお願い！ お店対抗 RANKING" in phases 2 and 3.
  - **Size:** the whole table in one `<b>` (10.7 KB) is refused (the game asks again, three
    times); HIRAKU sends it in `<b>` pieces of 768 bytes and leaves out the trailing zeros.
  HIRAKU builds it from `rank.exp` (the best per player and course, by score then COOL) and
  `rank.shop` (the latest per shop, ranked by wishes).
- **rank.course:** the server's ranking course, `<rank status="0" course_num music="a,b,c,d"
  area_num course_name user_name comment type/>` (`0x19e420`); status **117** means none.
- **rank.shop** sends `praystar_lo praystar_hi shop_name area_num`: the shop's HOSHI NI NEGAIO
  wishes (the items word of its settings, low and high 32 bits, signed decimal), its name and
  prefecture (`0x187a98`). Sent after a credit's HOSHI NI NEGAIO screen with e-AMUSEMENT on
  (`0x1a6f78`); answer `<rank status="0"/>` (121 is also taken). HIRAKU keeps the latest per shop
  (`C20-rank-shop`: `shop_name area_num mask mask_hi when`) for `rank.get`'s shop board.
- **message.get:** `<message status="0" phase="N" text="..."/>` (`0x19f4e0`). `phase` >= 0 sets the
  cabinet's event phase when it differs (`0x18dee0`, "PHASE ONLINE UPDATE"); a missing phase reads
  as 0, so answer `-1` unless you mean it. HIRAKU's server takes it from the file `phase-C20`.
- **eemall.get** (`card_id`): `<eemall status="0" now_point add_point>`, then `<item num/>` (up to
  99) and `<kisekae path pri/>` (up to 15) (`0x19eda8`).
- **shopinf.getsservip** (`srcip`): up to five `<sservip ipaddr/>`; none is fine.
- **ntp.get:** one attribute, `<ntp status="0" time="2003-06-01 12:00:00+9"/>` (`0x19da48`: six
  numbers, then an optional zone in hours).

**pop'n music 11** (`D11`; checked in HIRAKU: a new card registered, a credit saved with its
mileage, the card read back with its PIN; `oracle/scripts/popn11-eamuse-on.his`, `popn11-card-new.his`,
`popn11-card-again.his`). Game features move into a module named after the game, with a `type`;
answers are that element again (pop'n 11's parsers `0x18c858`-`0x18ddf8`), and its calls go to
the `local` service.
- **The card:** a new pop'n 11 card is `28 24 45 11` ("D11 NEW CARD"). After the card, red, the
  name (a kana grid: blue moves, red enters, the white buttons switch kana/letters; 決定 is one
  blue-left from あ), the PIN twice.
- **`D11 rank exp`:** pop'n 10's `rank.exp` (plus `chara_num`), answered on `<D11 update entry rank
  hi_score hi_cool/>`. **`pf ask`**, **`game select`** (play counts: `<music num play
  cnt/>` children), **`renewc reg`/`ask`** (a card's renewal: `old_card_id`, `new_card_id`): `<D11/>`.
- **`D11 rank get`:** the rankings in `<b>` pieces, as pop'n 10's `rank.get`, into a 0x1f80-byte
  table (`0x1932a8`): the 15 EXPERT courses as pop'n 10's, except that each entry has the
  prefecture at 0x0d and the place at 0x0e. Then (at 0x1c5c) POPN TOURIST's shop board, "ポップン・
  ツーリスト 獲得アイテム一覧": a count (up to 20), then 20 entries of 0x28 bytes: 0x00 the shop's
  name (24 bytes), 0x18 the place, 0x19 the prefecture, 0x1a-0x1d the month, day, hour and minute
  its finds were last added to, 0x20 the finds word (one bit per find; the STAGE column counts them), 0x24
  `extra` (`0x129240`). The attract mode shows the courses at phase 1 and on, and the shop board
  at phases 2 and 3. HIRAKU ranks the shops by finds, then by the earlier date.
- **`D11 rank shop`** sends `item extra shop_name area_num`: the shop's POPN TOURIST finds word and
  a second word, signed decimal. Answer `<D11/>`. HIRAKU keeps the latest per shop
  (`D11-rank-shop`), dated by the server's clock when the finds grew. (Checked with records written by
  hand; no HIRAKU run has sent one yet.)
- **`D11 pf get`:** `<D11 pf_num mobile/>`. **`D11 game get`:** `<D11 phase message expire_days/>`;
  `phase` as pop'n 10's `message.get` (-1 leaves the cabinet's own; HIRAKU's file `phase-D11`).
- **`D11 player set`** sends `card_id mileage ticket="t0,...,t8,"`; **`player get`** reads them back,
  with `course="c0,c1,c2,c3"`: `<D11 mileage ticket course/>`. HIRAKU keeps `player-D11-CARD`.
- **`eemall.check`:** `<eemall/>`. **`portfw.get`** (`privateip`): `<portfw globalip port/>`; none is fine (pop'n 12–14's NET TAISEN needs them, below).

**pop'n music 12** (`E12`; checked in HIRAKU: a new card registered, a credit saved, the card read
back with its PIN; `oracle/scripts/popn12-eamuse-on.his`, `popn12-card-new.his`, `popn12-card-again.his`).
The game-named calls as pop'n 11's, but **no `userdata`**: the player lives in `E12 playerdata`.
- **The card:** a new pop'n 12 card is `28 25 45 21` (type 0xf, `0x1ad4e4`), and pop'n 12 reads a card
  only with both CRCs right, the data's too (`0x1acbb0`): a fresh card needs the CRC-16 of its blank
  bytes 5-0x3e at 0x3f. Test mode > CARD R/W CHECK > CARD CHECK says NEW CARD for it. After the card,
  OK and red, the name (a kana/letter grid; the 99 s timer registers what is entered), the PIN twice.
- **New cards need the network up:** the game takes them only while its pings come back
  (`0x19803c`). It pings 10.2.1.10, or what the `keepalive` service names: `<item name="keepalive"
  url="ping://ADDRESS?t1=interval&amp;t2=wait&amp;rt=retries&amp;pa=router&amp;ga=center"/>`
  (`0x1981b8`; pop'n 10–14 all look `keepalive` up). It sends each ping on a socket it closes at
  once and reads the replies on another.
- **The interface board's own address:** after a new card's OK the game announces itself on the shop
  LAN (a 28-byte UDP broadcast to port 5700, `0x19fdc0`), first asking the board its address and
  netmask: board network commands **0x08** and **0x09** (RPCs 7 and 8 of the board library; board
  command = RPC number + 1), answered with 4 bytes in the reply's second word. Reliquary doesn't have
  them; without an answer the game waits there for good.
- **`playerdata new`** (`card_id card_num name password shop_name pref`) and **`get`** (`card_id`)
  answer the player's binary record, base64 in `<b>`: `<E12 status="0"><b>...</b></E12>` (`0x1917f8`;
  decoded at `0x18f860` into `0x1867e68`); `get` of an unknown card: status 109. The record, 0x708 bytes:
  0x00 the name (Shift-JIS, 13 bytes), 0x1b the puzzle position, 0x1c-0x33 six puzzle pieces
  (32-bit), 0x34 the password (four digits, a byte each), 0x38 the option and 0x3a the character
  (16-bit), from 0x3c a 16-bit word per song (2-bit clear marks per chart in bits 0-7, flags
  0x100-0x800; `0x1b3958`), from about 0x5f0 the network battle's (`0x19ef40`).
- **`playerdata set`** sends a credit's changes (`0x1919e8`): `card_id name password card_num option
  chara_num mode_num matching room_num puzzle_now puzzle_piece`, then `<music num sheet data score/>`
  per song played (`data` is the song's word in the record), `<expert course_num score_0..3/>`,
  `<netvs .../>`. HIRAKU merges them into the record (`pdata-E12-CARD`) and keeps the best score per
  chart (`pscore-E12-CARD`).
- **`playerdata score`** (`card_id music_num sheet_num`, asked before a song): `<E12 status="0"
  score="N"/>` (`0x192a00`).
- **`game active`** (`shop_name pref phase active card_use name`, at a credit's start and end):
  `<E12/>`. **`game get`** reads `phase message` and net battle's `play_enable local_matching_enable
  matching_sec room_num ojama_msec ojama_gauge cpu_level`.
- **`E12 rank get`:** the rankings in `<b>` pieces, as pop'n 10's `rank.get`, into a 0x1f30-byte
  table (`0x1974a8`, at `0x181c360`): pop'n 10's sizes, but the prefecture before the place, in the
  courses (0x0d, 0x0e) and in the shops (0x19, 0x1a). Then (at 0x1c5c) NINPOUCHO's shop board,
  "ポップン秘伝忍法帖": a count, then 20 entries of 0x24 bytes: 0x00 the shop's name, 0x19 the
  prefecture, 0x1a the place, 0x20 the shop's NINPOUCHO word (its 26 bits drawn as scrolls, all of
  them as 免許皆伝; `0x1a5778` copies the board). The attract mode shows the courses at phase 1 and
  on, the shop board at phase 3. HIRAKU ranks the shops by scrolls, then by the earlier date.
- **`E12 rank shop`** sends `item shop_name pref`: the shop's NINPOUCHO word (`0x197380`). Answer
  `<E12/>`; HIRAKU keeps the latest per shop (`E12-rank-shop`).
- **Not done yet:** `tmp`, `renew` (`playerdata conv`: below, "One card for all"); `cardreis.altercard`; a card left at the OK screen until
  the timer runs out stays on "カードをお取り下さい" (the card status's 0x40, card at the slot mouth,
  isn't played).

**pop'n music 13** (`E13`; checked in HIRAKU: a new card registered, a credit saved, the card read
back with its PIN, also with `-card0`; `oracle/scripts/popn13-eamuse-on.his`, `popn13-card-new.his`,
`popn13-card-again.his`). Its game program exists only in RAM (a second stage loads it at
0x1730000-0x1820000, data to about 0x1940000; the addresses below are there). The game-named calls as
pop'n 12's, keyed by a **ref id** from a new module, `cardmng`.
- **The services list needs `message`, `cardmng` and `cardreis`:** the library looks a call's URL up
  by its module's name in the services list (`0x17d94c0`) and fails the call at once, without
  sending it, when the name is missing (-10). Without `message` the start-up fails right after
  `services.get` with "センターサーバーと通信できません" (5-2007-0000: the status check `0x17e6798`
  sees `message.get` failed), and without `cardmng` a new card is given back after the PIN.
- **The card:** a new pop'n 13 card is `28 25 45 31` (type 0x13 of the header builder `0x180b498`;
  a used one `68 25 45 31`). Both CRCs are checked, as in pop'n 12. The game confirms the card by
  itself (no OK to press), then the name and the PIN twice, as in pop'n 12.
- **`cardmng`** (`0x17f66fc`...; answers parsed into the call's fields at `0x1497c8`...):

  | Call | Answer | When |
  |---|---|---|
  | `getnewid cardtype newflag` | `<cardmng status="0" cardid="16 digits" refid="..."/>` | a new card: its id (written on the card, as `numbering`'s number was) and its player's ref id |
  | `inquire cardtype cardid update` | `<cardmng status="0" refid="..." newflag="0\|1"/>` | a card read back |
  | `gettmpid cardtype oldid` | `<cardmng status="0" newid="..."/>` | an older card's player on a new id (not seen yet) |
  | `rebind cardtype newid` | `<cardmng status="0"/>` | (not seen yet) |

  `cardtype` is 0 for the magnetic card. HIRAKU keeps the cards in `cards` (cardid, refid) and gives
  ref ids `48524B` and 10 hex digits; `inquire` registers a card it doesn't know (newflag 1).
  `userid.ctou` gets the ref id as its `card`.
- **`playerdata`** as pop'n 12's, with `ref_id` in every call (`new` and `get` also send `card_id`);
  HIRAKU keys the player by it (`pdata-E13-REFID`, `pscore-E13-REFID`).
- **The record is compressed:** `new` and `get` answer `<E13 status="0" f_compress="1"><b>...</b></E13>`
  (`0x17f83c0`, `0x17f74d0`). With `f_compress` not 0 the game decompresses the `<b>` data
  (`0x1810240`): 4 bytes, the record's length (little-endian), then LZSS: a flag byte per 8 items, bit
  set = a literal byte, clear = a match of 2 bytes, `p0 | (p1 & 0xf0) << 4` a position in a 4 KiB
  window (starting at 0xfee, zero-filled) and `(p1 & 15) + 3` bytes. Uncompressed, the record (0xd6c
  bytes, about 4.6 KB in base64) makes the answer too big for the game's HTTP reader, and the call
  fails. With `f_compress` 0, `get` answers for an older card take a pop'n 12 record (0x708 bytes,
  `0x17e9dd8`), converted by the game (`conv`).
- **The record** (0xd6c bytes, at `0x166daa8`): 0x00 the name (Shift-JIS), 0x1c the password (four
  digits, a byte each), 0x20 the option and 0x22 the character (16-bit), from 0x28 a 32-bit word per
  song (the clear marks; `set`'s `data`, `0x1810e60`). The rest is not known yet.
- **`playerdata set`** (`ref_id name password card_num option chara_num mode_num music_num sheet_num
  category_num rare_ojama event_chara matching room_num`, then `<music num sheet data score/>` per
  song, `<netvs .../>`) and **`score`** (`ref_id music_num sheet_num s_challenge`) as pop'n 12's.
- **The network:** answers reach the game in pieces of at most 1460 bytes, as TCP over Ethernet
  delivers them; HIRAKU's virtual network hands them over that way (pop'n 13's reader fails on a
  2.5 KB answer handed over in one piece, `0x17ddb28`).
- **`E13 rank get`:** the rankings in `<b>` pieces into a 0x22a4-byte table (`0x17eaed8`, at
  `0x160fb20`; `f_compress` may be left out): the 15 courses as pop'n 12's, then two shop boards,
  POP'N LAND's (shown at phase 1, "ポップランド") and POP'N WORLD's (phase 2), each a count and 20
  entries of 0x28 bytes (`0x17eb010`): 0x00 the shop's name (24 bytes), 0x18 the place, 0x19 the
  prefecture, 0x1a the stage, 0x20 and 0x24 the area's words, low and high (the attractions built,
  drawn per bit, COMPLETE!! for all). pop'n 13's attract mode shows no course boards.
- **`E13 rank shop`** sends `stage item_hi item_lo shop_name pref` after a credit (`0x17ead80`): the
  area of the cabinet's phase. Answer `<E13/>`. HIRAKU puts it on that area's board, from the phase
  the shop's last `game active` sent (`E13-shop-phase`), and keeps the latest per shop and board.
- **Not done yet:** `tmp_set`, `tmp_ref`, `cardreis.altercard`, `gettmpid`, `rebind`
  (`playerdata conv`: below, "One card for all").

**pop'n music 14** (`F14`; checked in HIRAKU: a new e-AMUSEMENT PASS registered, a credit saved, the
card read back with its PIN; `oracle/scripts/popn14-eamuse-on.his`, `popn14-card-new.his`,
`popn14-card-again.his`). Its program is RAM-only too (0x1730000-0x1820000, as pop'n 13's). The
e-AMUSEMENT PASS replaces the magnetic card: an IC card that holds only its id, read by a slotted
reader with its own 10-key; the PIN is checked by the server.
- **The IC card reader** (Konami's slotted "ICCA", on the interface board's serial port at 19200
  baud; the driver is the game's own, `0x17f7f30`; HIRAKU's is `runtime/python/icca.cpp`):
  - The game sends `aa` and wants `aa` back; then packets `aa 00 HI LO 00 LEN payload SUM` (SUM: the
    8-bit sum of HI..payload), answered `aa 81 HI LO 00 LEN payload SUM` (the sum of all after the
    `aa`). Full duplex: no echo. **ACIO escaping:** after the `aa`, a byte `aa` or `ff` goes as `ff`
    and the byte inverted; the game undoes it, so an answer whose card id holds `ff` must be sent so,
    or it comes up short and the reads time out.
  - Commands: `00 03`, `01 00`, `01 30` (start-up, each answered with one byte 00); `01 34` the state;
    `01 35` the slot, payload `18 MODE` (11 open: take a card in; 12 eject; 00 close); `01 39` read
    the card; `01 10` the 10-key.
  - The state, 0x18 bytes: 0 the status (2 ready; the game requires 2 at start-up; 4 after a read
    with no card), 1 the sensors (0x10 front: a card at the slot; 0x20 rear: a card in), 2-9 the
    card's id. The game takes a card when it has seen both sensors on for more than 4 polls
    (`0x173bbd0`), reads it, and ejects it when the credit ends.
  - The 10-key answer is 4 bytes: 0, 0, then the keys held, bits 0-3 of the third byte blank 3 6 9,
    the fourth byte 0 1 4 7 00 2 5 8 (the game makes digit d its bit d, `0x17f93d8`).
  - Without the reader: "IC CARD R/W UNIT ERROR" 5-1508-000 at start-up; a reader that stops
    answering mid-game: 5-1500-000.
- **`facility.get`** at start-up, after `posevent`; without a `facility` service, 5-2009-0000. The
  answer is groups of text elements (`0x17d6860`): `<facility status="0" expire="600"><location><id/>
  <country/><region/><line/></location><portfw><globalip/><globalport/><privateport/></portfw>
  <public><name/><flag/></public></facility>`.
- **A card's life:** `cardmng.inquire cardtype="1" cardid="E004..."` (the card's id is the
  e-AMUSEMENT card number, as on later games and in spice2x). The answer needs all of `refid`,
  `binded`, `expired` and `newflag` (`0x17d51b0`). Then the PIN on the reader's 10-key (four equal
  digits are refused by the game) and `cardmng.authpass refid pass`: status 0, or **116 for a wrong
  PIN** (`0x17ac2c8`). A new player: `bindmodel refid newflag`, a choice between "はじめてあそぶ"
  and carrying data over from pop'n 13 ("カーニバルからデータを引きつぐ", `playerdata conv`), the
  name, then `F14 playerdata new ref_id card_id name ea_passwd`. HIRAKU keeps a card's PIN from its
  first `authpass` (or `getrefid cardtype cardid newflag passwd`, which answers a `refid`). Also
  seen in the program: `bindcard cardtype newid refid`, `playerdata expire`.
- **The record:** compressed as pop'n 13's (`f_compress="1"`, the same LZSS, `0x17fd3d8`). `get`
  answers 0xdb8 bytes (at `0x16506b8`); `new` is read as 0xd6c, and a pop'n 13 player's record
  (0xd6c, `conv`) too. 0x00 the name, 0x1c the option and 0x1e the character (16-bit), from 0x24 a
  32-bit word per song (`0x17fdfa0`); no password.
- **`F14 game get`** also reads `net_event`, `ir_ranking` and `shop_event_ranking` (`0x17e9308`).
  `net_event` 1 means the internet ranking runs (`0x17fdc78`): the attract mode then shows the course
  boards, and courses' results and high scores go online; 2 means NET TAISEN runs instead (below,
  "NET TAISEN"). HIRAKU answers 2, or what the data folder's `net_event-F14` says.
- **`F14 rank get`:** 13 EXPERT courses of 0x1e4 bytes as pop'n 12's (the prefecture before the
  place), a 0x1894-byte table (`0x17db420`, at `0x15f28f8`), and no shop board.
- **`F14 rank exp`** sends `ref_id course_num chara_num option stage_score cool great good bad combo
  shop_name pref`: no name and no score. HIRAKU ranks the player by ref id, scores the course as the
  four stages' scores added up, and takes the name from the player's record (`pdata-F14-REF`).
- **`F14 rank shop`** sends `stage item robo shop_name pref` after a credit (the SHOP EVENT's story,
  its counter, FEVER ROBO's gauge): `<F14/>`; HIRAKU keeps the latest per shop.
- **Not done yet:** `expire`, `bindcard`, `cardreis.altercard` (`playerdata conv`: below, "One
  card for all").

## One card for all (pop'n music 9–14)

Each pop'n carries a player over from the one before: it reads the older game's card, asks the
server for that player, offers to carry the name and clears over ("引き継ぎ"), and then wants a
card of its own (pop'n 14: an e-AMUSEMENT PASS and pop'n 13's transfer number). In HIRAKU one card
number (`-card0`, `card0.txt`, the launcher's Player 1 Card) is used in every game, and the player
follows it from game to game: 9 → 10 → 11 → 12 → 13 → 14, each step checked in HIRAKU with the
same number.

- **One server folder:** with the saves in their default places (the game window's,
  `~/.local/share/hiraku/<game>`, or a portable install's `user/data/<game>`), the pop'n games share
  one e-AMUSEMENT folder beside them, `eamuse/`. A game's own `eamuse/` from before is merged into
  it once and kept renamed (`eamuse.moved`). With `--nvram` elsewhere a game keeps its own folder,
  as before (and regress runs are unchanged).
- **The older card:** a numbered card a game has never seen (no `cards/NUMBER.bin` in its save) is
  shown as the game before's card, as that game left it in its save (`popn9`'s for pop'n 10, ...).
  pop'n 10 rewrites it as its own. pop'n 11-13 read it, then give it back and ask for an unused
  card of their own: once the player has agreed (the game asked the server about the player and
  the old PIN was typed), the card given back is followed by a fresh one of this game with the
  same number, which the game then formats. Refused, the older card stays for another try. pop'n
  14's card is only its number.
- **What each game asks, and what the server answers:**

  | Game | Asks | Answer |
  |---|---|---|
  | 10 | `userdata.read card="NUMBER:C00"` | pop'n 9's `userdata`, as saved (the game converts it) |
  | 11 | `D11 renewc ask`, `userdata.read card="NUMBER:C20"` | pop'n 10's `userdata` |
  | 12 | `E12 playerdata get model="D11"` | pop'n 11's `userdata` `<b>` pieces (the game appends them, `0x191910`) |
  | 13 | `E13 playerdata get model="E12"` | pop'n 12's record, `f_compress="0"` (converted by the game, `0x17e9dd8`) |
  | 14 | `F14 playerdata get ref_id="POP'N ID" model="E13"` | pop'n 13's record, compressed |

  pop'n 14 asks by the transfer number the player typed; the core adds the card's number, and the
  server answers that card's pop'n 13 player whatever was typed (it issues no transfer numbers).
- **`playerdata conv`** follows the player's yes. The games convert the older record in memory
  and their credit's `set` sends only the songs played, so the server builds each new record as
  the game does (checked against the game's own result in RAM):
  - **12** (`conv name new_card_id old_card_id`): from pop'n 11's 0xa28 bytes, the 600 song words
    at 0x410 into the record's words from 0x3c, the song numbers alike, bits 4-5 and 6-7 swapped and
    the high byte dropped (`0x1a0a38`); the PIN from 0x9f3 (a digit a byte; the name follows at
    0x9f7); the name as sent.
  - **13** (`conv name ref_id old_card_id new_card_id`): pop'n 12's song words' low byte (the four
    charts' clear marks, two bits each, the same in both: `0x1812f98`, `0x1812c4c`) as pop'n 13's
    32-bit words from 0x28; pop'n 12's flags above it are left out. The PIN and the name.
  - **14** (`conv name ref_id card_id popn_id ea_passwd`): the game takes the answer's record as
    its own, byte for byte. pop'n 13's record as it is would put song n at pop'n 14's n+1 (14's song
    words start at 0x24, `0x17fdfa0`; 13's at 0x28) and 13's PIN where 14 keeps its option and
    character, though both games number their songs alike (below). So the answer is the record in
    pop'n 14's layout: the name, 13's option and character (0x20, 0x22) at 0x1c, 0x1e, and each
    song's word at 0x24 + 4n; the server keeps the same.
- **Clear marks and scores are shared** (below); each game keeps its own player otherwise: options
  and a name changed in one game stay there. Characters and options: pop'n 14 takes pop'n 13's,
  the others start from the game's defaults. ee'MALL tickets don't carry over (the games say so).

### Shared clear marks

- **One song numbering:** each game's song table (entries with pointers to a genre name and a
  comment) has the same song at the same index in all six games (pop'n 9 and 10: 325 and 390
  entries; 11: 450; 12: 595; 13: 649; 14: 700), the differences being spelling only (kana or
  kanji, "(PS)"-style tags) and "-" entries left where a song was taken out (9's 252 and 257;
  12-14's 244-246, 252, 254, 257, 260, 262-263, 328-329, 337-338, 392; 11-14's 428). An
  entry's words at 8 and 0xc are the song's ids, the same in 11, 12 and 13; pop'n 14 gave 18
  songs new ones (450-492; ten of them rated anew, so charted anew presumably) and rewrote the
  genres and comments of the songs from other games (13's "SURGE LINE" is 14's ギターポップ).
  The songs sent in play agree (pop'n 14's 393 is アンパンマン, on screen and in its table;
  pop'n 12's 542 ドラゴンボールＺ).
- **The marks:** two bits a chart, 1 a clear and 2-3 the better medals, in the low byte of a song's
  word: pop'n 12, 13, 14 in one order (bits 0-1, 2-3, 4-5, 6-7), pop'n 10 and 11 with the last
  two charts the other way round. pop'n 9 keeps a cleared bit a chart, byte 0x416 + song, bits 0-3
  in pop'n 12's order. Found from the games' own conversions, with patterned players:
  - pop'n 9 -> 10: 9's byte 0x416 + n bits 0-3 become 10's flags 0x100, 0x200, 0x800, 0x400 of
    word n (10's song words: 600 at 0x3fc); 9's bits 4-7 and its 0x8-0x404 (scores, presumably)
    aren't carried;
  - pop'n 10 -> 11: the low byte as it is (11's words: 600 at 0x410), 10's flags 0x100-0x800
    become marks of 1 at bits 0, 2, 4, 6; songs past 10's table (390) left out;
  - 11 -> 12, 12 -> 13, 13 -> 14: above.
- **The server** gives each game, when it reads its own player (pop'n 9-11 `userdata.read`, 12-14
  `playerdata get`), every chart's best mark over all the games the card has played (their saved
  players: `userdata-*`, `pdata-*`, pop'n 13's and 14's by the card's ref id), in that game's
  format, for the songs that game has; a game's own better mark and its other bits stay. The game
  then saves them with its own at the end of a credit (pop'n 9-11 save the whole player). Checked:
  marks put in pop'n 9, 10, 11 and 13 reached all six games (in RAM for 12-14), and pop'n 9, 10 and
  11 played a credit with them and saved them.
- **Not shared:** pop'n 13's and 14's other flags in a song's word (0x100-0x4000).

### Shared scores

- **Where scores are:** pop'n 12-14 send each song played (`<music num sheet data score/>` in
  `playerdata set`) and ask a chart's best before playing it (`playerdata score music_num
  sheet_num`); the server keeps them (`pscore-GAME-PLAYER`). pop'n 9-11 keep no scores: their
  players (1.6-2.6 KB) hold the marks, and no call of theirs carries a song's score.
- **The sheet is the chart,** alike in the three (the same code: `0x1b3958`, `0x1810e60`,
  `0x17fdfa0`), and its levels' place in the song table: 0 Normal, 1 Hyper, 2 5 buttons,
  3 EX, 4-5 battle; 5 one mode's (the game's mode 2 with an option's 0x2000). Charts 0-3 are the
  marks' two-bit places. pop'n 13 and 14 play their own 5-button charts as 6 (marked by flag
  0x200 alone; 7 the same with 0x100), not pop'n 12's 2. Played: GRADIUS (418) on 5 buttons is
  313 notes in pop'n 12 (sheet 2) and 167 in both 13 and 14 (sheet 6); 9 buttons' Normal is
  sheet 0 (pop'n 12; pop'n 13 in NET TAISEN).
- **The server** answers `score` with the best of the card's pop'n 12, 13 and 14 scores (pop'n 12
  by card, 13 and 14 by the card's ref id) for that song and sheet; another game's only for songs
  both have, and pop'n 14's own for the 18 songs it gave new ids. Checked: pop'n 12 asking
  GRADIUS's sheet 0 got pop'n 13's 77777.

## NET TAISEN (pop'n music 12–14)

Battles between cabinets, three players to a match (the battle's three score slots), CPUs in the
empty seats. pop'n 12, 13 and 14 carry
the same library (the strings `globby://`, `LobbyEntry()`, `>>NETVS MASTER/SLAVE/LOCALVS`); the
addresses here are pop'n 12's. In HIRAKU the mode is offered and played: alone, when nobody
comes, the game says so and starts the credit in CHALLENGE mode (`oracle/scripts/popn12-nettaisen.his`);
two HIRAKU cabinets on one LAN cable (`--lan-host`/`--lan-join`, `runtime/python/lancable.h`)
meet through the first one's lobby and battle, a CPU in the third seat
(`oracle/scripts/popn12-nettaisen-a.his`/`-b.his`, regress `popn12-nettaisen-a`/`-b`); cabinets on
different PCs meet online through `hiraku-eamuse`'s relay (below, "Online").

- **When the mode is offered** (`0x19a300`, the mode wheel's NET対戦 and the title's `ICON_NET`):
  the event phase 2 or later (NET TAISEN MODE RELEASE), a card in, and on the network all of:
  - `E12 game get` answered with `play_enable` 1 (`0x191160`, read into `0x2d4748`: `phase`,
    `message`, `renew_f`, `xmas_f`, `play_enable`, `local_matching_enable`, `matching_sec`,
    `room_num` (up to ten, comma-separated), `ojama_msec`, `ojama_gauge`, `cpu_level`);
  - the services list's `globby` item, a URL `globby://HOST:PORT/` (`0x199400`: a dotted address or
    a name for DNS);
  - **`portfw get`** (`privateip`) answered `<portfw globalip="A.B.C.D" port="N"/>`
    (`0x190520`): where the world reaches the cabinet's battle socket. The services list must name
    `portfw`, as any module a call uses (else the library fails the call at once, -10, `0x184ed0`).
    Status 122 or 123: the mode stays off.
  HIRAKU answers `play_enable` 1, `local_matching_enable` 1, `matching_sec` 30, every room 0, and
  leaves out the rest (the game's own: an ojama lasts 1000 ms at least, `0x126f38`); `globalip` is
  the board's own address, `port` 5700, the lobby at the server's host, UDP 5740 (HIRAKU's choice).
- **The credit:** NET対戦, an explainer (red skips), ROOM SELECT (the player's level rooms; a room
  is `1 << room_num[i]` in the lobby), then song select with `matching_sec` to find opponents
  ("受付中"), then the battle, or CHALLENGE mode if nobody came.
- **The lobby** (UDP, from the cabinet's port 3200, one answer awaited per datagram, three tries,
  `0x199860`): a 12-byte header, `lob0`, the length (big-endian, all of it), a CRC-16/X-25 of the
  whole datagram taken with the field at 0 (`0x1adfd0`), a sequence number, the kind, a 0. The
  answer repeats the sequence number and has kind 0 (else "Replay type mismatched", `0x199d98`).
  - **list** (kind 1, 0x10 bytes): the room mask. Answered with the cabinets in its rooms, 0x1c
    bytes each from offset 0xc: the room mask, the address, the port (big-endian), the cabinet's 16
    bytes (below), two zeros; a zero entry ends it (`0x19d380`; at most 0x1bf0 bytes). The cabinet
    itself in the list is skipped; one other there and the cabinet joins it (`SLAVE`).
  - **entry** (kind 0, 0x28 bytes, `0x199ed8`): the room mask, `globalip`, `port`, how many seconds
    to keep it (`matching_sec` × 3 + 5), 16 bytes of the cabinet's own (its address on the LAN
    first). After an entry the cabinet waits for others to call (`MASTER`).
  - **leave** (kind 2, 0x14 bytes): the address and port, when the time runs out.
- **Between cabinets** (`0x19a628`, `0x19a948`): UDP from port 30000 to the other's port 5700
  (`globalip`'s; on one LAN, the 16 bytes' address), a 3-byte tag, `0`, the length, a CRC as the
  lobby's, a byte that turns a 16-byte XOR key (`0x2ef8c0`) over the rest (`0x19ab20`). Six kinds,
  0x18 to 0x1cc bytes (`0x19adc8`); the game's log names the players, standby, the countdown,
  ojama and dialogs among them.
- **On one shop's LAN** (`LOCALVS`, `0x19fdc0`, `0x19f9e0`): after a new card's OK the cabinet
  broadcasts a 0x1c-byte datagram to port 5700 (its own address and netmask: board commands 7 and
  8); a cabinet that has heard one shows a screen of its own first (`0x1644c8`, not read yet)
  and, for a local battle, calls those directly, no lobby.
- **Two cabinets, as seen in HIRAKU** (`popn12-nettaisen-a`/`-b`): each hears the other's
  broadcast at its card's OK, and from then on they exchange datagrams port 30000 to 5700 a few
  times a second. In NET対戦, A (in song select first) finds the lobby empty, enters and waits; B
  lists, finds A and calls it (SLAVE). Both leave song select together; the opponents' screen
  (対戦者紹介) shows both players and a CPU, the song starts on both at the same frame, "通信中…"
  while the scores go round, and each RESULT has all three scores. Each cabinet shows the others'
  scores as they arrive, so they can differ a little between the screens (the CPU's 2885 and 2818
  at one moment).
- **Three and four cabinets, as seen in HIRAKU:** three meet as two do (B and C each list, find A
  and call it) and play with no CPU; the three RESULTs agree. A full match takes its master out of
  the lobby, so a fourth cabinet finds the lobby empty, enters it and waits for a match of its own:
  with nobody else, CHALLENGE mode.
- **Receive timeouts:** the battle's sockets set one (`setsockopt` level ffff, option 0x200,
  seconds and microseconds): the lobby's 1 s a try and 1 ms to empty its queue, another 50 ms. A
  recv waits for a datagram that long; answered at once instead, a lobby request from another
  cabinet (one frame away) went out again before its answer came.
- **pop'n 13 and 14** (checked in HIRAKU: one cabinet, and two on a LAN cable, regress
  `popn13-nettaisen-a`/`-b`, `popn14-nettaisen-a`/`-b`):
  - **When the mode is offered:** no event phase; `game get`'s `net_event` must be 2 (13's
    `0x17edd48`, 14's `0x17ddef8`), with the lobby's URL parsed and `game get` answered. `net_event`
    is the network event running, one at a time: 1 the INTERNET RANKING (every use asks for exactly
    1: the IR icon, the attract mode's course boards, course results online), 2 NET TAISEN. HIRAKU
    answers 2 unless the data folder's `net_event-E13` or `net_event-F14` holds another number.
  - **`game get`'s fields:** 13 (`0x17f8bf0`, read into `0x190bc70`): `phase extra_event net_event
    renew_f id_announce_f message local_matching_enable matching_sec room_num ojama_msec ojama_gauge
    cpu_level`. 14 (`0x17e9308`, at `0x19033a0`): `phase net_event ir_ranking shop_event_ranking
    message local_matching_enable matching_sec ojama_gauge_rate cpu_level` (up to ten)
    `downlimit_win downlimit_class job_exp_rate cpu_higher_release job_sp_rate` (up to 18).
  - **Where the cabinet is reached:** 13 asks `portfw get` as 12 does; 14 reads it from `facility
    get`'s `<portfw><globalip/><globalport/>`. The core puts the cabinet's own address there (on a
    LAN cable, 192.168.50.2 + its number; online, the relay's), whatever the server said.
  - **The credit:** 13: NET対戦 one left of ENJOY on the mode wheel ("リアルタイム3人対戦"), a
    six-page explainer, ROOM SELECT, song select while the lobby looks, the opponents (対戦者紹介),
    the battle, RESULT. 14: one left of ENJOY too, an eight-page explainer, ROOM SELECT, a shop
    explainer, JOB SELECT, song select, MATCHING (the opponents, their jobs), the battle, RESULT.
    Alone, both say nobody came and start CHALLENGE mode.
  - **全国対戦 or 店内対戦:** a pop'n 13 cabinet that has heard another's broadcast asks first,
    nationwide (the lobby) or in-shop (the LAN, no lobby); left to its timer it takes nationwide.
- **Online** (`runtime/python/netrelay.h`): `hiraku-eamuse` keeps the lobby and a relay on TCP
  port 5740, the port its `globby` URL names. Each cabinet with `-url` pointing at it connects
  there at start, and the relay gives it an address of its own, 10.57.0.N. The cabinet puts that
  address in its `portfw` answer (port 5700), as a shop router's address would be, so the other
  games send there. The game's datagrams go over the connection with the address and port the
  game used: to the lobby's port at an address that isn't a cabinet's, the relay's lobby answers;
  to a cabinet's, the relay passes it on, from the sender's address; broadcasts go nowhere. The
  cabinets connect out, so no port forwarding is needed at the cabinets; only the server's 5740
  must be reachable. Not in lockstep: datagrams arrive when the network brings them, and the
  cabinets run free, as real ones did. Checked with two headless cabinets on one PC paced at real
  time ×4 (`HK_PY_PACE=4`): A waits in the lobby, B finds it and joins, a CPU takes the third
  seat, both RESULTs agree, stage 2 together. Against a server without the relay, the cabinet
  says so at start and NET TAISEN finds nobody.
- **After the credit:** `playerdata set` carries `<netvs music_num sheet_num caption fruit class
  class_skill badge get_ojama set_ojama rank stage_record member_name member_pref map_hi map_lo/>`
  (pop'n 13 and 14: their own fields).

## Not known yet

- What `alter` and `aflag` are for; what the pop'n friends statuses 600 and 601
  mean to a player.
- What 700/701 mean exactly.
- Why INTERNET RANKING shows no place for a card with a KONAMI ID (above).
