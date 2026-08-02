#pragma once

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/utils/hook.h>
#include <spa/param/audio/format-utils.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "VirtualMicConfig.hpp"

struct pw_thread_loop;
struct pw_context;
struct pw_core;

namespace wiremic::platform {

class PipeWireVirtualMic {
 public:
  explicit PipeWireVirtualMic(const VirtualMicConfig& config);
  ~PipeWireVirtualMic();

  PipeWireVirtualMic(const PipeWireVirtualMic&) = delete;
  PipeWireVirtualMic& operator=(const PipeWireVirtualMic&) = delete;

  bool start();
  void stop();
  [[nodiscard]] bool isRunning() const;

  void pushSamples(const int16_t* interleaved, size_t sampleCount);

  [[nodiscard]] static bool IsPipeWireAvailable();

 private:
  static void OnProcess(void* userdata);
  static void OnStreamStateChanged(void* userdata, pw_stream_state old_state,
                                    pw_stream_state new_state,
                                    const char* error);
  static void OnParamChanged(void* userdata, uint32_t id,
                              const struct spa_pod* param);
  void fillBuffer(void* dst, uint32_t maxFrames, uint32_t& outFrames);

  VirtualMicConfig config_;
  pw_thread_loop* loop_{nullptr};
  pw_context* context_{nullptr};
  pw_core* core_{nullptr};
  pw_stream* stream_{nullptr};
  struct spa_hook streamListener_{};

  std::vector<int16_t> ringBuffer_;
  std::atomic<uint64_t> writeCount_{0};
  std::atomic<uint64_t> readCount_{0};
  size_t maxBufferedSamples_{0};
  size_t cushionSamples_{0};
  bool primed_{false};
  bool running_{false};

  // Filled in by param_changed once the server has picked a format. Until then
  // there is nothing meaningful to render, so process() stays a no-op.
  std::atomic<uint32_t> negotiatedFormat_{0};
  std::atomic<uint32_t> negotiatedChannels_{0};
  std::atomic<uint32_t> negotiatedStride_{0};
};

}
