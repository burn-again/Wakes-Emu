#!/bin/sh
# Build the Wakes emulator from firmware/src, unchanged.
#
#   emulator/build.sh native   -> build/emu/native/wakes_headless (gcc/g++, for testing)
#   emulator/build.sh web      -> emulator/web/wakes.wasm         (clang wasm32-wasi + wasm-opt)
#   emulator/build.sh          -> both
#
# Generated headers come from the same tools/gen_*.py the firmware's CMake runs, the
# Plaits overrides from tools/host-tests/mkovr.py (which parses firmware/CMakeLists.txt),
# and the CONFIG_ values from firmware/Kconfig + prj.conf (emulator/tools/gen_config.py).
# Nothing is copied by hand, so the emulator follows the firmware it sits next to.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=${EMU_BUILD_DIR:-$ROOT/build/emu}
SRC=$ROOT/firmware/src
ER=$ROOT/third_party/eurorack
FELDD=$ROOT/third_party/feldd/src
GEN=$OUT/gen
OVR=$OUT/ovr
WHAT=${1:-all}
WASM_OUT=${EMU_WASM_OUT:-$HERE/web/wakes.wasm}     # tools/calibrate.sh builds a second one
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
# Kconfig overrides on top of prj.conf. Unset: the emulator's own, the experiments on --
# drum hits (docs/DRUMS.md), MIDI files (docs/MIDIFILE.md), the reverb and the arpeggiator
# (docs/HEADROOM.md).
# EMU_KCONFIG= (empty) builds
# exactly what prj.conf builds.
EMU_KCONFIG=${EMU_KCONFIG-SP1_DRUM=y SP1_MIDIFILE=y SP1_REVERB=y SP1_ARP=y}

mkdir -p "$GEN" "$OVR"

# ---- generated headers, as firmware/CMakeLists.txt makes them ----
python3 "$HERE/tools/gen_config.py" "$GEN" $EMU_KCONFIG >/dev/null
python3 "$ROOT/tools/gen_engines.py" "$ROOT/config/engines.cnfg" "$GEN/sp1_engines_gen.h" >/dev/null
python3 "$ROOT/tools/gen_midi.py"    "$ROOT/config/midi.cnfg"    "$GEN/sp1_midi_gen.h" >/dev/null
python3 "$ROOT/tools/gen_audio.py"   "$ROOT/config/audio.cnfg"   "$GEN/sp1_audio_gen.h" >/dev/null
python3 "$ROOT/tools/gen_logs.py"    "$ROOT/config/logs.cnfg"    "$GEN/sp1_logs_gen.h" >/dev/null
python3 "$ROOT/tools/gen_scales.py"  "$ROOT/config/scales.cnfg"  "$GEN/sp1_scales_gen.h" >/dev/null
python3 "$ROOT/tools/gen_prst.py"    "$GEN/sp1_prst_gen.h" >/dev/null
SP1_PLAITS_BLOCK=$(sed -n 's/^#define CONFIG_SP1_PLAITS_BLOCK //p' "$GEN/emu_autoconf.h") \
	python3 "$ROOT/tools/host-tests/mkovr.py" "$OVR" >/dev/null

# ---- the Plaits / Marbles sources, with Wakes' replacements and overrides swapped in ----
dsp_sources() {
	for f in $ER/plaits/dsp/voice.cc $ER/plaits/resources.cc \
		 $ER/stmlib/dsp/atan.cc $ER/stmlib/dsp/units.cc $ER/stmlib/utils/random.cc \
		 $ER/plaits/dsp/engine/*.cc $ER/plaits/dsp/engine2/*.cc \
		 $ER/plaits/dsp/chords/*.cc $ER/plaits/dsp/fm/*.cc \
		 $ER/plaits/dsp/physical_modelling/*.cc $ER/plaits/dsp/speech/*.cc \
		 $ER/marbles/random/*.cc $ER/marbles/ramp/*.cc $ER/marbles/resources.cc; do
		rel=${f#$ER/}
		if [ -f "$SRC/plaits_ovr/$rel" ]; then echo "$SRC/plaits_ovr/$rel"
		elif [ -f "$OVR/$rel" ]; then echo "$OVR/$rel"
		else echo "$f"
		fi
	done
}

# Wakes' own sources: firmware/CMakeLists.txt's list, less the two modules the
# emulator replaces (sp1_emmc.c, sp1_uac.c) and feldd's USB class (its receive path is
# fed directly by the emulator's USB model).
WAKES_C="main.c sp1_led.c sp1_power.c sp1_batt.c sp1_standby.c sp1_console.c sp1_logbuf.c
	sp1_logring.c sp1_threads.c sp1_usbd.c sp1_controls.c sp1_display.c sp1_calib.c
	sp1_audio.c sp1_meter.c sp1_playrow.c sp1_plaits_ui.c sp1_store.c sp1_cnfg.c
	sp1_marbles_ui.c sp1_prst.c sp1_scales.c sp1_midifile.c"
WAKES_CXX="sp1_synth.cc sp1_marbles.cc sp1_midi.cc sp1_zero.cc"
EMU_C="emu_kernel.c emu_board.c emu_fs.c emu_emmc.c emu_uac.c emu_api.c"

APP_INC="-I$HERE/include -I$GEN -I$SRC -I$FELDD"
DSP_INC="-I$OVR -I$SRC/plaits_ovr -I$SRC/plaits_shim -I$ER -I$SRC -I$GEN -I$HERE/include"
DEFS="-include $GEN/emu_autoconf.h -D_DEFAULT_SOURCE"
DSP_FLAGS="-O2 -funroll-loops -DTEST -Wno-unused-variable -Wno-unused-parameter -Wno-unused-local-typedefs -Wno-sign-compare"
WARN="-Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable"

# compile CC FLAGS OUTDIR FILE... -> object list on stdout
compile() {
	cc=$1; flags=$2; odir=$3; shift 3
	mkdir -p "$odir"
	for f in "$@"; do
		o=$odir/$(echo "$f" | sed "s|$ROOT/||; s|/|_|g").o
		if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$GEN/emu_autoconf.h" -nt "$o" ] || [ -n "$EMU_REBUILD" ]; then
			echo "$cc $flags -c $f -o $o"
		fi
		echo "#OBJ $o" >&2
	done
}

build() {   # $1 = native|web
	kind=$1
	if [ "$kind" = native ]; then
		CC="gcc"; CXX="g++"
		TFLAGS="-g"
		ARCH=emu_arch_native.c
	else
		CC="clang --target=wasm32-wasi"; CXX="clang++ --target=wasm32-wasi"
		TFLAGS="-fvisibility=hidden -fno-exceptions"
		ARCH=emu_arch_wasm.c
	fi
	ODIR=$OUT/$kind
	JOBF=$OUT/$kind.jobs
	OBJF=$OUT/$kind.objs
	: > "$JOBF"; : > "$OBJF"
	{
		compile "$CXX" "-std=gnu++14 $DEFS $DSP_INC $DSP_FLAGS $TFLAGS" "$ODIR/dsp" $(dsp_sources)
		for f in $WAKES_CXX; do
			extra=""
			[ "$f" = sp1_zero.cc ] && [ "$kind" = native ] && extra="-fno-tree-loop-distribute-patterns -fno-unroll-loops"
			compile "$CXX" "-std=gnu++14 $DEFS $DSP_INC $DSP_FLAGS $TFLAGS $extra" "$ODIR/app" "$SRC/$f"
		done
		for f in $WAKES_C; do
			extra=""
			[ "$f" = main.c ] && extra="-Dmain=wakes_main"
			compile "$CC" "-std=gnu11 -O2 $DEFS $APP_INC $WARN $TFLAGS $extra" "$ODIR/app" "$SRC/$f"
		done
		compile "$CC" "-std=gnu11 -O2 $DEFS $APP_INC $TFLAGS" "$ODIR/app" "$FELDD/usb_rt_parse.c"
		for f in $EMU_C $ARCH; do
			compile "$CC" "-std=gnu11 -O2 $DEFS $APP_INC $WARN $TFLAGS" "$ODIR/emu" "$HERE/src/$f"
		done
		if [ "$kind" = native ]; then
			compile "$CC" "-std=gnu11 -O2 -I$HERE/include $TFLAGS" "$ODIR/emu" "$HERE/native/wakes_headless.c"
		else
			compile "$CXX" "-std=gnu++14 -O2 $TFLAGS" "$ODIR/emu" "$HERE/src/emu_new.cc"
		fi
	} > "$JOBF" 2> "$OBJF"
	if [ -s "$JOBF" ]; then
		echo "[$kind] compiling $(wc -l < "$JOBF") files"
		xargs -P "$JOBS" -I{} sh -c '{}' < "$JOBF"
	fi
	OBJS=$(sed -n 's/^#OBJ //p' "$OBJF")
	if [ "$kind" = native ]; then
		mkdir -p "$OUT/native"
		$CXX -o "$OUT/native/wakes_headless" $OBJS -lm
		echo "[native] $OUT/native/wakes_headless"
	else
		RAW=$OUT/web/wakes-raw.wasm
		$CXX -mexec-model=reactor -o "$RAW" $OBJS \
			-Wl,--export-dynamic -Wl,--export=__stack_pointer -Wl,--export=emu_dwt -Wl,--export=emu_cpu_classes \
			-Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 -lm
		# Asyncify: every function that can reach emu_host_switch can unwind and rewind.
		# The DSP never can, so it is left alone (and stays fast).
		# The CPU meter: every basic block adds its estimated Cortex-M4F cost to
		# DWT->CYCCNT (offset 4 in emu_dwt), scaled by the hardware calibration in
		# emulator/cpu.json (tools/calibrate_cpu.mjs). EMU_CPU_SCALE overrides it.
		CYC=$OUT/web/wakes-cyc.wasm
		if [ -n "$EMU_CPU_PROFILE" ]; then
			CYCARGS="--profile emu_cpu_classes"
		elif [ -n "$EMU_CPU_SCALE" ]; then
			CYCARGS="--counter emu_dwt:4 --scale $EMU_CPU_SCALE"
		else
			CYCARGS="--counter emu_dwt:4 --model $HERE/cpu.json"
		fi
		echo "[web] $(python3 "$HERE/tools/wasm_cycles.py" "$RAW" "$CYC" $CYCARGS)"
		[ -f "$HERE/cpu.json" ] && cp "$HERE/cpu.json" "$HERE/web/cpu.json"
		wasm-opt "$CYC" -O2 --asyncify \
			--pass-arg=asyncify-imports@env.emu_host_switch \
			--pass-arg='asyncify-removelist@_ZN6plaits*,_ZNK6plaits*,_ZN7marbles*,_ZNK7marbles*,_ZN6stmlib*,_ZNK6stmlib*' \
			-o "$WASM_OUT"
		echo "[web] $WASM_OUT ($(wc -c < "$WASM_OUT") bytes)"
		# The manual and the reference docs, beside the page (emulator/web/manual/).
		echo "[web] $(python3 "$HERE/tools/gen_manual.py" "$HERE/web/manual")"
	fi
}

case $WHAT in
	native) build native ;;
	web)    build web ;;
	all)    build native; build web ;;
	*)      echo "usage: $0 [native|web|all]" >&2; exit 2 ;;
esac
