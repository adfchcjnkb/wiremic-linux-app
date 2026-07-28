#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wiremic::security {

struct TrustedDevice {
  std::string deviceId;
  std::string name;
  std::string certFingerprint;
  int64_t trustedAtEpochSeconds{};
};

class TrustedDeviceStore {
 public:
  explicit TrustedDeviceStore(std::filesystem::path storagePath);

  bool IsTrusted(const std::string& deviceId,
                  const std::string& certFingerprint) const;
  void Trust(const std::string& deviceId, const std::string& name,
             const std::string& certFingerprint);
  void Revoke(const std::string& deviceId);
  std::vector<TrustedDevice> All() const;
  std::optional<TrustedDevice> Find(const std::string& deviceId) const;

 private:
  void Load();
  void Save() const;

  std::filesystem::path storagePath_;
  std::unordered_map<std::string, TrustedDevice> devices_;
};

}
