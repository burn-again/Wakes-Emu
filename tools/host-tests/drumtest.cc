// Host test for the drum firmware's kit (sp1_drums.cc): drives it exactly as sp1_synth_render
// does -- 24-sample Plaits blocks, a Marbles-like gate stream, X/Y voltages -- for every kit,
// then checks the output and writes one WAV per kit.
// Build (the tinykit sources are C, so compile them with a C compiler):
//   for f in tinykit tinyfx tinybass; do gcc -O2 -c ../../third_party/tinykit/$f.c; done
//   g++ -O2 -DCONFIG_SP1_DRUMS -I../../firmware/src -I../../third_party/tinykit drumtest.cc
//       ../../firmware/src/sp1_drums.cc tinykit.o tinyfx.o tinybass.o -lm -o drumtest
//   ./drumtest <out dir>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sp1_drums.h"

static void wav(const char* path, const int16_t* x, unsigned n) {
  FILE* f = fopen(path, "wb"); if (!f) return;
  unsigned bytes = n * 2, sr = 48000, br = 96000, v; unsigned short s;
  fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(x, 2, n, f); fclose(f);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  const unsigned kBlock = 24, kSeconds = 6, kBlocks = 48000 * kSeconds / kBlock;
  static int16_t pcm[48000 * 6];
  int failures = 0;
  sp1_drums_init();
  for (int kit = 0; kit < SP1_DRUMS_KITS; ++kit) {
    sp1_synth_params p; memset(&p, 0, sizeof p);
    p.note = 60.0f; p.timbre = p.morph = p.harmonics = 0.5f; p.engine = 8 + kit;   // engines.cnfg slot order
    sp1_drums_begin(&p);
    uint8_t prev = 0; unsigned seed = 1234u + kit;
    float volts[4] = {0, 0, 0, 0};
    double sumsq = 0; int peak = 0, clipped = 0, nans = 0;
    for (unsigned b = 0; b < kBlocks; ++b) {
      if (b == kBlocks / 2) { p.timbre_mod = 0.4f; p.fm_mod = -0.5f; sp1_drums_begin(&p); }   // FX on halfway
      // a 16th-note master clock at 120 BPM (250 blocks), 25 % gates; t1 / t3 coin tosses
      const unsigned step = b / 250, phase = b % 250;
      seed = seed * 1103515245u + 12345u;
      const bool tick = phase < 62;
      static bool t1 = false, t3 = false;
      if (phase == 0) {
        seed = seed * 1103515245u + 12345u; t1 = (step % 4 == 0) || ((seed >> 16) % 7 == 0);
        seed = seed * 1103515245u + 12345u; t3 = (step % 8 == 4) || ((seed >> 16) % 9 == 0);
        for (int k = 0; k < 4; ++k) { seed = seed * 1103515245u + 12345u; volts[k] = ((seed >> 16) % 1000) / 100.0f - 5.0f; }
      }
      const uint8_t gates = (uint8_t)((t1 && tick ? 1 : 0) | (tick ? 2 : 0) | (t3 && tick ? 4 : 0));
      const uint8_t rise = gates & (uint8_t)~prev; prev = gates;
      float out[24];
      sp1_drums_render(rise, volts, b == 10, out, kBlock);
      for (unsigned i = 0; i < kBlock; ++i) {
        if (!isfinite(out[i])) { ++nans; out[i] = 0; }
        int v = (int)out[i]; if (v > 32767) { v = 32767; ++clipped; } if (v < -32768) { v = -32768; ++clipped; }
        pcm[b * kBlock + i] = (int16_t)v; sumsq += (double)v * v; if (abs(v) > peak) peak = abs(v);
      }
    }
    const double rms_db = 20 * log10(sqrt(sumsq / (kBlocks * kBlock)) / 32768.0 + 1e-12);
    const bool ok = nans == 0 && clipped == 0 && peak > 3000 && sp1_drums_kit() == kit;
    printf("kit %d %-14s peak %5d (%5.1f dBFS)  rms %6.1f dBFS  clipped %d  nan %d  %s\n", kit,
           sp1_drums_kit_name(kit), peak, 20 * log10(peak / 32768.0), rms_db, clipped, nans, ok ? "ok" : "FAIL");
    failures += !ok;
    char path[256]; snprintf(path, sizeof path, "%s/kit%d_%s.wav", dir, kit, sp1_drums_kit_name(kit));
    for (char* c = path + strlen(dir); *c; ++c) if (*c == ' ') *c = '_';
    wav(path, pcm, kBlocks * kBlock);
  }
  printf(failures ? "%d kit(s) FAILED\n" : "all kits ok\n", failures);
  return failures != 0;
}
