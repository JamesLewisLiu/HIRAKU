# Lola Retro for RetroArch

A CRT shader that simulates the whole analog chain instead of drawing scanlines on top of pixels:

**console video encoder → cable → TV decoder → electron beam → phosphor mask → glass**

Named after Lola, my retriever. Retro because it is retro.

## Quick start

1. Copy this `lola-retro` folder into RetroArch's shader folder (see [Install](#install)).
2. Start a game, open **Quick Menu → Shaders → Load** and pick **`lola-retro/curved/consumer-tv-composite.slangp`**.

That is the TV most people had, on the cable that came in the box. Everything else adjusts itself:
the console is detected from the frame size and the mask adapts to your screen resolution.
3D consoles can run at a higher internal resolution: the shader reads the upscaled frame as blocks of one
console pixel, so the signal stays right and the extra detail only makes edges smoother.
A few cases need one setting, see [RECOMMENDED-CORES.md](RECOMMENDED-CORES.md).

On an HDR display, use the same presets from `lola-retro/hdr/curved` instead, see [HDR](#hdr).

## Curved or flat

Every folder has the same 13 presets.

| Folder | Screen |
|---|---|
| `curved/` | The glass bulges like a real tube, with rounded corners. The picture on it stays nearly rectangular, as on a well adjusted set (see **Raster bend**). |
| `flat/` | Same presets, flat screen and square corners. Pick this if curvature bothers you. |
| `hdr/curved/`, `hdr/flat/` | The same presets for HDR displays, see [HDR](#hdr). |

## Presets

| Preset | Use it for | What it simulates |
|---|---|---|
| `consumer-tv-composite` | **Start here.** Everything from NES to PS1, especially Mega Drive and other games drawn for composite. | Ordinary living-room TV on the composite cable. Dithered waterfalls turn translucent, colours bleed. |
| `consumer-tv-scart-rgb` | The same games with a clean picture. The European SCART setup. | The same TV fed RGB. Sharp, no composite artefacts, same tube. |
| `trinitron-s-video` | PS2, GameCube, N64 and other late-90s consoles. | Sony Trinitron on S-Video. Aperture grille with the two damper wires, cool white. |
| `studio-monitor-rgb` | The sharpest picture with strong scanlines (the PVM/BVM look). | Broadcast/studio monitor on RGB, calibrated 6500 K white. |
| `old-tv-rf` | Nostalgia for the antenna cable. Noisy on purpose. | Old TV on the antenna input. RF snow, ghosting, soft focus, round tube. |
| `tv-show-broadcast` | Games as you saw them on a TV programme about games (test). | The console in a TV studio, broadcast in PAL and received with a roof antenna on the same living-room TV. The studio made a standard interlaced picture of the game, so the dark gaps between the lines are gone. See [TV show](#tv-show). |
| `crt-projector` | 24 kHz arcade games such as Daytona USA (Sega Model 2). | Arcade CRT projector (three tubes on a flat screen). No mask, colour fringes, soft. |
| `pc-monitor-vga` | DOS and Windows 9x games (320×200 up to 1024×768), Dreamcast on the VGA cable. | A typical mid-90s 15-inch PC monitor, described in millimetres (see [PC monitor](#pc-monitor)). 320×200 is drawn twice (400 lines), fine dot mask, a thin black border. |
| `pc-monitor-vga-good`, `-premium`, `-premium-plus` | The same, on a better 15-inch monitor; sharper Windows desktops at 800×600 and 1024×768. | Finer dot mask and a smaller, better focused beam (see [Better PC monitors](#better-pc-monitors)). |
| `pc-monitor-vga-extreme` | Long work on the Windows desktop. | Sharper than any real 15-inch tube: the gaps between the lines show even at 1024×768. |
| `amiga-1084s` | Amiga, and 15 kHz arcade games (CPS1/2, Neo Geo MVS). | Commodore 1084S on RGB, built from its service manual (see [Amiga 1084S](#amiga-1084s)). 0.42 mm slot mask, the picture nearly filling the glass. |

The CRT projector already has a flat screen, so its `curved` version only adds rounded corners.

## Install

1. In RetroArch, open **Settings → Directory → Video Shaders** to see where your shader folder is.
2. Copy this whole `lola-retro` folder into that folder. If an older copy is there, replace it.
3. Start a game, open **Quick Menu → Shaders → Load**, go into `lola-retro`, then `curved` or `flat`, and pick a preset.

The shaders are in the slang format, so the video driver must be **Vulkan, glcore, D3D11, D3D12 or Metal**
(**Settings → Drivers → Video**). The old `gl` driver does not run slang shaders.

To use a preset automatically for one system, load it and choose **Quick Menu → Shaders → Save → Save Core Preset**.

## Core settings that matter

For every system from NES to PS2 (core, preset, console timing and settings), see
[RECOMMENDED-CORES.md](RECOMMENDED-CORES.md). The short version:

The composite simulation needs the console's real pixel grid:

- **PlayStation** (Beetle PSX, SwanStation): no texture filtering, dithering on. Internal resolution 1× or 3× and up
  works by itself; at 2× set **Console timing** to 4.
- **Mega Drive / Genesis** (Genesis Plus GX, PicoDrive): any built-in "Blargg NTSC" filter **off**.
- **SNES** (Snes9x, bsnes): built-in NTSC/blargg filters **off**, hi-res blending **off**.
- **DOS and Windows 9x** (DOSBox Pure): leave the default VGA output; use `pc-monitor-vga`. It covers every PC
  mode from 320×200 to 1024×768 (the other presets would read 640×480 as a 480i TV picture).
- RetroArch: **Settings → Video → Scaling → Aspect Ratio**: *Core provided* or *4:3*. Integer scaling is optional.
  With **Around the tube** on, *Full* works too: the tube stays 4:3 and the monitor's surroundings fill the rest of the screen.

**Internal resolution.** Frames with more than 576 lines are read as an upscaled 240-line mode (3× and up).
At 2× an upscaled PS1 frame looks like 480i, so set Console timing 4 by hand. 480-line consoles
(PS2, GameCube, Dreamcast) upscaled: set Console timing 5, then the frame is read as upscaled 480i.
In the TV presets a higher internal resolution costs about as much GPU time as native: the encoder and
decoder always work at 1280 samples per line (see [Performance](#performance)).

## Parameters

Every preset only sets parameters, so you can tweak any preset in **Quick Menu → Shaders → Shader Parameters**.

| Parameter | Meaning |
|---|---|
| Console timing | Pixel clock and colour-subcarrier rhythm of the console. 0 = auto (see below). |
| TV standard | 0 auto (by line count), 1 NTSC, 2 PAL. Set it by hand for PAL consoles in 60 Hz mode (PAL60, common on PS2 and Dreamcast): the line count says NTSC but the colour is PAL. It also switches the colour decoder and the phosphors. On a TV show (Cable 4) it is the standard of the programme: `tv-show-broadcast` sets 2, a European programme, and a game in the other standard goes through the studio's standards converter (see [TV show](#tv-show)). |
| Cable | 0 RF, 1 Composite, 2 S-Video, 3 RGB, 4 TV show: the game as seen on a TV programme, from a studio through the antenna (see [TV show](#tv-show)). |
| Comb filter | Better TVs separated colour with a comb filter. Only works when the console flips the subcarrier phase every line (PlayStation, Amiga). On a TV show (Cable 4) the broadcast's own NTSC timing lets it work for every console. |
| Luma trap in the console | How much of the fine luma detail the console's encoder filtered out before mixing in colour. Lower it for a stronger rainbow on fine stripes (Sonic's waterfall), raise it for a cleaner picture. Real consoles differed a lot here: 0 is like an early Mega Drive Model 1 (strong rainbow), 0.75 (default) a typical console, 1 a later model with a better encoder. |
| Dither blend on RGB and S-Video | Off by default. Modern processing, not something a real TV did (similar to the notch filter of the RetroTINK 4K Pro): 1-pixel dither stripes blend into the colour or transparency the artists meant, without composite's rainbow and blur. Lines one pixel thin get slightly softer. |
| Sharpness, Colour, Tint, Brightness, Contrast | The TV's own knobs. Tint only exists on NTSC sets. |
| Colour decoder | 0 auto, 1 standard, 2 US TV, 3 Japanese TV. American and Japanese TVs decoded colour on shifted axes with extra red gain ("red push"): yellows lean orange, reds are stronger, skin looks warmer. Auto uses the US TV decoder (Sony CXA2025AS) for NTSC on composite, RF and S-Video, and the standard one for PAL and RGB. |
| RF noise | Snow on the antenna input (RF and TV show). |
| No flicker | On by default. Shows a still picture the way the eye saw the moving one: the dots and the cross-colour that crawl from frame to frame are averaged over the frames of their cycle (2 on the PlayStation, 3 on the NES), so they cancel, as the eye blended them at 50 or 60 Hz; both interlaced fields are drawn every frame. Turn it off for the real moving dot crawl and 480i field flicker. |
| TV show: tape copies | Cable 4 only. How many Betacam SP copies the game footage went through before it was broadcast (recorded, then edited onto the master tape: 2). Each copy softens luma and colour a little more. 0 is a live programme. |
| TV show: antenna ghost | Cable 4 only. A faint copy of the picture shifted to the right, from a reflection off a building or a hill on the way to the roof antenna. |
| Beam width in dark areas / on white | The beam spot grows with brightness, in both directions: bright lines merge and bright detail rounds off, dark lines show gaps and dark detail stays sharp. TVs; monitors in mm (PC monitors, the 1084S) use the spot sizes in mm below. |
| Horizontal focus | Horizontal spot size relative to the vertical one; 0.5 = a round spot. Not used on monitors in mm (their spot is round). |
| Tube gamma | 2.4 is typical. |
| Phosphor afterglow | Real phosphors fade within about a millisecond, so keep this low. |
| Misconvergence | How far the red and blue beams land from green, growing toward the edges. |
| Mask | 0 none, 1 aperture grille, 2 slot mask, 3 shadow mask, 4 projection screen. |
| Mask strength / triads per screen height | Lower the triad count for a coarser mask. Below about 2 screen pixels per triad the mask fades out to avoid moiré, so the fine PC mask shows best at 4K. On monitors in mm the triad count comes from the mask pitch instead. |
| White point | 6500 K neutral, 9300 K is the bluish white many TVs and monitors had. |
| Phosphors | 0 auto, 1 sRGB, 2 SMPTE-C, 3 EBU, 4 Japanese P22. The colours of the tube's phosphors. Auto: SMPTE-C on NTSC sets, EBU on PAL sets, sRGB on PC monitors, Japanese P22 on arcade monitors; the Trinitron preset uses Japanese P22 and the studio monitor EBU. Colours outside your display's range are clipped in SDR and kept in HDR. |
| Curvature, Rounded corners, Glass glow, Glass glow size, Darker edges, Overscan, Overscan height, Room reflection | The glass. Curvature is how much the glass bulges: its edges bow outwards and crop the corners of the picture. Glass glow sets how strong the halo around bright things is, Glass glow size how far it reaches. Set curvature and corners to 0 for a flat screen (that is all the `flat` presets change). Overscan is how far the picture runs past the edges of the glass; negative overscan adds a black border, like PC monitors. Overscan height is added to it for the height alone, like the monitor's height knob: raise it to fill the height (a 224-line SNES game keeps thin black bars at the top and bottom otherwise), lower it for a border at the top and bottom only. |
| Raster bend | How much of the glass curvature the picture follows. The deflection was corrected so that the raster looks nearly rectangular from the front; a well adjusted set kept the bow of its lines to about 1% of the height, which is the default (0.3). 0 is a perfectly corrected raster, 1 a picture bent as much as the glass. |
| Picture height | 0 (default): the picture sits in the TV's 240- or 288-line field, so a game with fewer lines (224) keeps a border, as on the real set. 1: the core's lines fill the height. Use it with **Aspect Ratio: Core provided** for cores that crop lines and set the aspect ratio from the lines they keep (Geargrafx, Beetle PCE/Saturn/PSX with their scanline options); with a fixed 4:3 it stretches the picture. It is a zoom of the same tube, so lines and beam keep their look. Not used on a TV show (Cable 4). |
| Linearity error horizontal, vertical | Off (0) by default. Real deflection was never perfectly linear: one side of the picture came out squeezed and the other stretched, differently on every set. Positive values squeeze the left or the top, negative the right or the bottom; 0.05 to 0.1 is a cheap set. |

| 15 kHz monitor in mm | Describes the tube in mm for 15 kHz modes too (on in the `amiga-1084s` preset): the parameters below then apply, the monitor shows the whole active line (a 320-pixel Amiga line leaves side borders), and 320×200 / 640×400 are read as an NTSC Amiga instead of VGA. PC monitors (31 kHz) always use mm. |
| Monitor size, Dot pitch | Nominal size in inches (the viewable picture is one inch less) and the mask pitch in mm. See [PC monitor](#pc-monitor) and [Amiga 1084S](#amiga-1084s). |
| Spot size in dark areas / on white | The beam spot in mm (width at half brightness). 0.30 / 0.45 is a typical 15-inch PC monitor, 0.28 / 0.65 the 1084S; lower both for a sharper tube. |
| Spot size in the corners | How much larger the spot is in the corners than in the centre (deflection defocus). 1.4 is typical for a PC monitor (dynamic focus kept good ones below 1.5, cheap ones reached 2); the 1084S had static focus, 1.5. |
| Real size: your screen's height, unit | Draws the tube as large as the monitor in **Monitor size** really was, with black (or **Around the tube**) around it, instead of filling your screen. Enter the height of your screen's picture area (measured, in cm, or in inches with unit 1); 0 fills the screen. On a large display this puts the monitor at its real size in front of you. Needs the picture area as tall as the screen (full screen). |
| Around the tube | What is around the tube: 0 black, 1 the monitor in a room, 2–11 the monitor in a scene (see [Scenes](#scenes)). The casing is measured on photos of real monitors and drawn as large as the real one, so it shows best with **Real size**. `amiga-1084s` gets an Amiga monitor (after the M1438S): warm off-white, a thick flat front, a tunnel with mitred corners down to a thin black mask at the glass, a plain chin with a green power light. The PC presets get a 90s PC monitor (after a Compaq 151FS): aged cream, a raised rim around a shallower recess, a blank plaque, and a control strip below with buttons and a green light. The TV presets get a 90s TV (after photos of several): dark grey plastic, thin sides, the glass almost flush with the front, and a tall chin with a speaker grille on each side of the controls (a power switch, a red light, four small buttons). TVs came from 14 to 29 inches, so the TV is sized to **Monitor size**. The walls around the glass face the picture, so the screen lights them softly in its colours and they change with it (**Screen light on the casing**); the room lights them from above, darker at the top and lighter at the bottom, and the satin plastic catches a little of the ceiling and the lamp on the edges that face up. |
| Room light | How bright the room is: 1 a dim room (warm lamps), 3 a lit room, 20 daylight. It lights the casing and the wall (in a scene only the casing, in the scene's colour: white in space, pink in synthwave, like its sun), and sets the reflection on the glass too (Room reflection is how much the tube reflects in a dim room), so in daylight the tube's black turns grey, as on the real one. Lower Room reflection if you want it darker. |
| Screen light on the casing | How strongly the screen lights the walls around the glass: 0 off, 1 as on the real monitor (worked out from the screen's light, the angle of the walls and the tinted glass; about 3–5% of the picture beside them, softly spread, as plastic scatters it), up to 3. Best seen in a dark room. |
| Scene speed | How fast the scenes move: 1 normal, 0 a still picture, up to 3. |
| HDR: paper white, HDR: display peak | Only in the `hdr` presets. Paper white is how bright a full white screen is, display peak the most your display can show. See [HDR](#hdr). |

### Scenes

**Around the tube** 2–11 put the monitor in front of a scene instead of in a room. They are decoration, not a
simulation, all drawn by the shader (no pictures), and they move (**Scene speed**). **Room light** lights the
casing in the scene's colour.

| | Scene |
|---|---|
| 2 | Space: the Milky Way across the sky with dark lanes of dust, clouds of glowing gas, bright stars with spikes of light and now and then a small galaxy; nearer stars slowly come closer and drift past. |
| 3 | Synthwave: a big striped sun over two ranges of mountains (the near one with neon lines along its slopes), palm trees at the sides, a neon grid floor that runs toward you, a purple sky full of stars. |
| 4 | Tiles: a 90s desktop wallpaper in the 16 Windows colours, scrolling slowly and diagonally. Eight patterns (cubes, studs, raised bricks, scales, pearls, a woven mat, a honeycomb, argyle) take turns every two minutes. |
| 5 | Bouncing lines: two shapes whose corners bounce off the edges of the screen, followed by fading copies that change colour along the trail, as on 90s screensavers. |
| 6 | Underwater: a calm sea floor from a 16-bit game: the surface rippling at the top, light falling in slanted rays, a reef far away, fish passing and now and then a school of small fish, rocks, corals, sponges and kelp on the sand with patches of light drifting over it, bubbles rising. |
| 7 | Copper bars: Amiga raster bars swinging around an invisible cylinder, stars flying sideways over a sky the copper fades to violet, a polished checkerboard floor that shows the bars. |
| 8 | Amiga demo: a screen from an early-90s Amiga demo, in 320×256 and 4096 colours. A glenz vector (a see-through solid of white and blue triangles, lit from one side) and a cube of vector balls that turns into a ball swing out from behind the monitor, stars fly toward you, a twister turns along the bottom and a DYCP scroller runs along the top, over a copper background. |
| 9 | Hills: a 16-bit landscape in parallax layers: the sun, clouds near and far, birds, pale mountains far away and snowy ones nearer, hills with bushes, round trees and pines, grass with flowers. |
| 10 | Night city: a skyline in three layers under a full moon: windows lighting up and going dark, thin clouds drifting past, now and then a plane, rooftops with water tanks, red lights and a hotel sign, cars on the street below. |
| 11 | Arcade carpet: neon shapes on black, like the carpets of 90s arcades, glowing as under black light: planets, stars, moons, lightning, spirals and more, with confetti between them. |

The pixel-art scenes draw their pixels as squares of a whole number of screen pixels, so they stay sharp on any
display. Space, Bouncing lines and Amiga demo take the most GPU time; on a slow GPU the other scenes are lighter.

### Console timing ids

| Id | Console | Why it matters |
|---|---|---|
| 1 | SNES / NES | 227⅓ subcarrier cycles per line: dot crawl moves diagonally. |
| 2 | Mega Drive H40 (320 wide) | Exactly 228 cycles: still vertical stripes; 1-pixel dithers become rainbow transparency. |
| 3 | Mega Drive H32 (256 wide) | Same rhythm as H40 at a slower pixel clock. |
| 4 | PlayStation | 227½ cycles: the pattern flips every line and every field, so a comb filter can remove it. |
| 5 | PlayStation 2 | 480i at 13.5 MHz. |
| 6 | Amiga | 320×256 PAL / 320×200 NTSC. The width tells lores, hires and superhires apart, and a frame that includes the overscan border (PUAE's default, 720 wide) keeps the right picture size. |
| 7 | PC VGA | 31 kHz; 200-line modes are drawn twice (400 lines). |
| 8 | Arcade 15 kHz | Generic 15 kHz RGB. |
| 9 | Arcade 24 kHz | Medium-res arcade, e.g. Sega Model 2 at 496×384 progressive. |

**Auto** guesses from the frame size: 256 wide → SNES, 320×224 → Mega Drive, 320×240 and other 240-line
modes → PlayStation, 320×200 or 640×400 → VGA (NTSC Amiga on the 1084S preset), 256 or 512 lines and PUAE's default
frames (360, 720 or 1440 wide with 287, 574, 242 or 484 lines) → Amiga, 336–400 lines → arcade 24 kHz,
more than 300 lines → interlaced, more than 576 lines → upscaled (see Internal resolution). Set the id by hand when the guess is wrong (for example a Mega Drive game
in 240-line PAL mode).

## TV show

`tv-show-broadcast` (Cable 4) shows a game the way it looked on a TV programme about games in the
90s: the same living-room TV as `consumer-tv-composite`, but the game reaches it from a TV studio,
through the transmitter and a roof antenna. On TV games looked smoother, brighter and cleaner than
on the RF or composite cable at home. Most of that is the chain, which the preset simulates:

- **No gaps between the lines.** A console cannot be locked to the studio's clock, so its picture
  went through a frame synchronizer, which writes out a standard interlaced picture. Each 240p/288p
  frame becomes one field and the next one lands half a line lower, so every console line is drawn
  twice and the dark gaps between the lines are filled. At living-room distance the 576-line raster
  of a broadcast is near the limit of the eye, while the 288-line one of a console at home is not.
  This is the biggest difference. With **No flicker** off, a thin horizontal line jumps up and down
  by half a line at 25 Hz, as on the real TV.
- **A clean source.** The preset assumes the studio took the console's RGB, as professional video
  did, instead of the RF or composite cable most homes used (how each programme connected its
  consoles is not documented), so there is no console encoder in the chain. The studio's
  Betacam SP tape (**TV show: tape copies**, Sony BVW-75P: luma 5.5 MHz, colour 1.5 MHz at -1.5 dB)
  and the station's PAL encoder (U and V 1.3 MHz) soften the picture a little. The broadcast is
  composite too, so dithering still melts into smooth shades and fine stripes still show a little
  rainbow, about as much as on the composite cable at home.
- **The transmitter.** A PAL B/G transmitter runs out of carrier a little above peak white (about
  114-120% of the picture signal), so the brightest saturated yellows and cyans (up to 133% in PAL)
  could not go out as they were. How stations handled it is not documented; the preset clips them
  softly a little below that, at 110%, so they come out slightly darker and paler. 75% colours pass untouched.
- **The antenna.** A little snow (**RF noise**: 0.06 is good reception, about 47 dB weighted,
  between "perceptible" and "imperceptible" on the ITU five-grade scale) and a faint ghost
  (**TV show: antenna ghost**, 3% at 1.2 µs). Unlike a console's RF modulator, the broadcast has
  stable sync, so the lines do not jitter.
- **TV standard.** The preset is a European (PAL) programme. A game in the other standard (an NTSC
  game, 240 lines) went through the studio's standards converter, which scaled it to the PAL raster,
  so it fills the screen; a PAL game with 256 lines keeps its black borders. Set **TV standard** to
  0 for a programme in the game's own standard (an American programme for NTSC games).

The rest was not in the signal: the footage was picked from the best moments, often from
pre-release versions or pre-rendered trailers, and seeing a game move at all was rare.

The preset has one pass more than the others (`show-lines`): it works out the beam of every line of
the programme once, so the CRT pass draws them as fast as any 480i or 576i picture. Cable 4 also works
in the other presets, but slower, because there the standards converter is worked out at every pixel.

## PC monitor

On 31 kHz modes (the `pc-monitor-vga` preset, or Console timing 7 in any preset) the tube is described in
millimetres, like a real monitor: viewable size, dot pitch, and the size of the beam spot on dark and on white.
Every video mode follows from the same tube, so 320×200, 640×480, 800×600 and 1024×768 each look the way they
did on that monitor.

The `pc-monitor-vga` preset is a typical mid-90s 15-inch monitor: 14-inch viewable picture, 0.28 mm dot mask, 85 MHz video
amplifier, a beam spot of 0.30 mm on dark and 0.45 mm on white, 1.4 times larger in the corners, a nearly flat
glass, and the picture filling it up to a thin black border, as owners set it with the monitor's controls. Monitors were
built with a spot about 1.2–1.3 times the pixel at their sharpest resolution, and an average 15-inch monitor was sharp at
800×600 and soft at 1024×768; that puts the spot at about 0.45 mm. Better monitors had smaller spots, see
[Better PC monitors](#better-pc-monitors).

What that gives (measured from the shader, centre of the screen, the picture size of the preset):

| Mode | Pixel width | 1-pixel stripes, white on black | Line structure, 30% grey / white |
|---|---|---|---|
| 320×200 (drawn as 400 lines) | 0.87 mm | 95% contrast | 46% / 7% |
| 640×480 | 0.44 mm | 49% | 24% / 2% |
| 800×600 | 0.35 mm | 28% | 7% / 0% |
| 1024×768 | 0.27 mm | 11% | 1% / 0% |

So 320×200 shows chunky pixels with fine lines that you see up close and in dark areas, 640×480 is sharp
with 1-pixel dots still visible, and 1024×768 is soft, as it was on an average 15-inch tube (for a sharper one use the presets below, or
set Monitor size to 17 for a 17-inch). The dot mask is too fine to show at 1080p, as it was on the real monitor at normal viewing
distance; at 4K it shows as a fine texture.

### Better PC monitors

The `pc-monitor-vga` preset is an average 15-inch monitor. Better ones had a finer mask and a smaller,
better focused beam, and on them 1024×768 was sharp. The same 15-inch tube in four more steps:

| Preset | Dot pitch | Spot dark / white | Corners | Like |
|---|---|---|---|---|
| `pc-monitor-vga` | 0.28 mm | 0.30 / 0.45 mm | 1.4× | an average 15-inch monitor |
| `pc-monitor-vga-good` | 0.25 mm | 0.25 / 0.35 mm | 1.4× | a good mid-range 15-inch monitor |
| `pc-monitor-vga-premium` | 0.25 mm | 0.20 / 0.28 mm | 1.3× | a premium 15-inch monitor |
| `pc-monitor-vga-premium-plus` | 0.24 mm | 0.17 / 0.24 mm | 1.25× | the best 15-inch monitors; misconvergence 0.08 |
| `pc-monitor-vga-extreme` | 0.22 mm | 0.13 / 0.18 mm | 1.15× | no real 15-inch monitor; misconvergence 0.05, glass glow 0.02 |

`extreme` goes past the real thing on purpose: the beam is narrower than the line pitch at 1024×768, so the
picture stays crisp and calm for long work on the desktop, with gaps between the lines no real monitor had in
that mode. For a Trinitron or Diamondtron, set **Mask** to 1 (aperture grille).

For the stand-alone PC emulator 86Box, the repository has these presets converted to GLSL (`86box/`).

## Amiga 1084S

The `amiga-1084s` preset is built from the Commodore 1084S-P1 service manual and photos of a real 1084S:

- 14-inch Philips M34EAQ10X tube, 0.42 mm slot mask, 6.5 MHz video amplifier.
- White set to "D" (D65, 6500 K), European phosphors (EBU) whatever the Amiga sends, PAL or NTSC.
- Picture size as owners set the monitor up: width and height turned up until a 320×256 screen nearly fills
  the glass, with about 5 mm of black left around it (Overscan 0.1). At the factory setting (Overscan −0.015)
  the whole active picture fills the 13-inch face, and a 320-pixel line, 45 of the 52 µs, leaves wide black
  borders on all sides.
- The tube is described in mm (beam spot 0.28 mm on dark, 0.65 mm on white), so NTSC games, with 200 lines
  further apart, show stronger lines than PAL games with 256, as on the real monitor. The spot was calibrated
  on a photo of a real monitor: the line contrast inside mid-bright text (39% in the photo) is matched.
- Frames with the overscan border (PUAE's default, 720×287) show that border at its real size around the
  picture. Glow and misconvergence are measured in lores pixels, so hires and lores frames look the same.

For the stand-alone emulator Amiberry, the repository has this preset converted to GLSL (`amiberry/`).

## HDR

A real tube concentrated its light in the phosphor dots and the centre of each scanline, with dark gaps
around them. On an SDR display the shader has to squeeze those bright dots under the white level, so the
picture loses brightness and contrast. On an HDR display the presets in `hdr/curved` and `hdr/flat` give the
dots the extra brightness instead, and the average stays at the paper white level.

1. Video driver **Vulkan, D3D11 or D3D12** (**Settings → Drivers → Video**, then restart RetroArch).
   On a Mac use Vulkan; the Metal driver has HDR only in nightly builds so far.
2. Turn HDR on for the display in the system: on Windows **Settings → System → Display → Use HDR**,
   on macOS **System Settings → Displays → High Dynamic Range**.
3. In RetroArch: **Settings → Video → HDR → Enable HDR**.
4. Load a preset from `lola-retro/hdr/curved` or `lola-retro/hdr/flat`.
5. In **Quick Menu → Shaders → Shader Parameters**, at the bottom:
   - **HDR: display peak (nits)**: the peak brightness of your display. About 400 for DisplayHDR 400
     monitors, 600–1000 for OLED TVs.
   - **HDR: paper white (nits)**: how bright a white screen should be. 200 is a good start; raise it for a
     brighter picture.

The shader writes HDR10 itself, so RetroArch's own HDR settings (paper white, peak, contrast) do not change
these presets. Use them only with HDR switched on, otherwise the picture looks grey and flat. The normal
presets keep working with HDR on as well (RetroArch converts them), they just don't use the extra brightness.

With a bright display (peak about 4× paper white or more) you can raise **Mask strength** towards 1.0:
the gaps between the phosphors go dark like on a real tube and the picture stays as bright.

## Performance

The chain has eight passes (`tv-show-broadcast` nine) and the encoder and decoder do real signal
filtering, so it needs a reasonably modern GPU. Integrated graphics from the last few years should be fine at 1080p.
The first pass (`native`) averages the rows of each console line of an upscaled frame once, and in the TV
presets the encoder and decoder work at a fixed 1280 samples per line (about 27 MHz, 4 per pixel of a
320-wide console), so a PS1 core at 4× or 8× internal resolution costs about as much as at 1×. (Before,
the encoder's work grew with the square of the internal resolution: 16 times as much at 4×.) The PC and
Amiga presets keep 4 samples per frame pixel, because their beam reads every pixel exactly.
The beam pass works out each source line once on an 8192-pixel-wide texture, so the GPU has to support
textures that wide (every desktop GPU of the last decade does).

## Tested

Rendered in RetroArch 1.18 with the glcore and Vulkan drivers, and reported working with Vulkan and glcore on
Windows (NVIDIA). Direct3D 10/11/12 failed in 1.0 and 1.1; 1.2 fixes the cause (every pass now samples
without derivatives, which the Direct3D compiler requires inside loops). Metal has not been confirmed yet.
The HDR output has been checked numerically (the HDR10 signal decodes back to the intended brightness)
and confirmed on macOS with the Vulkan driver and a DisplayHDR 400 monitor. Reports from other HDR
displays are welcome. The Amiberry and 86Box versions are in use in Amiberry and 86Box on macOS.
The PC monitor model was checked against its physics (spot width, stripe contrast, line structure and brightness
agree within 2% in every mode) and against photos of real VGA monitors; the 1084S against photos of a real 1084S.
Reports are welcome.

## License

MIT, see the LICENSE file in this folder.
