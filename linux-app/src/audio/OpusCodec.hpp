#pragma once

#include <cstdint>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace wiremic::audio {

class OpusFrameEncoder {
 public:
  OpusFrameEncoder(uint32_t sampleRate, int channels, int bitrateKbps);
  ~OpusFrameEncoder();

  OpusFrameEncoder(const OpusFrameEncoder&) = delete;
  OpusFrameEncoder& operator=(const OpusFrameEncoder&) = delete;

  std::vector<uint8_t> Encode(const int16_t* pcm, int frameSamples);

 private:
  ::OpusEncoder* encoder_{nullptr};
};

class OpusFrameDecoder {
 public:
  OpusFrameDecoder(uint32_t sampleRate, int channels);
  ~OpusFrameDecoder();

  OpusFrameDecoder(const OpusFrameDecoder&) = delete;
  OpusFrameDecoder& operator=(const OpusFrameDecoder&) = delete;

  std::vector<int16_t> Decode(const uint8_t* data, size_t length,
                               int frameSamples);
  std::vector<int16_t> DecodePacketLoss(int frameSamples);

 private:
  ::OpusDecoder* decoder_{nullptr};
  int channels_;
};

}
