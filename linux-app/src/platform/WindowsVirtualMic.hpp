#pragma once

#ifdef _WIN32

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "VirtualMicConfig.hpp"

struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;
struct tWAVEFORMATEX;

namespace wiremic::platform {

class WindowsVirtualMic {
 public:
  explicit WindowsVirtualMic(const VirtualMicConfig& config);
  ~WindowsVirtualMic();

  WindowsVirtualMic(const WindowsVirtualMic&) = delete;
  WindowsVirtualMic& operator=(const WindowsVirtualMic&) = delete;

  bool start();
  void stop();
  [[nodiscard]] bool isRunning() const;

  void pushSamples(const int16_t* interleaved, size_t sampleCount);

  [[nodiscard]] const std::string& lastError() const { return lastError_; }

  [[nodiscard]] static bool IsCableInstalled();
  [[nodiscard]] static std::string CableRenderDeviceName();
  [[nodiscard]] static std::string CableCaptureDeviceName();
  static bool MakeCableDefaultCaptureDevice(std::string* error);
  static void OpenSoundControlPanel();

 private:
  bool findCableRenderDevice(IMMDevice** device);
  void renderLoop();
  void writeInto(unsigned char* destination, uint32_t frames);

  VirtualMicConfig config_;
  std::string lastError_;

  IMMDevice* device_{nullptr};
  IAudioClient* client_{nullptr};
  IAudioRenderClient* render_{nullptr};
  tWAVEFORMATEX* mixFormat_{nullptr};
  void* renderEvent_{nullptr};

  uint32_t bufferFrames_{0};
  uint32_t deviceChannels_{2};
  uint32_t deviceRate_{48000};
  bool deviceIsFloat_{true};

  std::thread renderThread_;
  std::atomic<bool> running_{false};

  std::vector<int16_t> ring_;
  std::atomic<uint64_t> writeCount_{0};
  std::atomic<uint64_t> readCount_{0};
  size_t maxBufferedSamples_{0};
  double resampleCursor_{0.0};
  int16_t resampleHistory_{0};
  bool haveResampleHistory_{false};
};

}

#endif
