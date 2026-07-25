#include "PulseAudioVirtualMic.hpp"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>

#include "PipeWireVirtualMic.hpp"

namespace wiremic::platform {

namespace {

std::string RunCommandCaptureStdout(const std::string& command) {
  std::array<char, 256> buffer{};
  std::string result;
  std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(command.c_str(), "r"),
                                              pclose);
  if (!pipe) return result;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get())) {
    result += buffer.data();
  }
  return result;
}

int ParseModuleId(const std::string& output) {
  std::istringstream stream(output);
  int id = -1;
  stream >> id;
  return stream.fail() ? -1 : id;
}

}  // namespace

bool PulseAudioVirtualMic::IsPulseAudioAvailable() {
  int error = 0;
  pa_simple* probe = pa_simple_new(nullptr, "wiremic-probe", PA_STREAM_PLAYBACK,
                                    nullptr, "probe", nullptr, nullptr,
                                    nullptr, &error);
  if (probe) {
    pa_simple_free(probe);
    return true;
  }
  return false;
}

PulseAudioVirtualMic::PulseAudioVirtualMic(const VirtualMicConfig& config)
    : nodeName_(config.nodeName),
      sampleRate_(config.sampleRate),
      channels_(config.channels),
      sinkName_("wiremic_null_sink"),
      sourceName_("wiremic_virtual_mic") {}

PulseAudioVirtualMic::~PulseAudioVirtualMic() { stop(); }

bool PulseAudioVirtualMic::isRunning() const { return running_; }

bool PulseAudioVirtualMic::loadModules() {
  {
    std::ostringstream cmd;
    cmd << "pactl load-module module-null-sink sink_name=" << sinkName_
        << " sink_properties=device.description=WireMic_Virtual_Sink 2>/dev/null";
    const auto output = RunCommandCaptureStdout(cmd.str());
    nullSinkModuleId_ = ParseModuleId(output);
    if (nullSinkModuleId_ < 0) return false;
  }

  {
    std::ostringstream cmd;
    cmd << "pactl load-module module-remap-source master=" << sinkName_
        << ".monitor source_name=" << sourceName_
        << " source_properties=device.description=WireMic_Virtual_Microphone 2>/dev/null";
    const auto output = RunCommandCaptureStdout(cmd.str());
    remapSourceModuleId_ = ParseModuleId(output);
    if (remapSourceModuleId_ < 0) {
      unloadModules();
      return false;
    }
  }

  return true;
}

void PulseAudioVirtualMic::unloadModules() {
  if (remapSourceModuleId_ >= 0) {
    std::ostringstream cmd;
    cmd << "pactl unload-module " << remapSourceModuleId_ << " 2>/dev/null";
    std::system(cmd.str().c_str());
    remapSourceModuleId_ = -1;
  }
  if (nullSinkModuleId_ >= 0) {
    std::ostringstream cmd;
    cmd << "pactl unload-module " << nullSinkModuleId_ << " 2>/dev/null";
    std::system(cmd.str().c_str());
    nullSinkModuleId_ = -1;
  }
}

bool PulseAudioVirtualMic::start() {
  if (running_) return true;
  if (!loadModules()) return false;

  pa_sample_spec spec;
  spec.format = PA_SAMPLE_S16LE;
  spec.rate = sampleRate_;
  spec.channels = channels_;

  int error = 0;
  playbackStream_ =
      pa_simple_new(nullptr, nodeName_.c_str(), PA_STREAM_PLAYBACK,
                     sinkName_.c_str(), "WireMic audio feed", &spec, nullptr,
                     nullptr, &error);

  if (!playbackStream_) {
    unloadModules();
    return false;
  }

  running_ = true;
  return true;
}

void PulseAudioVirtualMic::stop() {
  if (playbackStream_) {
    pa_simple_free(playbackStream_);
    playbackStream_ = nullptr;
  }
  unloadModules();
  running_ = false;
}

void PulseAudioVirtualMic::pushSamples(const int16_t* interleaved,
                                        size_t sampleCount) {
  if (!running_ || !playbackStream_) return;
  int error = 0;
  pa_simple_write(playbackStream_, interleaved,
                   sampleCount * sizeof(int16_t), &error);
}

}  // namespace wiremic::platform
