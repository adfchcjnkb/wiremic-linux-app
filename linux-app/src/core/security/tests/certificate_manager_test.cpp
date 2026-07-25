#include "Check.hpp"
#include <filesystem>
#include <iostream>

#include "CertificateManager.hpp"

using namespace wiremic::security;

int main() {
  const std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "wiremic_cert_test";
  std::filesystem::remove_all(tempDir);

  CertificateManager first(tempDir);
  const auto& cert1 = first.localCertificate();
  WIREMIC_CHECK(!cert1.certificatePem.empty());
  WIREMIC_CHECK(!cert1.privateKeyPem.empty());
  WIREMIC_CHECK(cert1.fingerprintSha256.rfind("sha256:", 0) == 0);
  WIREMIC_CHECK(cert1.fingerprintSha256.size() == 7 + 64);
  std::cout << "GENERATED_FINGERPRINT: " << cert1.fingerprintSha256 << "\n";

  CertificateManager second(tempDir);
  const auto& cert2 = second.localCertificate();
  WIREMIC_CHECK(cert2.fingerprintSha256 == cert1.fingerprintSha256);
  WIREMIC_CHECK(cert2.certificatePem == cert1.certificatePem);
  std::cout << "RELOAD_STABLE_OK\n";

  auto recomputed = CertificateManager::ComputeFingerprint(cert1.certificatePem);
  WIREMIC_CHECK(recomputed.has_value());
  WIREMIC_CHECK(*recomputed == cert1.fingerprintSha256);
  std::cout << "FINGERPRINT_RECOMPUTE_OK\n";

  std::filesystem::remove_all(tempDir);
  std::cout << "CERT_MANAGER_TESTS_PASSED\n";
  return 0;
}
