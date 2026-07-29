#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "Check.hpp"
#include "JitterBuffer.hpp"
#include "OpusCodec.hpp"

using namespace wiremic::audio;

namespace {

constexpr uint32_t kRate = 48000;

std::vector<int16_t> Speechlike(size_t samples) {
  std::vector<int16_t> pcm(samples);
  double phase = 0.0;
  for (size_t i = 0; i < samples; ++i) {
    const double t = static_cast<double>(i) / kRate;
    const double f0 = 130.0 + 30.0 * std::sin(2.0 * M_PI * 3.0 * t);
    phase += 2.0 * M_PI * f0 / kRate;
    const double env = 0.5 + 0.5 * std::sin(2.0 * M_PI * 2.5 * t);
    const double v = std::sin(phase) + 0.5 * std::sin(2 * phase) +
                     0.25 * std::sin(3 * phase);
    pcm[i] = static_cast<int16_t>(9000.0 * env * v / 1.75);
  }
  return pcm;
}

// Digital silence in the middle of speech is what the listener hears as a
// dropout. Concealment should always produce *something*.
double SilenceMsIn(const std::vector<int16_t>& pcm) {
  size_t zeros = 0;
  size_t run = 0;
  for (const auto v : pcm) {
    if (v == 0) {
      ++run;
    } else {
      if (run >= 48) zeros += run;
      run = 0;
    }
  }
  if (run >= 48) zeros += run;
  return static_cast<double>(zeros) * 1000.0 / kRate;
}

bool RunLoss(uint8_t frameSizeMs, int lossPercent) {
  const int frameSamples = static_cast<int>(kRate) * frameSizeMs / 1000;
  const size_t frames = 400;

  OpusFrameEncoder encoder(kRate, 1, 96);
  OpusFrameDecoder decoder(kRate, 1);
  JitterBuffer buffer(frameSizeMs);

  const auto input = Speechlike(static_cast<size_t>(frameSamples) * frames);

  std::srand(1234);
  const auto now = std::chrono::steady_clock::now();

  std::vector<int16_t> output;
  output.reserve(input.size());

  size_t concealed = 0;
  size_t fecRecovered = 0;

  for (size_t f = 0; f < frames; ++f) {
    auto payload = encoder.Encode(input.data() + f * frameSamples, frameSamples);
    const bool drop = f > 5 && (std::rand() % 100) < lossPercent;
    if (!drop && !payload.empty()) {
      buffer.Push(f, std::move(payload), false,
                  now + std::chrono::milliseconds(f * frameSizeMs));
    }

    while (true) {
      auto outcome = buffer.Pop();
      if (outcome.result == JitterPopResult::NotReady) break;

      if (outcome.result == JitterPopResult::Ready) {
        auto pcm = decoder.Decode(outcome.payload.data(),
                                   outcome.payload.size(), frameSamples);
        output.insert(output.end(), pcm.begin(), pcm.end());
      } else if (outcome.result == JitterPopResult::Loss) {
        ++concealed;
        if (!outcome.fecPayload.empty()) {
          ++fecRecovered;
          auto pcm = decoder.DecodeWithFec(outcome.fecPayload.data(),
                                            outcome.fecPayload.size(),
                                            frameSamples);
          output.insert(output.end(), pcm.begin(), pcm.end());
        } else {
          auto pcm = decoder.DecodePacketLoss(frameSamples);
          output.insert(output.end(), pcm.begin(), pcm.end());
        }
      }
      break;
    }
  }

  const double silenceMs = SilenceMsIn(output);

  std::cout << "  frame=" << static_cast<int>(frameSizeMs) << "ms loss="
            << lossPercent << "%  concealed=" << concealed
            << " fecRecovered=" << fecRecovered
            << " silence=" << silenceMs << "ms\n";

  // Opus carries in-band FEC in its SILK layer, whose shortest frame is 10 ms.
  // At 5 ms the encoder is CELT-only, FEC cannot exist, and a burst of losses
  // eventually fades to digital silence no matter what the decoder does. Every
  // mode that can protect itself must; the 5 ms mode is only held to a bound.
  const bool fecAvailable = frameSizeMs >= 10;
  return fecAvailable ? silenceMs < 1.0 : silenceMs < 40.0;
}

}

int main() {
  std::cout << "PACKET LOSS CONCEALMENT\n";

  bool ok = true;
  for (const uint8_t frameSizeMs : {5, 10, 20}) {
    for (const int loss : {2, 5, 10, 20}) {
      ok &= RunLoss(frameSizeMs, loss);
    }
  }

  WIREMIC_CHECK(ok);
  std::cout << "PACKET_LOSS_TESTS_PASSED\n";
  return 0;
}
