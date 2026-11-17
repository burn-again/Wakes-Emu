#!/bin/sh
# HOME: power-on lands here. PLAY; F3 to scene 11 (reggaeton over lich techno, 92 BPM, taken up
# at the bar); RWD held: the tempo glides down; FFWD tapped: one BPM up; F2 to kit 3; F1 the
# balance down and back; F4 the DJ filter down and back; T1 SOUND, then a "••" double tap back
# to HOME; a triple tap to MARBLES and back (HOME); a "••" hold powers off: the slot keeps the
# tempo, the scene's line and the HOME faders. With WAV set, what it played.
H=${WAKES_HEADLESS:-build/emu/native/wakes_headless}
OUT=${OUT:-run8-drive}
tap() { echo "$1:$2=1 $(($1+90)):$2=0"; }
tri() { echo "$1:fnc=1 $(($1+90)):fnc=0 $(($1+200)):fnc=1 $(($1+290)):fnc=0 $(($1+400)):fnc=1 $(($1+490)):fnc=0"; }
EV="0:fnc=1 0:f1=0.5 0:f2=0.5 0:f3=0.5 0:f4=0.5 1700:fnc=0"
EV="$EV $(tap 2000 play)"
EV="$EV 2500:f3=0.0 2900:f3=0.35"                      # F3: down to catch it, then scene 11 (the travel ends at 0.90)
EV="$EV 6000:rwd=1 8000:rwd=0"                         # RWD held 2 s: about 34 BPM slower
EV="$EV $(tap 8500 ffwd)"                              # one BPM up
EV="$EV 9000:f2=0.0 9300:f2=0.28"                      # F2: kit 3 (modal gamelan), at the bar
EV="$EV 12000:f1=0.1 12500:f1=0.45"                    # F1: the line alone, then both
EV="$EV 13000:f4=0.1 13500:f4=0.45"                    # F4: the DJ filter low-pass, then off
EV="$EV $(tap 14000 t1)"                               # SOUND
EV="$EV 14500:fnc=1 14590:fnc=0 14700:fnc=1 14790:fnc=0"   # double tap: HOME
EV="$EV $(tri 15500)"                                  # MARBLES
EV="$EV $(tri 17000)"                                  # back: HOME
EV="$EV 19000:fnc=1 24000:fnc=0"
rm -rf "${OUT:?}"; mkdir -p "$OUT"
$H --ms 26000 --drive "$OUT/" ${WAV:+--wav "$WAV"} \
  --expect "CLOCK run" --expect "SCENE 11: reggaeton, lich techno, 92 BPM" \
  --expect "TEMPO 5" --expect "KIT 3: modal gamelan" --expect "PAGE sound" --expect "PAGE home" \
  --expect "MODULE marbles" --expect "PRST save slot 1: saved" \
  $EV || exit $?
P="$OUT/WAKES/PRST/1/1.prst"
grep -q '^line = reggaeton' "$P" && grep -q '^kit = 3' "$P" && grep -q '^home_faders = ' "$P" && grep -q '^tempo = 5' "$P" \
	&& echo "[run8] the slot keeps it: $(grep '^tempo\|^line\|^kit \|^home_faders' "$P" | tr '\n' ' ')" || {
	echo "[run8] the slot did not keep it:"; grep '^tempo\|^line\|^kit\|^home' "$P"; exit 5; }
