#!/bin/sh
# Builds a bootable CD image (bin/cue) from a PS-EXE with PCSX-Redux's
# ISO builder (scripts/make-disc.lua).
#   scripts/make-disc.sh forty-one.ps-exe out/
# The licence sectors are generated in the script (our own text and logo,
# no Sony data). PCSX_REDUX points at the emulator binary.
set -e
EXE=$1
OUT=$2
PCSX_REDUX=${PCSX_REDUX:-pcsx-redux}
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cp "$EXE" "$OUT/PSX.EXE"
printf 'BOOT = cdrom:\\PSX.EXE;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n' > "$OUT/SYSTEM.CNF"
DISC_EXE="$OUT/PSX.EXE" DISC_CNF="$OUT/SYSTEM.CNF" DISC_OUT="$OUT/forty-one-cycles.bin" \
    "$PCSX_REDUX" -cli -dofile "$HERE/scripts/make-disc.lua"
printf 'FILE "forty-one-cycles.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' > "$OUT/forty-one-cycles.cue"
