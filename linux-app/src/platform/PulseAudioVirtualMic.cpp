#include "PulseAudioVirtualMic.hpp"

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <vector>
#include <iostream>

namespace wiremic::platform {

namespace {
constexpr uint32_t kTargetBufferFrames = 3;
}

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

PulseAudioVirtualMic::ServerFlavour PulseAudioVirtualMic::QueryServerFlavour() {
    pa_mainloop* loop = pa_mainloop_new();
    if (!loop) return ServerFlavour::None;

    pa_context* context =
        pa_context_new(pa_mainloop_get_api(loop), "wiremic-detect");
    if (!context) {
        pa_mainloop_free(loop);
        return ServerFlavour::None;
    }

    auto flavour = ServerFlavour::None;
    bool finished = false;

    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(context);
        pa_mainloop_free(loop);
        return ServerFlavour::None;
    }

    struct Query {
        ServerFlavour* flavour;
        bool* finished;
        pa_context* context;
    } query{&flavour, &finished, context};

    pa_context_set_state_callback(
        context,
        [](pa_context* ctx, void* userdata) {
            auto* q = static_cast<Query*>(userdata);
            switch (pa_context_get_state(ctx)) {
                case PA_CONTEXT_READY:
                    pa_operation_unref(pa_context_get_server_info(
                        ctx,
                        [](pa_context*, const pa_server_info* info,
                           void* inner) {
                            auto* q2 = static_cast<Query*>(inner);
                            const std::string name =
                                info && info->server_name ? info->server_name : "";
                            std::string lowered = name;
                            std::transform(lowered.begin(), lowered.end(),
                                           lowered.begin(), ::tolower);
                            *q2->flavour =
                                lowered.find("pipewire") != std::string::npos
                                    ? ServerFlavour::PipeWirePulse
                                    : ServerFlavour::PulseAudio;
                            *q2->finished = true;
                        },
                        q));
                    break;
                case PA_CONTEXT_FAILED:
                case PA_CONTEXT_TERMINATED:
                    *q->finished = true;
                    break;
                default:
                    break;
            }
        },
        &query);

    constexpr int kMaxIterations = 200;
    for (int i = 0; i < kMaxIterations && !finished; ++i) {
        if (pa_mainloop_iterate(loop, 1, nullptr) < 0) break;
    }

    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(loop);
    return flavour;
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

void PulseAudioVirtualMic::unloadStaleModules() {
    std::string listing;
    if (!runPactlCommand("pactl list modules short 2>/dev/null", listing)) {
        return;
    }

    std::istringstream stream(listing);
    std::string line;
    std::vector<int> staleIds;

    while (std::getline(stream, line)) {
        if (line.find(sinkName_) == std::string::npos &&
            line.find(sourceName_) == std::string::npos) {
            continue;
        }
        std::istringstream lineStream(line);
        int id = -1;
        lineStream >> id;
        if (!lineStream.fail() && id >= 0) staleIds.push_back(id);
    }

    std::sort(staleIds.rbegin(), staleIds.rend());

    std::string output;
    for (const int id : staleIds) {
        std::ostringstream cmd;
        cmd << "pactl unload-module " << id << " 2>/dev/null";
        runPactlCommand(cmd.str(), output);
    }
}

bool PulseAudioVirtualMic::loadModules() {
    std::string output;

    unloadStaleModules();

    std::ostringstream cmd1;
    cmd1 << "pactl load-module module-null-sink sink_name=" << sinkName_
         << " rate=" << config_.sampleRate
         << " channels=" << static_cast<unsigned>(config_.channels)
         << " format=s16le"
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

    const uint32_t bytesPerSample =
        static_cast<uint32_t>(sizeof(int16_t)) * config_.channels;
    const uint32_t frameBytes =
        bytesPerSample *
        std::max<uint32_t>(1, config_.sampleRate * config_.frameSizeMs / 1000);

    pa_buffer_attr attr{};
    attr.maxlength = frameBytes * kTargetBufferFrames * 3;
    attr.tlength = frameBytes * kTargetBufferFrames;
    attr.prebuf = 0;
    attr.minreq = frameBytes;
    attr.fragsize = static_cast<uint32_t>(-1);

    int error = 0;
    playbackStream_ = pa_simple_new(nullptr,
                                    config_.nodeName.c_str(),
                                    PA_STREAM_PLAYBACK,
                                    sinkName_.c_str(),
                                    "WireMic audio feed",
                                    &spec,
                                    nullptr,
                                    &attr,
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

    constexpr size_t kMaxQueuedFrames = 4;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (queue_.size() >= kMaxQueuedFrames) queue_.pop_front();
        queue_.emplace_back(interleaved, interleaved + sampleCount);
    }
    queueSignal_.notify_one();
}

}
