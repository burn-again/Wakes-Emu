#!/bin/sh
# Steps 3-4: the sequencer playing, fill, a pattern swap, the bass on Y, three cables, and a
# power-off save. Leaves the drive in $OUT (default ./run3-drive) for run4-reload.sh.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run3-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
tri() { echo "$1:fnc=1 $(($1+90)):fnc=0 $(($1+200)):fnc=1 $(($1+290)):fnc=0 $(($1+400)):fnc=1 $(($1+490)):fnc=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV $(tap 1900 t4)"                           # KIT
EV="$EV $(tap 2200 play)"                         # the sequencer plays kit 1's maps
EV="$EV 2600:f4=0.6 2700:f4=0.8 2800:f4=0.95"      # KIT F4: fill up (ghost notes in)
EV="$EV 4200:f4=0.7 4300:f4=0.5"                   # and back
EV="$EV 4800:fnc=1 4890:fnc=0 5000:fnc=1 5090:fnc=0"   # HOME
EV="$EV 5600:fnc=1 5700:f1=0.0 5900:f1=0.3 6300:fnc=0" # HOME + SHIFT F1: swing
EV="$EV $(tap 7000 t4)"                            # KIT
EV="$EV $(tri 7600)"                               # MARBLES (t page)
EV="$EV $(fn 8500 t1)"                             # t1 -> slot 1
EV="$EV $(tap 9200 t3)"                            # X page
EV="$EV $(fn 9600 t2) $(fn 10200 t2)"              # X2 -> pitch -> tone
EV="$EV $(fn 10800 t4)"                            # Y -> bass note
EV="$EV $(tri 11500)"                              # back to KIT
EV="$EV 14000:fnc=1 19000:fnc=0"                   # hold "••": power off, the slot saved
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 21000 --drive "$OUT/" \
  --expect "CLOCK run" --expect "PAGE home" --expect "MODULE marbles" \
  --expect "CABLE t1 -> slot 1" --expect "MARBLES X page" --expect "CABLE X2 -> its slot's tone" \
  --expect "CABLE Y -> bass note" --expect "PRST save slot 1: saved" \
  $EV
