#include "Check.hpp"

#include <iostream>

#include "AudioPacketCodec.hpp"

using namespace wiremic::audio;

int main() {
  SessionKey key{};
  for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 3 + 1);

  std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05,
                                   0xAA, 0xBB, 0xCC, 0xDD};

  auto packet = AudioPacketCodec::Encrypt(key, 42, 123456, true, false, payload);
  WIREMIC_CHECK(packet.size() == 34 + payload.size());

  auto decrypted = AudioPacketCodec::Decrypt(key, packet.data(), packet.size());
  WIREMIC_CHECK(decrypted.has_value());
  WIREMIC_CHECK(decrypted->sequence == 42);
  WIREMIC_CHECK(decrypted->captureTimestampMs == 123456);
  WIREMIC_CHECK(decrypted->marker == true);
  WIREMIC_CHECK(decrypted->dtx == false);
  WIREMIC_CHECK(decrypted->opusPayload == payload);
  std::cout << "ENCRYPT_DECRYPT_ROUNDTRIP_OK\n";

  auto dtxPacket = AudioPacketCodec::Encrypt(key, 43, 123466, false, true, {});
  auto decryptedDtx = AudioPacketCodec::Decrypt(key, dtxPacket.data(), dtxPacket.size());
  WIREMIC_CHECK(decryptedDtx.has_value());
  WIREMIC_CHECK(decryptedDtx->dtx == true);
  WIREMIC_CHECK(decryptedDtx->opusPayload.empty());
  std::cout << "DTX_EMPTY_PAYLOAD_OK\n";

  auto tampered = packet;
  tampered[40] ^= 0xFF;
  auto tamperedResult = AudioPacketCodec::Decrypt(key, tampered.data(), tampered.size());
  WIREMIC_CHECK(!tamperedResult.has_value());
  std::cout << "TAMPERED_CIPHERTEXT_REJECTED_OK\n";

  auto tamperedHeader = packet;
  tamperedHeader[2] ^= 0xFF;
  auto tamperedHeaderResult =
      AudioPacketCodec::Decrypt(key, tamperedHeader.data(), tamperedHeader.size());
  WIREMIC_CHECK(!tamperedHeaderResult.has_value());
  std::cout << "TAMPERED_HEADER_REJECTED_OK\n";

  SessionKey wrongKey = key;
  wrongKey[0] ^= 0xFF;
  auto wrongKeyResult = AudioPacketCodec::Decrypt(wrongKey, packet.data(), packet.size());
  WIREMIC_CHECK(!wrongKeyResult.has_value());
  std::cout << "WRONG_KEY_REJECTED_OK\n";

  auto truncated = std::vector<uint8_t>(packet.begin(), packet.begin() + 10);
  auto truncatedResult = AudioPacketCodec::Decrypt(key, truncated.data(), truncated.size());
  WIREMIC_CHECK(!truncatedResult.has_value());
  std::cout << "TRUNCATED_PACKET_REJECTED_OK\n";

  std::cout << "AUDIO_PACKET_CODEC_TESTS_PASSED\n";
  return 0;
}
