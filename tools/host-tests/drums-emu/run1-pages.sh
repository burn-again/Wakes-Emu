#!/bin/sh
# Step 1: pages, taps, FX toggles, mutes, kit and model select.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }     # a 90 ms press
EV="0:fnc=1 1700:fnc=0"                         # power on: hold •• 1.5 s
EV="$EV $(tap 2500 t2)"                         # KIT -> DRUM 2
EV="$EV $(tap 3000 t3)"                         # DRUM 2 -> DRUM 3
EV="$EV $(tap 3500 t3)"                         # DRUM 3 -> KIT
EV="$EV 4000:fnc=1 4090:fnc=0 4200:fnc=1 4290:fnc=0"   # double tap -> SETTINGS
EV="$EV $(tap 5200 t2)"                         # SETTINGS T2: mute drum 2
EV="$EV $(tap 5600 t2)"                         # ...and unmute
EV="$EV 6000:fnc=1 6090:fnc=0"                  # one tap -> back to KIT (after 400 ms)
EV="$EV 7000:fnc=1 7100:t1=1 7190:t1=0 7300:fnc=0"     # •• + T1: DJ filter off
EV="$EV 7600:fnc=1 7700:ffwd=1 7790:ffwd=0 7900:fnc=0" # •• + FFWD on KIT: kit 2
EV="$EV $(tap 8300 t1)"                         # -> DRUM 1
EV="$EV 8600:fnc=1 8700:ffwd=1 8790:ffwd=0 8900:fnc=0" # •• + FFWD on DRUM 1: next model
EV="$EV $(tap 9300 t1)"                         # DRUM 1 -> KIT
EV="$EV 9800:fnc=1 9890:fnc=0 10000:fnc=1 10090:fnc=0 10200:fnc=1 10290:fnc=0"  # triple -> MARBLES
EV="$EV 11200:fnc=1 11290:fnc=0 11400:fnc=1 11490:fnc=0 11600:fnc=1 11690:fnc=0"  # triple -> KIT
EV="$EV 12500:f1=0.9 12600:f1=0.8 12700:f1=0.7"  # KIT F1 (kit pitch) moves
$H --ms 13500 \
  --expect "PAGE drum 2" --expect "PAGE drum 3" --expect "PAGE kit" --expect "PAGE settings" \
  --expect "MUTE drum 2 muted" --expect "MUTE drum 2 playing" \
  --expect "FX DJ filter off" --expect "KIT 2: 8-bit breaks" --expect "MODEL drum 1:" \
  --expect "MODULE marbles" --expect "MODULE drums (kit 2" \
  $EV
