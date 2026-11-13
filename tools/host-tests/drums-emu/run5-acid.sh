#!/bin/sh
# The acid line: SETTINGS, "••" + FFWD three times (bass on Y, in the rests, acid 1), drums 2
# and 4 muted so the kick and the hat play under it, SETTINGS + SHIFT F1 / F2 as its cutoff and
# resonance, "••" + FFWD again (acid 2: campfire, the 101), then a "••" hold to power off: the
# slot keeps the pattern by name. With WAV set, what it played is written there.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run5-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV 2000:fnc=1 2090:fnc=0 2200:fnc=1 2290:fnc=0"   # SETTINGS
EV="$EV $(tap 2900 t2) $(tap 3200 t4)"                 # mute drums 2 and 4
EV="$EV $(fn 3600 ffwd) $(fn 4100 ffwd) $(fn 4600 ffwd)"   # bass: y, rests, acid 1
EV="$EV $(tap 5200 play)"
# "••" held: SETTINGS + SHIFT. F1 down to catch the cutoff, then a sweep up; F2 up for resonance.
EV="$EV 9000:fnc=1 9200:f1=0.3 9400:f1=0.35 9800:f1=0.45 10200:f1=0.6 10600:f1=0.7 11000:f2=0.8 11400:f2=0.9 12000:fnc=0"
EV="$EV $(fn 14000 ffwd)"                              # acid 2
EV="$EV 20000:fnc=1 25000:fnc=0"                       # power off: the slot saved
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 27000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "PAGE settings" --expect "BASS on the Y clock" --expect "BASS only in the rests" \
  --expect "BASS acid 1: acid-home (303)" --expect "CLOCK run" \
  --expect "BASS acid 2: campfire (101)" --expect "PRST save slot 1: saved" \
  $EV || exit $?
grep -q '^bass = campfire' "$OUT/WAKES/PRST/1/1.prst" && echo "[run5] the slot keeps the pattern: bass = campfire" || {
	echo "[run5] the slot did not keep the pattern:"; grep '^bass' "$OUT/WAKES/PRST/1/1.prst"; exit 5; }
