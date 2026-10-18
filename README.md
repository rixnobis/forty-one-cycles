# FORTY-ONE CYCLES

A PlayStation demo. Code and music by Rix. Built with psyqo and nugget; runs on hardware.

## Build

    git submodule update --init nugget
    make -j8                  # forty-one.ps-exe; needs mipsel-none-elf-g++
    make DEBUG_SKIP=65        # start at bar 65

CD image: `scripts/make-disc.sh forty-one.ps-exe out/` (runs PCSX-Redux's ISO builder; set `PCSX_REDUX` to the binary). The licence sectors come from
`tools/gen_licence.py`: our own text and logo, no Sony data, so the logo screen reads FORTY-ONE CYCLES.

Overlay off: `make EXTRA=-DNO_OVERLAY`. Release (no overlay, no serial trace, so it runs with nothing reading the serial port): `make EXTRA="-DNO_OVERLAY -DNO_LOG"`. Note mirror mask override: `make MIRROR_MASK=0x...`
(bit n = MIDI channel n, 0-based).

## Music

`tools/build-music.sh` regenerates `assets/forty-one.{psm,vab}` from `tools/compose.py`.
The score carries cue points (tag 1 = bar number, tag 2 = section index); `tools/verify_audio.py`
checks that the cued MIDI plays the same as `tools/megademo-base.mid`. The bank is
`assets/megademo-gm.sf2`, a General MIDI subset (FluidR3 GM, MIT).

## Capture

    scripts/capture-bars.sh forty-one.ps-exe out/ 9:20,17:20,33:20   # PNGs at bar:position
    scripts/capture-video.sh forty-one.ps-exe out/demo.mp4 [last-bar]

Needs a PCSX-Redux binary (`REDUX=`), a BIOS (`BIOS=`), xvfb, ffmpeg, python3 with numpy and Pillow.

## Layout

`src/demo.hh` is the contract between the framework (`src/main.cpp`) and the sections
(`src/sNN_*.cpp`, one per section).
