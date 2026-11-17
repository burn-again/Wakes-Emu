#!/bin/sh
# HOME + "••": PLAY, "••" + FFWD (randomise at the bar: the present to temp first), again (a
# second randomise: temp is now the first one's result), "••" + RWD tapped (undo at the bar:
# back to temp, one randomise back), "••" + RWD held 1 s (write to the drive while playing; the log keeps running), then
# a "••" hold powers off and the shutdown save follows.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run9-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV $(tap 2000 play)"
EV="$EV $(fn 3000 ffwd)"                              # randomise
EV="$EV $(fn 7000 ffwd)"                              # and again
EV="$EV $(fn 11000 rwd)"                              # undo
EV="$EV 15000:fnc=1 15100:rwd=1 16300:rwd=0 16400:fnc=0"   # write
EV="$EV 19000:fnc=1 24000:fnc=0"
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 26000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "RANDOM at the bar" --expect "RANDOM: scene" --expect "UNDO at the bar: back to temp" \
  --expect "UNDO: back to temp (" --expect "WRITE this to the drive" \
  --expect "PRST write slot 1: writing" --expect "STORE PRST save slot 1 -> 1.prst: done" \
  --expect "PRST save slot 1: saved" \
  $EV || exit $?
n=$(grep -c "STORE PRST save slot 1 -> 1.prst: done" "$OUT/WAKES/LOGS/"*.logs 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
echo "[run9] the log on the drive holds $n saves (the write while playing, then the shutdown save)"
