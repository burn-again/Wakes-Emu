// wakes-sp1 — a tight, modulated delay with feedback (experimental, CONFIG_SP1_COMB;
// docs/HEADROOM.md).
//
// One delay line, read at a fractional position, a one-pole low-pass in the feedback loop
// and a slow sine on the delay time. Tuned to the period of the note the voice plays and fed
// back hard, it is Karplus-Strong: the voice becomes the excitation of a string at its own
// pitch. Set to a few milliseconds and swept, it is a flanger; to ~10 ms with little
// feedback, a chorus. Header-only and framework-agnostic, like sp1_synth.cc's other DSP.
#ifndef SP1_COMB_H
#define SP1_COMB_H

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sp1 {

class Comb {
 public:
  // > 48000 / 27.5 Hz (A0): the period of any note a Plaits voice reaches, twice over for
  // the octave-down string. 8 KB.
  static const size_t kSize = 2048;

  struct Settings {
    float input;      // how much of the voice goes into the loop
    float feedback;   // < 1
    float damp;       // the loop low-pass, 0..1 (1 = no damping)
    float depth;      // modulation, a fraction of the delay
    float rate;       // modulation, cycles per sample
    float mix;        // 0 dry .. 1 wet
  };

  void Init() {
    for (size_t i = 0; i < kSize; ++i) {
      line_[i] = 0.0f;
    }
    w_ = 0u;
    lp_ = 0.0f;
    phase_ = 0.0f;
    delay_ = 100.0f;
    started_ = false;
  }

  // One block, in place, at +-1. `delay` in samples: the target, glided to over the block
  // (a new note slides there in a block, rather than clicking).
  void Process(float* io, size_t n, float delay, const Settings& s) {
    // The loop low-pass delays low frequencies by (1 - a) / a samples: take it off, so a
    // tuned comb is in tune.
    delay -= (1.0f - s.damp) / s.damp;
    phase_ += s.rate * static_cast<float>(n);
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    float target = delay * (1.0f + s.depth * sinf(6.2831853f * phase_));
    const float lo = 2.0f, hi = static_cast<float>(kSize) - 3.0f;
    target = target < lo ? lo : (target > hi ? hi : target);
    if (!started_) {
      delay_ = target;
      started_ = true;
    }
    float d = delay_;
    const float step = (target - d) / static_cast<float>(n);
    delay_ = target;
    // Locals, so the loop does not reload them after every store to `io` (which the
    // compiler cannot prove does not alias them): ~25 % fewer instructions a sample.
    const float input = s.input, feedback = s.feedback, damp = s.damp, mix = s.mix;
    const float size = static_cast<float>(kSize);
    float* const line = line_;
    float lp = lp_;
    uint32_t w = w_;
    float wf = static_cast<float>(w);
    for (size_t i = 0; i < n; ++i) {
      d += step;
      float rp = wf - d;
      if (rp < 0.0f) {
        rp += size;
      }
      const uint32_t i0 = static_cast<uint32_t>(rp);
      const float frac = rp - static_cast<float>(i0);
      const float a = line[i0 & (kSize - 1)];
      const float b = line[(i0 + 1) & (kSize - 1)];
      lp += damp * (a + (b - a) * frac - lp);
      const float x = io[i];
      float in = input * x + feedback * lp;
      in = in > 2.0f ? 2.0f : (in < -2.0f ? -2.0f : in);   // bounded whatever comes in
      line[w] = in;
      w = (w + 1u) & (kSize - 1);
      wf = w ? wf + 1.0f : 0.0f;
      io[i] = x + mix * (lp - x);
    }
    lp_ = lp;
    w_ = w;
  }

 private:
  float line_[kSize];
  uint32_t w_;
  float lp_;
  float phase_;
  float delay_;
  bool started_;
};

}  // namespace sp1

#endif  // SP1_COMB_H
