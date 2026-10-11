#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run a Firebeat game in MAME (flatpak org.mamedev.MAME) under the HIRAKU
# oracle: a deterministic input script in, an MMIO/frame trace out.
#
#   oracle/mame.sh <game> <script.his> [run name]
#
# Output goes to out/oracle/<game>/<run name>/ (default run name = script
# basename). Every run starts from a FRESH nvram directory, so the machine
# boots from the ROM set's default RTC/NVRAM image and runs are repeatable.
#
#   HIRAKU_ROMS   rompath holding <set>.zip and <set>/*.chd   (default ~/bm3mame/roms)
#   HIRAKU_TAPS   MMIO groups to log, comma list               (default all)
#   RUN_SECONDS   emulated-time cap, -seconds_to_run           (default 900)
#   WINDOW=1      show a window and play sound (otherwise headless, unthrottled)
#   WAV=1         also record the mixed audio to <run>/audio.wav
#   EXACT_PC=1    run under -debug -debugger none: the PPC recompiler then
#                 updates PC per instruction, so trace PCs are exact (slower)
#
# Converting dumps to CHD (once per game, chdman ships in the flatpak):
#   flatpak run --command=chdman org.mamedev.MAME createcd -i cd.cue -o <set>/<name>.chd
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
game="${1:?usage: oracle/mame.sh <game> <script.his> [run name]}"
script="$(realpath "${2:?usage: oracle/mame.sh <game> <script.his> [run name]}")"
name="${3:-$(basename "$script" .his)}"

toml="$root/games/$game/game.toml"
[[ -f "$toml" ]] || { echo "no $toml" >&2; exit 1; }
set_name="$(python3 -c "import tomllib,sys; print(tomllib.load(open(sys.argv[1],'rb'))['mame'])" "$toml")"

roms="$(realpath "${HIRAKU_ROMS:-$HOME/bm3mame/roms}")"
out="$root/out/oracle/$game/$name"
rm -rf "$out"
mkdir -p "$out/nvram" "$out/snap" "$out/cfg"

if ! flatpak info org.mamedev.MAME >/dev/null 2>&1; then
    echo "MAME is not installed:  flatpak install --user -y flathub org.mamedev.MAME" >&2
    exit 1
fi

video=(-video none -sound none -nothrottle)
[[ "${WINDOW:-0}" == 1 ]] && video=(-window -nomaximize)
debug=()
[[ "${WAV:-0}" == 1 ]] && debug+=(-wavwrite "$out/audio.wav")
[[ "${EXACT_PC:-0}" == 1 ]] && debug=(-debug -debugger none)

# The flatpak sees only what it is given: every path and every variable.
flatpak run \
    --filesystem="$roms":ro --filesystem="$root" --filesystem="$(dirname "$script")":ro \
    --env=HIRAKU_OUT="$out" --env=HIRAKU_SCRIPT="$script" --env=HIRAKU_TAPS="${HIRAKU_TAPS:-all}" \
    org.mamedev.MAME \
    -rompath "$roms" -homepath "$out" \
    -cfg_directory "$out/cfg" -nvram_directory "$out/nvram" \
    -snapshot_directory "$out/snap" -state_directory "$out/sta" -diff_directory "$out/diff" \
    -skip_gameinfo -noreadconfig "${video[@]}" "${debug[@]}" \
    -seconds_to_run "${RUN_SECONDS:-900}" \
    -autoboot_script "$here/lua/hiraku.lua" \
    "$set_name" > "$out/mame.log" 2>&1 || true

grep -E 'hiraku:|error|Error|fatal|not found' "$out/mame.log" | grep -v -E 'tap |f[0-9]+ (press|set|snap)' | tail -20
echo "-> ${out#$root/}"
