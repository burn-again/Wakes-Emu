// Host test for the drum firmware: the kit (sp1_drums.cc), its pages (sp1_drums_ui.c) and its
// preset / config text (sp1_drums_patch.c). Drives the kit exactly as sp1_synth_render does --
// 24-sample Plaits blocks, a Marbles-like gate stream with its master ramp, X/Y voltages --
// then checks: every kit renders cleanly (one WAV each); the sequencer plays exactly what its
// maps, density, fill, morph and loop length say; a t cable takes a slot over; the bass plays
// on the Y clock and in full-kit rests; rests, rotation and tape slow; the [drums] preset
// section round-trips and survives damage; drums.cnfg reads its own template clean.
// Build: see README.md.  Run: ./drumtest <out dir>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sp1_drums.h"
#include "sp1_drums_ui.h"
#include "sp1_drums_patch.h"

// The kit's step-through talks to Marbles' clock (sp1_marbles.h); there is no Marbles here.
extern "C" void sp1_marbles_clock(const float*, int) {}
extern "C" bool sp1_marbles_running(void) { return false; }

static void wav(const char* path, const int16_t* x, unsigned n) {
  FILE* f = fopen(path, "wb"); if (!f) return;
  unsigned bytes = n * 2, sr = 48000, br = 96000, v; unsigned short s;
  fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(x, 2, n, f); fclose(f);
}

// A Marbles-like clock: one t2 tick (a beat) every 256 blocks, so a 16th is 64 blocks; 25 %
// gates; t1 on every beat, t3 on beats 2 and 4; fresh X/Y on each tick.
struct Clock {
  unsigned seed, b = 0; uint8_t prev = 0; float volts[4] = {0, 0, 0, 0}; bool running = true;
  explicit Clock(unsigned s) : seed(s) {}
  sp1_drums_frame next(bool audition = false) {
    const unsigned beat = b / 256, phase = b % 256;
    if (phase == 0) {
      for (int k = 0; k < 4; ++k) { seed = seed * 1103515245u + 12345u; volts[k] = ((seed >> 16) % 1000) / 100.0f - 5.0f; }
    }
    const bool tick = phase < 64;
    const bool t1 = true, t3 = (beat % 2) == 1;
    const uint8_t gates = running ? (uint8_t)((t1 && tick ? 1 : 0) | (tick ? 2 : 0) | (t3 && tick ? 4 : 0)) : 0;
    sp1_drums_frame f;
    f.rise = gates & (uint8_t)~prev; prev = gates;
    f.volts = running ? volts : nullptr;
    f.ramp = running ? (float)phase / 256.0f : 0.0f;
    f.running = running;
    f.audition = audition;
    ++b;
    return f;
  }
};

static struct sp1_drums_status st() { struct sp1_drums_status s; sp1_drums_status(&s); return s; }
static float out[24];
static void run(Clock& c, sp1_synth_params& p, int blocks) {
  for (int i = 0; i < blocks; ++i) { sp1_drums_frame f = c.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24); }
}
static const int kBar = 16 * 64;     // blocks per bar

static int failures = 0;
static void check(bool ok, const char* what) { printf("%-4s %s\n", ok ? "ok" : "FAIL", what); failures += !ok; }

// A fresh kit: controls as the pages make them, Marbles quiet (no jitter), only `solo` heard, and
// no rests left over from an earlier check (cleared at once, the clock stopped).
static void fresh(sp1_synth_params& p, int kit, uint8_t solo) {
  static uint32_t clear_seq;
  memset(&p, 0, sizeof p);
  p.drums.rest_clear = 0x0Fu;
  p.drums.rest_clear_seq = ++clear_seq;
  { Clock stopped(1u); stopped.running = false; sp1_drums_frame f = stopped.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24); }
  sp1_dui_init();
  sp1_dui_load_kit(kit);
  sp1_dui_ctl(&p.drums);
  p.drums.mute = (uint8_t)(~solo & 0x0Fu);
  p.drums.rest_clear_seq = clear_seq;
  p.drums.jitter = 0.0f;
  p.drums.deja = 0.5f;
}

// Hits on the solo slot over `bars` bars, after one bar to find the beat.
static uint32_t bars_of_hits(sp1_synth_params& p, int bars) {
  Clock c(7u);
  run(c, p, kBar);
  const uint32_t h0 = st().hits;
  run(c, p, bars * kBar);
  return st().hits - h0;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  sp1_drums_init();
  sp1_dui_init();

  // ---- every kit renders cleanly through the sequencer ----
  {
    const unsigned kBlocks = 48000 * 6 / 24;
    static int16_t pcm[48000 * 6];
    for (int kit = 0; kit < SP1_DRUMS_KITS; ++kit) {
      sp1_synth_params p; memset(&p, 0, sizeof p);
      sp1_dui_load_kit(kit);
      sp1_dui_ctl(&p.drums);
      p.drums.jitter = 0.3f; p.drums.deja = 0.3f;
      Clock clk(1234u + kit);
      double sumsq = 0; int peak = 0, clipped = 0, nans = 0;
      for (unsigned b = 0; b < kBlocks; ++b) {
        if (b == kBlocks / 2) { p.drums.fx[1] = 0.4f; p.drums.fx[0] = 0.25f; p.drums.morph = 1.0f; p.drums.fill = 0.3f; }
        sp1_drums_frame f = clk.next();
        sp1_drums_begin(&p);
        sp1_drums_render(&f, out, 24);
        for (unsigned i = 0; i < 24; ++i) {
          if (!isfinite(out[i])) { ++nans; out[i] = 0; }
          int v = (int)out[i]; if (v > 32767) { v = 32767; ++clipped; } if (v < -32768) { v = -32768; ++clipped; }
          pcm[b * 24 + i] = (int16_t)v; sumsq += (double)v * v; if (abs(v) > peak) peak = abs(v);
        }
      }
      const double rms_db = 20 * log10(sqrt(sumsq / (kBlocks * 24.0)) / 32768.0 + 1e-12);
      char what[160];
      snprintf(what, sizeof what, "kit %d %-14s peak %5d  rms %6.1f dBFS  clipped %d  nan %d", kit,
               sp1_drums_kit_name(kit), peak, rms_db, clipped, nans);
      check(nans == 0 && clipped == 0 && peak > 3000, what);
      char path[256]; snprintf(path, sizeof path, "%s/kit%d_%s.wav", dir, kit, sp1_drums_kit_name(kit));
      for (char* c = path + strlen(dir); *c; ++c) if (*c == ' ') *c = '_';
      wav(path, pcm, kBlocks * 24);
    }
  }

  // ---- the sequencer plays what the maps say ----
  {
    sp1_synth_params p;
    char what[160];
    // lich techno, slot 1: A "9...9...9...9..3", B "9...9..29...9.3."
    fresh(p, 0, 0x1); uint32_t h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "density 0.5: map A's weights 5+ only -- 2 bars play %u (want 8)", h); check(h == 8, what);
    fresh(p, 0, 0x1); p.drums.fill = 0.5f; h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "fill +0.5: every weight -- 2 bars play %u (want 10)", h); check(h == 10, what);
    fresh(p, 0, 0x1); p.drums.morph = 1.0f; p.drums.density[0] = 0.8f; h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "morph to B, density 0.8 (weights 2+) -- 2 bars play %u (want 12)", h); check(h == 12, what);
    fresh(p, 0, 0x1); p.drums.density[0] = 0.0f; p.drums.fill = 0.0f; h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "density 0: nothing -- 2 bars play %u (want 0)", h); check(h == 0, what);
    // 8-bit breaks, slot 1: "9.....7...8..3.." -> 3 a bar at 16 steps, "9.....7." -> 2 per 8 = 4 a bar at 8
    fresh(p, 1, 0x1); h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "16-step loop: 2 bars play %u (want 6)", h); check(h == 6, what);
    fresh(p, 1, 0x1); p.drums.len[0] = 8; h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "8-step loop over a 16-step map: 2 bars play %u (want 8)", h); check(h == 8, what);
    fresh(p, 1, 0x1); p.drums.swing = 1.0f; h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "swing keeps every hit: 2 bars play %u (want 6)", h); check(h == 6, what);
    // a t cable takes the slot: t1 (every beat here) -> slot 1 = 4 a bar, not the map's
    fresh(p, 1, 0x1); p.drums.t_dest[0] = SP1_DT_SLOT1; h = bars_of_hits(p, 2);
    snprintf(what, sizeof what, "t1 cabled to slot 1: it plays t1's gates -- 2 bars play %u (want 8)", h); check(h == 8, what);
    // the pages' controls reach the kit: density on DRUM n + SHIFT F1, length on F3
    sp1_dui_init(); sp1_dui_load_kit(1); sp1_dui_ctl(&p.drums);
    check(p.drums.len[3] == 32 && p.drums.len[0] == 16 && fabsf(p.drums.density[0] - 0.5f) < 1e-6f,
          "the pages: lengths from the maps (16, ..., 32), density centred");
  }

  // ---- the bass ----
  {
    sp1_synth_params p;
    char what[160];
    fresh(p, 0, 0x0); p.drums.bass_mode = SP1_BASS_Y; p.drums.y_ticks = 1; p.drums.bass_level = 0.5f;
    p.drums.n_deg = 5; const float pent[5] = {0, 2, 4, 7, 9}; memcpy(p.drums.degrees, pent, sizeof pent);
    p.drums.period = 12.0f; p.drums.bass_octaves = 2; p.drums.bass_root = 36.0f;
    Clock c(3u); run(c, p, kBar); uint32_t b0 = st().bass_notes; run(c, p, 2 * kBar);
    uint32_t n = st().bass_notes - b0;
    snprintf(what, sizeof what, "bass on the Y clock (every tick): 2 bars play %u notes (want 8)", n); check(n == 8, what);
    p.drums.bass_mode = SP1_BASS_OFF; b0 = st().bass_notes; run(c, p, 2 * kBar);
    n = st().bass_notes - b0;
    snprintf(what, sizeof what, "bass off: %u notes (want 0)", n); check(n == 0, what);
    p.drums.bass_mode = SP1_BASS_RESTS; p.drums.rest_write = 0x0Fu; run(c, p, kBar); p.drums.rest_write = 0u;
    b0 = st().bass_notes; run(c, p, kBar); n = st().bass_notes - b0;
    snprintf(what, sizeof what, "bass in the rests: a fully rested bar plays %u 8th notes (want 8)", n); check(n == 8, what);
  }

  // ---- rests, rotation, tape slow ----
  {
    sp1_synth_params p; fresh(p, 0, 0x0F);
    p.drums.fill = 0.5f;
    Clock c(99u);
    run(c, p, kBar);
    const uint32_t h0 = st().hits; run(c, p, kBar); const uint32_t played = st().hits - h0;
    p.drums.rest_write = 0x0Fu; run(c, p, kBar); p.drums.rest_write = 0u;
    const uint32_t h1 = st().hits; run(c, p, 2 * kBar); const uint32_t rested = st().hits - h1;
    p.drums.rest_clear = 0x0Fu; p.drums.rest_clear_seq++;
    run(c, p, kBar);
    const uint32_t h2 = st().hits; run(c, p, kBar); const uint32_t back = st().hits - h2;
    char what[160];
    snprintf(what, sizeof what, "rests: a bar plays %u; fully rested, 2 bars play %u; cleared, a bar plays %u", played, rested, back);
    check(played > 8 && rested == 0 && back == played, what);
    p.drums.rotate_q = 1; p.drums.rotate_seq++; run(c, p, 300);
    const uint8_t r1 = st().rotation; p.drums.rotate_seq += 3; run(c, p, 300);
    snprintf(what, sizeof what, "rotate: one tap -> r=%u, three more -> r=%u", r1, st().rotation);
    check(r1 == 1 && st().rotation == 0, what);
    p.drums.speed = 0.33f; int bad = 0; double e = 0;
    for (int i = 0; i < 2000; ++i) { sp1_drums_frame f = c.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
      for (float v : out) { if (!isfinite(v) || fabsf(v) > 44000.f) ++bad; e += (double)v * v; } }
    check(bad == 0 && e > 0, "tape slow at x0.33: finite and audible");
  }

  // ---- the [drums] section of a preset ----
  {
    struct sp1_drums_patch a, b;
    sp1_drums_patch_default(&a, 3);
    a.present = true; a.mute = 5; a.fx_on = 9; a.bass_mode = 2; a.rest_choke = 0;
    a.pat[2] = 6; a.t_dest[0] = SP1_DT_SLOT2; a.t_dest[3] = SP1_DT_BASS; a.x_dest[1] = SP1_DX_FILTER;
    a.x_dest[3] = SP1_DX_BASS_NOTE; a.eng[1] = 9; a.model[1] = 10; a.v[5][2] = 0.1234f; a.gain[0] = 1.25f;
    static char text[4096];
    const char* head = "[wakes]\nformat = 2\n[plaits]\nengine = virtual analog\n";
    strcpy(text, head);
    const int n = sp1_drums_patch_write(&a, text + strlen(head), sizeof text - strlen(head));
    sp1_drums_patch_default(&b, 0);
    const int bad = sp1_drums_patch_parse(text, strlen(text), &b);
    bool same = b.present && bad == 0 && b.kit == a.kit && b.mute == a.mute && b.fx_on == a.fx_on &&
                b.bass_mode == a.bass_mode && b.rest_choke == a.rest_choke &&
                memcmp(b.pat, a.pat, 4) == 0 && memcmp(b.t_dest, a.t_dest, 4) == 0 &&
                memcmp(b.x_dest, a.x_dest, 4) == 0 && memcmp(b.eng, a.eng, 4) == 0 &&
                memcmp(b.model, a.model, 4) == 0 && fabsf(b.gain[0] - 1.25f) < 1e-4f;
    for (int l = 0; l < SP1_DRUMS_LAYERS; ++l) for (int i = 0; i < 4; ++i) same = same && fabsf(a.v[l][i] - b.v[l][i]) < 1e-4f;
    char what[160];
    snprintf(what, sizeof what, "preset: [drums] written (%d B) and read back field for field", n);
    check(n > 0 && n < 1600 && same, what);
    // a stock slot: no [drums]
    sp1_drums_patch_default(&b, 0);
    sp1_drums_patch_parse(head, strlen(head), &b);
    check(!b.present, "preset: a slot with no [drums] says so (the drums stay as they are)");
    // damage: each bad field keeps its default, the rest still load
    const char* dmg = "[drums]\nkit = 9\nvoices = 9.0 9.99 7.1 8.2\nkit_faders = 0.5 nan 0.5 0.5\nfx = 3\n"
                      "mute = 2.5\nrests = maybe\nbass = rests\npatterns = 1 2 3 0\nx_cables = 13 0 0 1\n"
                      "t_cables = 1 2 3 4\nsettings_shift = 0.1 0.2 0.3 0.4 0.5\n";
    sp1_drums_patch_default(&b, 2);
    const int bad2 = sp1_drums_patch_parse(dmg, strlen(dmg), &b);
    snprintf(what, sizeof what, "preset: damaged [drums] -- %d bad fields, the good ones load", bad2);
    check(bad2 == 8 && b.kit == 2 && b.fx_on == 3 && b.bass_mode == 2 && b.t_dest[3] == SP1_DT_SLOT4 &&
          b.rest_choke == 1 && b.pat[3] == 2 && b.x_dest[0] == 0, what);
    // fuzz: every truncation of a good section parses without trouble
    int crashes_none = 1;
    for (size_t cut = 0; cut <= strlen(text); ++cut) { sp1_drums_patch_default(&b, 0); sp1_drums_patch_parse(text, cut, &b);
      crashes_none = crashes_none && b.kit >= 0 && b.kit < SP1_DRUMS_KITS; }
    check(crashes_none, "preset: every truncation of the text reads safely");
  }

  // ---- drums.cnfg ----
  {
    struct sp1_drums_cfg c, d;
    struct sp1_drums_cfg_reader r;
    sp1_drums_cfg_defaults(&d);
    sp1_drums_cfg_begin(&r, &c, nullptr, nullptr);
    for (size_t i = 0; i < SP1_DRUMS_CNFG_TEMPLATE_LEN; i += 37)       // in odd-sized chunks
      sp1_drums_cfg_feed(&r, SP1_DRUMS_CNFG_TEMPLATE + i, SP1_DRUMS_CNFG_TEMPLATE_LEN - i < 37 ? SP1_DRUMS_CNFG_TEMPLATE_LEN - i : 37);
    const int bad = sp1_drums_cfg_end(&r);
    check(bad == 0 && memcmp(&c, &d, sizeof c) == 0, "drums.cnfg: the template reads clean and gives the defaults");
    const char* edited = "[drums]\nkit = 3\ntape_release = 0\nrotate_quantize = bar\nrest_style = ring\n"
                         "tape_floor = 2\nwhat = 1\n[bass]\nmode = y\nlevel = -6 ; louder\noctaves = 9\n[oops]\n";
    sp1_drums_cfg_begin(&r, &c, nullptr, nullptr);
    sp1_drums_cfg_feed(&r, edited, strlen(edited));
    const int bad2 = sp1_drums_cfg_end(&r);
    char what[160];
    snprintf(what, sizeof what, "drums.cnfg: edited -- %d bad lines, the good ones read", bad2);
    check(bad2 == 4 && c.kit == 2 && c.tape_release == 0.0f && c.rotate_q == 2 && c.rest_choke == 0 &&
          c.bass_mode == 1 && c.bass_level_db == -6.0f && c.tape_floor == d.tape_floor && c.bass_octaves == 2, what);
  }

  printf(failures ? "%d check(s) FAILED\n" : "all checks ok\n", failures);
  return failures != 0;
}
