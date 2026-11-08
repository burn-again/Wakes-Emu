#!/bin/sh
# Steps 3-4: boot from run3's drive (IN, default ./run3-drive) with a damaged drums.cnfg,
# check the slot's [drums] comes back whole, power off again and check it saves unchanged.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
IN=${IN:-run3-drive}; OUT=${OUT:-run4-drive}
LOAD=$OUT.load
rm -rf "${LOAD:?}" "${OUT:?}"; cp -r "$IN" "$LOAD"; mkdir -p "$OUT"
sed -i 's/^tape_release = 1.2/tape_release = banana/; s/^level = -15 /level = -6  /' \
	"$LOAD/WAKES/CNFG/drums.cnfg"
$H --ms 12000 --load "$LOAD" --drive "$OUT/" \
  --expect "CNFG drums.cnfg line 17: tape_release -- not a number" \
  --expect "CNFG drums.cnfg: " --expect "1 problem" \
  --expect "STORE PRST 1: [drums] kit 1, 0 bad fields" \
  --expect "DRUMS kit 1: lich techno, bass y, 3 cables, rests choke" \
  --expect "PRST save slot 1: saved" \
  0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0 \
  2000:play=1 2090:play=0 6000:fnc=1 11000:fnc=0 || exit $?
a=$(sed -n '/^\[drums\]/,$p' "$IN/WAKES/PRST/1/1.prst")
b=$(sed -n '/^\[drums\]/,$p' "$OUT/WAKES/PRST/1/1.prst")
[ -n "$a" ] && [ "$a" = "$b" ] && echo "[run4] [drums] saved back unchanged" || {
	echo "[run4] [drums] changed:"; diff "$IN/WAKES/PRST/1/1.prst" "$OUT/WAKES/PRST/1/1.prst"; exit 5; }
