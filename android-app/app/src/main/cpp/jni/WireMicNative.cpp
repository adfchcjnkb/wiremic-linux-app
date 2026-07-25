#include <jni.h>

#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "ConnectionManager.hpp"

using json = nlohmann::json;
using wiremic::android::ConnectionManager;
using wiremic::android::ConnectionManagerSettings;

namespace {

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
  if (!guard.env) return;
  jstring jPayload = guard.env->NewStringUTF(payload.c_str());
  guard.env->CallVoidMethod(gListener, method, jPayload);
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

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  gJavaVm = vm;
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeSetListener(JNIEnv* env, jobject,
                                                           jobject listener) {
  if (gListener) {
    env->DeleteGlobalRef(gListener);
    gListener = nullptr;
  }
  if (!listener) return;

  gListener = env->NewGlobalRef(listener);
  jclass listenerClass = env->GetObjectClass(gListener);

  gOnDeviceListChanged =
      env->GetMethodID(listenerClass, "onDeviceListChanged", "(Ljava/lang/String;)V");
  gOnConnectionStateChanged = env->GetMethodID(
      listenerClass, "onConnectionStateChanged", "(Ljava/lang/String;)V");
  gOnConnectionEstablished = env->GetMethodID(
      listenerClass, "onConnectionEstablished", "(Ljava/lang/String;)V");
  gOnConnectionClosed =
      env->GetMethodID(listenerClass, "onConnectionClosed", "(Ljava/lang/String;)V");
  gOnConnectionFailed =
      env->GetMethodID(listenerClass, "onConnectionFailed", "(Ljava/lang/String;)V");
  gOnError = env->GetMethodID(listenerClass, "onNativeError", "(Ljava/lang/String;)V");

  env->DeleteLocalRef(listenerClass);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeStart(
    JNIEnv* env, jobject, jstring deviceId, jstring deviceName,
    jstring deviceModel, jstring dataDir) {
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

  const std::string dataDirStr = dirChars;

  env->ReleaseStringUTFChars(deviceId, idChars);
  env->ReleaseStringUTFChars(deviceName, nameChars);
  env->ReleaseStringUTFChars(deviceModel, modelChars);
  env->ReleaseStringUTFChars(dataDir, dirChars);

  std::lock_guard<std::mutex> lock(gManagerMutex);

  ConnectionManagerSettings settings;
  gManager = std::make_unique<ConnectionManager>(
      std::move(localDevice), std::filesystem::path(dataDirStr), settings);

  gManager->setDeviceListCallback(
      [](std::vector<wiremic::android::DiscoveredDevice> devices) {
        json array = json::array();
        for (const auto& device : devices) {
          array.push_back(DeviceToJson(
              device.info, device.status == wiremic::android::DeviceStatus::Online
                               ? "Online"
                               : "Offline"));
        }
        CallStringMethod(gOnDeviceListChanged, array.dump());
      });

  gManager->setStateCallback([](wiremic::protocol::ConnectionState state) {
    CallStringMethod(gOnConnectionStateChanged, ConnectionStateToString(state));
  });

  gManager->setEstablishedCallback([](wiremic::protocol::DeviceInfo device) {
    CallStringMethod(gOnConnectionEstablished, DeviceToJson(device).dump());
  });

  gManager->setClosedCallback([](wiremic::protocol::DisconnectReason reason) {
    CallStringMethod(gOnConnectionClosed,
                      std::to_string(static_cast<int>(reason)));
  });

  gManager->setFailedCallback(
      [](std::string reason) { CallStringMethod(gOnConnectionFailed, reason); });

  gManager->setErrorCallback(
      [](std::string message) { CallStringMethod(gOnError, message); });

  return gManager->start() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeStop(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(gManagerMutex);
  if (gManager) {
    gManager->stop();
    gManager.reset();
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeRequestConnection(
    JNIEnv* env, jobject, jstring deviceId) {
  std::lock_guard<std::mutex> lock(gManagerMutex);
  if (!gManager) return;
  const char* idChars = env->GetStringUTFChars(deviceId, nullptr);
  gManager->requestConnection(idChars);
  env->ReleaseStringUTFChars(deviceId, idChars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeDisconnect(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(gManagerMutex);
  if (gManager) gManager->disconnectActive();
}

extern "C" JNIEXPORT void JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeRefreshDiscovery(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(gManagerMutex);
  if (gManager) gManager->refreshDiscovery();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_wiremic_app_core_NativeBridge_nativeGetDevices(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(gManagerMutex);
  json array = json::array();
  if (gManager) {
    for (const auto& device : gManager->discoveredDevices()) {
      array.push_back(DeviceToJson(
          device.info, device.status == wiremic::android::DeviceStatus::Online
                           ? "Online"
                           : "Offline"));
    }
  }
  return env->NewStringUTF(array.dump().c_str());
}
