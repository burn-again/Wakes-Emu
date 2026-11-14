#!/bin/sh
# Step 2: the rocker, playing and stopped.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
tri() { echo "$1:fnc=1 $(($1+90)):fnc=0 $(($1+200)):fnc=1 $(($1+290)):fnc=0 $(($1+400)):fnc=1 $(($1+490)):fnc=0"; }
fn()  { echo "$1:fnc=1 $(($1+100)):$2=1 $(($1+190)):$2=0 $(($1+300)):fnc=0"; }
EV="0:fnc=1 1700:fnc=0"
EV="$EV $(tap 2000 play)"                       # clock runs, 120 BPM
EV="$EV $(fn 2400 t3)"                          # "••" + T3: DRUM 3 (slot 3: every t2 tick)
EV="$EV 3000:rwd=1 4200:rwd=0"                  # rest drum 3 for 1.2 s
EV="$EV $(tap 6000 ffwd)"                       # tap: clear drum 3 at the next bar
EV="$EV $(tap 8200 t4)"                         # T4: KIT
EV="$EV $(tap 8500 ffwd)"                       # rotate the kit
EV="$EV 9500:rwd=1 11000:rwd=0"                 # tape slow
EV="$EV 12500:fnc=1 12590:fnc=0 12700:fnc=1 12790:fnc=0"   # SETTINGS
EV="$EV $(tap 13300 ffwd)"                      # rests ring out
EV="$EV $(tap 13700 rwd)"                       # rests choke
EV="$EV 14200:fnc=1 14290:fnc=0"                # one tap: back to KIT
EV="$EV $(tri 15200)"                           # MARBLES
EV="$EV 16200:rwd=1 16800:rwd=0"                # rest every drum
EV="$EV 17300:fnc=1 17400:ffwd=1 17490:ffwd=0 17600:fnc=0" # •• + FFWD: t range x4
EV="$EV 17900:fnc=1 18000:rwd=1 18090:rwd=0 18200:fnc=0"   # •• + RWD: t range x1
EV="$EV $(tap 18600 ffwd)"                      # clear every drum at the next bar
EV="$EV $(tap 21000 play)"                      # stop
EV="$EV $(tap 21500 rwd)"                       # step Marbles (MARBLES page)
EV="$EV $(tap 22000 rwd)"                       # and again
EV="$EV $(tri 22500)"                           # back to the drum module (SOUND)
EV="$EV $(tap 23200 t4)"                        # KIT
EV="$EV $(tap 23600 rwd)"                       # step Marbles (KIT)
EV="$EV $(fn 24000 t2)"                         # DRUM 2
EV="$EV $(tap 24500 rwd)"                       # audition drum 2
EV="$EV 25000:ffwd=1 26000:ffwd=0"              # burst drum 2
$H --ms 27000 \
  --expect "CLOCK run" --expect "RESTS writing this drum" --expect "slot 3 " \
  --expect "RESTS clear this drum at the next bar" --expect "ROTATE the kit" \
  --expect "ROTATED: slot 1 plays drum 4" --expect "TAPE slow" --expect "TAPE released" \
  --expect "RESTS ring out" --expect "RESTS choke" --expect "MODULE marbles" \
  --expect "RESTS writing every drum" --expect "RANGE t x4" --expect "RANGE t x1" \
  --expect "RESTS clear every drum at the next bar" --expect "CLOCK stop" \
  --expect "STEP one 16th" --expect "HIT drums" --expect "AUDITION drum 2" \
  --expect "BURST on" --expect "BURST off" \
  $EV
