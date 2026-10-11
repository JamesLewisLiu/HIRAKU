#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Import dumps/<game>/program.bin into a Ghidra project under build/ and export
# build/<game>/manifest.json.
#
#   analyze/ghidra.sh bm3final
#
# GHIDRA_HEADLESS overrides the analyzeHeadless path.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
game="${1:?usage: analyze/ghidra.sh <game id>}"
headless="${GHIDRA_HEADLESS:-analyzeHeadless}"   # Ghidra's support/analyzeHeadless

prog="$root/dumps/$game/program.bin"
toml="$root/games/$game/game.toml"
[[ -f "$prog" ]] || { echo "missing $prog - run tools/ingest.py first" >&2; exit 1; }
entry="$(python3 -c "import tomllib,sys; print(hex(tomllib.load(open(sys.argv[1],'rb'))['program']['entry']))" "$toml")"
# [recomp] extra_entries = [0x..., ...] in game.toml: functions Ghidra cannot find
extra="$(python3 -c "import tomllib,sys; print(' '.join(hex(x) for x in tomllib.load(open(sys.argv[1],'rb')).get('recomp',{}).get('extra_entries',[])))" "$toml")"

out="$root/build/$game"
mkdir -p "$out/ghidra"
"$headless" "$out/ghidra" "$game" \
    -import "$prog" -overwrite \
    -processor "PowerPC:BE:32:4xx" -loader BinaryLoader -loader-baseAddr 0x80000000 \
    -scriptPath "$here" -postScript ExportManifest.java "$out/manifest.json" "$entry" $extra \
    > "$out/ghidra.log" 2>&1 || { tail -40 "$out/ghidra.log" >&2; exit 1; }
grep -h 'manifest:' "$out/ghidra.log" | sed 's/.*> //; s/ (GhidraScript).*//'
