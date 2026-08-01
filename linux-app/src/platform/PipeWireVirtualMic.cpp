#include "PipeWireVirtualMic.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

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
    const size_t frameSamples =
        static_cast<size_t>(config_.sampleRate) * config_.frameSizeMs / 1000;
    const size_t quantumSamples =
        static_cast<size_t>(config_.sampleRate) * 60 / 1000;
    maxBufferedSamples_ =
        std::max<size_t>(frameSamples * 4, quantumSamples) * config_.channels;

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

void PipeWireVirtualMic::OnParamChanged(void* userdata, uint32_t id,
                                         const struct spa_pod* param) {
    auto* self = static_cast<PipeWireVirtualMic*>(userdata);

    if (param == nullptr || id != SPA_PARAM_Format) return;

    struct spa_audio_info info{};
    if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0) return;
    if (info.media_type != SPA_MEDIA_TYPE_audio ||
        info.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
        return;
    }
    if (spa_format_audio_raw_parse(param, &info.info.raw) < 0) return;

    const uint32_t channels = info.info.raw.channels ? info.info.raw.channels : 1;
    const uint32_t sampleSize = info.info.raw.format == SPA_AUDIO_FORMAT_F32
                                     ? sizeof(float)
                                     : sizeof(int16_t);
    const uint32_t stride = sampleSize * channels;

    self->negotiatedChannels_.store(channels, std::memory_order_relaxed);
    self->negotiatedStride_.store(stride, std::memory_order_relaxed);
    self->negotiatedFormat_.store(info.info.raw.format,
                                   std::memory_order_release);

    // Answering with buffer parameters is not optional for a source. Until the
    // server has a stride and a size it can hand buffers out with, the stream
    // never leaves negotiation, and a node stuck in negotiation is one the
    // session manager will happily list but never build a link to — which is
    // exactly what a device that shows up and stays silent looks like.
    const uint32_t quantum = std::max<uint32_t>(
        1, self->config_.sampleRate * self->config_.frameSizeMs / 1000);

    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    params[0] = static_cast<const struct spa_pod*>(spa_pod_builder_add_object(
        &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 16),
        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size,
        SPA_POD_CHOICE_RANGE_Int(static_cast<int32_t>(quantum * stride),
                                  static_cast<int32_t>(stride),
                                  static_cast<int32_t>(8192 * stride)),
        SPA_PARAM_BUFFERS_stride, SPA_POD_Int(static_cast<int32_t>(stride))));

    pw_stream_update_params(self->stream_, params, 1);
}

void PipeWireVirtualMic::fillBuffer(void* dst, uint32_t maxFrames,
                                     uint32_t& outFrames) {
    const size_t sourceChannels = config_.channels;
    const uint32_t format = negotiatedFormat_.load(std::memory_order_acquire);
    const size_t outChannels = std::max<uint32_t>(
        1, negotiatedChannels_.load(std::memory_order_relaxed));

    const uint64_t written = writeCount_.load(std::memory_order_acquire);
    uint64_t read = readCount_.load(std::memory_order_relaxed);

    if (written - read > maxBufferedSamples_) {
        read = written - maxBufferedSamples_;
        read -= read % sourceChannels;
    }

    const size_t framesAvailable =
        static_cast<size_t>(written - read) / sourceChannels;
    const size_t framesToCopy = std::min<size_t>(maxFrames, framesAvailable);
    const size_t ringSize = ringBuffer_.size();

    auto* asFloat = static_cast<float*>(dst);
    auto* asShort = static_cast<int16_t*>(dst);

    for (size_t f = 0; f < maxFrames; ++f) {
        for (size_t c = 0; c < outChannels; ++c) {
            // The ring holds config_.channels interleaved samples per frame.
            // When the server settled on a wider layout, the last source
            // channel repeats, so a mono capture reaches every output channel
            // instead of leaving all but the first silent.
            int16_t sample = 0;
            if (f < framesToCopy) {
                const size_t sourceChannel = std::min(c, sourceChannels - 1);
                sample = ringBuffer_[(read + sourceChannel) % ringSize];
            }
            if (format == SPA_AUDIO_FORMAT_F32) {
                asFloat[f * outChannels + c] =
                    static_cast<float>(sample) / 32768.0f;
            } else {
                asShort[f * outChannels + c] = sample;
            }
        }
        if (f < framesToCopy) read += sourceChannels;
    }

    readCount_.store(read, std::memory_order_release);
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

    const uint32_t stride =
        self->negotiatedStride_.load(std::memory_order_acquire);
    if (stride == 0) {
        // Format negotiation has not finished; there is no layout to write in.
        pw_stream_queue_buffer(self->stream_, pwBuffer);
        return;
    }

    uint32_t maxFrames = spaBuffer->datas[0].maxsize / stride;
    if (pwBuffer->requested != 0) {
        maxFrames = std::min<uint32_t>(maxFrames,
                                        static_cast<uint32_t>(pwBuffer->requested));
    }

    void* dst = spaBuffer->datas[0].data;
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

    const uint32_t latencyFrames = std::max<uint32_t>(
        1, config_.sampleRate * config_.frameSizeMs / 1000);
    const std::string latencyHint =
        std::to_string(latencyFrames) + "/" + std::to_string(config_.sampleRate);

    struct pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CLASS, "Audio/Source",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_NODE_NAME, config_.nodeName.c_str(),
        PW_KEY_NODE_DESCRIPTION, config_.nodeDescription.c_str(),
        PW_KEY_NODE_NICK, config_.nodeDescription.c_str(),
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_NODE_ALWAYS_PROCESS, "true",
        PW_KEY_NODE_LATENCY, latencyHint.c_str(),
        nullptr);

    static const struct pw_stream_events streamEvents = [] {
        struct pw_stream_events events{};
        events.version = PW_VERSION_STREAM_EVENTS;
        events.process = &PipeWireVirtualMic::OnProcess;
        events.state_changed = &PipeWireVirtualMic::OnStreamStateChanged;
        events.param_changed = &PipeWireVirtualMic::OnParamChanged;
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

    // Offer float as well as S16. The graph runs in float internally, so a
    // server that would otherwise have to insert a converter — or give up on
    // the negotiation altogether — can simply take our samples as they are.
    struct spa_audio_info_raw floatInfo = audioInfo;
    floatInfo.format = SPA_AUDIO_FORMAT_F32;

    const struct spa_pod* params[2];
    params[0] =
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &floatInfo);
    params[1] =
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audioInfo);

    const int connectResult = pw_stream_connect(
        stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        // Deliberately not autoconnecting: this node is a device other people
        // record from, not a stream looking for somewhere to play.
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS |
                                      PW_STREAM_FLAG_RT_PROCESS),
        params, 2);

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
    writeCount_.store(0, std::memory_order_relaxed);
    readCount_.store(0, std::memory_order_relaxed);
    negotiatedFormat_.store(0, std::memory_order_relaxed);
    negotiatedChannels_.store(0, std::memory_order_relaxed);
    negotiatedStride_.store(0, std::memory_order_relaxed);
}

void PipeWireVirtualMic::pushSamples(const int16_t* interleaved,
                                      size_t sampleCount) {
    if (!running_ || !loop_ || interleaved == nullptr || sampleCount == 0) return;

    const size_t ringSize = ringBuffer_.size();
    uint64_t write = writeCount_.load(std::memory_order_relaxed);

    for (size_t i = 0; i < sampleCount; ++i) {
        ringBuffer_[write % ringSize] = interleaved[i];
        ++write;
    }

    writeCount_.store(write, std::memory_order_release);
}

}
