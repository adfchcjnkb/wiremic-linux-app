#include "OpenSLESCapture.hpp"

#include <cstring>

namespace wiremic::android {

namespace {
constexpr int kBufferCount = 4;
}

OpenSLESCapture::OpenSLESCapture(CaptureFramesCallback callback)
    : callback_(std::move(callback)) {}

OpenSLESCapture::~OpenSLESCapture() { stop(); }

void OpenSLESCapture::BufferQueueCallback(SLAndroidSimpleBufferQueueItf queue,
                                           void* context) {
  auto* self = static_cast<OpenSLESCapture*>(context);
  self->onBufferReady(queue);
}

void OpenSLESCapture::onBufferReady(SLAndroidSimpleBufferQueueItf queue) {
  if (!running_) return;

  const auto& filled = buffers_[readIndex_];
  if (callback_) callback_(filled.data(), framesPerBuffer_);

  (*queue)->Enqueue(queue, buffers_[readIndex_].data(),
                    static_cast<SLuint32>(framesPerBuffer_) * channels_ *
                        sizeof(int16_t));
  readIndex_ = (readIndex_ + 1) % kBufferCount;
}

bool OpenSLESCapture::start(int32_t requestedRate, int32_t requestedChannels,
                             int32_t framesPerCallback) {
  if (running_) return true;

  channels_ = requestedChannels > 0 ? requestedChannels : 1;
  framesPerBuffer_ = framesPerCallback > 0 ? framesPerCallback : 480;

  // OpenSL ES on Android only accepts a fixed set of rates. 48 kHz is what we
  // negotiate; 44.1 kHz is the common fallback on older hardware. Whatever is
  // granted is reported back so the sender resamples instead of assuming.
  SLuint32 slRate = SL_SAMPLINGRATE_48;
  sampleRate_ = 48000;
  if (requestedRate == 44100) {
    slRate = SL_SAMPLINGRATE_44_1;
    sampleRate_ = 44100;
  } else if (requestedRate == 16000) {
    slRate = SL_SAMPLINGRATE_16;
    sampleRate_ = 16000;
  }

  if (slCreateEngine(&engineObject_, 0, nullptr, 0, nullptr, nullptr) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not create the OpenSL ES engine";
    stop();
    return false;
  }
  if ((*engineObject_)->Realize(engineObject_, SL_BOOLEAN_FALSE) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not realise the OpenSL ES engine";
    stop();
    return false;
  }
  if ((*engineObject_)->GetInterface(engineObject_, SL_IID_ENGINE, &engine_) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not obtain the OpenSL ES engine interface";
    stop();
    return false;
  }

  SLDataLocator_IODevice deviceLocator = {
      SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
      SL_DEFAULTDEVICEID_AUDIOINPUT, nullptr};
  SLDataSource source = {&deviceLocator, nullptr};

  SLDataLocator_AndroidSimpleBufferQueue queueLocator = {
      SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, kBufferCount};

  SLDataFormat_PCM format{};
  format.formatType = SL_DATAFORMAT_PCM;
  format.numChannels = static_cast<SLuint32>(channels_);
  format.samplesPerSec = slRate;
  format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
  format.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
  format.channelMask =
      channels_ == 1 ? SL_SPEAKER_FRONT_CENTER
                     : (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);
  format.endianness = SL_BYTEORDER_LITTLEENDIAN;

  SLDataSink sink = {&queueLocator, &format};

  const SLInterfaceID interfaces[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                      SL_IID_ANDROIDCONFIGURATION};
  const SLboolean required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE};

  if ((*engine_)->CreateAudioRecorder(engine_, &recorderObject_, &source, &sink,
                                       2, interfaces, required) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not create the OpenSL ES recorder";
    stop();
    return false;
  }

  // Ask for the voice-communication preset so the platform applies the same
  // echo and noise handling the AAudio path requests.
  SLAndroidConfigurationItf configuration = nullptr;
  if ((*recorderObject_)
          ->GetInterface(recorderObject_, SL_IID_ANDROIDCONFIGURATION,
                         &configuration) == SL_RESULT_SUCCESS &&
      configuration != nullptr) {
    SLint32 preset = SL_ANDROID_RECORDING_PRESET_VOICE_COMMUNICATION;
    (*configuration)
        ->SetConfiguration(configuration, SL_ANDROID_KEY_RECORDING_PRESET,
                           &preset, sizeof(SLint32));
  }

  if ((*recorderObject_)->Realize(recorderObject_, SL_BOOLEAN_FALSE) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not realise the OpenSL ES recorder";
    stop();
    return false;
  }
  if ((*recorderObject_)
          ->GetInterface(recorderObject_, SL_IID_RECORD, &record_) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not obtain the OpenSL ES record interface";
    stop();
    return false;
  }
  if ((*recorderObject_)
          ->GetInterface(recorderObject_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                         &queue_) != SL_RESULT_SUCCESS) {
    lastError_ = "could not obtain the OpenSL ES buffer queue";
    stop();
    return false;
  }

  buffers_.assign(kBufferCount,
                  std::vector<int16_t>(
                      static_cast<size_t>(framesPerBuffer_) * channels_, 0));
  readIndex_ = 0;

  if ((*queue_)->RegisterCallback(queue_, &OpenSLESCapture::BufferQueueCallback,
                                   this) != SL_RESULT_SUCCESS) {
    lastError_ = "could not register the OpenSL ES buffer callback";
    stop();
    return false;
  }

  running_ = true;

  for (int i = 0; i < kBufferCount; ++i) {
    (*queue_)->Enqueue(queue_, buffers_[i].data(),
                       static_cast<SLuint32>(framesPerBuffer_) * channels_ *
                           sizeof(int16_t));
  }

  if ((*record_)->SetRecordState(record_, SL_RECORDSTATE_RECORDING) !=
      SL_RESULT_SUCCESS) {
    lastError_ = "could not start OpenSL ES recording";
    running_ = false;
    stop();
    return false;
  }

  lastError_.clear();
  return true;
}

void OpenSLESCapture::stop() {
  running_ = false;

  if (record_) {
    (*record_)->SetRecordState(record_, SL_RECORDSTATE_STOPPED);
    record_ = nullptr;
  }
  if (queue_) {
    (*queue_)->Clear(queue_);
    queue_ = nullptr;
  }
  if (recorderObject_) {
    (*recorderObject_)->Destroy(recorderObject_);
    recorderObject_ = nullptr;
  }
  if (engineObject_) {
    (*engineObject_)->Destroy(engineObject_);
    engineObject_ = nullptr;
    engine_ = nullptr;
  }
  buffers_.clear();
}

}
