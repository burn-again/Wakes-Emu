#!/bin/sh
# Re-fit the CPU meter (emulator/cpu.json) to Wakes' hardware measurements.
#
# The table in docs/PLAITS-ENGINES.md was measured on the M4a-M4d build, before #32 moved
# Plaits to 24-sample blocks and the audio to 2 ms blocks. Per-block setup work differs
# between the two, so both steps run on an emulator built the way that session's firmware
# was (Plaits block 12, 240-frame audio blocks). The model they fit is a cost per kind of
# work, not per configuration; the default build then counts with it.
#
#   1. a profiling build counts each kind of work; the fit gives a weight per kind
#   2. a build counting with those weights is checked against the table, on Wakes' own
#      AUD line, and the result goes in cpu.json for the page
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
CAL=$ROOT/build/emu-cal
M4="SP1_PLAITS_BLOCK=12 SP1_AUDIO_BLOCK_FRAMES=240"
CONFIG="Plaits block 12, 240-frame audio blocks (the M4a-M4d firmware's configuration)"

EMU_BUILD_DIR=$CAL EMU_WASM_OUT=$CAL/wakes-m4-profile.wasm EMU_CPU_PROFILE=1 EMU_KCONFIG="$M4" \
	sh "$HERE/build.sh" web | grep -E '^\[web\] cycles' || true
node "$HERE/tools/calibrate_cpu.mjs" --profile "$CAL/wakes-m4-profile.wasm" --config "$CONFIG"

EMU_BUILD_DIR=$CAL EMU_WASM_OUT=$CAL/wakes-m4.wasm EMU_KCONFIG="$M4" \
	sh "$HERE/build.sh" web | grep -E '^\[web\] cycles' || true
node "$HERE/tools/calibrate_cpu.mjs" --check "$CAL/wakes-m4.wasm" --config "$CONFIG"
echo "now rebuild the default: sh emulator/build.sh web"
