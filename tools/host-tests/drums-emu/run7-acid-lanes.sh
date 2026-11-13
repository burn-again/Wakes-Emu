#!/bin/sh
# The LANES page: SETTINGS, on to the markov pattern (acid 15, "••" + RWD fourteen times from
# off), two more "••" taps -> LANES. F2 (the accent lane's density) down and back; "••" held,
# F3 down: the slide lane's length steps through its zones; RWD held: snap back, the lanes
# home again; one tap -> SETTINGS, one more -> KIT. A "••" hold powers off and the slot keeps
# the page's faders (lanes_faders / lanes_shift).
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run7-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV 2000:fnc=1 2090:fnc=0 2200:fnc=1 2290:fnc=0"   # SETTINGS
t=3000; n=0                                            # off -> user -> gqom -> ... -> markov: 14 steps back
while [ $n -lt 14 ]; do EV="$EV $(fn $t rwd)"; t=$((t+450)); n=$((n+1)); done
B=$((t+300))                                           # everything after is from here
at() { echo $((B+$1)); }
EV="$EV $(tap $B play)"
EV="$EV $(at 800):fnc=1 $(at 890):fnc=0 $(at 1000):fnc=1 $(at 1090):fnc=0"   # two more taps: LANES
EV="$EV $(at 1800):f2=0.3 $(at 2000):f2=0.1 $(at 2800):f2=0.5"               # the accent lane's density down, and back
EV="$EV $(at 3800):fnc=1 $(at 3900):f3=0.9 $(at 4000):f3=1.0 $(at 4200):f3=0.8 $(at 4400):f3=0.55 $(at 4600):f3=0.3 $(at 4800):fnc=0"   # SHIFT F3
EV="$EV $(at 5800):rwd=1 $(at 6600):rwd=0"             # snap back
EV="$EV $(at 9800):fnc=1 $(at 9890):fnc=0"             # one tap: SETTINGS
EV="$EV $(at 10800):fnc=1 $(at 10890):fnc=0"           # one tap: KIT
EV="$EV $(at 12800):fnc=1 $(at 17800):fnc=0"
MS=$((B+20000))
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms $MS --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "BASS acid 15: markov (303)" --expect "PAGE lanes" \
  --expect "LANE slide: 12 steps" --expect "LANE slide: 5 steps" --expect "LANE slide: 4 steps" \
  --expect "ACID snap back: home" --expect "LANE" --expect "PAGE settings" --expect "PAGE kit" \
  --expect "PRST save slot 1: saved" \
  $EV || exit $?
grep -q '^lanes_shift = ' "$OUT/WAKES/PRST/1/1.prst" && grep -q '^bass = markov' "$OUT/WAKES/PRST/1/1.prst" \
	&& echo "[run7] the slot keeps the lanes: $(grep '^lanes_' "$OUT/WAKES/PRST/1/1.prst" | tr '\n' ' ')" || {
	echo "[run7] the slot did not keep the lanes"; exit 5; }
