#!/bin/sh
# The acid line from power-on: it is the page you start on (SOUND) and it plays on PLAY, over
# the kick and the hat. F1 / F2 are its cutoff and resonance; T2 opens PATTERN and "••" + FFWD
# steps to the next pattern (campfire, the 101), taken up at the bar; a "••" hold powers off
# and the slot keeps the line by name. With WAV set, what it played is written there.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run5-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV $(tap 2500 play)"
# SOUND: F1 down to catch the cutoff, then a sweep up; F2 up for resonance
EV="$EV 5000:f1=0.3 5200:f1=0.35 5600:f1=0.45 6000:f1=0.6 6400:f1=0.7 6800:f2=0.8 7200:f2=0.9"
EV="$EV $(tap 9000 t2)"                                # PATTERN
EV="$EV $(fn 9500 ffwd)"                               # the next pattern
EV="$EV 16000:fnc=1 21000:fnc=0"                       # power off: the slot saved
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 23000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "DRUMS kit 1: lich techno, bass acid-home" --expect "CLOCK run" \
  --expect "PAGE pattern" --expect "PATTERN 2: campfire (101)" --expect "PRST save slot 1: saved" \
  $EV || exit $?
grep -q '^line = campfire' "$OUT/WAKES/PRST/1/1.prst" && grep -q '^mute = 10' "$OUT/WAKES/PRST/1/1.prst" \
	&& echo "[run5] the slot keeps the line and the mutes: line = campfire, mute = 10" || {
	echo "[run5] the slot did not keep the line:"; grep '^line\|^bass\|^mute' "$OUT/WAKES/PRST/1/1.prst"; exit 5; }
