#!/bin/sh
# Builds a bootable CD image (bin/cue) from a PS-EXE with mkpsxiso.
#   scripts/make-disc.sh forty-one.ps-exe out/
# The licence sectors come from tools/gen_licence.py (our own text and
# logo, no Sony data).
set -e
EXE=$1
OUT=$2
MKPSXISO=${MKPSXISO:-mkpsxiso}
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cp "$EXE" "$OUT/PSX.EXE"
printf 'BOOT = cdrom:\\PSX.EXE;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n' > "$OUT/SYSTEM.CNF"
python3 -I "$HERE/tools/gen_licence.py" "$OUT/licence.dat"
cat > "$OUT/disc.xml" <<XML
<?xml version="1.0" encoding="UTF-8"?>
<iso_project image_name="forty-one-cycles.bin" cue_sheet="forty-one-cycles.cue">
  <track type="data">
    <identifiers system="PLAYSTATION" application="PLAYSTATION" volume="FORTY_ONE_CYCLES"
                 volume_set="FORTY_ONE_CYCLES" publisher="RIX" data_preparer="MKPSXISO"/>
    <license file="licence.dat"/>
    <directory_tree>
      <file name="SYSTEM.CNF" type="data" source="SYSTEM.CNF"/>
      <file name="PSX.EXE" type="data" source="PSX.EXE"/>
      <dummy sectors="1024"/>
    </directory_tree>
  </track>
</iso_project>
XML
cd "$OUT" && "$MKPSXISO" -y disc.xml
