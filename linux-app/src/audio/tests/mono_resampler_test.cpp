#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "Check.hpp"
#include "MonoResampler.hpp"

using wiremic::audio::MonoResampler;

namespace {

constexpr double kToneHz = 440.0;
constexpr double kAmplitude = 10000.0;

double Rms(const std::vector<int16_t>& pcm, size_t from) {
  if (pcm.size() <= from) return 0.0;
  double sum = 0.0;
  for (size_t i = from; i < pcm.size(); ++i) {
    sum += static_cast<double>(pcm[i]) * pcm[i];
  }
  return std::sqrt(sum / static_cast<double>(pcm.size() - from));
}

double DominantHz(const std::vector<int16_t>& pcm, size_t from, int32_t rate) {
  size_t crossings = 0;
  for (size_t i = from + 1; i < pcm.size(); ++i) {
    if ((pcm[i - 1] < 0) != (pcm[i] < 0)) ++crossings;
  }
  const double seconds = static_cast<double>(pcm.size() - from) / rate;
  if (seconds <= 0.0) return 0.0;
  return static_cast<double>(crossings) / (2.0 * seconds);
}

// The device hands audio over in small bursts, so the resampler has to stay
// continuous across call boundaries. Feeding it one big block would hide
// exactly the discontinuity that matters.
bool RunRate(int32_t inputRate, int32_t outputRate, int callbackFrames) {
  MonoResampler resampler;
  resampler.Reset(inputRate, outputRate);

  std::vector<int16_t> out;
  double phase = 0.0;
  const double step = 2.0 * M_PI * kToneHz / inputRate;

  const int callbacks = inputRate / callbackFrames;
  std::vector<int16_t> chunk(callbackFrames);

  for (int c = 0; c < callbacks; ++c) {
    for (int i = 0; i < callbackFrames; ++i) {
      chunk[i] = static_cast<int16_t>(kAmplitude * std::sin(phase));
      phase += step;
    }
    resampler.Process(chunk.data(), chunk.size(), out);
  }

  const size_t skip = static_cast<size_t>(outputRate) / 20;
  const double rms = Rms(out, skip);
  const double hz = DominantHz(out, skip, outputRate);
  const double expected =
      static_cast<double>(inputRate) * callbacks * callbackFrames /
      static_cast<double>(inputRate) * outputRate / outputRate;
  const double producedSeconds =
      static_cast<double>(out.size()) / static_cast<double>(outputRate);

  int glitches = 0;
  const int maxStep =
      static_cast<int>(kAmplitude * 2.0 * M_PI * kToneHz / outputRate) + 1;
  for (size_t i = skip + 1; i < out.size(); ++i) {
    if (std::abs(out[i] - out[i - 1]) > maxStep * 4) ++glitches;
  }

  std::cout << "  " << inputRate << " -> " << outputRate
            << " (cb=" << callbackFrames << ")"
            << " outHz=" << static_cast<int>(hz + 0.5)
            << " rms=" << static_cast<int>(rms)
            << " seconds=" << producedSeconds
            << " glitches=" << glitches << "\n";

  (void)expected;

  if (std::abs(hz - kToneHz) > 8.0) return false;
  if (rms < kAmplitude * 0.6 || rms > kAmplitude * 0.85) return false;
  if (glitches != 0) return false;
  if (std::abs(producedSeconds - 1.0) > 0.02) return false;
  return true;
}

}

int main() {
  std::cout << "MONO RESAMPLER (device rate -> negotiated rate)\n";

  bool ok = true;
  const int callbacks[] = {96, 240, 480};

  for (const int cb : callbacks) {
    ok &= RunRate(48000, 48000, cb);
    ok &= RunRate(44100, 48000, cb);
    ok &= RunRate(16000, 48000, cb);
    ok &= RunRate(48000, 44100, cb);
  }

  WIREMIC_CHECK(ok);
  std::cout << "MONO_RESAMPLER_TESTS_PASSED\n";
  return 0;
}
