// wakes-sp1 drum firmware: the kit, its bus FX and the rocker's rhythm tricks -- see sp1_drums.h.
#include "sp1_drums.h"

#include <math.h>
#include <string.h>

#include "sp1_marbles.h"

extern "C" {
#include "tinykit.h"
#include "tinyfx.h"
}

namespace {

// The eight built-in kits: the favourites from the renders in sp1-drum-bench.
// Voice 1 is on slot 1 (t1, kick-like), voice 2 on slot 2 (t3, snare-like), voice 3 on slot 3
// (every t2 tick), voice 4 on slot 4 (the Y clock) -- until the kit is rotated.
const sp1_drums_voice_def kKits[SP1_DRUMS_KITS][SP1_DRUMS_N] = {
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
  // 2 Modal gamelan: membrane, marimba, sekere, bell
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
  // 7 Classic: the LXR-derived voices (third_party/tinykit/lxr, GPL-3.0)
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

// The default model list (the spec's): the chirpy bytebeat models, three chip noises and two
// 1-bit clicks stay in the firmware but off this list.
struct Model { uint8_t e, m; };
const Model kModels[] = {
  {TK_ENG_FM, 0}, {TK_ENG_FM, 1}, {TK_ENG_FM, 2}, {TK_ENG_FM, 3}, {TK_ENG_FM, 4},
  {TK_ENG_KS, 0}, {TK_ENG_KS, 1}, {TK_ENG_KS, 2}, {TK_ENG_KS, 3},
  {TK_ENG_SHAKER, 0}, {TK_ENG_SHAKER, 1}, {TK_ENG_SHAKER, 2}, {TK_ENG_SHAKER, 3},
  {TK_ENG_SHAKER, 4},
  {TK_ENG_MODAL, 0}, {TK_ENG_MODAL, 1}, {TK_ENG_MODAL, 2}, {TK_ENG_MODAL, 3},
  {TK_ENG_MODAL, 4}, {TK_ENG_MODAL, 5}, {TK_ENG_MODAL, 6}, {TK_ENG_MODAL, 7},
  {TK_ENG_MODAL, 8}, {TK_ENG_MODAL, 9}, {TK_ENG_MODAL, 10}, {TK_ENG_MODAL, 11},
  {TK_ENG_CHIP, 0}, {TK_ENG_CHIP, 1}, {TK_ENG_CHIP, 2}, {TK_ENG_CHIP, 6}, {TK_ENG_CHIP, 7},
  {TK_ENG_RUNGLER, 0}, {TK_ENG_RUNGLER, 1}, {TK_ENG_RUNGLER, 2}, {TK_ENG_RUNGLER, 3},
  {TK_ENG_GENDY, 0}, {TK_ENG_GENDY, 1}, {TK_ENG_GENDY, 2}, {TK_ENG_GENDY, 3},
  {TK_ENG_AUTOMATON, 0}, {TK_ENG_AUTOMATON, 1}, {TK_ENG_AUTOMATON, 2},
  {TK_ENG_AUTOMATON, 3}, {TK_ENG_AUTOMATON, 4}, {TK_ENG_AUTOMATON, 5},
  {TK_ENG_ONEBIT, 0}, {TK_ENG_ONEBIT, 3},
#if defined(CONFIG_SP1_CLASSIC)
  {TK_ENG_CLASSIC, 0}, {TK_ENG_CLASSIC, 1}, {TK_ENG_CLASSIC, 2}, {TK_ENG_CLASSIC, 3},
  {TK_ENG_CLASSIC, 4}, {TK_ENG_CLASSIC, 5},
#endif
};
constexpr int kNumModels = static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));

constexpr uint32_t kMaxN = 24u;
constexpr float kMsPerSample = 1000.0f / 48000.0f;
constexpr float kRestWindowMs = 30.0f;     // a press this late in a step still takes it
constexpr int kFadeSamples = 576;          // 12 ms: a choke fade (shorter thumps on low drums)

// ---- this block's controls, and the voices ----
sp1_drums_ctl ctl;
tk_voice voice[SP1_DRUMS_N];
uint8_t cur_eng[SP1_DRUMS_N], cur_model[SP1_DRUMS_N];
bool voice_made[SP1_DRUMS_N];
tfx_state fx;
int fade_left[SP1_DRUMS_N];                // samples left in a choke fade; 0 = none

// ---- where we are in the bar ----
bool was_running;
int beat = -1;                             // 0..3 within the bar, -1 before the first tick
int step = -1;                             // 0..15, -1 while stopped
float step_age_ms;
uint32_t t2_count;
uint16_t rests[SP1_DRUMS_N];
bool write_at_start[SP1_DRUMS_N], erase_at_start[SP1_DRUMS_N];
uint32_t clear_seen;
uint8_t clear_pending;
uint32_t rot_seen;
int rot_pending, rot;
uint8_t last_hits;
uint32_t hit_count;

// ---- tape slow: the machine renders at `speed` and is resampled to 48 kHz ----
float hist[4];                             // the last four machine samples
float frac;                                // read position between hist[1] and hist[2]
float chunk[kMaxN], slot_buf[kMaxN];

// ---- step-through ----
uint32_t step_seen;
int steps_pending;
bool stepping;
float step_beats[SP1_MARBLES_MAX_FRAMES];
float step_base;

// ---- the snapshot main reads ----
volatile int8_t st_step = -1;
volatile uint8_t st_rot, st_last;
volatile uint16_t st_rests[SP1_DRUMS_N];
volatile uint32_t st_hits;

constexpr float kPenta[5] = {0.0f, 2.0f, 4.0f, 7.0f, 9.0f};

inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
// A Marbles voltage (any of its ranges) as 0..1.
inline float V01(float volts) { return Clamp01((volts + 5.0f) * 0.1f); }
// 0..1 -> one of ten pentatonic degrees over two octaves, in semitones.
inline float Penta(float u) {
  const int idx = static_cast<int>(u * 9.999f);
  return 12.0f * static_cast<float>(idx / 5) + kPenta[idx % 5];
}

void Strike(int v, float vel, const float* volt) {
  float note = ctl.note[v];
  float tone = ctl.tone[v];
  if (volt) {
    if (ctl.melodic & (1u << v)) {
      note += Penta(V01(*volt));
    } else {
      tone += (V01(*volt) - 0.5f) * 0.3f;            // a per-hit nudge, like a random p-lock
    }
  }
  tk_set(&voice[v], TK_PITCH, note / 120.0f);
  tk_set(&voice[v], TK_TONE, Clamp01(tone - (1.0f - vel) * 0.25f));       // ghosts are darker...
  tk_set(&voice[v], TK_DECAY, Clamp01(ctl.decay[v] - (1.0f - vel) * 0.2f));  // ...and shorter
  tk_set(&voice[v], TK_MOD, Clamp01(ctl.mod[v]));
  tk_set(&voice[v], TK_EXCITE, Clamp01(ctl.excite[v]));
  tk_trigger(&voice[v], vel);
  fade_left[v] = 0;                                  // a new hit cancels a choke
  hit_count = hit_count + 1u;
}

void Choke(int v) {
  if (tk_active(&voice[v]) && fade_left[v] == 0) {
    fade_left[v] = kFadeSamples;
  }
}

inline int VoiceOf(int slot) { return (slot - rot) & 3; }

// The machine, at its own speed: `m` samples of the voices and the bus FX.
void RenderMachine(float* buf, uint32_t m) {
  for (uint32_t i = 0; i < m; ++i) buf[i] = 0.0f;
  if (m == 0u) return;
  for (int v = 0; v < SP1_DRUMS_N; ++v) {
    if (!tk_active(&voice[v])) {
      fade_left[v] = 0;
      continue;
    }
    for (uint32_t i = 0; i < m; ++i) slot_buf[i] = 0.0f;
    tk_render_add(&voice[v], slot_buf, m);
    const float g = ctl.level[v];
    if (fade_left[v] > 0) {
      // smoothstep down: a raised-cosine shape without a cosf per sample
      for (uint32_t i = 0; i < m; ++i) {
        const int left = fade_left[v] > 0 ? fade_left[v] - 1 : 0;
        fade_left[v] = left;
        const float t = static_cast<float>(left) * (1.0f / kFadeSamples);
        buf[i] += slot_buf[i] * g * (t * t * (3.0f - 2.0f * t));
      }
      if (fade_left[v] == 0) {
        tk_choke(&voice[v]);
      }
    } else {
      for (uint32_t i = 0; i < m; ++i) buf[i] += slot_buf[i] * g;
    }
  }
  tfx_process(&fx, buf, nullptr, m);
}

// 4-point Hermite between y1 and y2.
inline float Hermite(const float* y, float t) {
  const float c1 = 0.5f * (y[2] - y[0]);
  const float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
  const float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
  return ((c3 * t + c2) * t + c1) * t + y[1];
}

}  // namespace

// ---- tables ----
extern "C" void sp1_drums_kit_voice(int kit, int v, sp1_drums_voice_def* out) {
  if (kit < 0 || kit >= SP1_DRUMS_KITS) kit = 0;
  if (v < 0 || v >= SP1_DRUMS_N) v = 0;
  *out = kKits[kit][v];
}

extern "C" const char* sp1_drums_kit_name(int k) {
  return (k >= 0 && k < SP1_DRUMS_KITS) ? kKitNames[k] : "?";
}

extern "C" int sp1_drums_models(void) { return kNumModels; }

extern "C" void sp1_drums_model_at(int i, uint8_t* eng, uint8_t* model) {
  if (i < 0 || i >= kNumModels) i = 0;
  *eng = kModels[i].e;
  *model = kModels[i].m;
}

extern "C" int sp1_drums_model_index(uint8_t eng, uint8_t model) {
  for (int i = 0; i < kNumModels; ++i) {
    if (kModels[i].e == eng && kModels[i].m == model) return i;
  }
  return -1;
}

extern "C" const char* sp1_drums_model_name(uint8_t eng, uint8_t model) {
  return tk_model_name(eng, model);
}

extern "C" bool sp1_drums_model_unique(uint8_t eng) {
  return eng == TK_ENG_CLASSIC;     // the LXR code has one instance of each voice
}

// ---- audio ----
extern "C" void sp1_drums_init(void) {
  tfx_init(&fx);
  memset(&ctl, 0, sizeof ctl);
  for (int v = 0; v < SP1_DRUMS_N; ++v) {
    const sp1_drums_voice_def& d = kKits[0][v];
    tk_init(&voice[v], d.eng, d.model);
    cur_eng[v] = d.eng;
    cur_model[v] = d.model;
    voice_made[v] = true;
    fade_left[v] = 0;
    rests[v] = 0u;
  }
  ctl.speed = 1.0f;
  for (float& h : hist) h = 0.0f;
  frac = 0.0f;
}

extern "C" void sp1_drums_begin(const sp1_synth_params* c) {
  ctl = c->drums;
  for (int v = 0; v < SP1_DRUMS_N; ++v) {
    if (!voice_made[v] || ctl.eng[v] != cur_eng[v] || ctl.model[v] != cur_model[v]) {
      tk_init(&voice[v], ctl.eng[v], ctl.model[v]);
      cur_eng[v] = ctl.eng[v];
      cur_model[v] = ctl.model[v];
      voice_made[v] = true;
      fade_left[v] = 0;
    }
  }
  for (int k = 0; k < 4; ++k) {
    const bool on = (ctl.fx_on & (1u << k)) != 0u;
    tfx_set(&fx, static_cast<unsigned>(k), on ? ctl.fx[k] : (k == TFX_FILTER ? 0.5f : 0.0f));
  }
  tfx_set(&fx, TFX_CRUSH, 0.0f);
  tfx_set_carrier(&fx, ctl.fm_hz > 0.0f ? ctl.fm_hz : 196.0f);
}

extern "C" void sp1_drums_clock(uint32_t frames, bool external) {
  const uint32_t sq = ctl.step_seq;
  if (sq != step_seen) {
    const int d = static_cast<int>(sq - step_seen);
    step_seen = sq;
    if (!sp1_marbles_running() || stepping) {
      steps_pending += d > 0 ? d : 0;
      if (steps_pending > 8) steps_pending = 8;
    }
  }
  if (stepping) {
    // The step's beat is over: stop the clock again (outputs read low / 0 V while stopped).
    sp1_marbles_clock(nullptr, SP1_MRB_TP_STOP);
    stepping = false;
    return;
  }
  if (steps_pending <= 0 || external || sp1_marbles_running() || frames == 0u) {
    if (external) steps_pending = 0;
    return;
  }
  if (frames > SP1_MARBLES_MAX_FRAMES) frames = SP1_MARBLES_MAX_FRAMES;
  // One beat across this block: the ramp rises through it and wraps on the last frame, which
  // is Marbles' tick. CONTINUE does not reset the sequences, so the DEJA VU loop walks on.
  for (uint32_t j = 0; j < frames; ++j) {
    step_beats[j] = step_base + static_cast<float>(j + 1u) / static_cast<float>(frames);
  }
  step_base += 1.0f;
  if (step_base > 4096.0f) step_base -= 4096.0f;    // float precision; a whole number of beats
  sp1_marbles_clock(step_beats, SP1_MRB_TP_CONTINUE);
  --steps_pending;
  stepping = true;
}

extern "C" void sp1_drums_render(const sp1_drums_frame* f, float* out, uint32_t n) {
  if (n > kMaxN) n = kMaxN;

  // ---- where we are in the bar ----
  const bool running = f->running;
  if (running && !was_running) {
    beat = -1;
    step = -1;
  }
  was_running = running;
  const bool beat_edge = running && (f->rise & 2u);
  if (beat_edge) {
    beat = (beat + 1) & 3;
  }
  int now_step = -1;
  if (running && beat >= 0) {
    int q = static_cast<int>(f->ramp * 4.0f);
    q = q < 0 ? 0 : (q > 3 ? 3 : q);
    now_step = beat * 4 + q;
  }
  if (now_step != step) {
    step = now_step;
    step_age_ms = 0.0f;
    if (step >= 0) {
      if (step == 0 && clear_pending) {                 // a bar line: the queued clears
        for (int s = 0; s < SP1_DRUMS_N; ++s) {
          if (clear_pending & (1u << s)) rests[s] = 0u;
        }
        clear_pending = 0u;
      }
      for (int s = 0; s < SP1_DRUMS_N; ++s) {
        write_at_start[s] = (ctl.rest_write & (1u << s)) != 0u;
        erase_at_start[s] = (ctl.rest_erase & (1u << s)) != 0u;
        if ((rests[s] & (1u << step)) && ctl.rest_choke) {
          Choke(VoiceOf(s));
        }
      }
    }
  } else {
    step_age_ms += static_cast<float>(n) * kMsPerSample;
  }

  // ---- the rocker's rests ----
  if (step >= 0) {
    const uint16_t bit = static_cast<uint16_t>(1u << step);
    for (int s = 0; s < SP1_DRUMS_N; ++s) {
      const bool late_ok = step_age_ms <= kRestWindowMs;
      if ((ctl.rest_write & (1u << s)) && (write_at_start[s] || late_ok)) {
        if (!(rests[s] & bit)) {
          rests[s] |= bit;
          if (ctl.rest_choke) Choke(VoiceOf(s));
        }
      }
      if ((ctl.rest_erase & (1u << s)) && (erase_at_start[s] || late_ok)) {
        rests[s] &= static_cast<uint16_t>(~bit);
      }
    }
  }
  if (ctl.rest_clear_seq != clear_seen) {
    clear_seen = ctl.rest_clear_seq;
    clear_pending |= ctl.rest_clear;
  }
  if (!running && clear_pending) {
    for (int s = 0; s < SP1_DRUMS_N; ++s) {
      if (clear_pending & (1u << s)) rests[s] = 0u;
    }
    clear_pending = 0u;
  }

  // ---- kit rotation, on the beat ----
  if (ctl.rotate_seq != rot_seen) {
    rot_pending += static_cast<int>(ctl.rotate_seq - rot_seen);
    rot_seen = ctl.rotate_seq;
  }
  if (rot_pending && (beat_edge || !running)) {
    rot = (rot + rot_pending) & 3;
    rot_pending = 0;
  }

  // ---- this block's hits ----
  uint8_t struck = 0u;
  if (f->rise) {
    float vel[SP1_DRUMS_N] = {0.95f, 0.9f, 0.8f, 0.85f};
    const float* vp[SP1_DRUMS_N] = {nullptr, nullptr, nullptr, nullptr};
    uint8_t slots = 0u;
    if (f->rise & 1u) { slots |= 1u; vp[0] = f->volts ? &f->volts[0] : nullptr; }
    if (f->rise & 4u) { slots |= 2u; vp[1] = f->volts ? &f->volts[2] : nullptr; }
    if (f->rise & 2u) {
      slots |= 4u;
      vp[2] = f->volts ? &f->volts[1] : nullptr;
      if (f->volts) vel[2] = 0.35f + 0.65f * V01(f->volts[1]);   // Marbles ghost notes
      if ((t2_count++ & 3u) == 0u) {                              // the Y clock
        slots |= 8u;
        vp[3] = f->volts ? &f->volts[3] : nullptr;
      }
    }
    for (int s = 0; s < SP1_DRUMS_N; ++s) {
      if (!(slots & (1u << s))) continue;
      if (step >= 0 && (rests[s] & (1u << step))) continue;      // a rest
      const int v = VoiceOf(s);
      if (ctl.mute & (1u << v)) continue;
      Strike(v, vel[s], vp[s]);
      struck |= static_cast<uint8_t>(1u << v);
    }
  }
  if (f->audition) {
    // DRUM n's page: that voice, muted or not. Elsewhere: whatever played last.
    uint8_t m = ctl.audition;
    const bool explicit_target = m != 0u;
    if (!m) m = last_hits ? last_hits : 1u;
    for (int v = 0; v < SP1_DRUMS_N; ++v) {
      if (!(m & (1u << v))) continue;
      if (!explicit_target && (ctl.mute & (1u << v))) continue;
      Strike(v, 0.9f, nullptr);
      if (!explicit_target) struck |= static_cast<uint8_t>(1u << v);
    }
  }
  if (struck) last_hits = struck;
  for (int v = 0; v < SP1_DRUMS_N; ++v) {
    if (ctl.mute & (1u << v)) Choke(v);              // a voice muted while it rings
  }

  // ---- render: the machine at `speed`, resampled to 48 kHz ----
  float s = ctl.speed;
  if (!(s > 0.0f) || s > 1.0f) s = 1.0f;            // zeroed params, NaN: normal speed
  if (s < 0.1f) s = 0.1f;
  if (s >= 0.9995f) {
    // Normal speed: no resampling. The same 3-sample delay the resampler has (out = hist[1],
    // hist[2], hist[3], then the new samples), so going in and out of tape slow is seamless;
    // coming back, the read position snaps to a whole sample (under one sample of phase).
    frac = 0.0f;
    RenderMachine(chunk, n);
    for (uint32_t i = 0; i < n; ++i) {
      out[i] = i < 3u ? hist[i + 1u] : chunk[i - 3u];
    }
    if (n >= 4u) {
      for (int i = 0; i < 4; ++i) hist[i] = chunk[n - 4u + static_cast<uint32_t>(i)];
    } else {
      for (uint32_t i = 0; i < n; ++i) {
        hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hist[3]; hist[3] = chunk[i];
      }
    }
    s = 0.0f;                                        // marks: rendered already
  }
  if (s > 0.0f) {
    // Tape slow: the machine renders `need` samples at its own speed and is read at `s`.
    uint32_t need = 0u;
    float fr = frac;
    for (uint32_t i = 0; i < n; ++i) {
      fr += s;
      while (fr >= 1.0f) { fr -= 1.0f; ++need; }
    }
    if (need > kMaxN) need = kMaxN;
    RenderMachine(chunk, need);
    uint32_t k = 0u;
    for (uint32_t i = 0; i < n; ++i) {
      out[i] = Hermite(hist, frac);
      frac += s;
      while (frac >= 1.0f) {
        frac -= 1.0f;
        hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hist[3];
        hist[3] = k < need ? chunk[k] : hist[3];
        ++k;
      }
    }
  }

  // A gentle soft clip, then the 16-bit scale sp1_synth's output stage takes from Plaits.
  const float g = (ctl.bus_gain > 0.0f ? ctl.bus_gain : 1.0f) * 1.2f;
  for (uint32_t i = 0; i < n; ++i) {
    const float x = out[i] * g;
    out[i] = x / (1.0f + fabsf(x)) * 1.5f * 29000.0f;
  }

  // ---- the snapshot ----
  st_step = static_cast<int8_t>(step);
  st_rot = static_cast<uint8_t>(rot);
  st_last = last_hits;
  for (int i = 0; i < SP1_DRUMS_N; ++i) st_rests[i] = rests[i];
  st_hits = hit_count;
}

extern "C" void sp1_drums_status(struct sp1_drums_status* out) {
  out->step = st_step;
  out->rotation = st_rot;
  out->last_hits = st_last;
  for (int i = 0; i < SP1_DRUMS_N; ++i) out->rests[i] = st_rests[i];
  out->hits = st_hits;
}
