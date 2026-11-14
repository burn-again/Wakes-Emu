#!/bin/sh
# Step 1: the pages. T1-T4 open SOUND, PATTERN, LANES and KIT; "••" + Tn opens DRUM n and,
# pressed again there, mutes it; T1 on SOUND switches the acid line; SETTINGS and its mutes;
# kit and model select; MARBLES and back.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }     # a 90 ms press
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 1700:fnc=0"                         # power on: hold •• 1.5 s (the page is SOUND)
EV="$EV $(tap 2500 t2)"                         # PATTERN
EV="$EV $(tap 3000 t3)"                         # LANES
EV="$EV $(tap 3500 t4)"                         # KIT
EV="$EV $(fn 4000 t2)"                          # •• + T2: DRUM 2
EV="$EV $(fn 4500 t2)"                          # again: drum 2 (muted at power-on) plays
EV="$EV $(fn 5000 t2)"                          # and again: muted
EV="$EV $(fn 5500 t3)"                          # DRUM 3
EV="$EV $(tap 6000 t4)"                         # KIT
EV="$EV 6500:fnc=1 6590:fnc=0 6700:fnc=1 6790:fnc=0"   # double tap -> SETTINGS
EV="$EV $(tap 7700 t1)"                         # SETTINGS T1: mute drum 1
EV="$EV $(tap 8100 t1)"                         # ...and unmute
EV="$EV 8500:fnc=1 8590:fnc=0"                  # one tap -> back to KIT (after 400 ms)
EV="$EV $(fn 9500 ffwd)"                        # •• + FFWD on KIT: kit 2
EV="$EV $(fn 10000 t1)"                         # DRUM 1
EV="$EV $(fn 10500 ffwd)"                       # •• + FFWD on DRUM 1: next model
EV="$EV $(tap 11000 t1)"                        # SOUND
EV="$EV $(tap 11500 t1)"                        # T1 on SOUND: the acid line off
EV="$EV $(tap 12000 t1)"                        # ...and on
EV="$EV 12500:fnc=1 12590:fnc=0 12700:fnc=1 12790:fnc=0 12900:fnc=1 12990:fnc=0"  # triple -> MARBLES
EV="$EV 13900:fnc=1 13990:fnc=0 14100:fnc=1 14190:fnc=0 14300:fnc=1 14390:fnc=0"  # triple -> back
$H --ms 15500 \
  --expect "PAGE pattern" --expect "PAGE lanes" --expect "PAGE kit" --expect "PAGE drum 2" \
  --expect "MUTE drum 2 playing" --expect "MUTE drum 2 muted" --expect "PAGE drum 3" \
  --expect "PAGE settings" --expect "MUTE drum 1 muted" --expect "MUTE drum 1 playing" \
  --expect "KIT 2: 8-bit breaks" --expect "PAGE drum 1" --expect "MODEL drum 1:" \
  --expect "PAGE sound" --expect "LINE off" --expect "LINE acid 1: acid-home (303)" \
  --expect "MODULE marbles" --expect "MODULE drums (kit 2" \
  $EV
