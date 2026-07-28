#include "TrustedDeviceStore.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace wiremic::security {

using json = nlohmann::json;

TrustedDeviceStore::TrustedDeviceStore(std::filesystem::path storagePath)
    : storagePath_(std::move(storagePath)) {
  Load();
}

void TrustedDeviceStore::Load() {
  devices_.clear();
  if (!std::filesystem::exists(storagePath_)) return;

  std::ifstream stream(storagePath_, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();

  const auto root = json::parse(buffer.str(), nullptr, false);
  if (!root.is_array()) return;

  for (const auto& entry : root) {
    TrustedDevice device;
    device.deviceId = entry.value("deviceId", "");
    device.name = entry.value("name", "");
    device.certFingerprint = entry.value("certFingerprint", "");
    device.trustedAtEpochSeconds = entry.value("trustedAt", int64_t{0});
    if (!device.deviceId.empty()) {
      devices_.emplace(device.deviceId, std::move(device));
    }
  }
}

void TrustedDeviceStore::Save() const {
  json root = json::array();
  for (const auto& [id, device] : devices_) {
    root.push_back(json{
        {"deviceId", device.deviceId},
        {"name", device.name},
        {"certFingerprint", device.certFingerprint},
        {"trustedAt", device.trustedAtEpochSeconds},
    });
  }

  std::filesystem::create_directories(storagePath_.parent_path());
  std::ofstream stream(storagePath_, std::ios::binary | std::ios::trunc);
  stream << root.dump(2);
}

bool TrustedDeviceStore::IsTrusted(const std::string& deviceId,
                                    const std::string& certFingerprint) const {
  const auto it = devices_.find(deviceId);
  if (it == devices_.end()) return false;
  return it->second.certFingerprint == certFingerprint;
}

void TrustedDeviceStore::Trust(const std::string& deviceId,
                                const std::string& name,
                                const std::string& certFingerprint) {
  TrustedDevice device;
  device.deviceId = deviceId;
  device.name = name;
  device.certFingerprint = certFingerprint;
  device.trustedAtEpochSeconds =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  devices_[deviceId] = std::move(device);
  Save();
}

void TrustedDeviceStore::Revoke(const std::string& deviceId) {
  if (devices_.erase(deviceId) > 0) {
    Save();
  }
}

std::vector<TrustedDevice> TrustedDeviceStore::All() const {
  std::vector<TrustedDevice> result;
  result.reserve(devices_.size());
  for (const auto& [id, device] : devices_) {
    result.push_back(device);
  }
  return result;
}

std::optional<TrustedDevice> TrustedDeviceStore::Find(
    const std::string& deviceId) const {
  const auto it = devices_.find(deviceId);
  if (it == devices_.end()) return std::nullopt;
  return it->second;
}

}
