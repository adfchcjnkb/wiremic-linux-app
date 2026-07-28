#include "PipeWireVirtualMic.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace wiremic::platform {

namespace {
constexpr size_t kRingBufferFrames = 48000 * 2;
}

bool PipeWireVirtualMic::IsPipeWireAvailable() {
    pw_init(nullptr, nullptr);
    struct pw_loop* probeLoop = pw_loop_new(nullptr);
    if (!probeLoop) return false;

    struct pw_context* probeContext = pw_context_new(probeLoop, nullptr, 0);
    if (!probeContext) {
        pw_loop_destroy(probeLoop);
        return false;
    }

    struct pw_core* probeCore = pw_context_connect(probeContext, nullptr, 0);
    const bool available = probeCore != nullptr;

    if (probeCore) pw_core_disconnect(probeCore);
    pw_context_destroy(probeContext);
    pw_loop_destroy(probeLoop);
    return available;
}

PipeWireVirtualMic::PipeWireVirtualMic(const VirtualMicConfig& config)
    : config_(config),
      ringBuffer_(kRingBufferFrames * config_.channels, 0) {
    pw_init(nullptr, nullptr);
}

PipeWireVirtualMic::~PipeWireVirtualMic() { stop(); }

bool PipeWireVirtualMic::isRunning() const { return running_; }

void PipeWireVirtualMic::OnStreamStateChanged(void* userdata,
                                               pw_stream_state ,
                                               pw_stream_state ,
                                               const char* ) {
    (void)userdata;
}

void PipeWireVirtualMic::fillBuffer(int16_t* dst, uint32_t maxFrames,
                                     uint32_t& outFrames) {
    const size_t channels = config_.channels;
    const size_t framesAvailable = available_ / channels;
    const size_t framesToCopy = std::min<size_t>(maxFrames, framesAvailable);

    for (size_t f = 0; f < framesToCopy; ++f) {
        for (size_t c = 0; c < channels; ++c) {
            dst[f * channels + c] = ringBuffer_[readPos_];
            readPos_ = (readPos_ + 1) % ringBuffer_.size();
        }
    }
    available_ -= framesToCopy * channels;

    for (size_t f = framesToCopy; f < maxFrames; ++f) {
        for (size_t c = 0; c < channels; ++c) {
            dst[f * channels + c] = 0;
        }
    }

    outFrames = maxFrames;
}

void PipeWireVirtualMic::OnProcess(void* userdata) {
    auto* self = static_cast<PipeWireVirtualMic*>(userdata);

    struct pw_buffer* pwBuffer = pw_stream_dequeue_buffer(self->stream_);
    if (!pwBuffer) return;

    struct spa_buffer* spaBuffer = pwBuffer->buffer;
    if (spaBuffer->datas[0].data == nullptr) {
        pw_stream_queue_buffer(self->stream_, pwBuffer);
        return;
    }

    const uint32_t stride = static_cast<uint32_t>(sizeof(int16_t)) * self->config_.channels;
    uint32_t maxFrames = spaBuffer->datas[0].maxsize / stride;
    if (pwBuffer->requested != 0) {
        maxFrames = std::min<uint32_t>(maxFrames,
                                        static_cast<uint32_t>(pwBuffer->requested));
    }

    auto* dst = static_cast<int16_t*>(spaBuffer->datas[0].data);
    uint32_t framesWritten = 0;
    self->fillBuffer(dst, maxFrames, framesWritten);

    spaBuffer->datas[0].chunk->offset = 0;
    spaBuffer->datas[0].chunk->stride = static_cast<int32_t>(stride);
    spaBuffer->datas[0].chunk->size = framesWritten * stride;

    pw_stream_queue_buffer(self->stream_, pwBuffer);
}

bool PipeWireVirtualMic::start() {
    if (running_) return true;

    loop_ = pw_thread_loop_new("wiremic-virtual-mic", nullptr);
    if (!loop_) return false;

    pw_thread_loop_lock(loop_);

    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_) {
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        pw_thread_loop_unlock(loop_);
        pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
        context_ = nullptr;
        loop_ = nullptr;
        return false;
    }

    struct pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_CLASS, "Audio/Source",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_NODE_NAME, config_.nodeName.c_str(),
        PW_KEY_NODE_DESCRIPTION, config_.nodeDescription.c_str(),
        PW_KEY_NODE_LATENCY, "480/48000",
        nullptr);

    static const struct pw_stream_events streamEvents = [] {
        struct pw_stream_events events{};
        events.version = PW_VERSION_STREAM_EVENTS;
        events.process = &PipeWireVirtualMic::OnProcess;
        events.state_changed = &PipeWireVirtualMic::OnStreamStateChanged;
        return events;
    }();

    stream_ = pw_stream_new(core_, config_.nodeName.c_str(), props);
    if (!stream_) {
        pw_thread_loop_unlock(loop_);
        stop();
        return false;
    }

    pw_stream_add_listener(stream_, &streamListener_, &streamEvents, this);

    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    struct spa_audio_info_raw audioInfo{};
    audioInfo.format = SPA_AUDIO_FORMAT_S16;
    audioInfo.rate = config_.sampleRate;
    audioInfo.channels = config_.channels;
    if (config_.channels == 1) {
        audioInfo.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else {
        audioInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
        audioInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
    }

    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audioInfo);

    const int connectResult = pw_stream_connect(
        stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                      PW_STREAM_FLAG_MAP_BUFFERS |
                                      PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(loop_);

    if (connectResult < 0) {
        stop();
        return false;
    }

    if (pw_thread_loop_start(loop_) < 0) {
        stop();
        return false;
    }

    running_ = true;
    return true;
}

void PipeWireVirtualMic::stop() {
    if (loop_) {
        pw_thread_loop_lock(loop_);
        if (stream_) {
            spa_hook_remove(&streamListener_);
            pw_stream_destroy(stream_);
            stream_ = nullptr;
        }
        if (core_) {
            pw_core_disconnect(core_);
            core_ = nullptr;
        }
        if (context_) {
            pw_context_destroy(context_);
            context_ = nullptr;
        }
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    running_ = false;
    writePos_ = 0;
    readPos_ = 0;
    available_ = 0;
}

void PipeWireVirtualMic::pushSamples(const int16_t* interleaved,
                                      size_t sampleCount) {
    if (!running_ || !loop_ || interleaved == nullptr || sampleCount == 0) return;

    pw_thread_loop_lock(loop_);
    for (size_t i = 0; i < sampleCount; ++i) {
        ringBuffer_[writePos_] = interleaved[i];
        writePos_ = (writePos_ + 1) % ringBuffer_.size();
        if (available_ < ringBuffer_.size()) {
            ++available_;
        } else {
            readPos_ = (readPos_ + 1) % ringBuffer_.size();
        }
    }
    pw_thread_loop_unlock(loop_);
}

}
