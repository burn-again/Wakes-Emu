// Host test for the drum firmware's kit (sp1_drums.cc) and its pages (sp1_drums_ui.c): drives
// the kit exactly as sp1_synth_render does -- 24-sample Plaits blocks, a Marbles-like gate
// stream with its master ramp, X/Y voltages -- for every kit, with the controls the pages
// produce, then checks the output and writes one WAV per kit. Then the rocker's rests and the
// kit rotation, against the kit's own status.
// Build (the tinykit sources are C, so compile them with a C compiler): see README.md.
//   ./drumtest <out dir>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sp1_drums.h"
#include "sp1_drums_ui.h"

static void wav(const char* path, const int16_t* x, unsigned n) {
  FILE* f = fopen(path, "wb"); if (!f) return;
  unsigned bytes = n * 2, sr = 48000, br = 96000, v; unsigned short s;
  fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(x, 2, n, f); fclose(f);
}

// A Marbles-like clock: one t2 tick every 250 blocks (120 BPM at 16ths... the test's own beat),
// 25 % gates, t1 / t3 coin tosses, fresh X/Y on each tick.
struct Clock {
  unsigned seed, b = 0; uint8_t prev = 0; bool t1 = false, t3 = false; float volts[4] = {0, 0, 0, 0};
  explicit Clock(unsigned s) : seed(s) {}
  sp1_drums_frame next(bool audition) {
    const unsigned step = b / 250, phase = b % 250;
    if (phase == 0) {
      seed = seed * 1103515245u + 12345u; t1 = (step % 4 == 0) || ((seed >> 16) % 7 == 0);
      seed = seed * 1103515245u + 12345u; t3 = (step % 8 == 4) || ((seed >> 16) % 9 == 0);
      for (int k = 0; k < 4; ++k) { seed = seed * 1103515245u + 12345u; volts[k] = ((seed >> 16) % 1000) / 100.0f - 5.0f; }
    }
    const bool tick = phase < 62;
    const uint8_t gates = (uint8_t)((t1 && tick ? 1 : 0) | (tick ? 2 : 0) | (t3 && tick ? 4 : 0));
    sp1_drums_frame f;
    f.rise = gates & (uint8_t)~prev; prev = gates;
    f.volts = volts;
    f.ramp = (float)phase / 250.0f;
    f.running = true;
    f.audition = audition;
    ++b;
    return f;
  }
};

static uint32_t hits() { struct sp1_drums_status s; sp1_drums_status(&s); return s.hits; }

// The kit's step-through talks to Marbles' clock (sp1_marbles.h); there is no Marbles here.
extern "C" void sp1_marbles_clock(const float*, int) {}
extern "C" bool sp1_marbles_running(void) { return false; }

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  const unsigned kBlock = 24, kSeconds = 6, kBlocks = 48000 * kSeconds / kBlock;
  static int16_t pcm[48000 * 6];
  int failures = 0;
  sp1_drums_init();
  sp1_dui_init();
  for (int kit = 0; kit < SP1_DRUMS_KITS; ++kit) {
    sp1_synth_params p; memset(&p, 0, sizeof p);
    sp1_dui_load_kit(kit);
    sp1_dui_ctl(&p.drums);
    sp1_drums_begin(&p);
    Clock clk(1234u + kit);
    double sumsq = 0; int peak = 0, clipped = 0, nans = 0;
    for (unsigned b = 0; b < kBlocks; ++b) {
      if (b == kBlocks / 2) { p.drums.fx[1] = 0.4f; p.drums.fx[0] = 0.25f; sp1_drums_begin(&p); }   // FX halfway
      const sp1_drums_frame f = clk.next(b == 10);
      float out[24];
      sp1_drums_render(&f, out, kBlock);
      for (unsigned i = 0; i < kBlock; ++i) {
        if (!isfinite(out[i])) { ++nans; out[i] = 0; }
        int v = (int)out[i]; if (v > 32767) { v = 32767; ++clipped; } if (v < -32768) { v = -32768; ++clipped; }
        pcm[b * kBlock + i] = (int16_t)v; sumsq += (double)v * v; if (abs(v) > peak) peak = abs(v);
      }
    }
    const double rms_db = 20 * log10(sqrt(sumsq / (kBlocks * kBlock)) / 32768.0 + 1e-12);
    const bool ok = nans == 0 && clipped == 0 && peak > 3000 && sp1_dui_kit() == kit;
    printf("kit %d %-14s peak %5d (%5.1f dBFS)  rms %6.1f dBFS  clipped %d  nan %d  %s\n", kit,
           sp1_drums_kit_name(kit), peak, 20 * log10(peak / 32768.0), rms_db, clipped, nans, ok ? "ok" : "FAIL");
    failures += !ok;
    char path[256]; snprintf(path, sizeof path, "%s/kit%d_%s.wav", dir, kit, sp1_drums_kit_name(kit));
    for (char* c = path + strlen(dir); *c; ++c) if (*c == ' ') *c = '_';
    wav(path, pcm, kBlocks * kBlock);
  }

  // ---- rests: a whole bar written on every slot plays nothing; a clear brings it back ----
  {
    sp1_synth_params p; memset(&p, 0, sizeof p);
    sp1_dui_load_kit(0);
    sp1_dui_ctl(&p.drums);
    Clock clk(99u);
    float out[24];
    auto bars = [&](int n) { for (int i = 0; i < n * 1000; ++i) { sp1_drums_frame f = clk.next(false); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24); } };
    bars(1);                                         // a bar to find the beat
    const uint32_t h0 = hits(); bars(1); const uint32_t played = hits() - h0;
    p.drums.rest_write = 0x0Fu; bars(1); p.drums.rest_write = 0u;   // RWD held for a bar
    const uint32_t h1 = hits(); bars(2); const uint32_t rested = hits() - h1;
    struct sp1_drums_status st; sp1_drums_status(&st);
    p.drums.rest_clear = 0x0Fu; p.drums.rest_clear_seq++;          // FFWD tap
    bars(1);                                                        // waits for the bar line
    const uint32_t h2 = hits(); bars(1); const uint32_t back = hits() - h2;
    const bool ok = played > 4 && rested == 0 && st.rests[0] == 0xFFFFu && back > 4;
    printf("rests  a bar plays %u hits; fully rested, 2 bars play %u (masks %04x %04x %04x %04x); cleared, a bar plays %u  %s\n",
           played, rested, st.rests[0], st.rests[1], st.rests[2], st.rests[3], back, ok ? "ok" : "FAIL");
    failures += !ok;

    // ---- rotation: one tap lands on the next beat, four come home ----
    p.drums.rotate_seq++;
    for (int i = 0; i < 260; ++i) { sp1_drums_frame f = clk.next(false); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24); }
    sp1_drums_status(&st);
    const uint8_t r1 = st.rotation;
    p.drums.rotate_seq += 3;
    for (int i = 0; i < 260; ++i) { sp1_drums_frame f = clk.next(false); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24); }
    sp1_drums_status(&st);
    const bool rok = r1 == 1 && st.rotation == 0;
    printf("rotate one tap -> r=%u, three more -> r=%u  %s\n", r1, st.rotation, rok ? "ok" : "FAIL");
    failures += !rok;

    // ---- tape slow: the machine at x0.33 renders fewer samples and stays finite ----
    p.drums.speed = 0.33f;
    int bad = 0; double e = 0;
    for (int i = 0; i < 2000; ++i) { sp1_drums_frame f = clk.next(false); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
      for (float v : out) { if (!isfinite(v) || fabsf(v) > 44000.f) ++bad; e += (double)v * v; } }
    const bool tok = bad == 0 && e > 0;
    printf("tape   x0.33 for 1 s: %s, %s  %s\n", bad ? "bad samples" : "finite", e > 0 ? "sound" : "silent", tok ? "ok" : "FAIL");
    failures += !tok;
  }
  printf(failures ? "%d check(s) FAILED\n" : "all checks ok\n", failures);
  return failures != 0;
}
