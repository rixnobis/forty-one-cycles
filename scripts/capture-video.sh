#!/bin/bash
# capture-video.sh <ps-exe> <out.mp4> [last-bar] [every]
# Video: Redux under xvfb, one frame per EVERY vsyncs (default 2 = 30 fps in emulated time;
# xvfb Redux runs faster than real time, so audio cannot come from this run).
# Audio: a second, real-time run (-no-ui, SDL disk driver, as in the song render scripts),
# trimmed to its first audible sample, which is music tick 1 = the first video frame.
# Env: REDUX, BIOS. Prints both durations so drift is visible.
set -u
EXE=$(readlink -f "$1"); MP4=$(readlink -f -m "$2"); LASTBAR=${3:-121}; EVERY=${4:-2}
REDUX=${REDUX:?set REDUX to a pcsx-redux binary}
BIOS=${BIOS:?set BIOS to a BIOS image}
HERE=$(cd "$(dirname "$0")" && pwd)
W=${MP4%.mp4}.work; mkdir -p "$W"; rm -f "$W"/*
ADDR=$(nm "${EXE%.ps-exe}.elf" | awk '$3=="demo_dbg" {print $1}')
CAP_ADDR=$ADDR CAP_OUT=$W CAP_VIDEO=$W/video.raw CAP_EVERY=$EVERY CAP_QUIT_BAR=$LASTBAR \
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  timeout -s KILL 600 xvfb-run -a -s "-screen 0 1280x800x24" stdbuf -oL "$REDUX" -testmode -stdout -lua_stdout -safe \
  -bios "$BIOS" -run -loadexe "$EXE" -dofile "$HERE/capture.lua" > "$W/log.txt" 2>&1
echo "video run rc=$?"
grep -E '^(VIDEO|VFRAMES)' "$W/log.txt"
SECS=$(python3 -c "print(int(($LASTBAR-1)*12/7+12))")
cd "$(dirname "$REDUX")"
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$W/audio.f32 SDL_AUDIO_DEVICE_SAMPLE_FRAMES=441 \
  timeout -s INT "$SECS" stdbuf -oL nice "$REDUX" -no-ui -run -bios "$BIOS" -loadexe "$EXE" -stdout > "$W/audio-run.log" 2>&1
echo "audio run rc=$?"
python3 -I - "$W" "$LASTBAR" <<'PY'
import sys, numpy as np
w, lastbar = sys.argv[1], int(sys.argv[2])
x = np.fromfile(w + '/audio.f32', np.float32).reshape(-1, 2)
on = int(np.argmax(np.abs(x).max(1) > 1e-3))
y = x[on:]
np.clip(np.round(y * 32768), -32768, 32767).astype('<i2').tofile(w + '/audio.s16')
print(f'audio: {len(x)/44100:.2f}s raw, onset {on/44100:.3f}s, kept {len(y)/44100:.2f}s, peak {abs(y).max():.3f}, clipped {int((abs(y)>=1).sum())}')
print(f'music to bar {lastbar} nominal {(lastbar-1)*12/7:.2f}s')
PY
FPS=$(python3 -c "print(60/$EVERY)")
ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgr555le -s 320x240 -r "$FPS" -i "$W/video.raw" \
  -f s16le -ar 44100 -ac 2 -i "$W/audio.s16" -vf scale=640:480:flags=neighbor -c:v libx264 -pix_fmt yuv420p -crf 18 \
  -c:a aac -b:a 160k -shortest "$MP4"
echo "mp4: $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$MP4")s"
