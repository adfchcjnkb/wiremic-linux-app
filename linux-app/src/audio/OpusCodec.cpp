#include "OpusCodec.hpp"

#include <opus/opus.h>

#include <stdexcept>

namespace wiremic::audio {

OpusFrameEncoder::OpusFrameEncoder(uint32_t sampleRate, int channels,
                                    int bitrateKbps) {
  int error = 0;
  encoder_ = opus_encoder_create(static_cast<opus_int32>(sampleRate),
                                  channels,
                                  OPUS_APPLICATION_VOIP,
                                  &error);
  if (error != OPUS_OK || encoder_ == nullptr) {
    throw std::runtime_error("Failed to create Opus encoder");
  }

  opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrateKbps * 1000));
  opus_encoder_ctl(encoder_, OPUS_SET_VBR(1));
  opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(5));
  opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
  opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(10));
}

OpusFrameEncoder::~OpusFrameEncoder() {
  if (encoder_) opus_encoder_destroy(encoder_);
}

std::vector<uint8_t> OpusFrameEncoder::Encode(const int16_t* pcm,
                                               int frameSamples) {
  std::vector<uint8_t> output(1275);
  const int bytesWritten =
      opus_encode(encoder_, pcm, frameSamples, output.data(),
                  static_cast<opus_int32>(output.size()));
  if (bytesWritten < 0) {
    throw std::runtime_error("Opus encode failed");
  }
  output.resize(static_cast<size_t>(bytesWritten));
  return output;
}

OpusFrameDecoder::OpusFrameDecoder(uint32_t sampleRate, int channels)
    : channels_(channels) {
  int error = 0;
  decoder_ = opus_decoder_create(static_cast<opus_int32>(sampleRate),
                                  channels, &error);
  if (error != OPUS_OK || decoder_ == nullptr) {
    throw std::runtime_error("Failed to create Opus decoder");
  }
}

OpusFrameDecoder::~OpusFrameDecoder() {
  if (decoder_) opus_decoder_destroy(decoder_);
}

std::vector<int16_t> OpusFrameDecoder::Decode(const uint8_t* data,
                                               size_t length,
                                               int frameSamples) {
  std::vector<int16_t> pcm(static_cast<size_t>(frameSamples) *
                            static_cast<size_t>(channels_));
  const int decodedSamples =
      opus_decode(decoder_, data, static_cast<opus_int32>(length), pcm.data(),
                  frameSamples, 0);
  if (decodedSamples < 0) {
    throw std::runtime_error("Opus decode failed");
  }
  pcm.resize(static_cast<size_t>(decodedSamples) *
             static_cast<size_t>(channels_));
  return pcm;
}

std::vector<int16_t> OpusFrameDecoder::DecodeWithFec(const uint8_t* nextPacket,
                                                      size_t length,
                                                      int frameSamples) {
  std::vector<int16_t> pcm(static_cast<size_t>(frameSamples) *
                            static_cast<size_t>(channels_));
  const int decodedSamples =
      opus_decode(decoder_, nextPacket, static_cast<opus_int32>(length),
                  pcm.data(), frameSamples, 1);
  if (decodedSamples < 0) {
    throw std::runtime_error("Opus FEC decode failed");
  }
  pcm.resize(static_cast<size_t>(decodedSamples) *
             static_cast<size_t>(channels_));
  return pcm;
}

std::vector<int16_t> OpusFrameDecoder::DecodePacketLoss(int frameSamples) {
  std::vector<int16_t> pcm(static_cast<size_t>(frameSamples) *
                            static_cast<size_t>(channels_));
  const int decodedSamples =
      opus_decode(decoder_, nullptr, 0, pcm.data(), frameSamples, 0);
  if (decodedSamples < 0) {
    throw std::runtime_error("Opus PLC decode failed");
  }
  pcm.resize(static_cast<size_t>(decodedSamples) *
             static_cast<size_t>(channels_));
  return pcm;
}

}
