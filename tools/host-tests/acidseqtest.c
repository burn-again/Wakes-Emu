/* acidseqtest.c -- the acid line's text form and resolver (firmware/src/sp1_acid.c) on the host.
 *
 *   cc -O2 -Wall -I../../firmware/src acidseqtest.c ../../firmware/src/sp1_acid.c -o acidseqtest && ./acidseqtest
 */
#include "sp1_acid.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
static void check(int ok, const char *what) {
  ++checks;
  if (!ok) ++fails;
  printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
}
static int parse(struct sp1_acid_pattern *p, const char *t) { return sp1_acid_parse(p, t, strlen(t)); }

/* run a pattern from PLAY and write each step's kind as a letter (n, s, t, .), accents in capitals,
 * a '-' after a step whose gate is held */
static void trace(const struct sp1_acid_pattern *p, const struct sp1_acid_perf *f, int steps, char *out,
                  float *notes) {
  struct sp1_acid_seq q; sp1_acid_seq_reset(&q);
  for (int g = 0; g < steps; ++g) {
    const struct sp1_acid_event e = sp1_acid_seq_step(&q, p, f, g);
    char c = e.kind == SP1_ACID_NOTE ? 'n' : e.kind == SP1_ACID_SLIDE ? 's' : e.kind == SP1_ACID_TIE ? 't' : '.';
    if (e.accent && c != '.') c = (char)(c - 32);
    *out++ = c;
    if (e.hold) *out++ = '-';
    if (notes) notes[g] = e.note;
  }
  *out = '\0';
}

int main(void) {
  struct sp1_acid_pattern p, p2; struct sp1_acid_perf f; char buf[256], what[400]; float notes[64];

  /* ---- the built-in patterns read, and write back as they read ---- */
  check(sp1_acid_init() == 0, "every built-in line reads");
  for (int i = 0; i < SP1_ACID_BUILTIN; ++i) {
    const struct sp1_acid_pattern *b = sp1_acid_pattern_of(i);
    const int n = sp1_acid_format(b, buf, sizeof buf);
    const int ok = n > 0 && parse(&p2, buf) == b->n && memcmp(&p2, b, sizeof p2) == 0;
    snprintf(what, sizeof what, "%-14s %2d steps: %s", sp1_acid_preset(i)->name, b->n, buf);
    check(ok && n < 120, what);            /* fits a drums.cnfg line with its key */
  }

  /* ---- the text form ---- */
  check(parse(&p, "C2 . D#2~ G2! _ Bb1^3 A-1v /") < 0, "a bare / is refused");
  { const int n = parse(&p, "C2 . D#2~ | G2! _ Bb1^3 C3v/5");
    snprintf(what, sizeof what, "notes, a rest, a tie, marks and weights: %d steps", n);
    check(n == 7 && p.s[0].note == 36 && p.s[0].w[0] == 9 && p.s[1].w[0] == 0 && p.s[2].note == 39 && p.s[2].w[2] == 9 &&
          p.s[3].note == 43 && p.s[3].w[1] == 9 && p.s[4].tie && p.s[4].note == 43 && p.s[5].note == 34 &&
          p.s[5].w[3] == 3 && p.s[5].oct == 1 && p.s[6].note == 48 && p.s[6].oct == -1 && p.s[6].w[0] == 5 &&
          p.s[1].note == 36, what);
    sp1_acid_format(&p, buf, sizeof buf);
    check(strcmp(buf, "C2 . D#2~ G2! _ A#1^3 C3v/5") == 0, buf);
  }
  { static const char *const bad[] = { "H2", "C", "C2x", "._", ". . .", "", "C2/", "c2", "C2 .C2" };
    int all = 1;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) all = all && parse(&p2, bad[i]) < 0;
    check(all, "nine things that are not patterns are refused");
    char big[200] = ""; for (int i = 0; i < 33; ++i) strcat(big, "C2 ");
    check(parse(&p2, big) < 0 && parse(&p2, big + 3) == 32, "32 steps read, 33 do not");
    p2.n = 99; check(parse(&p2, "X") < 0 && p2.n == 99, "a refused line leaves the pattern as it was");
  }

  /* ---- the resolver ---- */
  sp1_acid_perf_default(&f);
  parse(&p, "C2 D2~ _ . E2~ F2! G2~");
  trace(&p, &f, 7, buf, notes);
  snprintf(what, sizeof what, "note, slide, tie, rest, a slide after a rest (a plain note), accent, slide: %s", buf);
  check(strcmp(buf, "n-s-t.nN-s") == 0 && notes[1] == 38.0f && notes[6] == 43.0f, what);
  parse(&p, "_ C2");
  trace(&p, &f, 4, buf, 0);
  snprintf(what, sizeof what, "a tie with nothing sounding is a rest; round the loop it holds: %s", buf);
  check(strcmp(buf, ".n-tn-") == 0, what);

  /* ---- densities ---- */
  parse(&p, "C2 C2/4 C2!4 C2");
  trace(&p, &f, 4, buf, 0); check(strcmp(buf, "n.nn") == 0, "density 0.5: the weight-4 note and accent stay out");
  f.dens[SP1_ACID_L_GATE] = 1.0f; f.dens[SP1_ACID_L_ACCENT] = 1.0f;
  trace(&p, &f, 4, buf, 0); check(strcmp(buf, "nnNn") == 0, "density 1: both are in");
  f.dens[SP1_ACID_L_GATE] = 0.0f;
  trace(&p, &f, 4, buf, 0); check(strcmp(buf, "....") == 0, "gate density 0: nothing");

  /* ---- a lane of its own length drifts ---- */
  sp1_acid_perf_default(&f);
  parse(&p, "C2! C2 C2 C2");
  trace(&p, &f, 12, buf, 0); check(strcmp(buf, "NnnnNnnnNnnn") == 0, "accent lane at the pattern's length: every 4th");
  f.len[SP1_ACID_L_ACCENT] = 3;
  trace(&p, &f, 12, buf, 0); check(strcmp(buf, "NnnNnnNnnNnn") == 0, "accent lane 3 steps long: every 3rd, against 4 notes");

  /* ---- octaves and transpose ---- */
  sp1_acid_perf_default(&f); f.transpose = 7.0f;
  parse(&p, "C2^ C2v C2^4");
  trace(&p, &f, 3, buf, notes);
  check(notes[0] == 55.0f && notes[1] == 31.0f && notes[2] == 43.0f, "octave up, octave down, a weighted one left out; +7 semitones");

  /* ---- a step is decided once: a density moved between two steps cannot break a slide ---- */
  { struct sp1_acid_seq q; sp1_acid_seq_reset(&q); sp1_acid_perf_default(&f);
    parse(&p, "C2 D2~4");
    const struct sp1_acid_event a = sp1_acid_seq_step(&q, &p, &f, 0);
    f.dens[SP1_ACID_L_SLIDE] = 1.0f;                       /* now the slide would sound... */
    const struct sp1_acid_event b = sp1_acid_seq_step(&q, &p, &f, 1);
    check(!a.hold && b.kind == SP1_ACID_NOTE, "...but step 1 was decided with step 0: no hold, so a plain note");
  }

  /* ---- whatever the lanes do, a slide or a tie always has a held note behind it ---- */
  { unsigned seed = 99u; int bad = 0, n_slide = 0, n_tie = 0, n_note = 0;
    #define RND() ((seed = seed * 1103515245u + 12345u) >> 8 & 0xFFFFu)
    for (int i = 0; i < SP1_ACID_BUILTIN; ++i) {
      struct sp1_acid_seq q; sp1_acid_seq_reset(&q); sp1_acid_perf_default(&f);
      int held = 0;
      for (int g = 0; g < 20000; ++g) {
        if (RND() % 3 == 0) { const unsigned l = RND() % 4; f.dens[l] = (float)RND() / 65535.0f; }
        if (RND() % 50 == 0) { const unsigned l = RND() % 4; f.len[l] = (unsigned char)(RND() % 33); }
        const struct sp1_acid_event e = sp1_acid_seq_step(&q, sp1_acid_pattern_of(i), &f, g);
        const int legato = e.kind == SP1_ACID_SLIDE || e.kind == SP1_ACID_TIE;
        if (legato != held) ++bad;                          /* held <=> the next is a slide or a tie */
        if (e.note < 0.0f || e.note > 127.0f) ++bad;
        n_slide += e.kind == SP1_ACID_SLIDE; n_tie += e.kind == SP1_ACID_TIE; n_note += e.kind == SP1_ACID_NOTE;
        held = e.hold;
      }
    }
    snprintf(what, sizeof what, "160,000 steps with the lanes moving at random: %d broken (%d notes, %d slides, %d ties)",
             bad, n_note, n_slide, n_tie);
    check(bad == 0 && n_slide > 1000 && n_tie > 1000, what);
  }

  /* ---- the user line ---- */
  check(sp1_acid_set_user("A1 . A2~", 8) == 3 && sp1_acid_pattern_of(SP1_ACID_BUILTIN)->n == 3 &&
        sp1_acid_set_user("nope", 4) < 0 && sp1_acid_pattern_of(SP1_ACID_BUILTIN)->n == 3,
        "the user line is taken, and a bad one leaves it");

  printf("\n%d of %d checks passed\n", checks - fails, checks);
  return fails ? 1 : 0;
}
