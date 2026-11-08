# tinykit — drum voices, bus FX and bass for the SP-1 drum firmware

MIT. Original code; the algorithms are public ones (Karplus-Strong, PhISEM, modal synthesis,
OPL2-style FM, LFSR noise, Gendy, cellular automata, the Rungler, bytebeat, PolyBLEP).

- `tinykit.c/.h`: 66 drum models in 11 engines behind five controls (pitch, tone, decay, mod, excite).
  The "classic" engine forwards to `third_party/tinykit/lxr` (GPL-3.0; see its README) and exists only with `TK_WITH_LXR`
  (CONFIG_SP1_CLASSIC); without it, that engine has no models.
- `tinyfx.c/.h`: DJ filter, wavefolder, Chebyshev, through-zero FM, bit crusher (mono or stereo).
- `tinybass.c/.h`: the bass voice (saw -> square -> triangle, octave, filter, glide).

Prototypes, renders and the Cortex-M4 benchmarks live in sp1-drum-bench/tinykit.
