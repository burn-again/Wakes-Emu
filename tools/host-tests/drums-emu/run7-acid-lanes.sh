#!/bin/sh
# PATTERN and LANES: T2, F1 moved to the markov pattern (it is taken up at the bar); T3 LANES:
# F2 (the accent lane's density) down and back; "••" held, F3 down: the slide lane's length
# steps through its zones; RWD held: snap back, the lanes home again; T4 KIT. A "••" hold
# powers off and the slot keeps the line and the page's faders.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run7-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV $(tap 2200 play)"
EV="$EV $(tap 2700 t2)"                                # PATTERN
EV="$EV 3200:f1=0.0 3700:f1=0.468"                     # F1: down to catch it, then to the 15th of 28 (the fader's travel ends at 0.90)
EV="$EV $(tap 6500 t3)"                                # LANES
EV="$EV 7300:f2=0.3 7500:f2=0.1 8300:f2=0.5"           # the accent lane's density down, and back
EV="$EV 9300:fnc=1 9400:f3=0.9 9500:f3=1.0 9700:f3=0.8 9900:f3=0.55 10100:f3=0.3 10300:fnc=0"   # SHIFT F3
EV="$EV 11300:rwd=1 12100:rwd=0"                       # snap back
EV="$EV $(tap 15300 t4)"                               # KIT
EV="$EV 17000:fnc=1 22000:fnc=0"
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 24000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "PAGE pattern" --expect "PATTERN 15: markov (303)" --expect "PAGE lanes" \
  --expect "LANE slide: 12 steps" --expect "LANE slide: 5 steps" --expect "LANE slide: 4 steps" \
  --expect "ACID snap back: home" --expect "PAGE kit" --expect "PRST save slot 1: saved" \
  $EV || exit $?
grep -q '^lanes_shift = ' "$OUT/WAKES/PRST/1/1.prst" && grep -q '^line = markov' "$OUT/WAKES/PRST/1/1.prst" \
	&& echo "[run7] the slot keeps the line and the lanes: $(grep '^lanes_\|^line' "$OUT/WAKES/PRST/1/1.prst" | tr '\n' ' ')" || {
	echo "[run7] the slot did not keep them:"; grep '^line\|^bass\|^lanes' "$OUT/WAKES/PRST/1/1.prst"; exit 5; }
