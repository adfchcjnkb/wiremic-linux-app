#include "PulseAudioVirtualMic.hpp"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <iostream>

namespace wiremic::platform {

PulseAudioVirtualMic::PulseAudioVirtualMic(const VirtualMicConfig& config)
    : config_(config),
      sinkName_("wiremic_null_sink"),
      sourceName_("wiremic_virtual_mic") {}

PulseAudioVirtualMic::~PulseAudioVirtualMic() {
    stop();
}

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

bool PulseAudioVirtualMic::runPactlCommand(const std::string& cmd, std::string& output) {
    std::array<char, 256> buffer{};
    std::string result;

    std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get())) {
        result += buffer.data();
    }

    output = result;
    return !result.empty();
}

int PulseAudioVirtualMic::parseModuleId(const std::string& output) {
    std::istringstream stream(output);
    int id = -1;
    stream >> id;
    return stream.fail() ? -1 : id;
}

bool PulseAudioVirtualMic::loadModules() {
    std::string output;

    std::ostringstream cmd1;
    cmd1 << "pactl load-module module-null-sink sink_name=" << sinkName_
         << " sink_properties=device.description=WireMic_Virtual_Sink 2>/dev/null";

    if (!runPactlCommand(cmd1.str(), output)) {
        std::cerr << "Failed to run pactl for null sink" << std::endl;
        return false;
    }

    nullSinkModuleId_ = parseModuleId(output);
    if (nullSinkModuleId_ < 0) {
        std::cerr << "Failed to parse null sink module ID: " << output << std::endl;
        return false;
    }

    std::ostringstream cmd2;
    cmd2 << "pactl load-module module-remap-source master=" << sinkName_
         << ".monitor source_name=" << sourceName_
         << " source_properties=device.description=WireMic_Virtual_Microphone 2>/dev/null";

    if (!runPactlCommand(cmd2.str(), output)) {
        std::cerr << "Failed to run pactl for remap source" << std::endl;
        unloadModules();
        return false;
    }

    remapSourceModuleId_ = parseModuleId(output);
    if (remapSourceModuleId_ < 0) {
        std::cerr << "Failed to parse remap source module ID: " << output << std::endl;
        unloadModules();
        return false;
    }

    return true;
}

void PulseAudioVirtualMic::unloadModules() {
    std::string output;

    if (remapSourceModuleId_ >= 0) {
        std::ostringstream cmd;
        cmd << "pactl unload-module " << remapSourceModuleId_ << " 2>/dev/null";
        runPactlCommand(cmd.str(), output);
        remapSourceModuleId_ = -1;
    }

    if (nullSinkModuleId_ >= 0) {
        std::ostringstream cmd;
        cmd << "pactl unload-module " << nullSinkModuleId_ << " 2>/dev/null";
        runPactlCommand(cmd.str(), output);
        nullSinkModuleId_ = -1;
    }
}

bool PulseAudioVirtualMic::start() {
    if (running_) return true;

    if (!loadModules()) {
        return false;
    }

    pa_sample_spec spec;
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = config_.sampleRate;
    spec.channels = config_.channels;

    int error = 0;
    playbackStream_ = pa_simple_new(nullptr,
                                    config_.nodeName.c_str(),
                                    PA_STREAM_PLAYBACK,
                                    sinkName_.c_str(),
                                    "WireMic audio feed",
                                    &spec,
                                    nullptr,
                                    nullptr,
                                    &error);

    if (!playbackStream_) {
        std::cerr << "Failed to create PulseAudio stream: " << pa_strerror(error) << std::endl;
        unloadModules();
        return false;
    }

    running_ = true;
    writerRunning_ = true;
    writerThread_ = std::thread(&PulseAudioVirtualMic::writerLoop, this);
    return true;
}

void PulseAudioVirtualMic::stop() {
    writerRunning_ = false;
    queueSignal_.notify_all();
    if (writerThread_.joinable()) writerThread_.join();

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.clear();
    }

    if (playbackStream_) {
        pa_simple_free(playbackStream_);
        playbackStream_ = nullptr;
    }

    unloadModules();
    running_ = false;
}

void PulseAudioVirtualMic::writerLoop() {
    while (true) {
        std::vector<int16_t> frame;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueSignal_.wait(lock, [this] {
                return !queue_.empty() || !writerRunning_;
            });
            if (!writerRunning_ && queue_.empty()) return;
            frame = std::move(queue_.front());
            queue_.pop_front();
        }

        int error = 0;
        if (pa_simple_write(playbackStream_, frame.data(),
                            frame.size() * sizeof(int16_t), &error) < 0) {
            std::cerr << "Failed to write to PulseAudio: " << pa_strerror(error)
                      << std::endl;
        }
    }
}

bool PulseAudioVirtualMic::isRunning() const {
    return running_;
}

void PulseAudioVirtualMic::pushSamples(const int16_t* interleaved,
                                        size_t sampleCount) {
    if (!running_ || !playbackStream_ || interleaved == nullptr || sampleCount == 0) {
        return;
    }

    constexpr size_t kMaxQueuedFrames = 100;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.size() >= kMaxQueuedFrames) queue_.pop_front();
        queue_.emplace_back(interleaved, interleaved + sampleCount);
    }
    queueSignal_.notify_one();
}

}
