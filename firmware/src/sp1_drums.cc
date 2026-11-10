// wakes-sp1 drum firmware: the kit, the sequencer, Marbles' cables, the bass, the bus FX and the
// rocker's rhythm tricks -- see sp1_drums.h.
#include "sp1_drums.h"

#include <math.h>
#include <string.h>

#include "sp1_marbles.h"

extern "C" {
#include "tinykit.h"
#include "tinyfx.h"
#include "tinybass.h"
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

// ---- the pattern bank: each kit's weight maps, A and B, per slot ----
// A map is a run of weights, '.' = 0 .. '9'; its length is the loop it was written for (16 or
// 32 steps). DRUM n + SHIFT F3 sets how many steps a slot plays; a longer loop wraps the map.
struct MapPair { const char* a; const char* b; };
const MapPair kBank[SP1_DRUMS_KITS][SP1_DRUMS_N] = {
  {  // 0 lich techno
    {"9...9...9...9..3", "9...9..29...9.3."}, {"....9.......9...", "....9..3....9.25"},
    {"3.7.3.7.3.7.3374", "3474347434743474"}, {"..7.3.7...7.3.74", "..7.3.7..47.3.74"}},
  {  // 1 8-bit breaks
    {"9.....7...8..3..", "9.....7.7.8..37."}, {"....9..3.2..9.42", "....9..3.24.9.49"},
    {"7.5.8.5.7.5.8.56", "7.5.8.5.7.5.8.56"},
    {"9..7..8...6..7..9..7..8...6.7.5.", "9..7..8...6..7..9..7..8...6.7.5."}},
  {  // 2 modal gamelan
    {"9.....7...8..3..", "9..3..7...8..37."}, {"9.4.7..5.3.7.4.6", "9.4.7.35.3.7.476"},
    {"3.723.723.723.92", "3.723.723.723.92"},
    {"7.....3...6.....7...3.....5...2.", "7..2..3...6..2..7...3..4..5...2."}},
  {  // 3 idm rungler
    {"9..7......8.9...", "9..7.6....8.9.47"}, {"....9...3...9.2.", "....9...3.4.9.23"},
    {"7373737373737374", "7.5.8.5.7.5.8.56"}, {"8.3.6.3.8.3.6.47", "8.3.6.3.8.3.6.47"}},
  {  // 4 dub techno
    {"9...9...9...9...", "9...9...9..29..."}, {"...7..6...3.7..5", "...7..6..37.7.45"},
    {"..7.3.7...7.3.74", "..7.3.7...7.3.74"},
    {"7.......3.......5.......3.......", "7.......3.......5.......3......."}},
  {  // 5 iron vibes
    {"9.7...8.9.7...8.", "9.7...8.9.7..38."}, {"....9..3....9.24", "....9..7..3.9.79"},
    {"7373737373737374", "7373737373737374"},
    {"8.....6.......7.8...6.......5.4.", "8.....6.......7.8...6.......5.4."}},
  {  // 6 glass rungler
    {"9..7......8.9...", "9..7.6....8.9.47"}, {"....9...3...9.2.", "....9...3.4.9.23"},
    {"9.....6...7.....8...5....6....4.", "9.....6...7.....8...5....6....4."},
    {"8.3.6.3.8.3.6.47", "8.3.6.3.8.3.6.47"}},
#if defined(CONFIG_SP1_CLASSIC)
  {  // 7 classic
    {"9...9...9...9..3", "9...9..29...9.3."}, {"....9.......9...", "....9..3....9.25"},
    {"7373737373737374", "3474347434743474"},
    {"9.......3.......7.......3.......", "9.......3.......7.......3......."}},
#else
  {  // 7 gong garden
    {"9.....................7.........", "9...........5.........7...3....."},
    {"9.4.7..5.3.7.4.6", "9.4.7.35.3.7.476"}, {"3.723.723.723.92", "3.723.723.723.92"},
    {"7.....3...6.....7...3.....5...2.", "7..2..3...6..2..7...3..4..5...2."}},
#endif
};

constexpr uint32_t kMaxN = 24u;
constexpr float kMsPerSample = 1000.0f / 48000.0f;
constexpr float kRestWindowMs = 30.0f;     // a press this late in a step still takes it
constexpr int kFadeSamples = 576;          // 12 ms: a choke fade (shorter thumps on low drums)
// The output stage. kOutScale keeps the old curve's small-signal gain (1.2 x 1.5 x 29000
// = 52200 per machine unit) with its top moved from 43500 (over 16-bit full scale) to
// kCeiling; kLevThresh is where a stacked kit starts to be held back (the clip's knee).
constexpr float kCeiling = 32112.0f;                   // -0.2 dBFS: the clip's top
constexpr float kOutScale = 52200.0f / kCeiling;
constexpr float kLevThresh = 0.85f;
constexpr float kLevRelease24 = 0.99667f;              // exp(-24 / (0.15 s x 48 kHz))
constexpr float kShedAt = 0.80f;     // the CPU guard: a block's render above this share...
constexpr float kShedTail = 0.1f;    // ...fades the quietest voice below this level (-20 dB)
constexpr int kMap = 32;                   // the longest map / loop

// ---- this block's controls, and the voices ----
sp1_drums_ctl ctl;
tk_voice voice[SP1_DRUMS_N];
uint8_t cur_eng[SP1_DRUMS_N], cur_model[SP1_DRUMS_N];
bool voice_made[SP1_DRUMS_N];
tfx_state fx;
tb_voice bass;
int fade_left[SP1_DRUMS_N];                // samples left in a choke fade; 0 = none

// ---- where we are: 16ths since PLAY, off Marbles' master ramp ----
bool was_running;
int32_t g16 = -1;                          // 16ths since PLAY (-1 before the first)
int last_q = -1;                           // the quarter of the beat the ramp was in
int step = -1;                             // g16 within the bar, 0..15; -1 while stopped
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

// ---- the sequencer ----
float pend_vel[SP1_DRUMS_N];               // a swung hit waiting for its moment (0 = none)
float rnd[SP1_DRUMS_N][kMap];              // each step's jitter, -1..+1 (redrawn per DEJA VU)
uint32_t rng = 0x2545F491u;
bool accent_now;                           // an accent cable fired this step

// ---- Marbles' cables: this block's contributions ----
float g_amt[SP1_DX_COUNT];                 // kit / FX / fill: -1..+1, summed and clamped
float own_semis[SP1_DRUMS_N], own_tone[SP1_DRUMS_N], own_decay[SP1_DRUMS_N], own_vel[SP1_DRUMS_N];
float last_volts[4];

// ---- tape slow: the machine renders at `speed` and is resampled to 48 kHz ----
float hist[4];                             // the last four machine samples
// The output stage (sp1_drums_render's end): the leveller's envelope and gain, the last
// call's bus x drive gain, and the "••" + VOL drive sp1_synth hands over each DMA block.
float lev_env, lev_gain = 1.0f, bus_prev = 0.0f;
volatile float drive_to = 1.0f;
float frac;                                // read position between hist[1] and hist[2]
float chunk[kMaxN], slot_buf[kMaxN];

// ---- step-through ----
uint32_t step_seen;
int steps_pending;
bool stepping;
bool step_block;                           // this block's run is a step, not PLAY
float step_beats[SP1_MARBLES_MAX_FRAMES];
float step_base;

// ---- the snapshot main reads ----
volatile int8_t st_step = -1;
volatile uint8_t st_rot, st_last;
volatile uint16_t st_rests[SP1_DRUMS_N];
volatile uint32_t st_hits, st_bass, st_shed;
// The last DMA block's drum render, as a share of its cycle budget (sp1_drums_load).
float load_now;

constexpr float kPenta[5] = {0.0f, 2.0f, 4.0f, 7.0f, 9.0f};

inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float Clamp11(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }
// 0..1 -> one of ten pentatonic degrees over two octaves, in semitones.
inline float Penta(float u) {
  const int idx = static_cast<int>(u * 9.999f);
  return 12.0f * static_cast<float>(idx / 5) + kPenta[idx % 5];
}
inline float Rand11() {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return static_cast<float>(static_cast<int32_t>(rng)) * (1.0f / 2147483648.0f);
}
// Output k's voltage as 0..1 over the range it spans now (main resolves INTELLIGENT).
inline float Norm(int k, float v) {
  float lo = ctl.lo[k], hi = ctl.hi[k];
  if (!(hi > lo)) { lo = -5.0f; hi = 5.0f; }
  return Clamp01((v - lo) / (hi - lo));
}

inline int VoiceOf(int slot) { return (slot - rot) & 3; }
// The slot each output's "own slot" cables act on: its t partner's slot when that is routed to
// one, else the slot it plays by default (t1 -> 1, t2 -> 3, t3 -> 2, Y -> 4).
constexpr int kDefaultSlot[4] = {0, 2, 1, 3};
// ...and the reverse, for the built-in per-hit nudge: slot 1 X1, slot 2 X3, slot 3 X2, slot 4 Y.
constexpr int kSlotOutput[4] = {0, 2, 1, 3};
inline int OwnSlot(int k) {
  const uint8_t d = ctl.t_dest[k];
  return (d >= SP1_DT_SLOT1 && d <= SP1_DT_SLOT4) ? d - SP1_DT_SLOT1 : kDefaultSlot[k];
}
inline bool CabledSlot(int s) {
  for (int k = 0; k < 4; ++k) {
    if (ctl.t_dest[k] == SP1_DT_SLOT1 + s) return true;
  }
  return false;
}

inline int Weight(int bank, int slot, bool b, int pos) {
  const char* m = b ? kBank[bank][slot].b : kBank[bank][slot].a;
  const int n = static_cast<int>(strlen(m));
  const char c = m[pos % (n > 0 ? n : 1)];
  return (c >= '0' && c <= '9') ? c - '0' : 0;
}

// The built-in per-hit nudge (a random parameter lock): a melodic voice takes its pitch from
// its output, the rest a little tone. `u` is that output, 0..1, or < 0 for none.
void Strike(int v, float vel, float u, float add_semis, float add_tone, float add_decay) {
  float note = ctl.note[v] + add_semis;
  float tone = ctl.tone[v] + add_tone;
  if (u >= 0.0f) {
    if (ctl.melodic & (1u << v)) {
      note += Penta(u);
    } else {
      tone += (u - 0.5f) * 0.3f;
    }
  }
  if (vel > 1.0f) vel = 1.0f;
  tk_set(&voice[v], TK_PITCH, note / 120.0f);
  tk_set(&voice[v], TK_TONE, Clamp01(tone - (1.0f - vel) * 0.25f));       // ghosts are darker...
  tk_set(&voice[v], TK_DECAY, Clamp01(ctl.decay[v] + add_decay - (1.0f - vel) * 0.2f));
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

// One hit on slot `s`: the rest and the mute decide whether it plays; the cables and the
// built-in nudge shape it. Returns the voice struck, or -1.
int Hit(int s, float vel, bool running) {
  if (step >= 0 && (rests[s] & (1u << step))) return -1;     // a rest
  const int v = VoiceOf(s);
  if (ctl.mute & (1u << v)) return -1;
  float semis = 0.0f, tone = 0.0f, decay = 0.0f;
  bool own_pitch = false, own_tone_c = false;
  if (running) {
    semis = 12.0f * g_amt[SP1_DX_KIT_PITCH] + own_semis[s];
    tone = 0.4f * g_amt[SP1_DX_KIT_TONE] + 0.5f * own_tone[s];
    decay = 0.4f * g_amt[SP1_DX_KIT_DECAY] + 0.5f * own_decay[s];
    vel += own_vel[s];
    if (accent_now) vel += 0.25f;
    if (ctl.jitter > 0.0f) {                                   // Marbles' wobble
      tone += 0.1f * ctl.jitter * Rand11();
      decay += 0.1f * ctl.jitter * Rand11();
    }
    for (int k = 0; k < 4; ++k) {
      if (OwnSlot(k) != s) continue;
      own_pitch = own_pitch || ctl.x_dest[k] == SP1_DX_PITCH;
      own_tone_c = own_tone_c || ctl.x_dest[k] == SP1_DX_TONE;
    }
  }
  float u = -1.0f;
  const int k = kSlotOutput[s];
  if (running && !(own_pitch && (ctl.melodic & (1u << v))) && !own_tone_c) {
    u = Norm(k, last_volts[k]);
  }
  Strike(v, vel, u, semis, tone, decay);
  return v;
}

// The bass: a note from Marbles' scale, chosen by Y.
void BassHit(float vel) {
  if (ctl.bass_mode == SP1_BASS_OFF) return;
  float span = static_cast<float>(ctl.bass_octaves > 0 ? ctl.bass_octaves : 1);
  float u = Norm(3, last_volts[3]);
  if (ctl.x_dest[3] == SP1_DX_BASS_NOTE) {
    span *= fabsf(ctl.depth[3]);                 // the cable's depth sets how far Y reaches
    if (ctl.depth[3] < 0.0f) u = 1.0f - u;
  }
  const int nd = (ctl.n_deg > 0 && ctl.n_deg <= 12) ? ctl.n_deg : 0;
  float note = ctl.bass_root;
  if (nd > 0) {
    int steps = static_cast<int>(span * static_cast<float>(nd) + 0.5f);
    if (steps < 1) steps = 1;
    int i = static_cast<int>(u * static_cast<float>(steps));
    if (i >= steps) i = steps - 1;
    const float per = ctl.period > 0.0f ? ctl.period : 12.0f;
    note += per * static_cast<float>(i / nd) + ctl.degrees[i % nd];
  } else {
    note += Penta(u);
  }
  tb_note(&bass, note, vel);
  st_bass = st_bass + 1u;
}

// The machine, at its own speed: `m` samples of the voices, the bass and the bus FX.
void RenderMachine(float* buf, uint32_t m) {
  for (uint32_t i = 0; i < m; ++i) buf[i] = 0.0f;
  if (m == 0u) return;
  for (int v = 0; v < SP1_DRUMS_N; ++v) {
    if (!tk_active(&voice[v])) {
      fade_left[v] = 0;
      continue;
    }
    const float g = ctl.level[v];
    if (fade_left[v] == 0 && voice[v].engine != TK_ENG_CLASSIC) {
      tk_render_mix(&voice[v], buf, m, g);           // straight into the mix, at its level
      continue;
    }
    for (uint32_t i = 0; i < m; ++i) slot_buf[i] = 0.0f;
    tk_render_add(&voice[v], slot_buf, m);
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
  if (ctl.bass_mode != SP1_BASS_OFF && tb_active(&bass)) {
    tb_render_mix(&bass, buf, m, ctl.bass_level);
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

// This block's cable contributions, from Marbles' outputs (while the clock runs).
void Cables(const float* volts, bool running) {
  for (float& a : g_amt) a = 0.0f;
  for (int s = 0; s < SP1_DRUMS_N; ++s) own_semis[s] = own_tone[s] = own_decay[s] = own_vel[s] = 0.0f;
  if (!running || !volts) return;
  for (int k = 0; k < 4; ++k) {
    const uint8_t d = ctl.x_dest[k];
    if (d == SP1_DX_NONE || d >= SP1_DX_COUNT) continue;
    const float u = Norm(k, volts[k]);
    const float dep = ctl.depth[k];
    const float bi = dep * (2.0f * u - 1.0f), uni = dep * u;
    const int s = OwnSlot(k);
    switch (d) {
      case SP1_DX_PITCH: own_semis[s] += dep * volts[k] * 12.0f; break;   // 1 V / octave
      case SP1_DX_TONE:  own_tone[s] += bi; break;
      case SP1_DX_DECAY: own_decay[s] += bi; break;
      case SP1_DX_VEL:   own_vel[s] += uni; break;
      case SP1_DX_KIT_PITCH: case SP1_DX_KIT_TONE: case SP1_DX_KIT_DECAY: case SP1_DX_FILTER:
        g_amt[d] += bi; break;
      case SP1_DX_FILL: case SP1_DX_FOLD: case SP1_DX_CHEBY: case SP1_DX_FM:
        g_amt[d] += uni; break;
      default: break;
    }
  }
  // Several cables on one destination add up, clamped to what one could do alone.
  for (float& a : g_amt) a = Clamp11(a);
  for (int s = 0; s < SP1_DRUMS_N; ++s) {
    own_semis[s] = own_semis[s] < -24.0f ? -24.0f : (own_semis[s] > 24.0f ? 24.0f : own_semis[s]);
    own_tone[s] = Clamp11(own_tone[s]);
    own_decay[s] = Clamp11(own_decay[s]);
    own_vel[s] = Clamp01(own_vel[s]);
  }
}

void ApplyFx() {
  const float base[4] = {ctl.fx[0], ctl.fx[1], ctl.fx[2], ctl.fx[3]};
  const float cab[4] = {0.5f * g_amt[SP1_DX_FILTER], g_amt[SP1_DX_FOLD], g_amt[SP1_DX_CHEBY],
                        g_amt[SP1_DX_FM]};
  for (int k = 0; k < 4; ++k) {
    const bool on = (ctl.fx_on & (1u << k)) != 0u;
    tfx_set(&fx, static_cast<unsigned>(k),
            on ? Clamp01(base[k] + cab[k]) : (k == TFX_FILTER ? 0.5f : 0.0f));
  }
}

// One 16th of the sequencer: every slot no t output is cabled to plays its map.
void SequencerStep(bool running) {
  if (g16 < 0) return;
  const float fill = ctl.fill + 0.5f * g_amt[SP1_DX_FILL];
  const bool odd = (g16 & 1) != 0;
  for (int s = 0; s < SP1_DRUMS_N; ++s) {
    if (CabledSlot(s)) continue;
    int len = ctl.len[s];
    if (len < 1 || len > kMap) len = 16;
    const int pos = static_cast<int>(g16 % len);
    if (pos == 0 && ctl.deja < 0.5f) {        // the loop comes round: new randomness?
      const float p = (0.5f - ctl.deja) * 2.0f;
      for (int i = 0; i < kMap; ++i) {
        if (0.5f * (Rand11() + 1.0f) < p) rnd[s][i] = Rand11();
      }
    }
    const int bank = ctl.pat[s] < SP1_DRUMS_KITS ? ctl.pat[s] : 0;
    const float wa = static_cast<float>(Weight(bank, s, false, pos));
    const float wb = static_cast<float>(Weight(bank, s, true, pos));
    const float w = wa + (wb - wa) * Clamp01(ctl.morph);
    if (w <= 0.0f) continue;
    const float dens = Clamp01(ctl.density[s] + fill);
    const float thr = 1.0f - dens + 0.3f * ctl.jitter * rnd[s][pos];
    if (w * (1.0f / 9.0f) <= thr) continue;
    const int wi = static_cast<int>(w + 0.5f);
    const float vel = wi >= 9 ? 1.0f : wi >= 7 ? 0.85f : wi >= 5 ? 0.7f : 0.36f + 0.03f * static_cast<float>(wi);
    if (odd && ctl.swing > 0.0f) {
      pend_vel[s] = vel;                       // late, by the swing
    } else {
      const int v = Hit(s, vel, running);
      if (v >= 0) last_hits |= static_cast<uint8_t>(1u << v);
    }
  }
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

extern "C" bool sp1_drums_model_ok(uint8_t eng, uint8_t model) {
  return eng < TK_NUM_ENGINES && model < tk_engine_models(eng);
}

extern "C" int sp1_drums_map_len(int bank, int slot) {
  if (bank < 0 || bank >= SP1_DRUMS_KITS || slot < 0 || slot >= SP1_DRUMS_N) return 16;
  return static_cast<int>(strlen(kBank[bank][slot].a));
}

// ---- audio ----
extern "C" void sp1_drums_init(void) {
  tfx_init(&fx);
  tb_init(&bass);
  memset(&ctl, 0, sizeof ctl);
  for (int v = 0; v < SP1_DRUMS_N; ++v) {
    const sp1_drums_voice_def& d = kKits[0][v];
    tk_init(&voice[v], d.eng, d.model);
    cur_eng[v] = d.eng;
    cur_model[v] = d.model;
    voice_made[v] = true;
    fade_left[v] = 0;
    rests[v] = 0u;
    for (int i = 0; i < kMap; ++i) rnd[v][i] = Rand11();
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
  tfx_set(&fx, TFX_CRUSH, 0.0f);
  tfx_set_carrier(&fx, ctl.fm_hz > 0.0f ? ctl.fm_hz : 196.0f);
  for (int i = 0; i < TB_NPARAM && i < 8; ++i) tb_set(&bass, static_cast<unsigned>(i), Clamp01(ctl.bass_p[i]));
  ApplyFx();
  // The CPU guard: when the last block's render took over kShedAt of its budget, fade the
  // quietest ringing voice if it is a tail (below -20 dB) -- one per block, over the usual
  // 12 ms. It only acts in overload (every voice ringing long, every FX up), the way a
  // hardware drum machine steals a voice, so the next block has room.
  if (load_now > kShedAt) {
    int q = -1;
    float qe = kShedTail;
    for (int v = 0; v < SP1_DRUMS_N; ++v) {
      if (tk_active(&voice[v]) && fade_left[v] == 0 && voice[v].env < qe) {
        q = v;
        qe = voice[v].env;
      }
    }
    if (q >= 0) {
      Choke(q);
      st_shed = st_shed + 1u;
    }
    load_now = 0.0f;                   // one per measured block
  }
}

extern "C" void sp1_drums_set_drive(float gain) {
  drive_to = gain >= 1.0f ? gain : 1.0f;
}

extern "C" void sp1_drums_load(uint32_t cycles, uint32_t frames) {
  // 64 MHz / 48 kHz = 1333.3 cycles per sample (sp1_console.h)
  load_now = frames > 0u ? static_cast<float>(cycles) / (static_cast<float>(frames) * 1333.33f) : 0.0f;
}

extern "C" void sp1_drums_clock(uint32_t frames, bool external) {
  step_block = false;
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
    // The step is over: stop the clock again (outputs read low / 0 V while stopped).
    sp1_marbles_clock(nullptr, SP1_MRB_TP_STOP);
    stepping = false;
    return;
  }
  if (steps_pending <= 0 || external || sp1_marbles_running() || frames == 0u) {
    if (external) steps_pending = 0;
    return;
  }
  if (frames > SP1_MARBLES_MAX_FRAMES) frames = SP1_MARBLES_MAX_FRAMES;
  // A 16th across this block: the ramp rises a quarter of a beat, so the sequencer moves one
  // step and Marbles ticks on every fourth press. CONTINUE does not reset the sequences, so the
  // DEJA VU loop walks on.
  for (uint32_t j = 0; j < frames; ++j) {
    step_beats[j] = step_base + 0.25f * static_cast<float>(j + 1u) / static_cast<float>(frames);
  }
  step_base += 0.25f;
  if (step_base > 4096.0f) step_base -= 4096.0f;    // float precision; a whole number of beats
  sp1_marbles_clock(step_beats, SP1_MRB_TP_CONTINUE);
  --steps_pending;
  stepping = true;
  step_block = true;
}

extern "C" void sp1_drums_render(const sp1_drums_frame* f, float* out, uint32_t n) {
  if (n > kMaxN) n = kMaxN;
  const bool running = f->running;
  if (running && f->volts) {
    for (int k = 0; k < 4; ++k) last_volts[k] = f->volts[k];
  }
  Cables(f->volts, running);
  ApplyFx();

  // ---- where we are: a new 16th each time the master ramp enters a quarter of the beat ----
  if (running && !was_running && !step_block) {        // PLAY: from the top
    g16 = -1;
    last_q = -1;
    step = -1;
    t2_count = 0u;
    for (float& p : pend_vel) p = 0.0f;
  }
  was_running = running;
  bool new_step = false;
  if (running) {
    int q = static_cast<int>(f->ramp * 4.0f);
    q = q < 0 ? 0 : (q > 3 ? 3 : q);
    if (q != last_q) {
      last_q = q;
      ++g16;
      new_step = true;
    }
  }
  const int now_step = running && g16 >= 0 ? static_cast<int>(g16 & 15) : -1;
  if (new_step || now_step != step) {
    // A swung hit still waiting when its step ends plays now, before the next one.
    for (int s = 0; s < SP1_DRUMS_N; ++s) {
      if (pend_vel[s] > 0.0f) {
        const int v = Hit(s, pend_vel[s], running);
        if (v >= 0) last_hits |= static_cast<uint8_t>(1u << v);
        pend_vel[s] = 0.0f;
      }
    }
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
          pend_vel[s] = 0.0f;
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

  // ---- kit rotation, on the next step, beat or bar ----
  if (ctl.rotate_seq != rot_seen) {
    rot_pending += static_cast<int>(ctl.rotate_seq - rot_seen);
    rot_seen = ctl.rotate_seq;
  }
  if (rot_pending) {
    const int every = ctl.rotate_q == 0 ? 1 : (ctl.rotate_q == 2 ? 16 : 4);
    if (!running || (new_step && g16 >= 0 && (g16 % every) == 0)) {
      rot = (rot + rot_pending) & 3;
      rot_pending = 0;
    }
  }

  // ---- this block's hits: Marbles' gates through their cables, then the sequencer ----
  uint8_t struck = 0u;
  if (new_step && step >= 0) {
    accent_now = false;
  }
  if (running && f->rise) {
    // The Y clock: one tick per y_ticks of t2.
    bool yclk = false;
    if (f->rise & 2u) {
      const uint32_t yt = ctl.y_ticks > 0 ? ctl.y_ticks : 8u;
      yclk = (t2_count % yt) == 0u;
      ++t2_count;
    }
    uint8_t src[4] = {static_cast<uint8_t>(f->rise & 1u), static_cast<uint8_t>((f->rise >> 1) & 1u),
                      static_cast<uint8_t>((f->rise >> 2) & 1u), static_cast<uint8_t>(yclk ? 1u : 0u)};
    for (int k = 0; k < 4; ++k) {                // accents first: they lift this tick's hits
      if (src[k] && ctl.t_dest[k] == SP1_DT_ACCENT) accent_now = true;
    }
    for (int k = 0; k < 4; ++k) {
      if (!src[k]) continue;
      const uint8_t d = ctl.t_dest[k];
      if (d >= SP1_DT_SLOT1 && d <= SP1_DT_SLOT4) {
        float vel = 0.9f;
        if (k == 1 && f->volts) vel = 0.35f + 0.65f * Norm(1, f->volts[1]);   // t2: ghosts from X2
        const int v = Hit(d - SP1_DT_SLOT1, vel, true);
        if (v >= 0) struck |= static_cast<uint8_t>(1u << v);
      } else if (d == SP1_DT_BASS) {
        BassHit(0.9f);
      }
    }
    if (yclk && ctl.bass_mode == SP1_BASS_Y) BassHit(0.85f);
  }
  if (new_step && step >= 0) {
    const uint8_t before = last_hits;
    last_hits = 0u;
    SequencerStep(running);
    struck |= last_hits;
    last_hits = before;
    // Bass in the rests: 8th notes on steps every slot has rested.
    if (ctl.bass_mode == SP1_BASS_RESTS && (step & 1) == 0) {
      bool all = true;
      for (int s = 0; s < SP1_DRUMS_N; ++s) all = all && (rests[s] & (1u << step));
      if (all) BassHit(0.85f);
    }
  }
  // Swing: an odd 16th's hits land a little into their step.
  if (running && ctl.swing > 0.0f) {
    const float into = f->ramp * 4.0f - static_cast<float>(last_q);
    if (into >= ctl.swing * (1.0f / 3.0f)) {
      for (int s = 0; s < SP1_DRUMS_N; ++s) {
        if (pend_vel[s] > 0.0f) {
          const int v = Hit(s, pend_vel[s], running);
          if (v >= 0) struck |= static_cast<uint8_t>(1u << v);
          pend_vel[s] = 0.0f;
        }
      }
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
      Strike(v, 0.9f, -1.0f, 0.0f, 0.0f, 0.0f);
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

  // ---- the output stage: leveller, drive, one soft clip ----
  // The leveller holds a stacked kit near one level: a peak follower, 4:1 above kLevThresh,
  // released over 150 ms. Its gain comes from the peaks up to the last call (0.5 ms behind;
  // the clip catches what gets through first). Then SETTINGS' bus drive and the "••" + VOL
  // drive (sp1_drums_set_drive) push into ONE soft clip, x / (1 + |x|), whose top is just
  // under 16-bit full scale, so sp1_synth's stage never hard-clips what leaves here. Below
  // the knee the gain is the old curve's (kOutScale), so a single hit is as loud as before.
  const float x_lev = lev_env > kLevThresh ? kLevThresh / lev_env : 1.0f;
  const float sq = sqrtf(x_lev);
  const float lev_to = sq * sqrtf(sq);                  // x_lev^0.75: 4:1 above the knee
  const float bus = (ctl.bus_gain > 0.0f ? ctl.bus_gain : 1.0f) * kOutScale * drive_to;
  const float g_to = lev_to * bus;
  const float g_from = lev_gain * bus_prev;
  lev_gain = lev_to;
  bus_prev = bus;
  const float inc = n > 0u ? (g_to - g_from) / static_cast<float>(n) : 0.0f;
  float g = g_from, pk = 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    const float in = out[i];
    const float a = fabsf(in);
    pk = a > pk ? a : pk;
    g += inc;
    const float x = in * g;
    out[i] = x / (1.0f + fabsf(x)) * kCeiling;
  }
  const float rel = n >= 24u ? kLevRelease24 : 1.0f - (1.0f - kLevRelease24) * static_cast<float>(n) / 24.0f;
  lev_env = pk > lev_env ? pk : lev_env * rel;

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
  out->bass_notes = st_bass;
  out->shed = st_shed;
}
