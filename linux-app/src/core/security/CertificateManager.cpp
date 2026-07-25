#include "CertificateManager.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace wiremic::security {

namespace {

constexpr int kCertValidityDays = 3650;

using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

EvpPkeyPtr GenerateEcKey() {
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
  if (!ctx) throw std::runtime_error("Failed to create EC key context");

  EvpPkeyPtr pkey(nullptr, EVP_PKEY_free);
  EVP_PKEY* rawKey = nullptr;

  if (EVP_PKEY_keygen_init(ctx) <= 0 ||
      EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <=
          0 ||
      EVP_PKEY_keygen(ctx, &rawKey) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    throw std::runtime_error("Failed to generate EC key");
  }

  EVP_PKEY_CTX_free(ctx);
  pkey.reset(rawKey);
  return pkey;
}

X509Ptr GenerateSelfSignedCert(EVP_PKEY* pkey) {
  X509Ptr cert(X509_new(), X509_free);
  if (!cert) throw std::runtime_error("Failed to allocate X509");

  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()),
                   60L * 60L * 24L * kCertValidityDays);
  X509_set_pubkey(cert.get(), pkey);

  X509_NAME* name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "CN",
                              MBSTRING_ASC,
                              reinterpret_cast<const unsigned char*>(
                                  "wiremic-local-device"),
                              -1, -1, 0);
  X509_set_issuer_name(cert.get(), name);

  if (X509_sign(cert.get(), pkey, EVP_sha256()) == 0) {
    throw std::runtime_error("Failed to sign certificate");
  }
  return cert;
}

std::string BioToString(BIO* bio) {
  char* data = nullptr;
  long length = BIO_get_mem_data(bio, &data);
  return std::string(data, static_cast<size_t>(length));
}

std::string CertToPem(X509* cert) {
  BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
  PEM_write_bio_X509(bio.get(), cert);
  return BioToString(bio.get());
}

std::string KeyToPem(EVP_PKEY* pkey) {
  BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
  PEM_write_bio_PrivateKey(bio.get(), pkey, nullptr, nullptr, 0, nullptr,
                            nullptr);
  return BioToString(bio.get());
}

std::string HexEncode(const unsigned char* data, unsigned int length) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (unsigned int i = 0; i < length; ++i) {
    out.push_back(kHex[(data[i] >> 4) & 0xF]);
    out.push_back(kHex[data[i] & 0xF]);
  }
  return out;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void WriteFile(const std::filesystem::path& path, const std::string& data) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << data;
}

}  // namespace

std::optional<std::string> CertificateManager::ComputeFingerprint(
    const std::string& certificatePem) {
  BioPtr bio(BIO_new_mem_buf(certificatePem.data(),
                              static_cast<int>(certificatePem.size())),
             BIO_free);
  X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr),
               X509_free);
  if (!cert) return std::nullopt;

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digestLength = 0;
  if (X509_digest(cert.get(), EVP_sha256(), digest.data(), &digestLength) ==
      0) {
    return std::nullopt;
  }

  return "sha256:" + HexEncode(digest.data(), digestLength);
}

CertificateManager::CertificateManager(std::filesystem::path storageDir)
    : storageDir_(std::move(storageDir)), certificate_(LoadOrCreate()) {}

const Certificate& CertificateManager::localCertificate() const {
  return certificate_;
}

std::optional<Certificate> CertificateManager::TryLoad() const {
  const auto certPath = storageDir_ / "device.crt";
  const auto keyPath = storageDir_ / "device.key";
  if (!std::filesystem::exists(certPath) ||
      !std::filesystem::exists(keyPath)) {
    return std::nullopt;
  }

  Certificate certificate;
  certificate.certificatePem = ReadFile(certPath);
  certificate.privateKeyPem = ReadFile(keyPath);
  auto fingerprint = ComputeFingerprint(certificate.certificatePem);
  if (!fingerprint) return std::nullopt;
  certificate.fingerprintSha256 = *fingerprint;
  return certificate;
}

Certificate CertificateManager::Generate() const {
  auto key = GenerateEcKey();
  auto cert = GenerateSelfSignedCert(key.get());

  Certificate certificate;
  certificate.certificatePem = CertToPem(cert.get());
  certificate.privateKeyPem = KeyToPem(key.get());
  auto fingerprint = ComputeFingerprint(certificate.certificatePem);
  if (!fingerprint) {
    throw std::runtime_error("Failed to compute fingerprint after generation");
  }
  certificate.fingerprintSha256 = *fingerprint;
  return certificate;
}

void CertificateManager::Persist(const Certificate& certificate) const {
  std::filesystem::create_directories(storageDir_);
  WriteFile(storageDir_ / "device.crt", certificate.certificatePem);
  WriteFile(storageDir_ / "device.key", certificate.privateKeyPem);
  std::filesystem::permissions(
      storageDir_ / "device.key",
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

Certificate CertificateManager::LoadOrCreate() {
  if (auto existing = TryLoad()) {
    return *existing;
  }
  auto generated = Generate();
  Persist(generated);
  return generated;
}

}  // namespace wiremic::security
