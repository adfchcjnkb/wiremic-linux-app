#include <jni.h>

#include <android/log.h>

#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "ConnectionManager.hpp"

using json = nlohmann::json;
using wiremic::android::ConnectionManager;
using wiremic::android::ConnectionManagerSettings;

namespace {

constexpr const char* kLogTag = "WireMicNative";

#define WIREMIC_LOG_INFO(...) \
  __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define WIREMIC_LOG_ERROR(...) \
  __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

JavaVM* gJavaVm = nullptr;
jobject gListener = nullptr;

jmethodID gOnDeviceListChanged = nullptr;
jmethodID gOnConnectionStateChanged = nullptr;
jmethodID gOnConnectionEstablished = nullptr;
jmethodID gOnConnectionClosed = nullptr;
jmethodID gOnConnectionFailed = nullptr;
jmethodID gOnError = nullptr;

std::unique_ptr<ConnectionManager> gManager;
std::mutex gManagerMutex;

struct JniEnvGuard {
  JNIEnv* env{nullptr};
  bool attached{false};

  JniEnvGuard() {
    if (!gJavaVm) return;
    if (gJavaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) !=
        JNI_OK) {
      if (gJavaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        attached = true;
      }
    }
  }

  ~JniEnvGuard() {
    if (attached && gJavaVm) gJavaVm->DetachCurrentThread();
  }
};

void CallStringMethod(jmethodID method, const std::string& payload) {
  if (!gListener || !method) return;
  JniEnvGuard guard;
  if (!guard.env) {
    WIREMIC_LOG_ERROR("CallStringMethod: failed to attach JNIEnv");
    return;
  }
  jstring jPayload = guard.env->NewStringUTF(payload.c_str());
  guard.env->CallVoidMethod(gListener, method, jPayload);
  if (guard.env->ExceptionCheck()) {
    WIREMIC_LOG_ERROR("CallStringMethod: Java exception during callback");
    guard.env->ExceptionDescribe();
    guard.env->ExceptionClear();
  }
  guard.env->DeleteLocalRef(jPayload);
}

std::string PlatformToString(wiremic::protocol::Platform platform) {
  return wiremic::protocol::ToString(platform);
}

json DeviceToJson(const wiremic::protocol::DeviceInfo& device,
                   const std::string& status = {}) {
  json node{
      {"id", device.id},
      {"name", device.name},
      {"model", device.model},
      {"platform", PlatformToString(device.platform)},
      {"ip", device.ip},
      {"connectionType", wiremic::protocol::ToString(device.connectionType)},
      {"controlPort", device.controlPort},
  };
  if (!status.empty()) node["status"] = status;
  return node;
}

std::string ConnectionStateToString(wiremic::protocol::ConnectionState state) {
  using State = wiremic::protocol::ConnectionState;
  switch (state) {
    case State::Idle: return "Idle";
    case State::Discovering: return "Discovering";
    case State::RequestSent: return "RequestSent";
    case State::AwaitingApproval: return "AwaitingApproval";
    case State::Accepted: return "Accepted";
    case State::Streaming: return "Connected";
    case State::Disconnected: return "Disconnected";
    case State::Reconnecting: return "Reconnecting";
  }
  return "Idle";
}

template <typename Func>
void RunGuarded(const char* siteName, Func&& func) {
  try {
    func();
  } catch (const std::exception& e) {
    WIREMIC_LOG_ERROR("Exception in %s: %s", siteName, e.what());
    CallStringMethod(gOnError,
                      std::string("Internal error in ") + siteName + ": " +
                          e.what());
  } catch (...) {
    WIREMIC_LOG_ERROR("Unknown exception in %s", siteName);
    CallStringMethod(gOnError,
                      std::string("Unknown internal error in ") + siteName);
  }
}

}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  gJavaVm = vm;
  WIREMIC_LOG_INFO("Native library loaded");
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeSetListener(JNIEnv* env, jobject,
                                                           jobject listener) {
  RunGuarded("nativeSetListener", [&]() {
    if (gListener) {
      env->DeleteGlobalRef(gListener);
      gListener = nullptr;
    }
    if (!listener) return;

    gListener = env->NewGlobalRef(listener);
    jclass listenerClass = env->GetObjectClass(gListener);

    gOnDeviceListChanged = env->GetMethodID(
        listenerClass, "onDeviceListChanged", "(Ljava/lang/String;)V");
    gOnConnectionStateChanged = env->GetMethodID(
        listenerClass, "onConnectionStateChanged", "(Ljava/lang/String;)V");
    gOnConnectionEstablished = env->GetMethodID(
        listenerClass, "onConnectionEstablished", "(Ljava/lang/String;)V");
    gOnConnectionClosed = env->GetMethodID(
        listenerClass, "onConnectionClosed", "(Ljava/lang/String;)V");
    gOnConnectionFailed = env->GetMethodID(
        listenerClass, "onConnectionFailed", "(Ljava/lang/String;)V");
    gOnError =
        env->GetMethodID(listenerClass, "onNativeError", "(Ljava/lang/String;)V");

    env->DeleteLocalRef(listenerClass);
    WIREMIC_LOG_INFO("Listener registered");
  });
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeStart(
    JNIEnv* env, jobject, jstring deviceId, jstring deviceName,
    jstring deviceModel, jstring dataDir) {
  bool started = false;

  RunGuarded("nativeStart", [&]() {
    const char* idChars = env->GetStringUTFChars(deviceId, nullptr);
    const char* nameChars = env->GetStringUTFChars(deviceName, nullptr);
    const char* modelChars = env->GetStringUTFChars(deviceModel, nullptr);
    const char* dirChars = env->GetStringUTFChars(dataDir, nullptr);

    wiremic::protocol::DeviceInfo localDevice;
    localDevice.id = idChars;
    localDevice.name = nameChars;
    localDevice.model = modelChars;
    localDevice.platform = wiremic::protocol::Platform::Android;
    localDevice.connectionType = wiremic::protocol::ConnectionType::Wifi;
    localDevice.controlPort = 0;

    const std::string dataDirStr = dirChars;

    env->ReleaseStringUTFChars(deviceId, idChars);
    env->ReleaseStringUTFChars(deviceName, nameChars);
    env->ReleaseStringUTFChars(deviceModel, modelChars);
    env->ReleaseStringUTFChars(dataDir, dirChars);

    WIREMIC_LOG_INFO("Starting ConnectionManager: id=%s name=%s dataDir=%s",
                      localDevice.id.c_str(), localDevice.name.c_str(),
                      dataDirStr.c_str());

    std::lock_guard<std::mutex> lock(gManagerMutex);

    ConnectionManagerSettings settings;
    gManager = std::make_unique<ConnectionManager>(
        std::move(localDevice), std::filesystem::path(dataDirStr), settings);

    gManager->setDeviceListCallback(
        [](std::vector<wiremic::android::DiscoveredDevice> devices) {
          RunGuarded("deviceListCallback", [&]() {
            json array = json::array();
            for (const auto& device : devices) {
              array.push_back(DeviceToJson(
                  device.info,
                  device.status == wiremic::android::DeviceStatus::Online
                      ? "Online"
                      : "Offline"));
            }
            WIREMIC_LOG_INFO("Device list changed: %zu device(s)",
                              devices.size());
            CallStringMethod(gOnDeviceListChanged, array.dump());
          });
        });

    gManager->setStateCallback([](wiremic::protocol::ConnectionState state) {
      RunGuarded("stateCallback", [&]() {
        const auto stateStr = ConnectionStateToString(state);
        WIREMIC_LOG_INFO("Connection state changed: %s", stateStr.c_str());
        CallStringMethod(gOnConnectionStateChanged, stateStr);
      });
    });

    gManager->setEstablishedCallback([](wiremic::protocol::DeviceInfo device) {
      RunGuarded("establishedCallback", [&]() {
        WIREMIC_LOG_INFO("Connection established with %s", device.name.c_str());
        CallStringMethod(gOnConnectionEstablished, DeviceToJson(device).dump());
      });
    });

    gManager->setClosedCallback([](wiremic::protocol::DisconnectReason reason) {
      RunGuarded("closedCallback", [&]() {
        WIREMIC_LOG_INFO("Connection closed, reason=%d",
                          static_cast<int>(reason));
        CallStringMethod(gOnConnectionClosed,
                          std::to_string(static_cast<int>(reason)));
      });
    });

    gManager->setFailedCallback([](std::string reason) {
      RunGuarded("failedCallback", [&]() {
        WIREMIC_LOG_ERROR("Connection failed: %s", reason.c_str());
        CallStringMethod(gOnConnectionFailed, reason);
      });
    });

    gManager->setErrorCallback([](std::string message) {
      RunGuarded("errorCallback", [&]() {
        WIREMIC_LOG_ERROR("Native error: %s", message.c_str());
        CallStringMethod(gOnError, message);
      });
    });

    started = gManager->start();
    WIREMIC_LOG_INFO("ConnectionManager start() returned %d", started);
  });

  return started ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeStop(JNIEnv*, jobject) {
  RunGuarded("nativeStop", [&]() {
    std::lock_guard<std::mutex> lock(gManagerMutex);
    if (gManager) {
      gManager->stop();
      gManager.reset();
      WIREMIC_LOG_INFO("ConnectionManager stopped");
    }
  });
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeRequestConnection(
    JNIEnv* env, jobject, jstring deviceId) {
  RunGuarded("nativeRequestConnection", [&]() {
    std::lock_guard<std::mutex> lock(gManagerMutex);
    if (!gManager) {
      WIREMIC_LOG_ERROR("nativeRequestConnection called with no manager");
      return;
    }
    const char* idChars = env->GetStringUTFChars(deviceId, nullptr);
    const std::string id = idChars;
    env->ReleaseStringUTFChars(deviceId, idChars);
    WIREMIC_LOG_INFO("Requesting connection to device %s", id.c_str());
    gManager->requestConnection(id);
  });
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeDisconnect(JNIEnv*, jobject) {
  RunGuarded("nativeDisconnect", [&]() {
    std::lock_guard<std::mutex> lock(gManagerMutex);
    if (gManager) {
      WIREMIC_LOG_INFO("Disconnecting active connection");
      gManager->disconnectActive();
    }
  });
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeRefreshDiscovery(JNIEnv*, jobject) {
  RunGuarded("nativeRefreshDiscovery", [&]() {
    std::lock_guard<std::mutex> lock(gManagerMutex);
    if (gManager) gManager->refreshDiscovery();
  });
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeProbeHost(JNIEnv* env, jobject,
                                                        jstring host) {
  bool sent = false;
  RunGuarded("nativeProbeHost", [&]() {
    std::lock_guard<std::mutex> lock(gManagerMutex);
    if (!gManager) return;
    const char* hostChars = env->GetStringUTFChars(host, nullptr);
    const std::string address = hostChars;
    env->ReleaseStringUTFChars(host, hostChars);
    WIREMIC_LOG_INFO("Probing host %s directly", address.c_str());
    sent = gManager->probeHost(address);
  });
  return sent ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeGetDevices(JNIEnv* env, jobject) {
  std::string result = "[]";
  RunGuarded("nativeGetDevices", [&]() {
    std::lock_guard<std::mutex> lock(gManagerMutex);
    json array = json::array();
    if (gManager) {
      for (const auto& device : gManager->discoveredDevices()) {
        array.push_back(DeviceToJson(
            device.info,
            device.status == wiremic::android::DeviceStatus::Online
                ? "Online"
                : "Offline"));
      }
    }
    result = array.dump();
  });
  return env->NewStringUTF(result.c_str());
}
