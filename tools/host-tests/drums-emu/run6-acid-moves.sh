#!/bin/sh
# The acid line's recorded fader moves, from the buttons: acid 1 over a kick and a hat, a
# cutoff sweep on SETTINGS + SHIFT F1, FFWD tap (keep), RWD tap (undo), keep again, FFWD held
# (add: this is home), resonance moved, RWD held (snap back: home, the take kept), RWD held
# again (nothing changed since: the home before, no takes). With WAV set, what it played.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run6-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
hold() { echo "$1:$2=1 $(($1+800)):$2=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV 2000:fnc=1 2090:fnc=0 2200:fnc=1 2290:fnc=0"   # SETTINGS
EV="$EV $(tap 2900 t2) $(tap 3200 t4)"                 # mute drums 2 and 4
EV="$EV $(fn 3600 ffwd) $(fn 4100 ffwd) $(fn 4600 ffwd)"   # acid 1
EV="$EV $(tap 5200 play)"
# "••" held: F1 down through the cutoff to catch it, then a sweep up
EV="$EV 7000:fnc=1 7100:f1=0.25 7300:f1=0.3 7500:f1=0.4 7700:f1=0.5 7900:f1=0.6 8100:f1=0.7 8300:fnc=0"
EV="$EV $(tap 8500 ffwd)"                              # keep
EV="$EV $(tap 10500 rwd)"                              # undo
EV="$EV $(tap 11500 ffwd)"                             # keep again
EV="$EV $(hold 12500 ffwd)"                            # add: home
EV="$EV 14000:fnc=1 14100:f2=0.6 14300:f2=0.8 14500:f2=0.95 14700:fnc=0"   # resonance up
EV="$EV $(hold 15500 rwd)"                             # snap back -> home
EV="$EV $(hold 19000 rwd)"                             # again -> the home before
EV="$EV 23000:fnc=1 28000:fnc=0"
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 30000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "BASS acid 1: acid-home (303)" --expect "CLOCK run" \
  --expect "ACID keep: the last bar of fader moves loops (take 1)" \
  --expect "ACID undo: take 1 gone, 0 left" \
  --expect "ACID add: this is home now (1 take, 2 homes deep)" \
  --expect "ACID snap back at the bar" --expect "ACID snap back: home (1 take)" \
  --expect "ACID snap back: the home before (0 takes)" \
  --expect "PRST save slot 1: saved" \
  $EV
