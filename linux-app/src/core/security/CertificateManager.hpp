#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace wiremic::security {

struct Certificate {
  std::string certificatePem;
  std::string privateKeyPem;
  std::string fingerprintSha256;
};

class CertificateManager {
 public:
  explicit CertificateManager(std::filesystem::path storageDir);

  const Certificate& localCertificate() const;
  static std::optional<std::string> ComputeFingerprint(
      const std::string& certificatePem);

 private:
  Certificate LoadOrCreate();
  Certificate Generate() const;
  void Persist(const Certificate& certificate) const;
  std::optional<Certificate> TryLoad() const;

  std::filesystem::path storageDir_;
  Certificate certificate_;
};

}  // namespace wiremic::security
