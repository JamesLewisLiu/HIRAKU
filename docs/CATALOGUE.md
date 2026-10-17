# The rest of the catalogue (written 2026-10-15)

This compares the owner's own collection of MAME 0.243 BEMANI sets with what HIRAKU runs today. It ends in a plan.

## What the archive is

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

#### Phase 1 progress (2026-10-16)

**Installed from the archive: 19 games.**
- DJ Main: `bm2ndmixa`, `bm3rdmixa`, `bmclubmx`, `bmcompmxb`, `bs4thmix`, `hmcompmx`, `hmcompm2`, `popn1a`, `popn1k`, `popnstex`.
- System 573: `ddr3mk` (with the rar's installed NVRAM as its seed), `ddr3mka`, `ddr3ma`.
- Twinkle: `bmiidx3a`, `bmiidx3b`, `bmiidxa`, `bmiidxsa`, `bmiidx7`, `bmiidx6a`.
- New manifests: the six IIDX revisions and the three DDR 3rd regional versions.
- A revision shares its parent's video disc and hard disk: a hard link, or a symbolic one where the parent's sits on another filesystem.

**Ingest learned three more things:**
- ROM file names an older MAME used, told by CRC (`803k a01.bin` is now `803k_a01.bin`).
- `--game ID`, for a clone inside its parent's merged set (the rar's `ddr3mk.zip` holds `ddr3mka`/`ddr3ma`).
- CD sharing between games.

**Every game checked against MAME at boot.**
- Same script, MAME's power-on clock; an `out/oracle-roms` rompath of links under current names; Twinkle `--mame-cd`.
- **All in lockstep:** main/work RAM on every frame, every snapshot.
- `bmiidxa` (1st style) is checked against the SIO1-patched MAME (`oracle/mame-patches`), as `bmiidx` is: stock MAME stops it at E600.
- The DDR Korea/Asia clones must not get the archive's own zips: MAME then loads their pre-master cassette dumps by name.

**DJ Main, the whole catalogue in lockstep.**
- Checking the new DJ Main sets showed the known M13 gap (PLAN.md): later games leave MAME's RAM about 78 s in.
- **Two causes:**
  1. Musashi's per-bit cost on rotates by a register count, which MAME doesn't charge before the 68030.
  2. Each disk's own geometry: MAME's drive takes it from the CHD; HIRAKU assumed 16×63.
- Found by cycle-stamped instruction traces of both CPUs (`HK_DJ_ITRACE`, `HIRAKU_ITRACE_CYCLES=1`).
- **Now all 25 DJ Main sets on hand run cycle-exact through boot and attract.** `bscompmx` and `popn1` have no media here.
- Regress cases `bmclubmx-boot`, `bmcompm2-boot` and `popnstex-boot` are added, with baselines recorded.
- **A scripted game on each IIDX revision**, from the NVRAM its first boot leaves (coins, mode and sound select, a stage played to its result), is in lockstep with MAME: main RAM on every frame and every snapshot. That's 10,000–14,000 frames each; `bmiidxa` is checked against the patched MAME after its backup clear.
- **DDR 3rd regional:** `ddr3mk`, `ddr3mka` and `ddr3ma` install from CD with the install cassette, in lockstep with MAME (16,200 frames, 270 snapshots). Then, from what the install left, they play XANADU stepped through to its result, again in lockstep (16,000 frames, 266 snapshots).
- The launcher shows all 19 ready: installed in `dumps/`, binary built. `ddr3mk` starts from the rar's installed saves; the Korea and Asia versions install on their first start.
- **Phase 1 is done.** Left: hands-on play.

### Phase 2: System 573 analog catalogue (1–2 weeks; MAME is the oracle)

The analog I/O board (GX700-PWB(F)): music is CD audio, and MAME at the pin runs most of these sets. So everything here is checked in lockstep with MAME, as `ddr2m` was. Written 2026-10-16 from the catalogue and a MAME boot of each family with the archive's files.

#### What the media allows

| Family | Sets | Media | MAME | Status |
|---|---|---|---|---|
| DDR 1st | `dstage`, `dstagea`, `ddru`, `ddrj`, `ddra`, `ddrja` | complete; the rar's merged `dstage.zip` has master's cassette dumps | `ddrja` NOT_WORKING, the rest run | **ready** |
| DDR 2nd versions | `ddr2mla` (Link, JAB), `ddrbocd` (Best of Cool Dancers) | complete | run | **ready** |
| DDR 2nd club | `ddr2mc`, `ddr2mc2` | complete | run | ready to boot; the session with IIDX is Phase 6 |
| Dancing Stage | `dsftkd` (TRUE KiSS DESTiNATiON) | older cassette dump (X76F100, as GF 2nd's) | runs | probably fine, like GF 2nd; check first |
| GuitarFreaks 2nd | `gtrfrk2m`, 5 regional clones, Link Kits 1 and 2 | older cassette dumps (X76F100) | run | **ready**: MAME boots them to the first-boot flash install |
| GuitarFreaks 1st | `gtrfrks` and 7 clones | older X76F041 dumps: the write password is zeros | runs with master's dumps | **blocked**: PASSWORD-CHIP ERROR (-2N) in MAME |
| DrumMania 1st | `pcnfrk`, `pcnfrka`, `pcnfrkk`, `drmn`, `drmnu` | older X76F041 dumps | NOT_WORKING | **blocked**: SECURITY-PCB ERROR (-2N) in MAME |
| missing pieces | `ddr2ml` (JAC: CD `885jac01`), `drmna` (`881jaa02`), `ddrjb` (its flash dumps), `ddr2mlb` and four GQ886 GuitarFreaks (not in the archive) | — | — | need media |
| karaoke | `ddrkara` | the karaoke unit (`k573kara`) | NOT_WORKING | out of scope |

**Media to ask the owner for:**
- master-era cassette dumps for GuitarFreaks 1st (`gq886*.u1`) and DrumMania 1st (`gq881*.u1`): 548-byte X76F041 images, the CRCs MAME lists;
- the four missing disks and sets above.

Nothing about the cassettes can be made up: the passwords are the chips' own.

#### What HIRAKU's 573 core lacks for these

The core already has:
- the analog board;
- every cassette kind these sets use (`x`, `xi`, `yi`);
- PC Card slot 1 at 32 MiB;
- the JVS memory card reader;
- the GuitarFreaks and DrumMania input maps (from 11th and 10th Mix).

Three small gaps:
1. **PC Card slot 1 at 16 MiB** (`pccard1_16mb()`: `ddr2mla`, `ddrbocd`). MAME's linear flash card device in that size, as the 32 MiB one.
2. **The install CD.** MAME's CD drive holds three discs (`install`, `install2`, `runtime`; the menu swaps them). `ddr2mla`, `ddrbocd` and the club versions install from a separate CD first.
   - `[cd] install_chd` in game.toml, `--cd install` to boot with it (the switch exists for DrumMania 10th's network installer).
   - The launcher's first-boot step offers it, as the install cassette is offered now.
3. **DrumMania 1st's lamps.** On the analog board `init_drmn` sends the outputs to `drmn_output_callback`; HIRAKU sends them all to DDR's.

#### Tooling first: manifests from MAME's driver

About 90 System 573 sets are ahead (Phases 2–5). Writing game.toml by hand doesn't scale, so `tools/gen573.py` will:
- read `tools/data/mame-bemani.json` for the ROMs (the cassette's `cassette:game:eeprom`/`:id`, the install cassette's, the H8, the digital board's ID) and disks (`runtime`, `install`, `install2`, `multisession`);
- read the machine config from MAME's `ksys573.cpp` (pinned copy): `k573a`/`k573d` gives the I/O board; `cassx`/`cassxi`/`cassyi`/`cassyyi`/`casszi`/`cassxzi` the cassette kinds; `pccard1_16mb`/`_32mb`, `pccard2_32mb`; the memory card reader; the multi-session unit;
- take the family from the title (DDR / Dancing Stage → `ddr`, GuitarFreaks → `gtrf`, DrumMania / Percussion Freaks → `drmn`);
- write the manifest with the comments the hand-written ones have.

Regenerating `ddr2m`, `ddr3mj`, `drmn10m` and `gtfrk11m` must give manifests equivalent to the ones that run.

#### Order of work

1. **`gen573.py`**, checked against the four existing 573 manifests.
2. **DDR 1st** (`dstage`, then its clones):
   - ingest from the rar's merged set with `--game`, as the DDR 3rd clones;
   - boot against MAME; then the install (DDR 1st installs to flash from the runtime CD, as 2nd does: Test twice);
   - a scripted song, its script made from `ddr2m-play.his`.
   - Regress cases `dstage-boot` and `dstage-play`.
3. **The 16 MiB PC Card and the install CD**, then `ddr2mla`, `ddrbocd`, `ddr2mc`, `ddr2mc2`:
   - first boot with the install CD, then the game CD;
   - lockstep through install and attract;
   - the club versions alone (their IIDX session waits for Phase 6).
4. **GuitarFreaks 2nd** (`gtrfrk2m`):
   - first boot: the flash and PC Card install MAME already reaches;
   - then attract and a song played with the guitar map (`gtfrk11m`'s);
   - then the regional clones and the Link Kits. The memory card reader is in; the link between cabinets is out of scope here, as in MAME.
   - Regress `gtrfrk2m-boot` / `-play`.
5. **Dancing Stage TKD**: boot first, as it has an older X76F100 dump. If it fails as GF 1st does, it joins the blocked list.
6. **When the owner has master's dumps:**
   - GuitarFreaks 1st, against MAME;
   - DrumMania 1st: MAME doesn't run it (NOT_WORKING), so it's matched to MAME as far as MAME goes, then carried on from the documentation (§19 decision 2). Its lamps need gap 3.

#### Checks, as in Phase 1

- Every set: boot in lockstep with MAME (the same script, MAME's power-on clock, `out/oracle-roms`).
- One per family: the install, and a song from what the install left. Regress cases for `dstage` and `gtrfrk2m`.
- Mind the 573 MAME build's speed: about 1 h 25 min per install and song. Run them one at a time, and tell the other sessions before long runs.

**Exit:** DDR 1st, DDR 2nd's versions and GuitarFreaks 2nd play a song in lockstep with MAME; GuitarFreaks 1st and DrumMania 1st wait for media.

#### Phase 2 progress (2026-10-16): done, where the media allows

Split across three sessions on the owner's request:
- DDR 1st, Dancing Stage TKD, GuitarFreaks 1st and DrumMania 1st here;
- GuitarFreaks 2nd in a second session (3e46e7e);
- the install CD and DDR 2nd's versions in a third (d933420).

**Every game below runs in lockstep with MAME at the pin: main RAM and every snapshot, through boot, the first-boot install and a song.**

| Family | Sets | Notes |
|---|---|---|
| DDR 1st | `dstage` (install and song); `dstagea`, `ddru`, `ddrj`, `ddra`, `ddrja` (boot) | Three coins a credit on `dstage`. `ddrja` has no cassette dump and stops where MAME does. Regress `dstage-install`, `dstage-play`. |
| DDR 2nd versions | `ddr2mla`, `ddrbocd`, `ddr2mc`, `ddr2mc2` | Installed from their install CD (`--cd install`; the launcher's first boot). The club versions alone; their IIDX session is Phase 6. |
| Dancing Stage TKD | `dsftkd` | With the archive's older X76F100 dump. Two coins a credit. |
| GuitarFreaks 2nd | `gtrfrk2m` and its five clones (boot); `gtrfrk2m`, `gtrfrk2mja` and both Link Kits (install and song) | Regress `gtrfrk2m-install`, `gtrfrk2m-play`. |
| GuitarFreaks 1st | `gtrfrksj` | With master's cassette dump from the owner. |
| DrumMania 1st | `drmn` | With master's cassette dump and the set's pre-installed flash and timekeeper. MAME flags it NOT_WORKING, yet with that dump MAME plays it too, and HIRAKU matches it. |

**GuitarFreaks 2nd's install paths:**
- `gtrfrk2m` and Link Kit 2 (929jbb02) end at INSTALLATION NOW COMPLETE and need a power cycle.
- The regional CD (883__a01) and Link Kit 1 (929jaa02) end at FLASH-ROM INITIALIZE COMPLETE; a second Test starts the game.
- The Link Kits check the whole PC Card at power-on (to frame ~6800) and add an EDIT DATA screen.

**Tooling:**
- `tools/gen573.py` writes a 573 manifest from MAME's driver. Its configs are parsed into `tools/data/mame-bemani.json` by `tools/mameset.py --refresh LISTXML KSYS573_CPP`, which also keeps each ROM's region.
- Manifests can list chips the set has filled: `preload = ["29f016a.27h:file", "m48t58:file"]`. MAME's devices start from such regions; DrumMania 1st has them, and so do later DrumMania and GuitarFreaks sets.
- **`oracle/lockstep.sh`** runs MAME and then HIRAKU on one script with MAME's clock and compares them; NVRAM is carried from step to step.
  - MAME hashes RAM on every 10th frame (`HIRAKU_HASH_EVERY`). Hashing 4 MiB in Lua every frame had been ~96 % of a 573 run.
  - A difference is pinned to its frame by a second pass hashing every frame around it.
  - MAME's result is reused when the script, start NVRAM, build, options and the rompath's files for the set are unchanged.

**The 573 core:**
- PC Card slot 1 at 16 MiB.
- The analog board's outputs go where MAME's init sends them; DrumMania's analog lamp numbering is mapped.
- GuitarFreaks' effect knobs read 3 at rest (MAME's PORT_REVERSE dials).
- A missing cassette dump is the chip's zeros rather than a crash.
- **The CD shim lays discs out as MAME does:** pregaps (PREGAP, INDEX 00, POSTGAP), and track offsets in a .bin that mixes 2048-byte and audio sectors.

**Left in Phase 2, all for media:**
- GuitarFreaks 1st's and DrumMania 1st's other regions: their master-era cassette dumps (`gq886eac/uac/aac.u1`; `gq881ua/ea/aa/ka.u1`).
- `ddrjb`'s flash dumps, `ddr2ml`'s CD `885jac01`, `drmna`'s `881jaa02`.
- `ddr2mlb` and the four GQ886 EAA/UAA/JAA/AAA GuitarFreaks, which aren't in the archive.
- `ddrkara` (the karaoke unit) stays out of scope.

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

#### Phase 3 progress (2026-10-17)

Split across three sessions again:
- 3rd Mix Plus, the 4th Mixes and Solo here;
- 5thMIX, MAX, MAX2 and EXTREME in a second session (bbfe79a);
- DDR USA and Dancing Stage in a third (cefdb4b).

The fork wasn't needed as the oracle: with current dumps, MAME at the pin runs every one of these games, and that's what they're compared with.

**In lockstep with MAME (main RAM and every snapshot), from the first boot's install to a song:**

| Sets | Notes |
|---|---|
| `ddr3mp` | Installs with no input, like `ddr3mj`. |
| `ddr4m`, `ddr4mj`, `ddr4mp`, `ddr4mps` | The CD first offers to update the CD-ROM drive's firmware (NOTE 604): Test goes on. INFO 506 by frame ~22,800. `ddr4mp` and `ddr4mps` then say the RTC RAM was cleared (NOTE 605): Test again. Three coins a credit on `ddr4m` (AAA). `ddr4mps` is Solo: six panels, two sensors each. |
| `ddrusa` | NOTE 604: Test, and the install is done by frame ~7,500. |
| `ddr5m`, `ddrmax` | NOTE 604, then the install; NOTE 605 after it. |
| `ddrmax2`, `ddrextrm` | Install with no input; NOTE 605 after it. EXTREME runs its beginner tutorial first. Regress `ddrmax2-install`, `ddrmax2-play`, `ddrextrm-install`, `ddrextrm-play`. |
| `ddrsbm` | Solo Bass Mix: Test answers INITIALIZE FLASH-ROM?, then a game from that flash. |

**The 573 core:**
- The Solo cabinet (`ddrsolo` family): MAME's ports, each panel's two sensors (a script or the shell can press a panel, which presses both), the lamp outputs, and IN2's bit 7 cleared at init as MAME's driver does.
- Solo Bass Mix's own FPGA program (MAME's `set_ddrsbm_fpga`): the MP3 counter runs free from power-on, and the game syncs its playback to it. Without it, the game waited at READY with the counter at 0; the cycle-exact traces matched, and two bytes of RAM gave it away.
- `preload` for the timekeeper as well as the flash.
- The install cassette's own variant (`install_kind`), when it differs from the game cassette's: X76F041 install beside a ZS01 game cassette, as on 3rd Mix Plus and the 4th Mixes.

**The rompath:** an older clone zip shadows a merged parent zip that holds the current dumps under `<clone>/`, since MAME loads by name. So `ddr4mj.zip` had to go from the oracle rompath, as `ddr3mka`/`ddr3ma` had in Phase 1.

**Left, all for media:**
- **Cassette dumps older than MAME's** (MAME stops at the same ERROR 121, or "insert the security cassette", that HIRAKU does):
  - `dsem`, `dsem2`, `dsfdr`, `dsfdct`, `dsfdcta`;
  - `ddr4ms`, `ddr4msj`;
  - `ddrs2k`, `ddrs2kj`.
  - No zip in the archive has the current dumps. The exception is `dsfdr`'s install cassette, the common X76F041 one, but its ZS01 game cassette is missing. The manifests are in, so the dumps are all these sets need.
- `ddrkara2` isn't in the archive.

### Phase 4: GuitarFreaks and DrumMania 2nd–10th (3–5 weeks)

- **Base:** the `drmn10m` and `gtfrk11m` code, going backwards.
  - From 4th Mix on: the multi-session unit, already done.
  - 9th/10th: the e-AMUSEMENT network unit, already done for 10th/11th.
- **Session play for each pair** over `runtime/sys573/link`.
- **Stretch: Keyboardmania 3rd in the GF 6th × DM 5th session.** It needs a serial link from the Firebeat runtime to the 573 link, a cross-board session no emulator runs today.
- **Exit:** every pair plays a session song.

#### Phase 4 progress (2026-10-17)

**The media:** the archive's cassette dumps for every set here were older than MAME's. The owner supplied current ones, and the CDs the archive lacked: DrumMania 9th's four, and the export discs of `gtrfrk5ma`, `pcnfrk9m` and `pcnfrk10m`.

**Tooling:**
- In current MAME the Japanese DrumMania sets are clones of Percussion Freaks (`drmn2m`'s parent is `pcnfrk2m`), so their CDs sit in the parent's folder.
- Split across three sessions: GF 3rd/4th × DM 2nd/3rd here; GF 5th–7th × DM 4th–6th in a second (8d64281, 35254c9); GF 8th–10th × DM 7th–9th in a third (ef8832c).

**In lockstep with MAME (main RAM and every snapshot):**

| Sets | Runs | Notes |
|---|---|---|
| `gtrfrk3m`, `drmn2m`, `gtrfrk4m`, `drmn3m` | install, a song | GF 3rd installs from its install CD (INFO 507), DM 2nd on its own (INFO 506); GF 4th and DM 3rd first offer the CD-ROM drive update (NOTE 604: Test). GF 3rd and DM 2nd then load from the CD for 4–5 minutes before the title. |
| `drmn2mpu` | the kit's install, a song | DrumMania 2nd's Session Power Up Kit, after MAME's notes: DM 2nd installed, then the kit's disc booted with DIP switch 4 on the CD-ROM drive (`--boot-cd`; MAME: `oracle/lua/boot-cd.lua`). |
| `gtrfrk5m`, `gtrfrk6m`, `gtrfrk7m`, `gtrfrk8m`, `gtfrk10m` | install, a song | NOTE 604 or none, INFO 508; NOTE 605 on the next boot. |
| `drmn4m`–`drmn9m`, `drmn7ma`, `drmn9ma` | install; a song until the boot checks reach the multi-session unit | MAME's unit is a stub ("Please call attendant"); HIRAKU's answers, and the game plays on (two runs agree), as with `drmn10m`. |
| `gtfrk3ma`, `gtfrk3mb`, `drmn2mpu`, `pcnfrk`, `pcnfrka`, `pcnfrkk`, `pcnfrk2m`, `pcnfrk2mk`, `pcnfrk3m`, `pcnfrk3mk` | boot | |
| `gtrfrk5ma`, `pcnfrk4m`, `gtrfrk8ma`, `gtfrk10ma` | boot, install | |

**Session play (MAME can't run sessions; two runs agree on both cabinets, every frame):**
- **Before the unit, SIO1 to SIO1:** GF 3rd × DM 2nd with its Power Up Kit ("SESSION MODE", CUTIE PIE together), and GF 4th × DM 3rd ("Extreme session").
- **Through the unit:** GF 5th × DM 4th, GF 6th × DM 5th, GF 7th × DM 6th, GF 8th × DM 7th, GF 10th × DM 9th.
- **Regress:** `drmn3m-install`, `gtrfrk4m-install`, `drmn3m-session` + `gtrfrk4m-session`; `gtrfrk5m-install`, `gtrfrk5m-play`, `drmn4m-install`, `drmn4m-session` + `gtrfrk5m-session`.

**The 573 core:**
- **The session cable joins SIO1 to SIO1:** either cabinet can host when neither has the unit. The link's hello (version 3) says whether a side sends its unit's music, so a guest waits for sound only from a host that sends it.
- **`--boot-cd`:** DIP switch 4 on the CD-ROM drive, for an install over an installed game.

**Open:**
- **`gtrfrk9m`** halted at "DATA 7 : INITIALIZING", in MAME as in HIRAKU (lockstep to the halt); it runs since 2026-10-17 (below). It's an assertion (mts.c line 120): the CD lookup of `/DATA4/BG_LIST.BIN` returns -1 although the file is on the disc, so a table loop runs over a thread id. GF 9th × DM 8th waits on it.
  - **Cause (2026-10-17), MAME's "konami80s" DMA hack, which HIRAKU copies:**
    - The lookup is `0x80021804` in the game's ATAPI library. On a cache miss it reads 64 sectors from the directory's LBA into a 128 KiB buffer with READ(12), in 2,048-byte DRQ blocks. Each block gets an IRQ; the handler at `0x8001f4f8` DMAs it on channel 5.
    - BG_LIST's read (LBA 29, f9499) is the only one of its kind that fails: the poll returns -8, the lookup -8, the caller -1.
    - The IRQ after block 1 is serviced 18.2 ms late (normally 1.85 ms). By then `atapi_xfer_end`'s "DMA more data than requested" re-arm (DRQ still set, 240,000 cycles ≈ 7.1 ms) has already moved block 2 by itself.
    - The game then sees 63 data IRQs, and the last one has no DRQ with 2,048 bytes left: -8 at `0x8001f5b0`. The same packet, retried at f9508, works.
    - Evidence: `out/oracle/gtrfrk9m/lookup/lookup.txt` and `irq/irq.txt`; probes in `out/gf9/probe/`.
    - A real drive holds DRQ until the host reads, so this is an emulation bug shared with MAME (`ksys573.cpp:1165-1170`; HIRAKU `runtime/sys573/board.cpp:614-616`).
  - **The departure, for `gtrfrk9m` only (2026-10-17):** `atapi_xfer_end` re-arms only when the DMA stopped inside a DRQ block, not when the drive raised a fresh block during it (a flag set by `ata_interrupt` within the read loop). For every game it changed DDR 3rd Mix's and Dancing Stage's install and play (they rely on the re-arm after a whole block), so it's that game's alone (`runtime/sys573/board.cpp`; MAME the same way: `oracle/mame-patches/ksys573-dma.patch`, not built yet).
    - Checked: from a blank NVRAM the install (28,800 frames) and the boot reach the attract demo (frame 24,000: GAME OVER, DEMO). Every other 573 regress case is unchanged (the 40 of them run against the core binary). GF 9th × DM 8th's session is next.
- **The multi-session unit's CD isn't dumped** for `pcnfrk4mk`, `pcnfrk5m`, `pcnfrk5mk`, `pcnfrk6m`, `pcnfrk7m`, `pcnfrk8m`, `pcnfrk9m`, `pcnfrk10m`, though MAME fits them with the unit. Manifests only.
- **The existing `drmn10m-session` + `gtfrk11m-session` regress pair** differs from its 2026-10-14 baseline from frame 11,717 (picture). Found 2026-10-17: da578d6's effect knobs at rest (3, MAME's PORT_REVERSE dials, where they were 0) make GuitarFreaks 11th play otherwise in the session (a MISS at frame ~12,000). Intended; with the knobs held at the old rest value the pair matches the old baseline over all 16,800 frames. The baseline needs re-recording (out/regress-session-drift/ has the runs).
- **Keyboardmania 3rd in the GF 6th × DM 5th session** (the stretch) isn't started.

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

