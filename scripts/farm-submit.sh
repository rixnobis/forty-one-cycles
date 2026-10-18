#!/bin/bash
# farm-submit.sh <ps-exe> <device> [seconds]
# Submits the demo to ONE console of a hardware test farm and
# prints the ticket id. Needs HWTEST_TOKEN and HWTEST_SERVER in the environment, and
# HWTEST_CLI pointing at the farm CLI (node .../packages/cli/dist/index.js).
# The demo prints BAR/SECTION lines on the serial port; a marker-less run ends in TIMEOUT,
# which is the expected outcome: read the ticket's serial.log, not the verdict.
set -eu
EXE=$(readlink -f "$1"); DEV=${2:?device id (see the farm CLI devices command)}; SECS=${3:-45}
: "${HWTEST_CLI:?set HWTEST_CLI to 'node <farm>/packages/cli/dist/index.js'}"
$HWTEST_CLI devices | grep -E "^$DEV +AVAILABLE" > /dev/null || { echo "$DEV is not AVAILABLE"; exit 2; }
SHA=$(sha256sum "$EXE" | cut -c1-16)
$HWTEST_CLI submit --exe "$EXE" --pool ps1 --device "$DEV" --run-seconds "$SECS" \
  --idempotency-key "forty-one-$SHA-$DEV-$(date +%s)" --json
