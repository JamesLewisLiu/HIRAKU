# The rest of the catalogue (written 2026-10-15)

This compares the owner's archive with what HIRAKU runs today. The archive is `ROGABACKUP/BIG MAME BEMANI FOLDER`. It ends in a plan.

## What the archive is

It comes as three qBittorrent torrents. The roster below is from the torrents' file lists, so it is final even while the download runs (83 of 119 GB on 2026-10-15 at 23:30).

- **`roms/`** (398 files, 119 GB): 187 MAME 0.243 entries, as zips plus CHD folders. Clones mostly come as a zip that uses its parent's CHD folder.
  - The BEMANI sets of DJ Main, Firebeat, Twinkle and System 573, and `ddrkids` (Konami GS).
  - BIOS and device sets: `sys573`, `gq863`, `k573_dio`, `k573kara`, `k573mcr`, `k573msu`, `k573npu`, `kviper`, `cr589fw`/`cr589fwa`, and the PS1 BIOSes `psa`/`pse`/`psj`/`psu`.
  - `ppp2nd`/`ppp2nda` (not Firebeat; out of scope).
  - **No Python sets.** Every Python set in MAME is a NOT_WORKING skeleton.
- **`videos_iidx/`** (460 files, 14 GB) holds the IIDX Video CDs and DVDs that HIRAKU already uses.
- **`mame0243_bemani__windows_64bit.rar`** (7.3 GB) is **windyfairy's (987123879113) `bemani` MAME branch**. It's the fork HIRAKU already follows for the multi-session unit and the e-AMUSEMENT network unit. The rar holds:
  - the Windows `mame.exe`, built from djmain, firebeat, konamigs, ksys573, psx, twinkle and viper only;
  - **15 System 573 DDR sets with CHDs**: `dstage`, `ddr2m`, `ddr3mk`, `ddr3mp`, `ddr4m`, `ddr4ms`, `ddr4mp`, `ddr4mps`, `ddr5m`, `ddrmax`, `ddrmax2`, `ddrextrm`, `ddrusa`, `ddrsbm`, and `ddrkids` (Konami GS);
  - **installed NVRAM for every one of them** (flash, timekeeper, cassettes, PC Cards): the first-boot install is already done in that fork;
  - cheats for about 85 sets;
  - notes in `mame_compile_bemani.bat` on setup, unlock codes, and session wiring: DDR×IIDX club, DDR Karaoke, and GF×DM×Keyboardmania 3rd.

**One naming catch.** MAME 0.243 names differ from master's, which `games/*/game.toml` follows. `tools/ingest.py` matches by set name, so it doesn't recognise the old names. In the roster: `bm2ndmxa` (master `bm2ndmixa`) and `gtfrk10mb` (not in master; to be resolved by hash).

**MAME master sets the archive lacks:**
- DJ Main: `bm3rdmixa`, `bm3rdmixe`, `bmcompmxb`, `bscompmx`, `popn1a`.
- Firebeat: the license-expired and JAA Animelo sets (`popnanma`, `popnanm2a`, `popnanm2ja`, `popnanm2jaa`).
- System 573: `ddr2mlb`, the four GQ886 EAA/UAA/JAA/AAA GuitarFreaks, `gtrfrk5ma`, `drmn9ma`, `ddrkara2`, `dmxa`, `mrtlbeata`.
- Nothing of Punch Mania, Fighting Mania or GunMania.

## What HIRAKU runs today

There are 69 manifests in `games/`:

| Board | Sets with a manifest |
|---|---|
| DJ Main | all 29 of MAME's |
| Firebeat | 21 |
| Twinkle | 10 |
| System 573 | 4: `ddr2m`, `ddr3mj`, `drmn10m`, `gtfrk11m` |
| Python 1 | pop'n 9–14 |

## The list: not supported, or never run

### A. Manifest exists, but never run here (no `dumps/<set>`)

These are DJ Main. All 29 DJ Main manifests were generated from MAME's driver, but these 13 were never booted:

| Sets | In the archive? |
|---|---|
| `bmclubmx`, `bmcompmx`, `bmcmxaac`, `bs4thmix`, `hmcompmx`, `hmcompm2` | **yes** |
| `popn1`, `popn1k`, `popnstex` | **yes** |
| `bm2ndmixa` | **yes**, as `bm2ndmxa` |
| `bm3rdmixa`, `bmcompmxb`, `bscompmx`, `popn1a` | no |

### B. Clones of games that already run (same board, new manifest)

| Board | Sets | In the archive? |
|---|---|---|
| Twinkle | `bmiidx3a` (JAA), `bmiidx3b` (JAB) | **yes**: `gc992-jaa01`, `gc992-jab01` |
| Twinkle | `bmiidx6a` (JAA) | **yes**: HIRAKU's `bmiidx6` is JAB; `b4ujaa01` is in `roms/bmiidx6/` |
| Twinkle | `bmiidx7` (JAB) | **yes**: HIRAKU has `bmiidx7a` (JAA); `b44jab01` is in `roms/bmiidx7/` |
| Twinkle | `bmiidxa` (JAA), `bmiidxsa` | **yes**: zips over the parent's CDs (`gq863a01` is in `roms/bmiidx/`) |
| System 573 | `ddr3mk`, `ddr3mka`, `ddr3ma` (DDR 3rd Korea and Asia) | **yes**, in the rar, installed |
| Firebeat | `popnmt` (Mickey Tunes, not the `!`) | **yes**, with the `976jaa01` CD that was missing until now. A new program CD is a new recompile. |
| Firebeat | `popnanma`, `popnanm2a`, `popnanm2ja`, `popnanm2jaa` | no |

### C. System 573, analog board (MAME runs them: lockstep is possible)

| Family | Sets |
|---|---|
| DDR 1st | `dstage`, `dstagea`, `ddru`, `ddrj`, `ddrja`, `ddrjb`, `ddra` |
| DDR 2nd versions | `ddr2ml`, `ddr2mla`, `ddr2mlb` (Link), `ddrbocd` (Best of Cool Dancers) |
| DDR club versions | `ddr2mc`, `ddr2mc2`: they session-link to the IIDX club sets |
| Dancing Stage | `dsftkd` (featuring TRUE KiSS DESTiNATiON) |
| GuitarFreaks 1st | `gtrfrks` and 11 regional or revision clones |
| GuitarFreaks 2nd | `gtrfrk2m` and 7 clones, including Link Kits 1 and 2 |
| DrumMania 1st | `pcnfrk` (**in the archive**), `pcnfrka`, `pcnfrkk`, `drmn`, `drmna`, `drmnu`. MAME: NOT_WORKING. |
| DDR Karaoke Mix | `ddrkara` (karaoke unit; MAME: NOT_WORKING) |

Everything in this table is in the archive except `ddr2mlb` and the four GQ886 EAA/UAA/JAA/AAA GuitarFreaks. The IIDX club sets `bmiidxc`, `bmiidxca` and `bmiidxc2` are there too, for Phase 6.

### D. System 573, digital board: DDR line (same board as `ddr3mj`)

| Group | Sets |
|---|---|
| DDR 3rd–5th | `ddr3mp`, `ddr4m`/`ddr4mj`, `ddr4mp`, `ddrusa`, `ddr5m` |
| DDRMAX and EXTREME | `ddrmax`, `ddrmax2`, `ddrextrm` |
| Solo (own FPGA map and outputs) | `ddrsbm`, `ddrs2k`/`ddrs2kj`, `ddr4ms`/`ddr4msj`, `ddr4mps` |
| Dancing Stage | `dsfdct`/`dsfdcta`, `dsem`, `dsem2`, `dsfdr` (Disney's Rave) |
| Karaoke | `ddrkara2` |

MAME master flags nearly all of these NOT_WORKING. All are in the archive except `ddrkara2`. The rar's fork has 12 of them installed.

### E. System 573, digital board: GuitarFreaks and DrumMania

| Group | Sets |
|---|---|
| GuitarFreaks 3rd–10th | `gtrfrk3m` (+`3ma`, `3mb`), `gtrfrk4m`, `gtrfrk5m` (+`5ma`), `gtrfrk6m`, `gtrfrk7m`, `gtrfrk8m` (+`8ma`), `gtrfrk9m`, `gtfrk10m` (+`10ma`) |
| DrumMania 2nd–9th (Japan) | `drmn2m`, `drmn2mpu`, `drmn3m`, `drmn4m`, `drmn5m`, `drmn6m`, `drmn7m` (+`7ma`), `drmn8m`, `drmn9m` (+`9ma`) |
| Percussion Freaks (export) | `pcnfrk2m`/`2mk`, `3m`/`3mk`, `4m`/`4mk`, `5m`/`5mk`, `6m`, `7m`, `8m`, `9m`, `10m` |

All are in the archive except `gtrfrk5ma` and `drmn9ma`. It also has `gtfrk10mb`, a 0.243 name.

What the games need:
- From 4th Mix on, the multi-session unit (already in HIRAKU for 10th/11th).
- Session play pairs GF n+1 with DM n.
- Keyboardmania 3rd joins the GF 6th × DM 5th session (the rar's notes).

### F. System 573: the other BEMANI games

| Game | Sets | What it needs |
|---|---|---|
| Dance Maniax | `dncfrks`, `dmx`, `dmxa`, `dmx2m`, `dmx2majp` | hand IR sensors |
| Mambo a GoGo | `mamboagg`, `mamboagga` | congas |
| Martial Beat | `mrtlbeat`, `mrtlbeata` | motion sensors |
| Punch Mania / Fighting Mania | `pnchmn`, `pnchmna`, `pnchmn2`, `fghtmn` and 4 clones | punch pads that move |

The archive has Dance Maniax (all but `dmxa`), Mambo a GoGo and `mrtlbeat`. It has no Punch Mania or Fighting Mania.

Fishing, Bishi Bashi, GunMania, Step Champ, Kick & Kick and the other non-BEMANI 573 games stay out of scope (§19).

### G. New boards

| Board | Sets | Notes |
|---|---|---|
| Konami GS (`konamigs.cpp`) | `ddrkids` (DDR Kids, GQAN4) | in the archive; it works in MAME; a small board |
| Konami Python 2 (not in the archive) | DDR SuperNOVA 1/2 and Dancing Stage SuperNOVA, GuitarFreaks and DrumMania V–V3, DANCE 86.4, Toy's March 1/2 | Every set is NOT_WORKING in MAME. This is the MAIN session's PY-6 (PLAN.md §23); HIRAKU runs Python through PCSX2 Reliquary. |
| Konami Viper | (the fork builds it) | no BEMANI game; out of scope |

IIDX 9th style and later, and the other PC-based games, are in neither MAME nor the archive.

## The plan

The order is by what each step costs against what it opens. Every step keeps the house rules:
- match MAME, or the fork, first;
- departures are switches;
- regress stays bit-identical.

### Ground rules for this work, beside the MAIN session

- **Python is MAIN's.** Stay out of these:
  - `runtime/python`;
  - the Reliquary tree;
  - `games/popn9`–`popn14`;
  - Python regress cases;
  - Python 2 (G), unless MAIN hands it over.
- **Shared files** (`tools/regress.py`, `tools/ingest.py`, `PLAN.md`, `docs/PLAY.md`, the launcher): commit only my own hunks. Ask MAIN before using `out/regress`.
- **The DATA disk went read-only under heavy IO today**, and it has 98 GB free. `dumps/` is already 80 GB. So:
  - one big extract at a time;
  - never alongside MAIN's builds or regress;
  - watch `/proc/pressure/io`.
- **Read the archive in place.** It's on a USB hard disk (exFAT: no symlinks, no sparse files). Don't copy it to DATA wholesale.

### Phase 0: inventory and ingest (1–2 days, no core changes)

1. **`tools/catalogue.py ROMDIR`:**
   - For every set it finds, it prints the board, MAME's status, and HIRAKU's status (runs / manifest only / clone of a running game / known board / new board / out of scope).
   - It checks files against MAME master's hashes, parsed from the driver sources.
   - It runs `chdman verify` one CHD at a time, read-only.
   - Rerun it when the download finishes; that turns this list into the final one.
2. **Old names in `ingest.py`:** a 0.243 → master alias table (`bm2ndmxa` → `bm2ndmixa`, and whatever the full archive shows). Where 0.243 and master CHDs differ by hash, say so instead of ingesting the wrong one.
3. **CDs read straight from CHD**, so that 80+ 573 CDs don't need ~50 GB of extracted images on DATA:
   - `third_party` libchdr, already built for `tools/chdread.py`, behind the shell's CD image class;
   - hunk cache, read-ahead on a worker;
   - extract only where timing needs it (Twinkle's video DVD stays as it is).
   - Regress must stay identical with a CHD in place of the image.
4. **The rar's NVRAM** goes in as seeds for the matching sets: installed flash, cassette EEPROMs, PC Cards. This skips each game's first-boot install. Keep the launcher's install path for people without it.

#### Phase 0 progress (2026-10-16)

**1. The catalogue (done).**
- `tools/mameset.py` holds MAME's BEMANI sets as data, in `tools/data/mame-bemani.json` (284 sets, from MAME 0.289's `-listxml`).
  - For each set it keeps the ROMs (name, size, CRC) and the disks (SHA-1).
  - It covers the BIOS sets and the devices with ROMs.
  - It tells a set by what it holds, not its name: ROM CRCs weighted by how few sets share them, half weight for a name match with another CRC, and a CHD's header SHA-1.
- `tools/catalogue.py ROMDIR...` takes several folders, as MAME's rompath.
  - Per set it reports the board, HIRAKU's status, missing ROMs, ROMs that are other dumps, missing disks, and disks that are other dumps.
  - With `--verify` it also runs `chdman verify` on every CHD.
- **What it found in the archive:**
  - **Renamed sets.** 0.243 kept older revisions under the parent's name: `bm3rdmix` is master's `bm3rdmixa`, `bmcompmx` is `bmcompmxb`, `popn1` is `popn1a`, `popnanm`/`popnanm2` are the license-expired `popnanma`/`popnanm2a`, and `gtfrk10mb` is `gtfrk10m`. So the archive *has* `bm3rdmixa`, `bmcompmxb` and `popn1a`. It lacks master's `bm3rdmix` (JA-B), `bmcompmx` (JA-C) and `popn1` (TA-A).
  - **Every System 573 cassette EEPROM in `roms/` is an older dump than master's** (the `.u1` files: X76F041, X76F100, ZS01).
    - Same size, other contents.
    - The ZS01 ones are 4116-byte files without the configuration registers. Master's are 140 bytes with them, and their data differs too. So no conversion is possible.
  - **The rar's DDR zips carry master's cassette dumps** (all but `ddr4ms`/`ddr4msj`). With `roms/` and the rar's `roms/` together, every DDR set is complete except `ddr4ms`, `ddr4msj`, `ddrs2k`/`ddrs2kj`, `ddrjb` and `ddr2ml`.
  - **GF, DM, Dance Maniax and Mambo exist only with the old cassette dumps.** Phase 2–4 must find out whether the games accept them, or source master's.
  - Firebeat pop'n DVDs are other dumps of the same discs (0.243 kept DVDs as hard disk CHDs). Keyboardmania lacks master's newer `sin-table.bin`.
- **`chdman verify`:** all 191 of the archive's CHDs that match a disk MAME knows check out, none bad. The other 20 of the 211 aren't checked by the catalogue: they're other dumps of a disk (the 0.243 pop'n DVDs, for example) or disks MAME doesn't list.
- **The rar's small files** (zips, NVRAM, cfg) are unpacked in `out/catalogue/rar/` (547 MB, mostly PC Card images).

**2. Older names in ingest (done).**
- `tools/ingest.py` tells a set by its contents (`set_name_of` → `mameset.current_name`).
- It tells a CHD by its header SHA-1, so a renamed file such as `ge984a01,ddr.chd` still gets its role.
- It notes ROMs and disks that are other dumps than MAME's.
- It takes the parent's, BIOS's and devices' ROMs from archives beside the set, so a split set (BIOS in `sys573.zip`) works.
- Checked: `bm2ndmxa.zip` ingests as `bm2ndmixa`, a set never run before, and boots to attract with sound.

**4. NVRAM seeds (done).**
- `tools/ingest.py --nvram-seed DIR` keeps a MAME NVRAM folder as `dumps/<game>/nvram-seed/`. A `nvram/<set>` folder among the inputs is taken as a seed by itself.
- On a game's first start, the launcher copies the seed into the game's saves and leaves the install cassette out.
- Checked: `ddr2m` from the rar's zip, the archive's CD and the rar's `nvram/ddr2m` passes the flash check and plays its attract demo with sound.

**3. CHDs read in place (in progress).**
- `runtime/dev/image.h` opens a `.chd` as a stdio stream (`fopencookie`) that reads as `chdman extract*` would have written:
  - a CD as its `.bin`, with the cue sheet `extractcd` writes;
  - a disk or DVD as its raw bytes.
- It uses libchdr (BSD-3), vendored in `third_party/libchdr` with dr_flac and the LZMA SDK's decoder (both public domain).
- So the readers change only where they open files:
  - `DiscImage` and `DiskImage` (DJ Main, Firebeat);
  - the MAME CD and hard-disk shims (System 573, Twinkle);
  - Twinkle's video player.
- **Checked byte for byte against chdman** (`hiraku-chdcat`):
  - `895jaa02`, a 73-track CD (one data track, 72 audio), bin and cue;
  - `853jaa11` (DJ Main HD);
  - `gq863a04` (Video CD, bin and cue);
  - the first 200 MB of `b4ujaa02` (DVD).
- `tools/ingest.py --keep-chd` points a DJ Main, 573 or Twinkle game at its CHDs instead of extracting them.
- **Games run identically from CHDs.** Five regress-style cases, each run from the extracted images and from the archive's CHDs on the USB disk, give identical frame and sound digests: `bm1stmix-boot`, `ddr2m-boot`, `bmiidx-clear`, `bmiidx-video` (Video CD playing) and `bm3final-practice`. With `--mame-cd`, `bmiidx-clear` also matches its regress baseline.
- **Read-ahead.** A worker decompresses the 4 MiB after each run of reads into a 32 MiB cache, with its own handle on the CHD.
  - Without it, the worst frames came from waiting on the USB disk: up to 1.4 s on `bmiidx-video`.
  - With it, measured while `chdman verify` was also reading that disk (worst frame, image → CHD): `bm1stmix-boot` 8 → 23 ms, `ddr2m-boot` 40 → 16 ms, `bmiidx-clear` 23 → 65 ms, `bmiidx-video` 22 → 52 ms, `bm3final-practice` 9 → 158 ms (the SPU's hard disk, random reads).
  - Means and p99 are the same.
- **Downsides kept in view:**
  - The CHDs must stay where machine.txt points; a portable install wants them inside its folder.
  - v3-era CD metadata (CHCD) and parent CHDs aren't read.
  - Firebeat still extracts its program CD.
  - Each open CHD costs 32 MiB of cache and a thread.
  - `--keep-chd` stays opt-in until it's measured on an N100 with an idle disk.

### Phase 1: the cheap wins (about a week)

- **DJ Main never-run sets (A):** boot each to attract against MAME 0.289, then add regress cases for one beatmania set (`bmclubmx`) and one complete MIX set.
- **Twinkle clones (B):** `bmiidx3a`, `bmiidx3b`, `bmiidx6a`, `bmiidx7`. Each needs:
  - a manifest;
  - a boot-to-attract lockstep check;
  - one song.
- **DDR 3rd regional (B):** `ddr3mk`, `ddr3mka`, `ddr3ma`, on `ddr3mj`'s path.
- **Exit:** each listed set boots and plays a song, and the launcher shows it ready.

### Phase 2: System 573 analog catalogue (1–2 weeks; MAME is the oracle)

- **DDR 1st (`dstage` family), DDR 2nd variants, `dsftkd`:** the `ddr2m` path, with the cassette variant and I/O per set.
- **GuitarFreaks 1st and 2nd:** analog-board guitar input (the `gtrf` port map from `gtfrk11m`) and the X76F041 cassette.
  - The Link Kits share a cabinet link: stub it as MAME does at first.
- **DrumMania 1st (`pcnfrk`, `drmn`):** MAME doesn't run it, so documentation rules apply (§19 decision 2).
- **Exit:** a lockstep run to attract and one scripted song per family; regress cases for `dstage` and `gtrfrks`.

### Phase 3: the DDR digital line (2–4 weeks; the fork is the oracle)

- **The oracle:** build windyfairy's `bemani` branch on Linux (memory-capped, 573 driver only, as `mame-pin/k573` was). Its 2022 commit should match the rar's `mame.exe`. If the build fails, run the Windows exe under Wine.
  - Record each game's oracle run from the rar's installed NVRAM.
- **Work per game:**
  - the cassette, per MAME's config: X76F100 pair for `ddr3m`; X76F041 + ZS01 for `ddr3mp`/`ddr4m`/`ddr4mp`; ZS01 for `ddrusa`, `ddr5m`, MAX, MAX2 and EXTREME; X76F041 for `dsem`;
  - the FPGA and MP3 variant;
  - PC Cards;
  - the first-boot install.
- **Order:**
  1. `ddr3mp`, `ddr4m`, `ddr4mp`, `ddrusa`, `ddr5m`;
  2. `ddrmax`, `ddrmax2`;
  3. `ddrextrm` (the prize);
  4. Dancing Stage;
  5. **Solo** last: its own FPGA map, 6-panel pads and outputs.
- **Exit:** each plays a song; `ddrmax2` and `ddrextrm` in regress; worst frame inside the N100 budget (§19).

### Phase 4: GuitarFreaks and DrumMania 2nd–10th (3–5 weeks)

- **Base:** the `drmn10m` and `gtfrk11m` code, going backwards.
  - From 4th Mix on: the multi-session unit, already done.
  - 9th/10th: the e-AMUSEMENT network unit, already done for 10th/11th.
- **Session play for each pair** over `runtime/sys573/link`.
- **Stretch: Keyboardmania 3rd in the GF 6th × DM 5th session.** It needs a serial link from the Firebeat runtime to the 573 link, a cross-board session no emulator runs today.
- **Exit:** every pair plays a session song.

### Phase 5: the rest of the BEMANI 573 games (ask the owner per game)

- Dance Maniax 1/2/J-Paradise: IR hand sensors mapped to keys or a controller.
- Mambo a GoGo.
- Martial Beat.
- Punch Mania / Fighting Mania.
- DDR Karaoke Mix 1/2 (`k573kara`, out of scope by default).

Each needs an input design before code. The owner decides which are worth it.

### Phase 6: cross-cabinet sessions

- **DDR 2nd Club Version × IIDX club** (`ddr2mc`/`bmiidxc`, `ddr2mc2`/`bmiidxc2`): System 573 ↔ Twinkle over serial at 19,200 or 38,400 baud (the rar's notes).
  - As the fork notes, the IIDX cabinet plays both cabinets' music; HIRAKU can forward it.
  - This needs the IIDX club sets (B) too.

### Phase 7: new boards

- **`ddrkids` on Konami GS:** a small new core, and MAME runs it. Low priority.
- **Python 2:** MAIN's PY-6, when the media turns up. This archive has none.

### Sizes, roughly

| Phase | Sets | Effort |
|---|---|---|
| 0 | — | 1–2 days |
| 1 | ~20 | ~1 week |
| 2 | ~45 (most are clones) | 1–2 weeks |
| 3 | ~22 | 2–4 weeks |
| 4 | ~35 | 3–5 weeks |
| 5 | ~17 | owner's call |
| 6 | 4 | ~1 week after B and C |
| 7 | 1 + Python 2 | separate |

