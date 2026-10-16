#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A game run in MAME and in HIRAKU on the same input script, compared frame by frame.
#
#   oracle/lockstep.sh GAME SCRIPT [RUN] [-- HIRAKU ARGS...]
#
# MAME runs first (oracle/mame.sh: the 573 and Twinkle cores use the builds at HIRAKU's pin,
# MAME_573 and MAME_TWINKLE, by default mame-pin/k573 and mame-pin/twinkle beside the HIRAKU
# folder; the others the flatpak); HIRAKU then starts from MAME's power-on clock (--rtc, read from MAME's log)
# and runs to the script's quit frame. Prints oracle/snapcmp.py's verdict.
#
#   LOCKSTEP_NVRAM=DIR   both start from a copy of this NVRAM directory (MAME's file names), e.g.
#                        what HIRAKU's run of the game's first boot left
#   LOCKSTEP_NVRAM_OUT=DIR  keep HIRAKU's NVRAM at the end in DIR (for the next step's start)
#   HIRAKU_ROMS          MAME's rompath (default out/oracle-roms: the owner's sets, current names)
#   MAME_ARGS            more MAME options (e.g. "-cassette install"); pass HIRAKU's own after --
#
# Twinkle games get --mame-cd (MAME's CD drive timing) and --video off, as regress runs them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
cd "$root"
game="${1:?usage: oracle/lockstep.sh GAME SCRIPT [RUN] [-- HIRAKU ARGS]}"
script="${2:?usage: oracle/lockstep.sh GAME SCRIPT [RUN] [-- HIRAKU ARGS]}"
shift 2
run="lockstep"
if [[ $# -gt 0 && "$1" != "--" ]]; then run="$1"; shift; fi
[[ "${1:-}" == "--" ]] && shift
toml="games/$game/game.toml"
core="$(python3 -c "import tomllib,sys; print(tomllib.load(open(sys.argv[1],'rb')).get('core',''))" "$toml")"
case "$core" in
    sys573) bin="${MAME_573:-$root/../mame-pin/k573}"; extra=() ;;
    twinkle) bin="${MAME_TWINKLE:-$root/../mame-pin/twinkle}"; extra=(--mame-cd --video off) ;;
    *) bin=""; extra=() ;;
esac
export HIRAKU_ROMS="${HIRAKU_ROMS:-out/oracle-roms}"
quit="$(awk '$1=="at" && $3=="quit" {print $2}' "$script" | tail -1)"
[[ -n "$quit" ]] || { echo "$script: no 'at N quit'" >&2; exit 1; }

m="out/oracle/$game/$run"
h="out/oracle-h/$game-$run"
nv="out/oracle-h/$game-$run-nvram"
rm -rf "$h" "$nv"
mkdir -p "$h" "$nv"
mame_nv=""
if [[ -n "${LOCKSTEP_NVRAM:-}" ]]; then
    mame_nv="out/oracle-h/$game-$run-nvram-mame"
    rm -rf "$mame_nv"; cp -r "$LOCKSTEP_NVRAM" "$mame_nv"
    cp -r "$LOCKSTEP_NVRAM"/. "$nv"/
fi

HIRAKU_NVRAM="${mame_nv:+$root/$mame_nv}" MAME_BIN="$bin" HIRAKU_TAPS="${HIRAKU_TAPS:-none}" nice oracle/mame.sh "$game" "$script" "$run" > "$h.mame.txt" 2>&1 || true
rtc="$(grep -o 'power-on time [0-9]*' "$m/log.txt" 2>/dev/null | head -1 | awk '{print $NF}' || true)"
args=(--frames $((quit + 1)) --out "$h" --script "$script" --nvram "$nv" "${extra[@]}")
[[ -n "$rtc" ]] && args+=(--rtc "$rtc")
systemd-run --user --scope -q -p MemoryMax=3G nice "build/cmake/hiraku-$game" "${args[@]}" "$@" > "$h.log" 2>&1 || true
echo "=== $game $run ($(grep -h '^frames' "$h.log" | cut -c1-80))"
python3 oracle/snapcmp.py "$h" "$m" 2>&1 | grep -E "^hash|^snapshots|Error|error" || true
if [[ -n "${LOCKSTEP_NVRAM_OUT:-}" ]]; then rm -rf "$LOCKSTEP_NVRAM_OUT"; cp -r "$nv" "$LOCKSTEP_NVRAM_OUT"; fi
