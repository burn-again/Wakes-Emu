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
#include "sp1_acid.h"

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

  // ---- the CPU guard: under load it fades a quiet tail, never a loud hit; at normal load nothing ----
  {
    sp1_synth_params p; fresh(p, 2, 0x0F);           // modal gamelan, long tails
    for (int v = 0; v < 4; ++v) p.drums.decay[v] = 1.0f;
    p.drums.fill = 0.5f;
    Clock c(5u);
    run(c, p, kBar);
    const uint32_t s0 = st().shed;
    for (int i = 0; i < kBar; ++i) {                  // 60 % of the budget: no shedding
      sp1_drums_load((uint32_t)(0.6f * 24 * 1333.33f), 24u);
      sp1_drums_frame f = c.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
    }
    const uint32_t s1 = st().shed;
    // Overloaded, with the clock stopped so nothing is struck again: the tails fall under -20 dB
    // and are faded one by one, after which nothing is left to shed.
    Clock stopped(1u); stopped.running = false;
    for (int i = 0; i < 48000 * 8 / 24; ++i) {
      sp1_drums_load((uint32_t)(0.95f * 24 * 1333.33f), 24u);
      sp1_drums_frame f = stopped.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
    }
    const uint32_t s2 = st().shed;
    char what[160];
    snprintf(what, sizeof what, "CPU guard: 60 %% load sheds %u; 95 %% load sheds %u ringing tails (1..4)", s1 - s0, s2 - s1);
    check(s1 == s0 && s2 - s1 >= 1u && s2 - s1 <= 4u, what);
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

  // ---- the acid line ----
  {
    sp1_synth_params p;
    char what[200];
    // The controls as the SETTINGS page would send them for pattern i.
    auto acid = [&p](int i) {
      const struct sp1_acid_preset* ap = sp1_acid_preset(i);
      p.drums.bass_mode = (uint8_t)(SP1_BASS_ACID + i);
      p.drums.acid_chr = ap->chr;
      for (int k = 0; k < SP1_ACID_NPARAM; ++k) p.drums.acid_p[k] = ap->p[k];
      p.drums.acid_gate = ap->gate; p.drums.acid_level = 0.6f; p.drums.acid_duck = 0.0f; p.drums.acid_transpose = 0.0f;
      for (int l = 0; l < 4; ++l) { p.drums.acid_dens[l] = 0.5f; p.drums.acid_len[l] = ap->len[l]; }
    };
    // acid-home: 16 steps, rests on steps 5 and 10, no ties -> 14 notes a bar
    fresh(p, 0, 0x0); acid(0);
    { Clock c(5u); run(c, p, kBar); uint32_t b0 = st().bass_notes; run(c, p, 2 * kBar); uint32_t n = st().bass_notes - b0;
      snprintf(what, sizeof what, "acid-home with the kit muted: 2 bars play %u notes (want 28)", n); check(n == 28, what);
      double sq = 0; for (int i = 0; i < kBar; ++i) { sp1_drums_frame f = c.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
        for (int k = 0; k < 24; ++k) sq += (double)out[k] * out[k]; }
      const double db = 20 * log10(sqrt(sq / (kBar * 24.0)) / 32768.0 + 1e-12);
      snprintf(what, sizeof what, "it is heard: %.1f dBFS rms over a bar", db); check(db > -40.0 && db < -6.0, what);
      p.drums.acid_dens[0] = 0.0f; run(c, p, kBar); b0 = st().bass_notes; run(c, p, 2 * kBar); n = st().bass_notes - b0;
      snprintf(what, sizeof what, "gate density 0: %u notes (want 0)", n); check(n == 0, what);
      // campfire: 8 notes and 8 ties a bar
      acid(1); run(c, p, kBar); b0 = st().bass_notes; run(c, p, 2 * kBar); n = st().bass_notes - b0;
      snprintf(what, sizeof what, "campfire: 2 bars play %u notes, the ties none (want 16)", n); check(n == 16, what);
      // fold-garden: 12 steps, 8 notes a loop -> 4 bars (64 steps) hold 5 loops and 4 steps (3 notes): 43
      acid(6); c = Clock(5u); c.running = false; run(c, p, 8); c.running = true; b0 = st().bass_notes; run(c, p, 4 * kBar);
      n = st().bass_notes - b0;
      snprintf(what, sizeof what, "fold-garden, 12 steps against the bar: 4 bars play %u notes (want 43)", n); check(n == 43, what);
      // the bass's own modes leave the acid voice alone, and the other way round
      p.drums.bass_mode = SP1_BASS_OFF; run(c, p, kBar); b0 = st().bass_notes; run(c, p, kBar);
      check(st().bass_notes == b0, "bass off again: the acid line stops");
    }
    // the kick's duck reaches the voice: the same bar with and without it differs
    { double diff = 0; static float ref[16 * 64 * 24];
      for (int pass = 0; pass < 2; ++pass) {
        sp1_drums_init(); fresh(p, 0, 0x1); acid(0); p.drums.acid_duck = pass ? 2.0f : 0.0f;
        Clock c(5u);
        for (int i = 0; i < kBar; ++i) { sp1_drums_frame f = c.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
          for (int k = 0; k < 24; ++k) { if (pass) diff += fabs(out[k] - ref[i * 24 + k]); else ref[i * 24 + k] = out[k]; } }
      }
      snprintf(what, sizeof what, "the kick ducks the cutoff: the bar differs by %.0f in all", diff); check(diff > 1000.0, what);
    }
    // ---- recorded fader moves: the last bar becomes a take that loops under the hand ----
    { sp1_drums_init(); fresh(p, 0, 0x0); acid(0);
      Clock c(5u);
      auto bar = [&](float from, float to, float at) {      // one bar, the cutoff fader moving; -> its value `at` of the way through
        float seen = -1.0f;
        for (int i = 0; i < kBar; ++i) {
          p.drums.acid_p[0] = from + (to - from) * (float)i / (float)kBar;
          sp1_drums_frame f = c.next(); sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
          if (i == (int)(at * kBar)) seen = st().acid_now[0];
        }
        return seen;
      };
      bar(0.3f, 0.3f, 0.5f);
      bar(0.2f, 0.8f, 0.5f);                                 // the hand sweeps up over a bar...
      p.drums.acid_p[0] = 0.8f; p.drums.acid_keep_seq++;     // ...rests at the top, and keeps it
      const float lo = bar(0.8f, 0.8f, 0.125f), hi = bar(0.8f, 0.8f, 0.875f);
      snprintf(what, sizeof what, "keep: the sweep loops with the fader still -- %.3f an eighth in, %.3f near the end (want 0.275, 0.725), %d take",
               lo, hi, st().acid_takes);
      check(fabsf(lo - 0.275f) < 0.03f && fabsf(hi - 0.725f) < 0.03f && st().acid_takes == 1, what);
      const float under = bar(0.5f, 0.5f, 0.875f);
      snprintf(what, sizeof what, "the take rides under the hand: fader at 0.5 -> %.3f near the end (want 0.425)", under);
      check(fabsf(under - 0.425f) < 0.03f, what);
      p.drums.acid_undo_seq++;
      const float flat = bar(0.5f, 0.5f, 0.125f);
      check(flat == 0.5f && st().acid_takes == 0, "undo: the take is gone, the fader alone again");
      for (int k = 0; k < 6; ++k) { p.drums.acid_keep_seq++; run(c, p, 4); }
      check(st().acid_takes == 4, "six keeps: four takes, no more");
      p.drums.acid_snap_to = 1; p.drums.acid_snap_seq++; run(c, p, 4);
      check(st().acid_takes == 1, "snap back to one take");
      acid(2); run(c, p, 4);
      check(st().acid_takes == 0, "another pattern: its takes go");
      c.running = false; run(c, p, 8); p.drums.acid_keep_seq++; run(c, p, 4);
      check(st().acid_takes == 0, "stopped: there is no bar to keep");
    }
    // ---- for the ears: each pattern over a kick and a hat at its own tempo. `script` is the
    // hands: called every block with the position in bars, it moves the controls. ----
    int r_kit = 4; uint8_t r_solo = 0x5; float r_duck = 0.5f;
    auto render = [&](const char* name, int i, int bars, void (*script)(float, sp1_drums_ctl&), int want_takes) {
      const struct sp1_acid_preset* ap = sp1_acid_preset(i);
      sp1_drums_init();
      fresh(p, r_kit, r_solo);                            // by default dub techno's kick and tambourine
      acid(i); p.drums.acid_duck = r_duck;
      p.drums.fx_on = 0x0F; for (int k = 0; k < 4; ++k) p.drums.fx[k] = ap->fx[k];
      p.drums.fm_hz = 196.0f;
      const unsigned per_beat = (unsigned)(48000.0f * 60.0f / ap->bpm / 24.0f + 0.5f);   // blocks
      const unsigned blocks = per_beat * 4u * (unsigned)bars;
      static int16_t pcm[48000 * 40];
      int peak = 0, clipped = 0, nans = 0; double sumsq = 0; uint8_t prev = 0;
      const uint32_t n0 = st().bass_notes;
      float volts[4] = {0, 0, 0, 0};
      for (unsigned b = 0; b < blocks && (b + 1) * 24 <= sizeof pcm / 2; ++b) {
        script((float)b / (float)(per_beat * 4u), p.drums);
        const unsigned phase = b % per_beat;
        const uint8_t gates = phase < per_beat / 4 ? 3u : 0u;
        sp1_drums_frame f; f.rise = gates & (uint8_t)~prev; prev = gates; f.volts = volts;
        f.ramp = (float)phase / (float)per_beat; f.running = true; f.audition = false;
        sp1_drums_begin(&p); sp1_drums_render(&f, out, 24);
        for (unsigned k = 0; k < 24; ++k) {
          if (!isfinite(out[k])) { ++nans; out[k] = 0; }
          int v = (int)out[k]; if (v > 32767) { v = 32767; ++clipped; } if (v < -32768) { v = -32768; ++clipped; }
          pcm[b * 24 + k] = (int16_t)v; sumsq += (double)v * v; if (abs(v) > peak) peak = abs(v);
        }
      }
      snprintf(what, sizeof what, "%-22s %-13s %3.0f bpm %2d bars %3u notes  peak %5d  rms %6.1f dBFS  clipped %d  nan %d  takes %d",
               name, ap->name, ap->bpm, bars, st().bass_notes - n0, peak,
               20 * log10(sqrt(sumsq / (blocks * 24.0)) / 32768.0 + 1e-12), clipped, nans, st().acid_takes);
      check(nans == 0 && clipped == 0 && peak > 3000 && st().bass_notes - n0 > 20 && st().acid_takes == want_takes, what);
      char path[256]; snprintf(path, sizeof path, "%s/%s.wav", dir, name);
      wav(path, pcm, blocks * 24);
    };
    // The patterns as written for four bars, then the cutoff up a little and every lane at 1.
    for (int i = 0; i < 8; ++i) {
      char name[64]; snprintf(name, sizeof name, "acid%d_%s", i + 1, sp1_acid_preset(i)->name);
      static float base; base = sp1_acid_preset(i)->p[0];
      render(name, i, 8, [](float bar, sp1_drums_ctl& d) {
        if (bar >= 4.0f) { d.acid_p[0] = base + 0.12f; for (int l = 0; l < 4; ++l) d.acid_dens[l] = 1.0f; }
      }, 0);
    }
    // ---- the faders moving: four ways to use keep, undo and snap back ----
    // The scripts do what the SETTINGS page does: move cutoff [0], resonance [1], decay [3],
    // drive [5], and count up the keep / undo / snap counters once at a bar line.
    #define ONCE(at, what_) do { static int done_; if (bar < 0.01f) done_ = 0; if (bar >= (at) && !done_) { done_ = 1; what_; } } while (0)
    // 1. acid-home. A sweep you play once and keep; a second take on top; undo it; snap back.
    render("jam1_sweep-and-keep", 0, 12, [](float bar, sp1_drums_ctl& d) {
      const float in = bar - floorf(bar);
      if (bar >= 2.0f && bar < 3.0f) d.acid_p[0] = 0.20f + 0.55f * in;          // bar 3: the hand sweeps the cutoff up
      ONCE(3.0f, { d.acid_p[0] = 0.75f; d.acid_keep_seq++; });                   // keep: it loops, hands off (bars 4-6)
      if (bar >= 6.0f && bar < 7.0f) d.acid_p[1] = ((int)(in * 8.0f) & 1) ? 0.97f : 0.60f;   // bar 7: resonance flicked on the off-8ths
      ONCE(7.0f, { d.acid_p[1] = 0.60f; d.acid_keep_seq++; });                   // keep: two takes loop (bars 8-9)
      ONCE(9.0f, d.acid_undo_seq++);                                             // undo: the resonance take goes (bar 10)
      ONCE(10.0f, { d.acid_p[0] = 0.32f; d.acid_p[1] = 0.75f; d.acid_snap_to = 0; d.acid_snap_seq++; });   // snap back: home (bars 11-12)
    }, 0);
    // 2. octave-hopper. Filter stabs kept as a rhythm, then the hand lifts the whole thing:
    //    the take rides under the fader, so one hand plays the level and the loop plays the shape.
    render("jam2_stabs-under-hand", 3, 10, [](float bar, sp1_drums_ctl& d) {
      const float in = bar - floorf(bar);
      if (bar >= 1.0f && bar < 2.0f) d.acid_p[0] = ((int)(in * 16.0f) % 4 == 2) ? 0.75f : 0.15f;   // bar 2: open on each off-beat 16th
      ONCE(2.0f, { d.acid_p[0] = 0.15f; d.acid_keep_seq++; });                   // keep, the fader left low (bars 3-4)
      if (bar >= 4.0f && bar < 8.0f) d.acid_p[0] = 0.15f + 0.40f * (bar - 4.0f) / 4.0f;   // bars 5-8: the hand rises slowly under the stabs
      ONCE(8.0f, { d.acid_p[0] = 0.40f; d.acid_snap_to = 0; d.acid_snap_seq++; });   // snap back to the pattern's own cutoff (bars 9-10)
    }, 0);
    // 3. campfire. A slow breath on cutoff and decay over one bar, kept: the pad-like line swells
    //    the same way every bar while you do something else.
    render("jam3_breathing", 1, 8, [](float bar, sp1_drums_ctl& d) {
      const float in = bar - floorf(bar);
      if (bar >= 2.0f && bar < 3.0f) {
        const float s = sinf(3.14159265f * in);                                  // up to the middle of the bar and back
        d.acid_p[0] = 0.30f + 0.30f * s;
        d.acid_p[3] = 0.75f - 0.40f * s;                                         // shorter decay as it opens
      }
      ONCE(3.0f, { d.acid_p[0] = 0.30f; d.acid_p[3] = 0.75f; d.acid_keep_seq++; });   // keep; bars 4-8 breathe on their own
    }, 1);
    // 4. crawl. A build and a drop: keep a closing-filter bar, push the drive up by hand over four
    //    bars with the loop running, then snap back on the bar line -- everything drops to home at once.
    render("jam4_build-and-drop", 5, 12, [](float bar, sp1_drums_ctl& d) {
      const float in = bar - floorf(bar);
      if (bar >= 1.0f && bar < 2.0f) d.acid_p[0] = 0.55f - 0.33f * in;           // bar 2: the cutoff closes over the bar
      ONCE(2.0f, { d.acid_p[0] = 0.22f; d.acid_keep_seq++; });                   // keep (rests closed): each bar now opens and closes
      if (bar >= 4.0f && bar < 8.0f) {                                           // bars 5-8: the build, by hand
        d.acid_p[5] = 0.30f + 0.70f * (bar - 4.0f) / 4.0f;                       // drive up
        d.acid_p[1] = 0.65f + 0.30f * (bar - 4.0f) / 4.0f;                       // resonance up
        d.acid_p[0] = 0.22f + 0.25f * (bar - 4.0f) / 4.0f;                       // cutoff up under the loop
      }
      ONCE(8.0f, { d.acid_p[0] = 0.22f; d.acid_p[1] = 0.65f; d.acid_p[5] = 0.80f; d.acid_snap_to = 0; d.acid_snap_seq++; });   // the drop
    }, 0);
    // ---- nine more, by genre, each with its kit, a fader move kept as a take, then a lane ----
    // Ten bars: two as written; bar 3 the hand plays `shape` on one fader; keep at the bar line
    // and three bars loop hands-off; bars 7-8 one lane changes (its density or its loop length,
    // as the LANES page would); snap back at bar 9: the fader, the take and the lane are home.
    // Where a row names a bus FX, the hand also takes it from its start to `fxto` over bars 4-8
    // (KIT + SHIFT; FX moves are not recorded) and lets it go at the snap back.
    struct Genre { int i, kit; uint8_t solo; float duck; int param, shape; float lo, hi; int lane; float dens; int len; const char* does;
                   int fxi = -1; float fxto = 0.0f, swing = 0.0f; };
    static const Genre kGenres[] = {
      {  8, 1, 0xF, 0.3f, 0, 2, 0.20f, 0.50f, 0, -1.0f,  6, "cutoff wobble kept; then the gate lane 6 steps long: the sub's rhythm turns against the break" },
      {  9, 2, 0x5, 0.3f, 0, 2, 0.30f, 0.62f, 2,  0.0f, -1, "a breath on the cutoff kept; then slide density 0: the bends become plucked notes" },
      { 10, 0, 0x7, 0.6f, 0, 3, 0.20f, 0.62f, 0, -1.0f,  6, "filter stabs on the off-beats kept; then the gate lane 6 steps long: the 3 + 3 keeps rolling over the bar" },
      { 11, 2, 0x5, 0.4f, 1, 4, 0.40f, 0.85f, 3,  1.0f, -1, "resonance flicked on the 8ths kept; then octave density up: the weighted octave jump comes in" },
      { 12, 0, 0x5, 0.2f, 0, 0, 0.30f, 0.70f, 1, -1.0f,  3, "a rising cutoff kept, so every bar opens; then the accent lane 3 steps long: the riff's accents shift each beat" },
      { 13, 7, 0x1, 0.0f, 0, 2, 0.20f, 0.52f, 0, -1.0f, 12, "one slow swell kept (a bar is four seconds here); then the gate lane 12 steps long" },
      { 14, 3, 0x5, 0.4f, 1, 4, 0.55f, 0.95f, 2, -1.0f,  5, "resonance flicks kept; then the slide lane 5 steps long, on top of its 12-step lanes" },
      { 15, 0, 0x5, 1.5f, 0, 1, 0.30f, 0.72f, 1,  0.0f, -1, "a closing filter kept, with the kick ducking hard; then accent density 0: the line goes flat and even" },
      { 16, 4, 0x5, 1.0f, 0, 2, 0.15f, 0.78f, 0, -1.0f,  8, "the filter sweep kept; then the gate lane 8 steps long: the first half-bar's rhythm under all the notes" },
      { 17, 0, 0x5, 0.8f, 0, 0, 0.25f, 0.65f, 1,  1.0f, -1, "a rising cutoff kept; the DJ filter opened to a high-pass over bars 4-8; then accent density up", 0, 0.80f },
      { 18, 4, 0xF, 0.3f, 0, 2, 0.20f, 0.60f, 0, -1.0f, 12, "the stab's filter swell kept; the bus FM brought in over bars 4-8; then the gate lane 12 steps long", 3, 0.55f },
      { 19, 1, 0x7, 0.4f, 3, 4, 0.25f, 0.70f, 0, -1.0f,  6, "the decay flicked short and long on the 8ths, kept; swung; the wavefolder in over bars 4-8; then the gate lane 6 steps", 1, 0.35f, 0.7f },
      { 20, 0, 0x5, 0.0f, 5, 0, 0.30f, 1.00f, 2,  0.0f, -1, "the drive pushed up over a bar and kept, so every bar swells; Chebyshev in over bars 4-8; then slide density 0: the bends stop", 2, 0.50f },
      { 21, 5, 0x5, 0.5f, 0, 2, 0.35f, 0.75f, 3, -1.0f,  4, "the filter opened and closed, kept; the DJ filter down to a low-pass over bars 4-8; then the octave lane 4 steps long", 0, 0.25f },
      { 22, 2, 0x7, 0.4f, 1, 4, 0.30f, 0.80f, 0, -1.0f,  6, "resonance flicked on the 8ths kept; the wavefolder in; then the gate lane 6 steps long: the phrase turns over", 1, 0.25f, 0.4f },
      { 23, 3, 0x5, 0.6f, 0, 3, 0.25f, 0.75f, 1, -1.0f,  3, "filter stabs on the off-beats kept; Chebyshev in over bars 4-8; then the accent lane 3 steps long", 2, 0.45f },
      { 24, 1, 0xF, 0.2f, 5, 4, 0.30f, 0.90f, 0, -1.0f,  5, "the drive flicked on the 8ths kept; the bus FM in; then the gate lane 5 steps long: threes become fives", 3, 0.40f },
      { 25, 5, 0x5, 0.5f, 0, 1, 0.30f, 0.72f, 0, -1.0f,  8, "a closing filter kept; the DJ filter opened to a high-pass over bars 4-8; then the gate lane 8 steps long", 0, 0.75f },
      { 26, 3, 0x7, 0.9f, 1, 0, 0.45f, 0.98f, 1,  0.0f, -1, "resonance wound up over a bar and kept; the wavefolder in over bars 4-8; then accent density 0", 1, 0.45f },
    };
    static const Genre* G; static float g_rest; static float g_home[4]; static float g_fx0;
    for (const Genre& g : kGenres) {
      G = &g;
      const struct sp1_acid_preset* ap = sp1_acid_preset(g.i);
      g_rest = ap->p[g.param];
      g_home[0] = ap->len[0]; g_home[1] = ap->len[1]; g_home[2] = ap->len[2]; g_home[3] = ap->len[3];
      r_kit = g.kit; r_solo = g.solo; r_duck = g.duck;
      g_fx0 = g.fxi >= 0 ? ap->fx[g.fxi] : 0.0f;
      char name[64]; snprintf(name, sizeof name, "genre_%s", ap->name);
      render(name, g.i, 10, [](float bar, sp1_drums_ctl& d) {
        const float in = bar - floorf(bar);
        if (bar >= 2.0f && bar < 3.0f) {
          float u = 0.0f;                                                      // 0..1: where the fader is between lo and hi
          switch (G->shape) {
            case 0: u = in; break;                                             // a ramp up
            case 1: u = 1.0f - in; break;                                      // a ramp down
            case 2: u = sinf(3.14159265f * in); break;                         // up and back
            case 3: u = ((int)(in * 16.0f) % 4 == 2) ? 1.0f : 0.0f; break;     // open on the off-beat 16ths
            default: u = ((int)(in * 8.0f) & 1) ? 1.0f : 0.0f; break;          // flicked on the off 8ths
          }
          d.acid_p[G->param] = G->lo + (G->hi - G->lo) * u;
        }
        d.swing = G->swing;
        if (G->fxi >= 0 && bar >= 3.0f && bar < 8.0f) d.fx[G->fxi] = g_fx0 + (G->fxto - g_fx0) * (bar - 3.0f) / 5.0f;
        if (G->fxi >= 0 && bar >= 8.0f) d.fx[G->fxi] = g_fx0;
        ONCE(3.0f, { d.acid_p[G->param] = g_rest; d.acid_keep_seq++; });
        ONCE(6.0f, { if (G->dens >= 0.0f) d.acid_dens[G->lane] = G->dens; if (G->len >= 0) d.acid_len[G->lane] = (uint8_t)G->len; });
        ONCE(8.0f, { d.acid_dens[G->lane] = 0.5f; d.acid_len[G->lane] = (uint8_t)g_home[G->lane]; d.acid_snap_to = 0; d.acid_snap_seq++; });
      }, 0);
      printf("     %s: %s\n", ap->name, g.does);
    }
    #undef ONCE
    // ---- the preset's word and drums.cnfg's [acid] ----
    { struct sp1_drums_cfg c; struct sp1_drums_cfg_reader r;
      const char* t = "[bass]\nmode = crawl\n[acid]\nlevel = -9\nduck = 1.5\ntranspose = -12\n"
                      "line = E1 . E2~ _\nline = E1 nope\nwhat = 1\n[bass]\nmode = french-house\n";
      sp1_drums_cfg_begin(&r, &c, nullptr, nullptr); sp1_drums_cfg_feed(&r, t, strlen(t));
      const int bad = sp1_drums_cfg_end(&r);
      snprintf(what, sizeof what, "drums.cnfg [acid]: %d bad lines (want 2), the good ones read, the user line is the first", bad);
      check(bad == 2 && c.bass_mode == SP1_BASS_ACID + 16 && c.acid_level_db == -9.0f && c.acid_duck == 1.5f &&
            c.acid_transpose == -12.0f && sp1_acid_pattern_of(SP1_ACID_BUILTIN)->n == 4, what);
      check(strcmp(sp1_drums_bass_name(SP1_BASS_ACID + 1), "campfire") == 0 && strcmp(sp1_drums_bass_name(2), "rests") == 0 &&
            strcmp(sp1_drums_bass_name(200), "off") == 0, "a bass mode's word: rests, campfire, and off for nonsense");
    }
  }

  printf(failures ? "%d check(s) FAILED\n" : "all checks ok\n", failures);
  return failures != 0;
}
