#!/bin/sh
# The LANES page: SETTINGS, on to the markov pattern (acid 15, "••" + RWD four times from
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
EV="$EV $(fn 3000 rwd) $(fn 3500 rwd) $(fn 4000 rwd) $(fn 4500 rwd)"   # off -> user -> french -> electro -> markov
EV="$EV $(tap 5200 play)"
EV="$EV 6000:fnc=1 6090:fnc=0 6200:fnc=1 6290:fnc=0"   # two more taps: LANES
EV="$EV 7000:f2=0.3 7200:f2=0.1 8000:f2=0.5"           # the accent lane's density down, and back to the middle
EV="$EV 9000:fnc=1 9100:f3=0.9 9200:f3=1.0 9400:f3=0.8 9600:f3=0.55 9800:f3=0.3 10000:fnc=0"   # SHIFT F3: the slide lane's length
EV="$EV 11000:rwd=1 11800:rwd=0"                       # snap back
EV="$EV 15000:fnc=1 15090:fnc=0"                       # one tap: SETTINGS
EV="$EV 16000:fnc=1 16090:fnc=0"                       # one tap: KIT
EV="$EV 18000:fnc=1 23000:fnc=0"
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 25000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "BASS acid 15: markov (303)" --expect "PAGE lanes" \
  --expect "LANE slide: 12 steps" --expect "LANE slide: 5 steps" --expect "LANE slide: 4 steps" \
  --expect "ACID snap back: home" --expect "LANE" --expect "PAGE settings" --expect "PAGE kit" \
  --expect "PRST save slot 1: saved" \
  $EV || exit $?
grep -q '^lanes_shift = ' "$OUT/WAKES/PRST/1/1.prst" && grep -q '^bass = markov' "$OUT/WAKES/PRST/1/1.prst" \
	&& echo "[run7] the slot keeps the lanes: $(grep '^lanes_' "$OUT/WAKES/PRST/1/1.prst" | tr '\n' ' ')" || {
	echo "[run7] the slot did not keep the lanes"; exit 5; }
