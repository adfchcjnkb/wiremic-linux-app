#ifdef _WIN32

#include "WindowsVirtualMic.hpp"

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace wiremic::platform {

namespace {

constexpr const char* kCableRenderMatch = "CABLE Input";
constexpr const char* kCableCaptureMatch = "CABLE Output";
constexpr uint32_t kTargetBufferMs = 40;

std::string ToUtf8(const wchar_t* wide) {
  if (!wide) return {};
  const int length =
      ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) return {};
  std::string out(static_cast<size_t>(length - 1), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), length, nullptr,
                        nullptr);
  return out;
}

std::wstring ToWide(const std::string& text) {
  if (text.empty()) return {};
  const int length = ::MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(length), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), length);
  return out;
}

std::string FriendlyNameOf(IMMDevice* device) {
  IPropertyStore* store = nullptr;
  if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || !store) return {};

  PROPVARIANT value;
  PropVariantInit(&value);
  std::string name;
  if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) &&
      value.vt == VT_LPWSTR) {
    name = ToUtf8(value.pwszVal);
  }
  PropVariantClear(&value);
  store->Release();
  return name;
}

// Finds an endpoint whose friendly name contains `match`. VB-CABLE names its
// endpoints "CABLE Input (VB-Audio Virtual Cable)" and "CABLE Output (...)",
// and the parenthesised part varies between versions, so match on the stem.
IMMDevice* FindEndpointContaining(EDataFlow flow, const char* match,
                                  std::string* foundName) {
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
      !enumerator) {
    return nullptr;
  }

  IMMDeviceCollection* collection = nullptr;
  if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE,
                                             &collection)) ||
      !collection) {
    enumerator->Release();
    return nullptr;
  }

  UINT count = 0;
  collection->GetCount(&count);

  IMMDevice* result = nullptr;
  for (UINT i = 0; i < count && !result; ++i) {
    IMMDevice* candidate = nullptr;
    if (FAILED(collection->Item(i, &candidate)) || !candidate) continue;

    const std::string name = FriendlyNameOf(candidate);
    if (name.find(match) != std::string::npos) {
      if (foundName) *foundName = name;
      result = candidate;
    } else {
      candidate->Release();
    }
  }

  collection->Release();
  enumerator->Release();
  return result;
}

// Setting the default endpoint has no public API. IPolicyConfig is the
// interface the Sound control panel itself uses; it is undocumented but has
// been stable since Windows 7 and is how every "set default device" tool works.
const GUID kPolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
const GUID kPolicyConfigVista = {
    0x568b9108, 0x44bf, 0x40b4, {0x90, 0x06, 0x86, 0xaf, 0xe5, 0xb5, 0xa6, 0x20}};

struct IPolicyConfigVista : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT,
                                                     WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*,
                                                     WAVEFORMATEX*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64,
                                                         PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, struct DeviceShareMode*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, struct DeviceShareMode*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&,
                                                      PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&,
                                                      PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

}

WindowsVirtualMic::WindowsVirtualMic(const VirtualMicConfig& config)
    : config_(config) {
  const size_t frameSamples =
      static_cast<size_t>(config_.sampleRate) * config_.frameSizeMs / 1000;
  const size_t windowSamples =
      static_cast<size_t>(config_.sampleRate) * kTargetBufferMs / 1000;
  maxBufferedSamples_ =
      std::max<size_t>(frameSamples * 4, windowSamples) * config_.channels;
  ring_.assign(static_cast<size_t>(config_.sampleRate) * 2 * config_.channels,
               0);
}

WindowsVirtualMic::~WindowsVirtualMic() { stop(); }

bool WindowsVirtualMic::isRunning() const { return running_.load(); }

bool WindowsVirtualMic::IsCableInstalled() {
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::string name;
  IMMDevice* device = FindEndpointContaining(eRender, kCableRenderMatch, &name);
  const bool found = device != nullptr;
  if (device) device->Release();
  return found;
}

std::string WindowsVirtualMic::CableRenderDeviceName() {
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::string name;
  IMMDevice* device = FindEndpointContaining(eRender, kCableRenderMatch, &name);
  if (device) device->Release();
  return name;
}

std::string WindowsVirtualMic::CableCaptureDeviceName() {
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::string name;
  IMMDevice* device =
      FindEndpointContaining(eCapture, kCableCaptureMatch, &name);
  if (device) device->Release();
  return name;
}

bool WindowsVirtualMic::MakeCableDefaultCaptureDevice(std::string* error) {
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  std::string name;
  IMMDevice* device =
      FindEndpointContaining(eCapture, kCableCaptureMatch, &name);
  if (!device) {
    if (error) *error = "VB-CABLE capture endpoint not found";
    return false;
  }

  LPWSTR id = nullptr;
  const bool haveId = SUCCEEDED(device->GetId(&id)) && id != nullptr;
  device->Release();
  if (!haveId) {
    if (error) *error = "could not read the endpoint id";
    return false;
  }

  IPolicyConfigVista* policy = nullptr;
  HRESULT hr = ::CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL,
                                   kPolicyConfigVista,
                                   reinterpret_cast<void**>(&policy));
  bool ok = false;
  if (SUCCEEDED(hr) && policy) {
    ok = SUCCEEDED(policy->SetDefaultEndpoint(id, eConsole)) &&
         SUCCEEDED(policy->SetDefaultEndpoint(id, eCommunications)) &&
         SUCCEEDED(policy->SetDefaultEndpoint(id, eMultimedia));
    policy->Release();
  }

  if (!ok && error) {
    *error = "Windows refused the default-device change";
  }

  ::CoTaskMemFree(id);
  return ok;
}

void WindowsVirtualMic::OpenSoundControlPanel() {
  ::ShellExecuteW(nullptr, L"open", L"control", L"mmsys.cpl,,1", nullptr,
                  SW_SHOWNORMAL);
}

bool WindowsVirtualMic::findCableRenderDevice(IMMDevice** device) {
  std::string name;
  *device = FindEndpointContaining(eRender, kCableRenderMatch, &name);
  if (*device == nullptr) {
    lastError_ =
        "VB-CABLE is not installed (no \"CABLE Input\" playback device)";
    return false;
  }
  return true;
}

bool WindowsVirtualMic::start() {
  if (running_.load()) return true;

  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  if (!findCableRenderDevice(&device_)) return false;

  HRESULT hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&client_));
  if (FAILED(hr) || !client_) {
    lastError_ = "could not activate the VB-CABLE audio client";
    stop();
    return false;
  }

  hr = client_->GetMixFormat(&mixFormat_);
  if (FAILED(hr) || !mixFormat_) {
    lastError_ = "could not read the VB-CABLE mix format";
    stop();
    return false;
  }

  deviceChannels_ = mixFormat_->nChannels;
  deviceRate_ = mixFormat_->nSamplesPerSec;
  deviceIsFloat_ = mixFormat_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
  if (mixFormat_->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat_);
    deviceIsFloat_ = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  }

  renderEvent_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!renderEvent_) {
    lastError_ = "could not create the render event";
    stop();
    return false;
  }

  const REFERENCE_TIME duration = kTargetBufferMs * 10000LL;
  hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                           AUDCLNT_STREAMFLAGS_EVENTCALLBACK, duration, 0,
                           mixFormat_, nullptr);
  if (FAILED(hr)) {
    lastError_ = "could not initialise the VB-CABLE stream";
    stop();
    return false;
  }

  hr = client_->SetEventHandle(static_cast<HANDLE>(renderEvent_));
  if (FAILED(hr)) {
    lastError_ = "could not attach the render event";
    stop();
    return false;
  }

  hr = client_->GetBufferSize(&bufferFrames_);
  if (FAILED(hr)) {
    lastError_ = "could not read the VB-CABLE buffer size";
    stop();
    return false;
  }

  hr = client_->GetService(__uuidof(IAudioRenderClient),
                           reinterpret_cast<void**>(&render_));
  if (FAILED(hr) || !render_) {
    lastError_ = "could not obtain the VB-CABLE render client";
    stop();
    return false;
  }

  writeCount_.store(0);
  readCount_.store(0);
  resampleCursor_ = 0.0;
  haveResampleHistory_ = false;

  hr = client_->Start();
  if (FAILED(hr)) {
    lastError_ = "could not start the VB-CABLE stream";
    stop();
    return false;
  }

  running_.store(true);
  renderThread_ = std::thread(&WindowsVirtualMic::renderLoop, this);
  lastError_.clear();
  return true;
}

void WindowsVirtualMic::stop() {
  running_.store(false);
  if (renderEvent_) ::SetEvent(static_cast<HANDLE>(renderEvent_));
  if (renderThread_.joinable()) renderThread_.join();

  if (client_) client_->Stop();
  if (render_) {
    render_->Release();
    render_ = nullptr;
  }
  if (client_) {
    client_->Release();
    client_ = nullptr;
  }
  if (mixFormat_) {
    ::CoTaskMemFree(mixFormat_);
    mixFormat_ = nullptr;
  }
  if (device_) {
    device_->Release();
    device_ = nullptr;
  }
  if (renderEvent_) {
    ::CloseHandle(static_cast<HANDLE>(renderEvent_));
    renderEvent_ = nullptr;
  }
}

void WindowsVirtualMic::pushSamples(const int16_t* interleaved,
                                     size_t sampleCount) {
  if (!running_.load() || interleaved == nullptr || sampleCount == 0) return;

  const size_t ringSize = ring_.size();
  uint64_t write = writeCount_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < sampleCount; ++i) {
    ring_[write % ringSize] = interleaved[i];
    ++write;
  }
  writeCount_.store(write, std::memory_order_release);
}

// Converts our 48 kHz mono stream into whatever shared-mode format VB-CABLE
// negotiated: usually 44.1 or 48 kHz float32 stereo. Anything left over from a
// previous call is carried in resampleCursor_/resampleHistory_ so the
// interpolation stays continuous across buffers.
void WindowsVirtualMic::writeInto(unsigned char* destination, uint32_t frames) {
  const uint64_t written = writeCount_.load(std::memory_order_acquire);
  uint64_t read = readCount_.load(std::memory_order_relaxed);

  if (written - read > maxBufferedSamples_) {
    read = written - maxBufferedSamples_;
  }

  const size_t ringSize = ring_.size();
  const double ratio = static_cast<double>(config_.sampleRate) /
                        static_cast<double>(deviceRate_);

  auto* asFloat = reinterpret_cast<float*>(destination);
  auto* asShort = reinterpret_cast<int16_t*>(destination);

  for (uint32_t f = 0; f < frames; ++f) {
    double sample = 0.0;

    if (read < written) {
      const int16_t current = ring_[read % ringSize];
      if (!haveResampleHistory_) {
        resampleHistory_ = current;
        haveResampleHistory_ = true;
      }
      const double a = resampleHistory_;
      const double b = current;
      sample = a + (b - a) * resampleCursor_;

      resampleCursor_ += ratio;
      while (resampleCursor_ >= 1.0 && read < written) {
        resampleHistory_ = ring_[read % ringSize];
        ++read;
        resampleCursor_ -= 1.0;
      }
    }

    for (uint32_t c = 0; c < deviceChannels_; ++c) {
      const size_t index = static_cast<size_t>(f) * deviceChannels_ + c;
      if (deviceIsFloat_) {
        asFloat[index] = static_cast<float>(sample / 32768.0);
      } else {
        asShort[index] = static_cast<int16_t>(
            std::clamp(sample, -32768.0, 32767.0));
      }
    }
  }

  readCount_.store(read, std::memory_order_release);
}

void WindowsVirtualMic::renderLoop() {
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  while (running_.load()) {
    if (::WaitForSingleObject(static_cast<HANDLE>(renderEvent_), 200) !=
        WAIT_OBJECT_0) {
      continue;
    }
    if (!running_.load()) break;

    UINT32 padding = 0;
    if (FAILED(client_->GetCurrentPadding(&padding))) continue;

    const UINT32 available = bufferFrames_ - padding;
    if (available == 0) continue;

    BYTE* buffer = nullptr;
    if (FAILED(render_->GetBuffer(available, &buffer)) || !buffer) continue;

    writeInto(buffer, available);
    render_->ReleaseBuffer(available, 0);
  }

  ::CoUninitialize();
}

}

#endif
