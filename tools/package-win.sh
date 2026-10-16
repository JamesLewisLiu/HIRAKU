#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A Windows build as a zip (PLAN.md section 26), as tools/package.sh does for Linux:
#
#   tools/package-win.sh BUILD_DIR OUT.zip [portable]
#
# BUILD_DIR: a Windows build (cmake/x86_64-w64-mingw32.cmake). portable: the package has a folder
# hiraku/user, so its settings and saves stay in that folder (runtime/shell/userdir.h); without
# one they go in %LOCALAPPDATA%\hiraku.
#
# The zip holds hiraku/bin (the games and the launcher hiraku.exe, stripped, with SDL3.dll: the
# launcher's Qt is linked into it, tools/win-deps.sh), hiraku/games, hiraku/docs and an empty
# hiraku/dumps. The binaries take the folder above them
# as their root. Game data goes in with tools/pack-data.py, or tools/ingest.py on that PC.
set -euo pipefail
build=${1:?build dir}
out=${2:?output .zip}
mode=${3:-}
case $mode in ""|portable) ;; *) echo "the third argument is 'portable' or nothing" >&2; exit 2 ;; esac
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(realpath -m "$out")
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
d=$stage/hiraku
mkdir -p "$d/bin" "$d/dumps"
[ "$mode" = portable ] && mkdir -p "$d/user/config" "$d/user/data"
strip=$(command -v llvm-strip || command -v x86_64-w64-mingw32-strip)
for f in "$build"/hiraku*.exe; do
  [ -f "$f" ] || continue
  case $(basename "$f") in hiraku-recomp.exe) continue ;; esac
  "$strip" -o "$d/bin/$(basename "$f")" "$f"
done
cp "$build"/SDL3.dll "$d/bin/"
# Python for tools/ingest.py and unlock.py: python.org's embeddable one (tools/win-deps.sh), so
# adding a game needs nothing installed (the launcher looks for hiraku\python\python.exe first)
py=$root/out/win-deps/python
if [ -f "$py/python.exe" ]; then cp -r "$py" "$d/python"; else echo "no $py: the package will need Python installed" >&2; fi
cp -r "$root/games" "$root/docs" "$root/shaders" "$d/"
cp -r "$root/oracle/scripts" "$d/scripts"
mkdir -p "$d/tools"
cp "$root"/tools/{ingest.py,chd.py,fbexe.py,pack-data.py,unlock.py,chdread.py,mameset.py,catalogue.py} "$d/tools/"
cp -r "$root/tools/data" "$d/tools/"
cp "$root/LICENSE" "$d/" 2>/dev/null || true
for f in $(grep -o '"third_party/[^"]*"' "$root/launcher/aboutdialog.cpp" | tr -d '"'); do
  mkdir -p "$d/$(dirname "$f")" && cp "$root/$f" "$d/$f"
done
sed 's/$/\r/' > "$d/README.txt" <<'EOF'
HIRAKU for Windows (64-bit Windows 10 or later).

The games are bin\hiraku-<game>.exe; bin\hiraku.exe is the launcher, when this package has one.
SDL3 (and the launcher's Qt) are included. Not yet in the Windows build: the Konami Python
games (pop'n music 9-14, GuitarFreaks and DrumMania V), cardio card readers, and Twinkle's
background video.

Game data, either way:
- drop the MAME ROM set (.zip, or .7z with 7-Zip installed) and its CHDs on the launcher's
  window, or use File > Add game files. Everything needed comes in this package (its own
  Python in python\, bin\hiraku-chdcat.exe for the CHDs);
- or copy it from a PC that has it ingested: tools/pack-data.py there.

Saves and settings go in %LOCALAPPDATA%\hiraku. A portable install: with a folder named user in
this folder (tools/package-win.sh ... portable makes one), they go in user\data and user\config
instead, and this folder runs as itself wherever it is moved.

Run a game from a command prompt in this folder:
    bin\hiraku-popn8 --window          (--window 3 for 3x, --fullscreen)
    bin\hiraku-popn8 --help            (all options)
A repeatable test, no window:
    bin\hiraku-bm1stmix --frames 12600 --script scripts/bm1stmix-play.his
EOF
rm -f "$out"
(cd "$stage" && 7z a -tzip -mx=9 -bd -bso0 "$out" hiraku)
ls -la "$out"
