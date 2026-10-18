#!/bin/bash
# Regenerate the cued song: compose.py -> MML -> MIDI -> checks -> PSM + VAB.
# MIDI2PSM: a midi2psm built from pcsx-redux main (>= 2026-10-09, has USER events).
set -eu
cd "$(dirname "$0")"
: "${MIDI2PSM:=midi2psm}"
python3 -I compose.py
python3 -I mml2mid.py megademo-cued.mml megademo-cued.mid
python3 -I verify_audio.py megademo-base.mid megademo-cued.mid
$MIDI2PSM megademo-cued.mid -s ../assets/megademo-gm.sf2 -o ../assets/forty-one.psm -b ../assets/forty-one.vab
