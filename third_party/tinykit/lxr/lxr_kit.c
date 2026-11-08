/* lxr_kit.c -- see lxr_kit.h. GPL-3.0-only (it links the LXR DSP code). */
#include "lxr_kit.h"
#include <string.h>
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/SlopeEg2.h"
#include "Soundengine/DSPAudio/Decay.h"
#include "Soundengine/MIDI/valueShaper.h"

#define LX_BLOCK OUTPUT_DMA_SIZE          /* LXR updates its envelopes once per 64-sample block */
#define LX_SHAPER (-0.9f)                 /* FILTER_SHAPER in LXR's globals.h */

typedef struct { int16_t buf[LX_BLOCK]; int pos, active; float ctl[5]; } lx_slot;
static lx_slot S[LX_COUNT];
static int inited;
static const char *const kNames[LX_COUNT] = {"classic kick", "classic tom", "classic perc",
                                             "classic snare", "classic hi-hat", "classic cymbal"};
static const float kPitch[LX_COUNT] = {0.30f, 0.38f, 0.50f, 0.50f, 0.50f, 0.50f};

const char *lx_name(int id) { return (id >= 0 && id < LX_COUNT) ? kNames[id] : "?"; }

void lx_init(void) {
  if (!inited) { initDrumVoice(); Snare_init(); HiHat_init(); Cymbal_init(); inited = 1; }
  for (int i = 0; i < LX_COUNT; ++i) {
    memset(&S[i], 0, sizeof S[i]);
    S[i].pos = LX_BLOCK;
    for (int c = 0; c < 5; ++c) S[i].ctl[c] = 0.5f;
    S[i].ctl[0] = kPitch[i];
  }
}

void lx_set(int id, int ctl, float v) {
  if (id < 0 || id >= LX_COUNT || ctl < 0 || ctl > 4) return;
  S[id].ctl[ctl] = v < 0 ? 0 : (v > 1 ? 1 : v);
}

static void coarse(OscInfo *o, int note) {          /* as MidiParser's F_OSCn_COARSE, fine tune centred */
  if (note < 0) note = 0; if (note > 127) note = 127;
  o->midiFreq = (uint16_t)((note << 8) | 64);
  osc_recalcFreq(o);
}
static uint8_t u7(float v) { return (uint8_t)(v * 127.0f + 0.5f); }
/* LXR decay 127 never decays (an endless drone); stop just short of it so a voice always ends. */
static uint8_t dec7(float v) { return u7(v * 0.96f); }

/* the five controls, written with the same calls LXR's MIDI parser uses */
static void apply(int id) {
  const float *c = S[id].ctl;
  const int note = (int)(c[0] * 120.0f + 0.5f);
  const float cut = valueShaperF2F(c[1], LX_SHAPER);
  switch (id) {
  case 0: case 1: case 2: {
    DrumVoice *v = &voiceArray[id];
    coarse(&v->osc, note);
    coarse(&v->modOsc, note + 12);
    SVF_directSetFilterValue(&v->filter, cut);                       /* tone = filter cutoff     */
    slopeEg2_setDecay(&v->oscVolEg, dec7(c[2]), AMP_EG_SYNC);          /* decay                    */
    DecayEg_setDecay(&v->oscPitchEg, (uint8_t)(20 + c[2] * 70));     /* pitch sweep follows decay */
    v->fmModAmount = c[3];                                            /* mod = FM amount          */
    v->egPitchModAmount = c[4] * c[4] * PITCH_AMOUNT_FACTOR;          /* excite = pitch sweep     */
    break;
  }
  case 3:
    coarse(&snareVoice.osc, note);
    SVF_directSetFilterValue(&snareVoice.filter, cut);
    snareVoice.noiseOsc.freq = (0.3f + 0.7f * c[1]) * 22000.0f;
    slopeEg2_setDecay(&snareVoice.oscVolEg, dec7(c[2]), 0);
    snareVoice.mix = c[3];                                            /* mod = noise vs tone      */
    setDistortionShape(&snareVoice.distortion, c[4] * 0.9f);          /* excite = drive           */
    break;
  case 4: {
    coarse(&hatVoice.osc, note);
    SVF_directSetFilterValue(&hatVoice.filter, cut);
    hatVoice.decayClosed = slopeEg2_calcDecay(u7(c[2] * 0.8f));
    const float open = c[2] * 127.0f + 40.0f;
    hatVoice.decayOpen = slopeEg2_calcDecay((uint8_t)(open > 127.0f ? 127.0f : open));
    hatVoice.fmModAmount1 = c[3];                                     /* mod = metallic FM        */
    break;                                                            /* excite > 0.5 = open hat  */
  }
  default:
    coarse(&cymbalVoice.osc, note);
    SVF_directSetFilterValue(&cymbalVoice.filter, cut);
    slopeEg2_setDecay(&cymbalVoice.oscVolEg, dec7(c[2]), 0);
    cymbalVoice.fmModAmount1 = c[3];
    setDistortionShape(&cymbalVoice.distortion, c[4] * 0.9f);
    break;
  }
}

void lx_trigger(int id, float vel) {
  if (id < 0 || id >= LX_COUNT) return;
  apply(id);
  const uint8_t v = u7(vel < 0 ? 0 : (vel > 1 ? 1 : vel));
  switch (id) {
  case 0: case 1: case 2: Drum_trigger((uint8_t)id, v, SEQ_DEFAULT_NOTE); break;
  case 3: Snare_trigger(v, SEQ_DEFAULT_NOTE); break;
  case 4: HiHat_trigger(v, S[id].ctl[4] > 0.5f, SEQ_DEFAULT_NOTE); break;
  default: Cymbal_trigger(v, SEQ_DEFAULT_NOTE); break;
  }
  S[id].active = 1;
}

/* LXR's envelope never returns to EG_STOPPED once triggered: it sits in EG_D at zero (the original
 * hardware renders every voice all the time). Treat a finished decay as stopped too. */
static int eg_off(const SlopeEg2 *e) { return e->state == EG_STOPPED || (e->state == EG_D && e->value <= 0.0f); }

static int eg_done(int id) {
  switch (id) {
  case 0: case 1: case 2: return eg_off(&voiceArray[id].oscVolEg) && voiceArray[id].ampFilterInput < 1e-5f;
  case 3: return eg_off(&snareVoice.oscVolEg) && snareVoice.egValueOscVol < 1e-5f;
  case 4: return eg_off(&hatVoice.oscVolEg) && hatVoice.egValueOscVol < 1e-5f;
  default: return eg_off(&cymbalVoice.oscVolEg) && cymbalVoice.egValueOscVol < 1e-5f;
  }
}

static void render_block(int id) {
  int16_t *b = S[id].buf;
  switch (id) {
  case 0: case 1: case 2: calcDrumVoiceAsync((uint8_t)id); calcDrumVoiceSyncBlock((uint8_t)id, b, LX_BLOCK); break;
  case 3: Snare_calcAsync(); Snare_calcSyncBlock(b, LX_BLOCK); break;
  case 4: HiHat_calcAsync(); HiHat_calcSyncBlock(b, LX_BLOCK); break;
  default: Cymbal_calcAsync(); Cymbal_calcSyncBlock(b, LX_BLOCK); break;
  }
}

void lx_render_add(int id, float *out, unsigned n) {
  if (id < 0 || id >= LX_COUNT) return;
  lx_slot *s = &S[id];
  for (unsigned i = 0; i < n; ++i) {
    if (s->pos >= LX_BLOCK) {
      if (!s->active) return;
      render_block(id);
      s->pos = 0;
      if (eg_done(id)) s->active = 0;            /* play this last block out, then stop rendering */
    }
    out[i] += (float)s->buf[s->pos++] * (1.0f / 32768.0f);
  }
}

void lx_choke(int id) {
  if (id < 0 || id >= LX_COUNT) return;
  switch (id) {
  case 0: case 1: case 2:
    voiceArray[id].oscVolEg.value = 0; voiceArray[id].oscVolEg.state = EG_STOPPED;
    voiceArray[id].ampFilterInput = 0; voiceArray[id].lastGain = 0; break;
  case 3: snareVoice.oscVolEg.value = 0; snareVoice.oscVolEg.state = EG_STOPPED; snareVoice.egValueOscVol = 0; break;
  case 4: hatVoice.oscVolEg.value = 0; hatVoice.oscVolEg.state = EG_STOPPED; hatVoice.egValueOscVol = 0; break;
  default: cymbalVoice.oscVolEg.value = 0; cymbalVoice.oscVolEg.state = EG_STOPPED; cymbalVoice.egValueOscVol = 0; break;
  }
  S[id].active = 0; S[id].pos = LX_BLOCK;
}

int lx_active(int id) { return (id >= 0 && id < LX_COUNT) && (S[id].active || S[id].pos < LX_BLOCK); }
