#pragma once

#include <pipewire/stream.h>
#include <spa/utils/hook.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;

namespace wiremic::platform {

struct VirtualMicConfig {
  std::string nodeName{"WireMic Virtual Microphone"};
  std::string nodeDescription{"Wireless Mic (from Android)"};
  uint32_t sampleRate{48000};
  uint8_t channels{1};
};

class PipeWireVirtualMic {
 public:
  explicit PipeWireVirtualMic(VirtualMicConfig config);
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
  void FillBuffer(int16_t* dst, uint32_t maxFrames, uint32_t& outFrames);

  VirtualMicConfig config_;
  pw_thread_loop* loop_{nullptr};
  pw_context* context_{nullptr};
  pw_core* core_{nullptr};
  pw_stream* stream_{nullptr};
  struct spa_hook streamListener_{};

  std::vector<int16_t> ringBuffer_;
  size_t writePos_{0};
  size_t readPos_{0};
  size_t available_{0};
  bool running_{false};
};

}  // namespace wiremic::platform
