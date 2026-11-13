/* acidbench.c -- instructions per sample for tinyacid, with tinybass alongside as the yardstick.
 *
 *   F="-O2 -fno-strict-aliasing -funroll-loops -finline-functions-called-once -fmove-loop-invariants -funswitch-loops"
 *   cc $F -I../../third_party/tinykit acidbench.c ../../third_party/tinykit/tinyacid.c \
 *      ../../third_party/tinykit/tinybass.c -lm -o acidbench
 *   valgrind --tool=callgrind --callgrind-out-file=/dev/null ./acidbench 303
 *
 * Callgrind's "Collected" total, less the same run with 0 seconds, over the samples printed is
 * instructions per sample on this host. tools/plaits-bench/README.md has the rule for the SP-1:
 * 1333 cycles a sample in all, cycles = instructions x 1.0 .. 1.5. The M4 itself is not measured here.
 *
 * what: 303 | 101 | 303fm (sub, FM, drive) | 101full (pulse, sub, noise, FM) | bass   [seconds]
 */
#include "tinyacid.h"
#include "tinybass.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  const char *what = argc > 1 ? argv[1] : "303";
  const unsigned secs = argc > 2 ? (unsigned)atoi(argv[2]) : 20u;
  static float buf[24];
  static const int notes[16] = {36,36,48,36,0,39,36,43,36,0,48,46,36,36,41,39};
  static const int acc[16]   = {1,0,0,0,0,1,0,0,1,0,1,0,0,0,1,0};
  static const int sld[16]   = {0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1};
  ta_voice a; tb_voice b; ta_init(&a); tb_init(&b);
  const int bass = !strcmp(what, "bass");
  if (!strncmp(what, "101", 3)) ta_character(&a, TA_CHAR_101);
  if (!strcmp(what, "303fm")) { ta_set(&a, TA_SUB, 0.6f); ta_set(&a, TA_FM, 0.5f); ta_set(&a, TA_DRIVE, 0.7f); }
  if (!strcmp(what, "101full")) { ta_set(&a, TA_WAVE, 0.6f); ta_set(&a, TA_PW, 0.4f); ta_set(&a, TA_SUB, 0.5f);
                                  ta_set(&a, TA_NOISE, 0.3f); ta_set(&a, TA_FM, 0.4f); }
  ta_set(&a, TA_HOLD, 1.0f); tb_set(&b, TB_DECAY, 1.0f); tb_set(&b, TB_SHAPE, 0.3f);
  const unsigned blocks = secs * 2000u, per_step = 238u;       /* 24-sample blocks, 16ths at 126 bpm */
  double sum = 0;
  for (unsigned i = 0; i < blocks; ++i) {
    const unsigned s = (i / per_step) % 16u;
    if (i % per_step == 0 && notes[s]) {
      if (bass) tb_note(&b, (float)notes[s], 1.0f);
      else ta_note(&a, (float)notes[s], (acc[s] ? TA_F_ACCENT : 0u) | (sld[s] ? TA_F_SLIDE : 0u));
    }
    /* the gate is never let go, so the voice sounds the whole time: the worst case */
    memset(buf, 0, sizeof buf);
    if (bass) tb_render_mix(&b, buf, 24, 1.0f); else ta_render_mix(&a, buf, 24, 1.0f);
    sum += buf[7];
  }
  printf("%s: %u samples (%g)\n", what, blocks * 24u, sum);
  return 0;
}
