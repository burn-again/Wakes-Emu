/* acidtest.c -- tinyacid on the host: what makes it a 303 and a 101, and that nothing breaks it.
 *
 *   cc -O2 -Wall -I../../third_party/tinykit acidtest.c ../../third_party/tinykit/tinyacid.c -lm -o acidtest
 *   ./acidtest [dir]      a demo pattern per character is written to dir as WAV, if given
 */
#include "tinyacid.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000
#define BLK 24

static int fails = 0, checks = 0;
static void check(int ok, const char *what) {
  ++checks;
  if (!ok) { ++fails; printf("FAIL  %s\n", what); } else printf("ok    %s\n", what);
}

static float buf[SR * 40];

/* render `ms` into buf from `at`, in firmware-sized blocks; returns the new position */
static unsigned run(ta_voice *v, unsigned at, float ms) {
  unsigned n = (unsigned)(ms * SR / 1000.0f);
  while (n) {
    const unsigned m = n < BLK ? n : BLK;
    memset(buf + at, 0, m * sizeof(float));
    ta_render_mix(v, buf + at, m, 1.0f);
    at += m; n -= m;
  }
  return at;
}

static float rms(unsigned a, unsigned b) {
  double s = 0; for (unsigned i = a; i < b; ++i) s += (double)buf[i] * buf[i];
  return (float)sqrt(s / (double)(b - a));
}
static float peak(unsigned a, unsigned b) {
  float p = 0; for (unsigned i = a; i < b; ++i) if (fabsf(buf[i]) > p) p = fabsf(buf[i]);
  return p;
}
/* frequency from upward zero crossings, with the first and last crossing interpolated */
static float zc_hz(unsigned a, unsigned b) {
  double first = -1, last = -1; int n = 0;
  for (unsigned i = a + 1; i < b; ++i)
    if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) {
      const double x = (double)(i - 1) + (double)(-buf[i - 1]) / (double)(buf[i] - buf[i - 1]);
      if (first < 0) first = x;
      last = x; ++n;
    }
  return n > 1 ? (float)((n - 1) * (double)SR / (last - first)) : 0.0f;
}

static void fresh(ta_voice *v, unsigned chr) {
  ta_init(v); ta_character(v, chr);
}

static void wav(const char *dir, const char *name, unsigned n) {
  char path[512]; snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "wb"); if (!f) { printf("cannot write %s\n", path); return; }
  const unsigned bytes = n * 2u;
  const unsigned char h[44] = { 'R','I','F','F', (unsigned char)(36 + bytes), (unsigned char)((36 + bytes) >> 8),
    (unsigned char)((36 + bytes) >> 16), (unsigned char)((36 + bytes) >> 24), 'W','A','V','E','f','m','t',' ',
    16,0,0,0, 1,0, 1,0, 0x80,0xBB,0,0, 0x00,0x77,0x01,0, 2,0, 16,0, 'd','a','t','a',
    (unsigned char)bytes, (unsigned char)(bytes >> 8), (unsigned char)(bytes >> 16), (unsigned char)(bytes >> 24) };
  fwrite(h, 1, 44, f);
  for (unsigned i = 0; i < n; ++i) {
    float x = buf[i] * 0.8f; if (x > 1) x = 1; if (x < -1) x = -1;
    const short s = (short)(x * 32767.0f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

/* a 16-step line: note (0 = rest), accent, slide INTO this step */
typedef struct { int note, acc, slide; } step;
static const step LINE[16] = {
  {36,1,0},{36,0,0},{48,0,0},{36,0,1},{0,0,0},{39,1,0},{36,0,0},{43,0,1},
  {36,1,0},{0,0,0},{48,1,0},{46,0,1},{36,0,0},{36,0,0},{41,1,0},{39,0,1} };

/* play LINE for `bars` at `bpm`, calling tweak(bar position 0..1) each step */
static unsigned play(ta_voice *v, unsigned at, int bars, float bpm, void (*tweak)(ta_voice *, float)) {
  const float step_ms = 60000.0f / bpm / 4.0f;
  for (int s = 0; s < bars * 16; ++s) {
    const step *x = &LINE[s % 16], *nx = &LINE[(s + 1) % 16];
    if (tweak) tweak(v, (float)s / (float)(bars * 16));
    if (x->note) ta_note(v, (float)x->note, (x->acc ? TA_F_ACCENT : 0u) | (x->slide ? TA_F_SLIDE : 0u));
    if (x->note && nx->note && nx->slide) at = run(v, at, step_ms);          /* held into the slide */
    else { at = run(v, at, step_ms * 0.5f); ta_gate_off(v); at = run(v, at, step_ms * 0.5f); }
  }
  return at;
}
static void sweep(ta_voice *v, float t) {
  const float tri = t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f;
  ta_set(v, TA_CUTOFF, 0.15f + 0.55f * tri);
  ta_set(v, TA_RES, 0.55f + 0.4f * tri);
}

int main(int argc, char **argv) {
  ta_voice v; char what[200]; unsigned at;

  /* ---- at rest ---- */
  fresh(&v, TA_CHAR_303);
  at = run(&v, 0, 100);
  check(!ta_active(&v) && peak(0, at) == 0.0f, "a fresh voice is idle and silent");

  /* ---- the oscillator is in tune ---- */
  for (int chr = 0; chr < TA_NCHAR; ++chr) {
    fresh(&v, (unsigned)chr);
    ta_set(&v, TA_CUTOFF, 1); ta_set(&v, TA_RES, 0); ta_set(&v, TA_ENVMOD, 0); ta_set(&v, TA_HOLD, 1);
    ta_note(&v, 45.0f, 0); at = run(&v, 0, 2000);
    const float hz = zc_hz(SR / 2, at);
    snprintf(what, sizeof what, "%s: A2 plays %.3f Hz (want 110, within 0.5 cent)", chr ? "101" : "303", hz);
    check(fabsf(1200.0f * log2f(hz / 110.0f)) < 0.5f, what);
  }

  /* ---- the pulse carries next to no DC at any width (the little there is comes from the
     filter's saturator, which an uneven pulse drives unevenly) ---- */
  for (int i = 0; i < 3; ++i) {
    fresh(&v, TA_CHAR_101);
    ta_set(&v, TA_WAVE, 1); ta_set(&v, TA_PW, 0.5f * (float)i); ta_set(&v, TA_CUTOFF, 1); ta_set(&v, TA_RES, 0);
    ta_set(&v, TA_HOLD, 1); ta_note(&v, 45.0f, 0); at = run(&v, 0, 1000);
    double m = 0; for (unsigned j = SR / 2; j < at; ++j) m += buf[j];
    m /= (double)(at - SR / 2);
    snprintf(what, sizeof what, "pulse at PW %.1f: mean %.5f, rms %.3f", 0.5 * i, m, rms(SR / 2, at));
    check(fabs(m) < 0.03 && rms(SR / 2, at) > 0.05f, what);
  }

  /* ---- a slide does not retrigger, and glides ---- */
  fresh(&v, TA_CHAR_303);
  ta_note(&v, 36.0f, 0); at = run(&v, 0, 100);
  { const float e0 = v.fenv;
    ta_note(&v, 48.0f, TA_F_SLIDE);
    const float e1 = v.fenv; at = run(&v, at, 30);
    const float mid = v.note; at = run(&v, at, 600);
    snprintf(what, sizeof what, "slide: envelope %.3f -> %.3f (not retriggered), pitch %.2f at 30 ms, %.2f at the end",
             e0, e1, mid, v.note);
    check(e1 == e0 && mid > 37.0f && mid < 47.0f && v.note == 48.0f, what);
    ta_note(&v, 36.0f, 0);
    check(v.fenv == 1.0f && v.note == 36.0f, "a plain note retriggers the envelope and jumps in pitch");
    ta_gate_off(&v); at = run(&v, 0, 200); ta_note(&v, 40.0f, TA_F_SLIDE);
    check(v.fenv == 1.0f && v.note == 40.0f, "a slide after a rest is a plain note");
  }

  /* ---- accents in a row stack on the 303 only ---- */
  for (int chr = 0; chr < TA_NCHAR; ++chr) {
    float pk[3] = {0, 0, 0};
    fresh(&v, (unsigned)chr); ta_set(&v, TA_ACCENT, 1); at = 0;
    for (int i = 0; i < 3; ++i) {
      ta_note(&v, 36.0f, TA_F_ACCENT);
      for (int b = 0; b < 250; ++b) {                       /* 125 ms a step, gate off half way */
        if (b == 125) ta_gate_off(&v);
        at = run(&v, at, 0.5f); if (v.fc > pk[i]) pk[i] = v.fc;
      }
    }
    snprintf(what, sizeof what, "%s: three accents peak at %.0f, %.0f, %.0f Hz", chr ? "101" : "303", pk[0], pk[1], pk[2]);
    if (chr == TA_CHAR_303) check(pk[1] > pk[0] * 1.03f && pk[2] > pk[1] * 1.01f, what);
    else check(fabsf(pk[2] / pk[0] - 1.0f) < 0.01f, what);
  }

  /* ---- an accented 303 note's filter decay is short whatever DECAY says ---- */
  { float e[2];
    for (int a = 0; a < 2; ++a) {
      fresh(&v, TA_CHAR_303); ta_set(&v, TA_DECAY, 1);
      ta_note(&v, 36.0f, a ? TA_F_ACCENT : 0u); run(&v, 0, 300); e[a] = v.fenv;
    }
    snprintf(what, sizeof what, "303 at DECAY 1, 300 ms in: envelope %.3f plain, %.4f accented", e[0], e[1]);
    check(e[0] > 0.3f && e[1] < 0.01f, what);
  }

  /* ---- an accent is louder ---- */
  for (int chr = 0; chr < TA_NCHAR; ++chr) {
    float l[2];
    for (int a = 0; a < 2; ++a) {
      fresh(&v, (unsigned)chr); ta_set(&v, TA_ENVMOD, 0); ta_set(&v, TA_CUTOFF, 0.8f); ta_set(&v, TA_RES, 0);
      ta_set(&v, TA_HOLD, 1);
      ta_note(&v, 36.0f, a ? TA_F_ACCENT : 0u); at = run(&v, 0, 200); l[a] = rms(SR / 10, at);
    }
    snprintf(what, sizeof what, "%s: accent %.1f dB over a plain note", chr ? "101" : "303", 20.0f * log10f(l[1] / l[0]));
    check(l[1] > l[0] * 1.2f, what);
  }

  /* ---- the 101 self-oscillates in tune, the 303 does not ---- */
  { const float want[3] = {200.0f, 1000.0f, 5000.0f};
    for (int chr = 0; chr < TA_NCHAR; ++chr)
      for (int i = 0; i < 3; ++i) {
        fresh(&v, (unsigned)chr);
        ta_set(&v, TA_OSC, 0); ta_set(&v, TA_RES, 1); ta_set(&v, TA_ENVMOD, 0); ta_set(&v, TA_HOLD, 1);
        ta_set(&v, TA_CUTOFF, log2f(want[i] / 50.0f) / 6.5f); ta_set(&v, TA_NOISE, 0.3f);
        ta_note(&v, 48.0f, 0); at = run(&v, 0, 20); ta_set(&v, TA_NOISE, 0); at = run(&v, at, 1000);
        const float r = rms(at - SR / 5, at), hz = zc_hz(at - SR / 5, at);
        if (chr == TA_CHAR_101) {
          snprintf(what, sizeof what, "101, no oscillator, RES 1 at %.0f Hz: rings on at rms %.3f, %.1f Hz (%.0f cents)",
                   v.fc, r, hz, 1200.0f * log2f(hz / v.fc));
          check(r > 0.05f && fabsf(1200.0f * log2f(hz / v.fc)) < 50.0f, what);
        } else {
          snprintf(what, sizeof what, "303, no oscillator, RES 1 at %.0f Hz: dies away (rms %.5f)", v.fc, r);
          check(r < 0.002f, what);
        }
      }
  }

  /* ---- gate off ---- */
  fresh(&v, TA_CHAR_303); ta_set(&v, TA_HOLD, 1);
  ta_note(&v, 36.0f, 0); at = run(&v, 0, 100); ta_gate_off(&v); at = run(&v, at, 100);
  snprintf(what, sizeof what, "303: 50-100 ms after gate off the peak is %.5f, and the voice is %s",
           peak(at - SR / 20, at), ta_active(&v) ? "active" : "idle");
  check(peak(at - SR / 20, at) < 1e-3f && !ta_active(&v), what);
  fresh(&v, TA_CHAR_101); ta_set(&v, TA_HOLD, 0.8f); ta_set(&v, TA_DECAY, 0);
  ta_note(&v, 36.0f, 0); at = run(&v, 0, 500);
  { const float held = rms(at - SR / 10, at); ta_gate_off(&v); at = run(&v, at, 400);
    snprintf(what, sizeof what, "101: sustains at rms %.3f while held, %.5f 0.4 s after gate off", held, rms(at - SR / 20, at));
    check(held > 0.05f && rms(at - SR / 20, at) < 1e-3f, what);
  }

  /* ---- the kick's duck ---- */
  fresh(&v, TA_CHAR_303); ta_set(&v, TA_ENVMOD, 0); ta_set(&v, TA_HOLD, 1);
  ta_note(&v, 36.0f, 0); at = run(&v, 0, 100);
  { const float f0 = v.fc; ta_duck(&v, 1.0f); at = run(&v, at, 1); const float f1 = v.fc; at = run(&v, at, 1000);
    snprintf(what, sizeof what, "duck of an octave: cutoff %.0f -> %.0f Hz, back to %.0f after 1 s", f0, f1, v.fc);
    check(f1 < f0 * 0.55f && f1 > f0 * 0.45f && fabsf(v.fc / f0 - 1.0f) < 0.01f, what);
  }

  /* ---- nothing breaks it: every setting at random, both characters, odd block sizes ---- */
  { unsigned seed = 12345u; int bad = 0; float top = 0;
    #define RND() ((seed = seed * 1103515245u + 12345u) >> 8 & 0xFFFFu)
    for (int chr = 0; chr < TA_NCHAR; ++chr) {
      fresh(&v, (unsigned)chr);
      for (int ev = 0; ev < 6000; ++ev) {                   /* about 2 minutes each */
        for (int p = 0; p < TA_NPARAM; ++p)
          if (RND() % 4 == 0) { const unsigned r = RND() % 5; ta_set(&v, (unsigned)p, r == 0 ? 0.0f : r == 1 ? 1.0f : (float)RND() / 65535.0f); }
        const unsigned what_now = RND() % 8;
        if (what_now < 5) { const unsigned fl = RND() % 4; ta_note(&v, (float)(12 + RND() % 96), fl); }
        else if (what_now == 5) ta_gate_off(&v);
        else if (what_now == 6) ta_duck(&v, (float)(RND() % 4));
        else if (RND() % 16 == 0) ta_character(&v, RND() % 2);
        const unsigned m = 1 + RND() % 1500;
        unsigned left = m, a = 0;
        while (left) {
          const unsigned b = 1 + RND() % 64, c = b < left ? b : left;
          memset(buf + a, 0, c * sizeof(float)); ta_render_mix(&v, buf + a, c, 1.0f); a += c; left -= c;
        }
        for (unsigned i = 0; i < m; ++i) { if (!isfinite(buf[i])) ++bad; else if (fabsf(buf[i]) > top) top = fabsf(buf[i]); }
        if (!isfinite(v.s1 + v.s2 + v.s3 + v.s4 + v.hp + v.amp + v.fenv + v.cap + v.note)) ++bad;
      }
    }
    snprintf(what, sizeof what, "random settings, notes and block sizes: %d bad samples, peak %.3f (must stay under 1)", bad, top);
    check(bad == 0 && top < 1.0f, what);
  }

  /* ---- a pattern, for the ears ---- */
  for (int chr = 0; chr < TA_NCHAR; ++chr) {
    fresh(&v, (unsigned)chr);
    if (chr == TA_CHAR_101) { ta_set(&v, TA_WAVE, 0.6f); ta_set(&v, TA_PW, 0.4f); ta_set(&v, TA_SUB, 0.5f); ta_set(&v, TA_HOLD, 0.3f); }
    at = play(&v, 0, 8, 126.0f, sweep);
    snprintf(what, sizeof what, "%s pattern: 8 bars, peak %.3f, rms %.3f", chr ? "101" : "303", peak(0, at), rms(0, at));
    check(peak(0, at) < 1.0f && rms(0, at) > 0.03f, what);
    if (argc > 1) wav(argv[1], chr ? "acid-101.wav" : "acid-303.wav", at);
  }
  /* the TD-3-MO corner: sub, filter FM and drive */
  fresh(&v, TA_CHAR_303); ta_set(&v, TA_SUB, 0.6f); ta_set(&v, TA_FM, 0.5f); ta_set(&v, TA_DRIVE, 0.7f); ta_set(&v, TA_WAVE, 1);
  at = play(&v, 0, 8, 126.0f, sweep);
  snprintf(what, sizeof what, "303 with sub, FM and drive: peak %.3f, rms %.3f", peak(0, at), rms(0, at));
  check(peak(0, at) < 1.0f && rms(0, at) > 0.03f, what);
  if (argc > 1) wav(argv[1], "acid-303-modded.wav", at);

  printf("\n%d of %d checks passed\n", checks - fails, checks);
  return fails ? 1 : 0;
}
