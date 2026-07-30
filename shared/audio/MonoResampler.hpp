#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wiremic::audio {

// Converts whatever rate the microphone hardware granted into the rate the two
// devices negotiated.
//
// This has to be band limited. Linear interpolation is a one-line resampler,
// but it leaves the spectral images of the source rate sitting in the output:
// resampling 44.1 kHz speech to 48 kHz folds a 4 kHz formant back in at
// 7.9 kHz, and the whole band above the voice turns into a broadband hiss that
// tracks the talker. A windowed-sinc polyphase filter puts a real stopband
// where the images are.
//
// Not real-time safe: Reset builds the coefficient table and Process appends to
// a caller-owned vector. Both belong on the sender thread, never on the device
// audio callback.
class MonoResampler {
 public:
  void Reset(int32_t inputRate, int32_t outputRate) {
    inputRate_ = inputRate > 0 ? inputRate : 48000;
    outputRate_ = outputRate > 0 ? outputRate : 48000;
    cursor_ = static_cast<double>(kHalfTaps - 1);
    history_.assign(kTaps - 1, 0);
    work_.clear();
    if (!passthrough()) buildTable();
  }

  [[nodiscard]] bool passthrough() const { return inputRate_ == outputRate_; }

  void Process(const int16_t* input, size_t count, std::vector<int16_t>& out) {
    if (count == 0) return;

    if (passthrough()) {
      out.insert(out.end(), input, input + count);
      return;
    }

    work_.clear();
    work_.reserve(history_.size() + count);
    work_.insert(work_.end(), history_.begin(), history_.end());
    work_.insert(work_.end(), input, input + count);

    const double ratio =
        static_cast<double>(inputRate_) / static_cast<double>(outputRate_);

    while (static_cast<size_t>(cursor_) + kHalfTaps < work_.size()) {
      const size_t index = static_cast<size_t>(cursor_);
      const double fraction = cursor_ - static_cast<double>(index);
      const size_t base = index - (kHalfTaps - 1);

      // The table is sampled at kPhases positions between two input samples;
      // blending the two nearest rows keeps the residual phase error far below
      // the noise floor instead of adding its own quantisation hiss.
      double scaled = fraction * kPhases;
      size_t phase = static_cast<size_t>(scaled);
      if (phase >= kPhases) phase = kPhases - 1;
      const float blend = static_cast<float>(scaled - static_cast<double>(phase));

      const float* row = table_.data() + phase * kTaps;
      const float* nextRow = row + kTaps;

      float accumulator = 0.0f;
      for (size_t tap = 0; tap < kTaps; ++tap) {
        const float coefficient =
            row[tap] + (nextRow[tap] - row[tap]) * blend;
        accumulator += static_cast<float>(work_[base + tap]) * coefficient;
      }

      int32_t sample = static_cast<int32_t>(
          accumulator >= 0.0f ? accumulator + 0.5f : accumulator - 0.5f);
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      out.push_back(static_cast<int16_t>(sample));

      cursor_ += ratio;
    }

    // Keep only the tail the next output sample can still reach back into.
    const size_t keepFrom = static_cast<size_t>(cursor_) - (kHalfTaps - 1);
    history_.assign(work_.begin() + static_cast<long>(keepFrom), work_.end());
    cursor_ -= static_cast<double>(keepFrom);
  }

 private:
  static constexpr size_t kTaps = 32;
  static constexpr size_t kHalfTaps = kTaps / 2;
  static constexpr size_t kPhases = 256;

  static double BesselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 40; ++k) {
      const double half = x / (2.0 * k);
      term *= half * half;
      sum += term;
      if (term < sum * 1e-14) break;
    }
    return sum;
  }

  void buildTable() {
    // When downsampling the stopband has to start at the *output* Nyquist or
    // the discarded top end aliases back down; when upsampling the source is
    // already band limited and the filter only has to suppress the images.
    const double rateRatio =
        static_cast<double>(outputRate_) / static_cast<double>(inputRate_);
    const double cutoff = 0.92 * (rateRatio < 1.0 ? rateRatio : 1.0);

    constexpr double kKaiserBeta = 8.6;
    const double normalisation = BesselI0(kKaiserBeta);

    table_.assign((kPhases + 1) * kTaps, 0.0f);

    for (size_t phase = 0; phase <= kPhases; ++phase) {
      const double fraction =
          static_cast<double>(phase) / static_cast<double>(kPhases);

      double sum = 0.0;
      for (size_t tap = 0; tap < kTaps; ++tap) {
        const double distance = static_cast<double>(kHalfTaps - 1) + fraction -
                                static_cast<double>(tap);

        const double scaled = cutoff * distance;
        const double sinc =
            std::fabs(scaled) < 1e-9
                ? 1.0
                : std::sin(M_PI * scaled) / (M_PI * scaled);

        const double u = distance / static_cast<double>(kHalfTaps);
        const double window =
            std::fabs(u) >= 1.0
                ? 0.0
                : BesselI0(kKaiserBeta * std::sqrt(1.0 - u * u)) / normalisation;

        const double value = sinc * window;
        table_[phase * kTaps + tap] = static_cast<float>(value);
        sum += value;
      }

      // Unity DC gain per phase, otherwise the output level ripples at the
      // difference between the two rates.
      if (sum > 1e-9) {
        for (size_t tap = 0; tap < kTaps; ++tap) {
          table_[phase * kTaps + tap] /= static_cast<float>(sum);
        }
      }
    }
  }

  int32_t inputRate_{48000};
  int32_t outputRate_{48000};
  double cursor_{static_cast<double>(kHalfTaps - 1)};
  std::vector<int16_t> history_ = std::vector<int16_t>(kTaps - 1, 0);
  std::vector<int16_t> work_;
  std::vector<float> table_;
};

}
