#!/bin/bash
# capture-bars.sh <ps-exe> <outdir> <bar:frac,...> [seconds]
# Runs the demo headless in Redux and saves a PNG when the song first reaches each bar:frac.
# frac is 0..255 inside the bar. Needs xvfb-run. Env: REDUX (binary), BIOS.
set -u
EXE=$(readlink -f "$1"); OUT=$2; BARS=$3; SECS=${4:-260}
REDUX=${REDUX:?set REDUX to a pcsx-redux binary}
BIOS=${BIOS:?set BIOS to a BIOS image}
HERE=$(cd "$(dirname "$0")" && pwd)
ELF=${EXE%.ps-exe}.elf
ADDR=$(nm "$ELF" | awk '$3=="demo_dbg" {print $1}')
test -n "$ADDR" || { echo "demo_dbg not found in $ELF"; exit 2; }
mkdir -p "$OUT"; rm -f "$OUT"/*.raw "$OUT"/*.png "$OUT/log.txt"
LAST=$(echo "$BARS" | tr ',' '\n' | sed 's/:.*//' | sort -n | tail -1)
CAP_ADDR=$ADDR CAP_OUT=$OUT CAP_BARS=$BARS CAP_QUIT_BAR=$((LAST + 2)) LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  timeout -s KILL "$SECS" xvfb-run -a -s "-screen 0 1280x800x24" "$REDUX" -testmode -stdout -lua_stdout -safe \
  -bios "$BIOS" -run -loadexe "$EXE" -dofile "$HERE/capture.lua" > "$OUT/log.txt" 2>&1
echo "redux rc=$?"
python3 -I "$HERE/raw2png.py" "$OUT/log.txt" "$OUT"
grep -cE 'Loaded BIOS' "$OUT/log.txt"
