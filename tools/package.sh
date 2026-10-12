#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A copy of a build for another Linux PC (a test laptop):
#
#   tools/package.sh BUILD_DIR OUT.tar.xz
#
# BUILD_DIR should be configured for it:
#   cmake -S . -B BUILD_DIR -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
#     '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc' '-DCMAKE_BUILD_RPATH=$ORIGIN/lib'
# (CMAKE_BUILD_RPATH, not -Wl,-rpath: ninja hands the flags to a shell, which eats $ORIGIN)
# (no FluidSynth, the C++ runtime linked in, SDL3 looked for beside the binaries).
# For the System 573 games ~30 % faster, add -DHK_573_PGO_PROFILE=<this PC's build/pgo/573.profdata>
# (made by tools/pgo.py).
#
# The archive holds hiraku/bin (the binaries, stripped, with libSDL3 in bin/lib),
# hiraku/games, hiraku/docs and an empty hiraku/dumps: the binaries take the folder
# above them as their root. Game data goes in with tools/pack-data.py.
set -euo pipefail
build=${1:?build dir}
out=${2:?output .tar.xz}
root=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
d=$stage/hiraku
mkdir -p "$d/bin/lib" "$d/dumps"
for f in "$build"/hiraku "$build"/hiraku-*; do
  [ -x "$f" ] && [ -f "$f" ] || continue
  case $(basename "$f") in hiraku-recomp) continue ;; esac
  strip -o "$d/bin/$(basename "$f")" "$f"
done
sdl=$(ldd "$build"/hiraku-bm1stmix | awk '/libSDL3/ {print $3}')
[ -n "$sdl" ] && cp -L "$sdl" "$d/bin/lib/"
cp -r "$root/games" "$root/docs" "$root/shaders" "$d/"
cp -r "$root/oracle/scripts" "$d/scripts"
mkdir -p "$d/tools"
cp "$root"/tools/{ingest.py,chd.py,fbexe.py,pack-data.py,regress.py,unlock.py} "$d/tools/"
cp "$root/LICENSE" "$d/" 2>/dev/null || true
cat > "$d/README.txt" <<'EOF'
HIRAKU, a build for another Linux PC.

Needs glibc 2.38 or later (Ubuntu 24.04, Fedora 39, Debian 13, Arch and newer).
SDL3 is included. The launcher (bin/hiraku) needs Qt 6.12 or later from the system;
the games run without it.

Game data, either way:
- ingest here, as on the main PC (Python 3.11+, 7z, and chdman for CHDs):
      tools/ingest.py --help
- or copy it from a PC that has it ingested:
      tools/pack-data.py /path/to/this/hiraku GAME...
  (dumps/GAME/ and its disc images, with relative paths in machine.txt). Saved settings and NVRAM live in
~/.local/share/hiraku/GAME (copy them over for games that need a first-boot setup).

Run a game:
    bin/hiraku-popn8 --window          (--window 3 for 3x, --fullscreen)
    bin/hiraku-popn8 --window --picture light   (a CRT: light or full; F10 cycles off/light/full)
    bin/hiraku-popn8 --help            (all options)
Each run prints its frame times when it ends ("frametime:": host work per frame,
mean, percentiles and the worst frame). A repeatable test, no window:
    bin/hiraku-bm1stmix --frames 12600 --script scripts/bm1stmix-play.his
    bin/hiraku-popn8 --frames 7200
A run without a window starts from blank NVRAM unless given --nvram DIR (kbm, ppp
and pop'n 6 need theirs: --nvram ~/.local/share/hiraku/kbm).

Smooth frames (display sync, on by default): on a 60, 120 or 240 Hz display every frame
stays up the same number of refreshes, and pop'n runs 2.6 % faster to match (docs/PLAY.md,
"Frame pacing"). The console says what it chose ("display sync: on: 60.00 Hz display, ...");
the exit report's "video: display sync" line says how it went ("held their N refreshes",
"irregular"). --display-sync off: the exact speed, for a variable-refresh display.
EOF
# xz with a window wider than one binary: the binaries share most of their code,
# so the archive is a quarter of a .tar.gz (about 14 MB)
tar -C "$stage" -cf - hiraku | xz -9e -T1 --lzma2=preset=9e,dict=192MiB > "$out"
ls -la "$out"
