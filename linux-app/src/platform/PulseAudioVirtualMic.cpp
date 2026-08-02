#include "PulseAudioVirtualMic.hpp"

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <memory>
#include <sstream>
#include <vector>
#include <iostream>

namespace wiremic::platform {

namespace {
constexpr uint32_t kTargetBufferMs = 80;
constexpr uint32_t kMinTargetBufferFrames = 4;
constexpr uint32_t kQueueBufferMs = 80;
constexpr size_t kMinQueuedFrames = 4;

// How much audio the server holds before it starts playing, and refills to
// after any interruption. Small enough not to be heard as delay, large enough
// that a late frame is absorbed instead of leaving a hole.
constexpr uint32_t kCushionMs = 40;
constexpr uint32_t kMinCushionFrames = 2;

std::atomic<bool>& InstanceActive() {
    static std::atomic<bool> active{false};
    return active;
}

uint32_t BufferFramesFor(uint32_t windowMs, uint8_t frameSizeMs,
                         uint32_t minimumFrames) {
    const uint32_t safeFrameMs = frameSizeMs > 0 ? frameSizeMs : 10;
    const uint32_t byWindow = (windowMs + safeFrameMs - 1) / safeFrameMs;
    return std::max(minimumFrames, byWindow);
}
}

PulseAudioVirtualMic::PulseAudioVirtualMic(const VirtualMicConfig& config)
    : config_(config),
      sinkName_(kPulseSinkName),
      sourceName_(kPulseSourceName) {}

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

// Sweeps leftovers from a previous run, and does it exactly once per process.
//
// The sweep matches on module name, which cannot tell a module abandoned by a
// crashed run from one this very process is feeding right now. Running it a
// second time therefore pulled the sink out from under our own open playback
// stream: the stream was silently re-routed to a suspended device that never
// drains, and the writer thread blocked inside pa_simple_write forever, taking
// shutdown with it. Once per process is all the cleanup that was ever wanted.
void PulseAudioVirtualMic::unloadStaleModules() {
    static std::once_flag sweptOnce;
    bool shouldSweep = false;
    std::call_once(sweptOnce, [&shouldSweep]() { shouldSweep = true; });
    if (!shouldSweep) return;

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
         << " sink_properties=device.description=WireMic_Virtual_Sink"
            " 2>/dev/null";

    if (!runPactlCommand(cmd1.str(), output)) {
        std::cerr << "Failed to run pactl for null sink" << std::endl;
        return false;
    }

    nullSinkModuleId_ = parseModuleId(output);
    if (nullSinkModuleId_ < 0) {
        std::cerr << "Failed to parse null sink module ID: " << output << std::endl;
        return false;
    }

    // device.class matters more than it looks. A remapped source inherits
    // "filter" from its master, and every UI that asks someone to choose a
    // microphone -- the GNOME and KDE sound panels, Telegram, conferencing apps
    // -- lists only sources classed as "sound" and hides the rest. That is why
    // this device turned up in a browser, which enumerates everything, and
    // nowhere else. Calling it "sound" is not a fiction either: to anything
    // recording from it, this is a microphone, and the icon and form factor
    // make it look like one in the places that draw pictures.
    //
    // The value has to arrive as one argument with the quotes intact, because
    // pactl splits its arguments on spaces before PulseAudio ever parses the
    // property list. That also rules out spaces inside any single value, which
    // is why the description is spelled with underscores.
    std::ostringstream cmd2;
    cmd2 << "pactl load-module module-remap-source master=" << sinkName_
         << ".monitor source_name=" << sourceName_
         << " source_properties='\"device.description=WireMic_Virtual_Microphone"
            " device.class=sound"
            " device.icon_name=audio-input-microphone"
            " device.form_factor=microphone\"' 2>/dev/null";

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

    // One virtual microphone per process, enforced rather than assumed.
    //
    // The sink and source names are global to the audio server, so a second
    // instance does not get a second device -- it gets a stream pointed at the
    // first instance's sink and a set of modules that either instance may
    // unload. When the first one shut down it took the second one's sink with
    // it, and that instance's writer thread was left blocked inside
    // pa_simple_write on a device that would never drain again, which hung
    // shutdown for good. There is one microphone here; refusing the second is
    // both the truthful answer and the safe one.
    bool alreadyActive = false;
    if (!InstanceActive().compare_exchange_strong(alreadyActive, true)) {
        std::cerr << "A WireMic virtual microphone is already published by this "
                     "process" << std::endl;
        return false;
    }

    if (!loadModules()) {
        InstanceActive().store(false);
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
    const uint32_t targetFrames = BufferFramesFor(
        kTargetBufferMs, config_.frameSizeMs, kMinTargetBufferFrames);
    const uint32_t cushionFrames = BufferFramesFor(
        kCushionMs, config_.frameSizeMs, kMinCushionFrames);

    // prebuf is what keeps this stream alive and smooth, and it does both jobs
    // better than anything this side of the socket can.
    //
    // Alive: with prebuf at zero the server drains the stream whether or not
    // anything is being written, so the write position falls behind the read
    // position during any quiet stretch and every sample handed over afterwards
    // arrives stamped in the past and is dropped. The stream stays open, the
    // writes report success, and nothing is ever heard again -- which is what a
    // microphone published at launch and spoken into ten minutes later looked
    // like. With prebuf set, the server simply stops until there is audio again.
    //
    // Smooth: after any underrun the server refills to prebuf before resuming,
    // so playback restarts with a cushion instead of on the edge of running dry.
    // Frames arrive over a network and are handed on by a timer, neither of
    // which is punctual, and at a 10 ms frame size the margin was thin enough
    // that ordinary lateness was audible as crackling.
    attr.maxlength = frameBytes * targetFrames * 3;
    attr.tlength = frameBytes * targetFrames;
    attr.prebuf = frameBytes * cushionFrames;
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
        InstanceActive().store(false);
        return false;
    }

    running_ = true;
    writerRunning_ = true;
    writerThread_ = std::thread(&PulseAudioVirtualMic::writerLoop, this);
    return true;
}

void PulseAudioVirtualMic::stop() {
    const bool wasRunning = running_;

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
    if (wasRunning) InstanceActive().store(false);
}

// Hands over what has arrived, and nothing else.
//
// Filling the gaps with silence was tried and was a mistake. A frame arriving a
// millisecond late is completely ordinary -- it crossed a network and was passed
// on by a timer -- and writing silence in its place turns that into an audible
// hole, so the audio crackled at the shorter frame size where late frames are
// most common. Quiet stretches are the server's business, not this loop's:
// prebuf tells it to stop rather than run ahead, and to refill a cushion before
// it starts again.
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
            return;
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

    const size_t maxQueuedFrames = BufferFramesFor(
        kQueueBufferMs, config_.frameSizeMs,
        static_cast<uint32_t>(kMinQueuedFrames));

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (queue_.size() >= maxQueuedFrames) queue_.pop_front();
        queue_.emplace_back(interleaved, interleaved + sampleCount);
    }
    queueSignal_.notify_one();
}

}
