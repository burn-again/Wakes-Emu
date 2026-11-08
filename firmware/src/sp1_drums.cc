// wakes-sp1 drum firmware, stage 1 -- see sp1_drums.h.
#include "sp1_drums.h"

#include <math.h>
#include <string.h>

extern "C" {
#include "tinykit.h"
#include "tinyfx.h"
}

namespace {

struct SlotDef {
  uint8_t eng, model;
  float note, tone, decay, mod, excite;
  float gain;                    // level in the mix, before the bus FX
  bool melodic;                  // pitch from its Marbles output, pentatonic
};

// The eight built-in kits: the favourites from the renders in sp1-drum-bench.
// Slot 1 is t1 (kick-like), slot 2 t3 (snare-like), slot 3 t2 (every tick), slot 4 the Y clock.
const SlotDef kKits[SP1_DRUMS_KITS][4] = {
  // 0 Lich techno: FM kick, KS clap, FM hat, cabasa
  {{TK_ENG_FM, 0, 36, 0.20f, 0.45f, 0.30f, 0.60f, 0.80f, false},
   {TK_ENG_KS, 0, 50, 0.50f, 0.40f, 0.20f, 0.30f, 0.45f, false},
   {TK_ENG_FM, 3, 72, 0.40f, 0.20f, 0.50f, 0.60f, 0.30f, false},
   {TK_ENG_SHAKER, 1, 60, 0.60f, 0.15f, 0.50f, 0.50f, 0.35f, false}},
  // 1 8-bit breaks: NES triangle kick, NES noise snare, Game Boy hat, 12.5% pulse
  {{TK_ENG_CHIP, 6, 40, 0.50f, 0.35f, 0.60f, 0.70f, 0.85f, false},
   {TK_ENG_CHIP, 0, 60, 0.85f, 0.30f, 0.40f, 0.20f, 0.45f, false},
   {TK_ENG_CHIP, 2, 60, 0.95f, 0.08f, 0.50f, 0.00f, 0.22f, false},
   {TK_ENG_CHIP, 7, 40, 0.50f, 0.25f, 0.10f, 0.00f, 0.40f, true}},
  // 2 Modal gamelan: membrane, marimba, bell, sekere
  {{TK_ENG_MODAL, 0, 43, 0.30f, 0.50f, 0.30f, 0.20f, 0.70f, false},
   {TK_ENG_MODAL, 4, 60, 0.55f, 0.45f, 0.55f, 0.30f, 0.45f, true},
   {TK_ENG_SHAKER, 2, 60, 0.50f, 0.20f, 0.50f, 0.50f, 0.30f, false},
   {TK_ENG_MODAL, 2, 72, 0.70f, 0.75f, 0.40f, 0.40f, 0.35f, true}},
  // 3 IDM Rungler: 1-bit kick, Gendy snare, rule-30 hats, Rungler steps
  {{TK_ENG_ONEBIT, 0, 38, 0.60f, 0.30f, 0.60f, 0.50f, 0.70f, false},
   {TK_ENG_GENDY, 1, 62, 0.70f, 0.25f, 0.60f, 0.80f, 0.45f, false},
   {TK_ENG_AUTOMATON, 0, 72, 0.30f, 0.12f, 0.50f, 0.30f, 0.30f, false},
   {TK_ENG_RUNGLER, 2, 36, 0.40f, 0.60f, 0.50f, 0.40f, 0.35f, false}},
  // 4 Dub techno: FM tom kick, KS rimshot, tambourine, FM cymbal
  {{TK_ENG_FM, 2, 40, 0.10f, 0.50f, 0.20f, 0.70f, 0.80f, false},
   {TK_ENG_KS, 2, 74, 0.00f, 0.35f, 0.00f, 0.00f, 0.40f, false},
   {TK_ENG_SHAKER, 3, 60, 0.50f, 0.25f, 0.50f, 0.50f, 0.30f, false},
   {TK_ENG_FM, 4, 66, 0.50f, 0.60f, 0.60f, 0.50f, 0.22f, false}},
  // 5 Iron vibes: tuned-drum kick, FM snare, FM hat, vibraphone
  {{TK_ENG_MODAL, 11, 31, 0.10f, 0.35f, 0.80f, 0.05f, 0.85f, false},
   {TK_ENG_FM, 1, 55, 0.60f, 0.35f, 0.50f, 0.50f, 0.45f, false},
   {TK_ENG_FM, 3, 72, 0.40f, 0.15f, 0.50f, 0.60f, 0.28f, false},
   {TK_ENG_MODAL, 5, 57, 0.50f, 0.55f, 0.30f, 0.20f, 0.35f, true}},
  // 6 Glass Rungler: 1-bit kick, Gendy snare, glass, Rungler comparator
  {{TK_ENG_ONEBIT, 0, 38, 0.60f, 0.30f, 0.60f, 0.50f, 0.70f, false},
   {TK_ENG_GENDY, 1, 62, 0.70f, 0.25f, 0.60f, 0.80f, 0.40f, false},
   {TK_ENG_MODAL, 6, 67, 0.55f, 0.45f, 0.35f, 0.30f, 0.35f, true},
   {TK_ENG_RUNGLER, 0, 36, 0.40f, 0.45f, 0.50f, 0.40f, 0.30f, false}},
#if defined(CONFIG_SP1_CLASSIC)
  // 7 Classic: the LXR-derived voices (third_party/lxr-classic, GPL-3.0)
  {{TK_ENG_CLASSIC, 0, 36, 0.50f, 0.45f, 0.20f, 0.45f, 1.00f, false},
   {TK_ENG_CLASSIC, 3, 60, 0.60f, 0.40f, 0.55f, 0.25f, 0.45f, false},
   {TK_ENG_CLASSIC, 4, 64, 0.75f, 0.25f, 0.50f, 0.20f, 0.40f, false},
   {TK_ENG_CLASSIC, 5, 60, 0.60f, 0.70f, 0.50f, 0.30f, 0.30f, false}},
#else
  // 7 Gong garden: gong, marimba, sekere, kalimba
  {{TK_ENG_MODAL, 10, 38, 0.35f, 0.80f, 0.25f, 0.20f, 0.60f, true},
   {TK_ENG_MODAL, 4, 60, 0.55f, 0.45f, 0.55f, 0.25f, 0.45f, true},
   {TK_ENG_SHAKER, 2, 60, 0.50f, 0.22f, 0.50f, 0.50f, 0.30f, false},
   {TK_ENG_MODAL, 9, 72, 0.65f, 0.55f, 0.45f, 0.30f, 0.35f, true}},
#endif
};

const char* const kKitNames[SP1_DRUMS_KITS] = {
  "lich techno", "8-bit breaks", "modal gamelan", "idm rungler",
  "dub techno", "iron vibes", "glass rungler",
#if defined(CONFIG_SP1_CLASSIC)
  "classic",
#else
  "gong garden",
#endif
};

tk_voice voice[4];
tfx_state fx;
int kit = 0;
float kit_semis = 0.0f, d_tone = 0.0f, d_decay = 0.0f, d_mod = 0.0f;
uint32_t t2_count = 0;
float slot_buf[4][24];

constexpr float kPenta[5] = {0.0f, 2.0f, 4.0f, 7.0f, 9.0f};

inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
// A Marbles voltage (any of its ranges) as 0..1.
inline float V01(float volts) { return Clamp01((volts + 5.0f) * 0.1f); }
// 0..1 -> one of ten pentatonic degrees over two octaves, in semitones.
inline float Penta(float u) {
  const int idx = static_cast<int>(u * 9.999f);
  return 12.0f * static_cast<float>(idx / 5) + kPenta[idx % 5];
}

void LoadKit(int k) {
  for (int s = 0; s < 4; ++s) {
    tk_init(&voice[s], kKits[k][s].eng, kKits[k][s].model);
  }
}

void Strike(int s, float vel, const float* volt) {
  const SlotDef& d = kKits[kit][s];
  float note = d.note + kit_semis;
  float tone = d.tone + d_tone;
  if (volt) {
    if (d.melodic) {
      note += Penta(V01(*volt));
    } else {
      tone += (V01(*volt) - 0.5f) * 0.3f;             // a per-hit nudge, like a random p-lock
    }
  }
  tk_set(&voice[s], TK_PITCH, note / 120.0f);
  tk_set(&voice[s], TK_TONE, Clamp01(tone - (1.0f - vel) * 0.25f));      // ghosts are darker...
  tk_set(&voice[s], TK_DECAY, Clamp01(d.decay + d_decay - (1.0f - vel) * 0.2f));  // ...and shorter
  tk_set(&voice[s], TK_MOD, Clamp01(d.mod + d_mod));
  tk_set(&voice[s], TK_EXCITE, d.excite);
  tk_trigger(&voice[s], vel);
}

}  // namespace

extern "C" void sp1_drums_init(void) {
  tfx_init(&fx);
  kit = 0;
  LoadKit(kit);
}

extern "C" void sp1_drums_begin(const sp1_synth_params* c) {
  const int k = ((c->engine % SP1_DRUMS_KITS) + SP1_DRUMS_KITS) % SP1_DRUMS_KITS;
  if (k != kit) {
    kit = k;
    LoadKit(k);
  }
  float semis = c->note - 60.0f;
  semis = semis < -12.0f ? -12.0f : (semis > 12.0f ? 12.0f : semis);
  kit_semis = static_cast<float>(static_cast<int>(semis + (semis >= 0.0f ? 0.5f : -0.5f)));
  d_tone = (c->timbre - 0.5f) * 0.8f;
  d_decay = (c->morph - 0.5f) * 0.8f;
  d_mod = (c->harmonics - 0.5f) * 0.8f;
  tfx_set(&fx, TFX_FILTER, 0.5f + 0.5f * c->fm_mod);
  tfx_set(&fx, TFX_FOLD, fabsf(c->timbre_mod));
  tfx_set(&fx, TFX_CHEBY, fabsf(c->morph_mod));
  tfx_set(&fx, TFX_FM, fabsf(c->harm_mod));
  tfx_set(&fx, TFX_CRUSH, 0.0f);
}

extern "C" void sp1_drums_render(uint8_t rise, const float* volts, bool audition, float* out,
                                 uint32_t n) {
  if (audition) {
    Strike(0, 0.9f, nullptr);
  }
  if (rise) {
    if (rise & 1u) Strike(0, 0.95f, volts ? &volts[0] : nullptr);
    if (rise & 4u) Strike(1, 0.9f, volts ? &volts[2] : nullptr);
    if (rise & 2u) {
      const float vel = volts ? 0.35f + 0.65f * V01(volts[1]) : 0.8f;   // Marbles ghost notes
      Strike(2, vel, volts ? &volts[1] : nullptr);
      if ((t2_count++ & 3u) == 0u) {
        Strike(3, 0.85f, volts ? &volts[3] : nullptr);                   // the Y clock
      }
    }
  }
  if (n > 24u) n = 24u;
  for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  for (int s = 0; s < 4; ++s) {
    if (!tk_active(&voice[s])) continue;
    float* b = slot_buf[s];
    for (uint32_t i = 0; i < n; ++i) b[i] = 0.0f;
    tk_render_add(&voice[s], b, n);
    const float g = kKits[kit][s].gain;
    for (uint32_t i = 0; i < n; ++i) out[i] += b[i] * g;
  }
  tfx_process(&fx, out, nullptr, n);
  // A gentle soft clip, then the 16-bit scale sp1_synth's output stage takes from Plaits.
  for (uint32_t i = 0; i < n; ++i) {
    const float x = out[i] * 1.2f;
    out[i] = x / (1.0f + fabsf(x)) * 1.5f * 29000.0f;
  }
}

extern "C" int sp1_drums_kit(void) { return kit; }

extern "C" const char* sp1_drums_kit_name(int k) {
  return (k >= 0 && k < SP1_DRUMS_KITS) ? kKitNames[k] : "?";
}
