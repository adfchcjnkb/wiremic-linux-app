#include "AudioPacketCodec.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <memory>
#include <stdexcept>

namespace wiremic::audio {

namespace {

constexpr size_t kHeaderBytes = 34;
constexpr size_t kTagBytes = 16;
constexpr size_t kNonceBytes = 12;

using CipherCtxPtr =
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

void WriteU64(uint8_t* out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<uint8_t>((value >> (56 - i * 8)) & 0xFF);
  }
}

uint64_t ReadU64(const uint8_t* in) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | in[i];
  }
  return value;
}

void WriteU32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(value & 0xFF);
}

uint32_t ReadU32(const uint8_t* in) {
  return (static_cast<uint32_t>(in[0]) << 24) |
         (static_cast<uint32_t>(in[1]) << 16) |
         (static_cast<uint32_t>(in[2]) << 8) | static_cast<uint32_t>(in[3]);
}

}

std::vector<uint8_t> AudioPacketCodec::Encrypt(
    const SessionKey& key, uint64_t sequence, uint32_t captureTimestampMs,
    bool marker, bool dtx, const std::vector<uint8_t>& opusPayload) {
  std::array<uint8_t, kHeaderBytes> header{};
  header[0] = 1;
  header[1] = static_cast<uint8_t>((marker ? 0x01 : 0x00) |
                                    (dtx ? 0x02 : 0x00));
  WriteU64(header.data() + 2, sequence);
  WriteU32(header.data() + 10, captureTimestampMs);

  uint32_t salt = 0;
  RAND_bytes(reinterpret_cast<unsigned char*>(&salt), sizeof(salt));
  WriteU32(header.data() + 14, salt);

  std::array<uint8_t, kNonceBytes> nonce{};
  WriteU64(nonce.data(), sequence);
  WriteU32(nonce.data() + 8, salt);

  CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx) throw std::runtime_error("Failed to allocate cipher context");

  if (EVP_EncryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr,
                          key.data(), nonce.data()) != 1) {
    throw std::runtime_error("Failed to init ChaCha20-Poly1305 encryption");
  }

  int outLen = 0;
  if (EVP_EncryptUpdate(ctx.get(), nullptr, &outLen, header.data(),
                         static_cast<int>(18)) != 1) {
    throw std::runtime_error("Failed to authenticate header as AAD");
  }

  std::vector<uint8_t> ciphertext(opusPayload.size());
  int ciphertextLen = 0;
  if (!opusPayload.empty()) {
    if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &ciphertextLen,
                           opusPayload.data(),
                           static_cast<int>(opusPayload.size())) != 1) {
      throw std::runtime_error("Failed to encrypt audio payload");
    }
  }

  int finalLen = 0;
  std::array<uint8_t, 16> finalBlock{};
  if (EVP_EncryptFinal_ex(ctx.get(), finalBlock.data(), &finalLen) != 1) {
    throw std::runtime_error("Failed to finalize encryption");
  }

  std::array<uint8_t, kTagBytes> tag{};
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG,
                           static_cast<int>(kTagBytes), tag.data()) != 1) {
    throw std::runtime_error("Failed to obtain authentication tag");
  }

  std::vector<uint8_t> packet;
  packet.reserve(kHeaderBytes + static_cast<size_t>(ciphertextLen));
  packet.insert(packet.end(), header.begin(), header.begin() + 18);
  packet.insert(packet.end(), tag.begin(), tag.end());
  packet.insert(packet.end(), ciphertext.begin(),
                ciphertext.begin() + ciphertextLen);
  return packet;
}

std::optional<DecryptedAudioPacket> AudioPacketCodec::Decrypt(
    const SessionKey& key, const uint8_t* data, size_t length) {
  if (length < kHeaderBytes) return std::nullopt;
  if (data[0] != 1) return std::nullopt;

  const bool marker = (data[1] & 0x01) != 0;
  const bool dtx = (data[1] & 0x02) != 0;
  const uint64_t sequence = ReadU64(data + 2);
  const uint32_t captureTimestampMs = ReadU32(data + 10);
  const uint32_t salt = ReadU32(data + 14);

  std::array<uint8_t, kNonceBytes> nonce{};
  WriteU64(nonce.data(), sequence);
  WriteU32(nonce.data() + 8, salt);

  const uint8_t* tag = data + 18;
  const uint8_t* ciphertext = data + kHeaderBytes;
  const size_t ciphertextLen = length - kHeaderBytes;

  CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx) return std::nullopt;

  if (EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr,
                          key.data(), nonce.data()) != 1) {
    return std::nullopt;
  }

  int outLen = 0;
  if (EVP_DecryptUpdate(ctx.get(), nullptr, &outLen, data,
                         static_cast<int>(18)) != 1) {
    return std::nullopt;
  }

  std::vector<uint8_t> plaintext(ciphertextLen > 0 ? ciphertextLen : 1);
  int plaintextLen = 0;
  if (ciphertextLen > 0) {
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &plaintextLen,
                           ciphertext, static_cast<int>(ciphertextLen)) != 1) {
      return std::nullopt;
    }
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG,
                           static_cast<int>(kTagBytes),
                           const_cast<uint8_t*>(tag)) != 1) {
    return std::nullopt;
  }

  std::array<uint8_t, 16> finalBlock{};
  int finalLen = 0;
  if (EVP_DecryptFinal_ex(ctx.get(), finalBlock.data(), &finalLen) != 1) {
    return std::nullopt;
  }

  plaintext.resize(static_cast<size_t>(plaintextLen));

  DecryptedAudioPacket result;
  result.sequence = sequence;
  result.captureTimestampMs = captureTimestampMs;
  result.marker = marker;
  result.dtx = dtx;
  result.opusPayload = std::move(plaintext);
  return result;
}

}
