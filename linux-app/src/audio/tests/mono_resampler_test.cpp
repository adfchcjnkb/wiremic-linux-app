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

// A pure tone in has to be a pure tone out. Whatever is left after the
// fundamental is subtracted is the resampler's own contribution: the images of
// the source rate folded back into the audible band. That residue is what the
// ear hears as a gritty hiss riding on speech, and it is invisible to a test
// that only checks pitch and level.
double SpuriousRejectionDb(int32_t inputRate, int32_t outputRate,
                            double toneHz, int callbackFrames) {
  MonoResampler resampler;
  resampler.Reset(inputRate, outputRate);

  std::vector<int16_t> out;
  double phase = 0.0;
  const double step = 2.0 * M_PI * toneHz / inputRate;
  std::vector<int16_t> chunk(callbackFrames);

  const int callbacks = inputRate / callbackFrames;
  for (int c = 0; c < callbacks; ++c) {
    for (int i = 0; i < callbackFrames; ++i) {
      chunk[i] = static_cast<int16_t>(kAmplitude * std::sin(phase));
      phase += step;
    }
    resampler.Process(chunk.data(), chunk.size(), out);
  }

  // Skip the start-up transient: the filter history begins as silence.
  const size_t skip = static_cast<size_t>(outputRate) / 10;
  if (out.size() <= skip + 1000) return 0.0;

  // Least-squares fit of the fundamental, which absorbs any amplitude and
  // phase shift the filter applies, then measure what is left over.
  const double w = 2.0 * M_PI * toneHz / outputRate;
  double sumCosCos = 0.0, sumSinSin = 0.0, sumCosSin = 0.0;
  double sumXCos = 0.0, sumXSin = 0.0;
  for (size_t i = skip; i < out.size(); ++i) {
    const double t = static_cast<double>(i);
    const double c = std::cos(w * t);
    const double s = std::sin(w * t);
    sumCosCos += c * c;
    sumSinSin += s * s;
    sumCosSin += c * s;
    sumXCos += out[i] * c;
    sumXSin += out[i] * s;
  }
  const double det = sumCosCos * sumSinSin - sumCosSin * sumCosSin;
  if (std::abs(det) < 1e-9) return 0.0;
  const double a = (sumXCos * sumSinSin - sumXSin * sumCosSin) / det;
  const double b = (sumXSin * sumCosCos - sumXCos * sumCosSin) / det;

  double signalPower = 0.0, residualPower = 0.0;
  for (size_t i = skip; i < out.size(); ++i) {
    const double t = static_cast<double>(i);
    const double fitted = a * std::cos(w * t) + b * std::sin(w * t);
    const double residual = out[i] - fitted;
    signalPower += fitted * fitted;
    residualPower += residual * residual;
  }
  if (residualPower <= 0.0) return 120.0;
  return 10.0 * std::log10(signalPower / residualPower);
}

bool RunRejection(int32_t inputRate, int32_t outputRate, double toneHz,
                   double minimumDb) {
  const double db = SpuriousRejectionDb(inputRate, outputRate, toneHz, 240);
  std::cout << "  " << inputRate << " -> " << outputRate << " @ "
            << static_cast<int>(toneHz) << " Hz: rejection " << db
            << " dB (need " << minimumDb << ")\n";
  return db >= minimumDb;
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

  std::cout << "IMAGE REJECTION (a pure tone must stay a pure tone)\n";
  // 60 dB puts the folded images below the noise floor of the microphone
  // itself. Plain linear interpolation manages roughly 25 dB here.
  ok &= RunRejection(44100, 48000, 4000.0, 60.0);
  ok &= RunRejection(44100, 48000, 1000.0, 60.0);
  ok &= RunRejection(16000, 48000, 3000.0, 60.0);
  ok &= RunRejection(48000, 44100, 4000.0, 60.0);

  WIREMIC_CHECK(ok);
  std::cout << "MONO_RESAMPLER_TESTS_PASSED\n";
  return 0;
}
