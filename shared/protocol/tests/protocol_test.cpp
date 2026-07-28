#include "Check.hpp"
#include <iostream>

#include "Protocol.hpp"

using namespace wiremic::protocol;

int main() {
  AnnouncePacket announce;
  announce.device.id = "d290f1ee-6c54-4b01-90e6-d701748f0851";
  announce.device.name = "Artin's Pixel 8";
  announce.device.model = "Pixel 8 Pro";
  announce.device.platform = Platform::Android;
  announce.device.ip = "192.168.1.34";
  announce.device.connectionType = ConnectionType::Wifi;
  announce.device.controlPort = 47600;

  auto json1 = ToJson(announce);
  auto parsed1 = ParseAnnounce(json1);
  WIREMIC_CHECK(parsed1.has_value());
  WIREMIC_CHECK(parsed1->device.id == announce.device.id);
  WIREMIC_CHECK(parsed1->device.platform == Platform::Android);
  WIREMIC_CHECK(parsed1->device.connectionType == ConnectionType::Wifi);
  std::cout << "ANNOUNCE_OK: " << json1 << "\n";

  ConnectRequest request;
  request.requestId = "req-1";
  request.device = announce.device;
  request.certFingerprint = "sha256:ab12cd34";
  request.capabilities.sampleRates = {48000, 44100};
  request.capabilities.codec = AudioCodec::Opus;
  request.capabilities.maxBitrateKbps = 128;

  auto json2 = ToJson(request);
  auto parsed2 = ParseConnectRequest(json2);
  WIREMIC_CHECK(parsed2.has_value());
  WIREMIC_CHECK(parsed2->capabilities.sampleRates.size() == 2);
  WIREMIC_CHECK(parsed2->capabilities.sampleRates[0] == 48000);
  WIREMIC_CHECK(parsed2->audioRole == AudioRole::Sender);
  WIREMIC_CHECK(!parsed2->offeredSession.has_value());
  std::cout << "CONNECT_REQUEST_OK: " << json2 << "\n";

  ConnectRequest receiverRequest = request;
  receiverRequest.requestId = "req-2";
  receiverRequest.audioRole = AudioRole::Receiver;
  AudioSession offered;
  offered.udpPort = 47712;
  offered.sampleRate = 48000;
  offered.channels = 1;
  offered.frameSizeMs = 10;
  offered.bitrateKbps = 96;
  for (size_t i = 0; i < offered.sessionKey.size(); ++i) {
    offered.sessionKey[i] = static_cast<uint8_t>(i * 5 + 1);
  }
  receiverRequest.offeredSession = offered;

  auto json2b = ToJson(receiverRequest);
  auto parsed2b = ParseConnectRequest(json2b);
  WIREMIC_CHECK(parsed2b.has_value());
  WIREMIC_CHECK(parsed2b->audioRole == AudioRole::Receiver);
  WIREMIC_CHECK(parsed2b->offeredSession.has_value());
  WIREMIC_CHECK(parsed2b->offeredSession->udpPort == 47712);
  WIREMIC_CHECK(parsed2b->offeredSession->frameSizeMs == 10);
  WIREMIC_CHECK(parsed2b->offeredSession->sessionKey == offered.sessionKey);
  std::cout << "CONNECT_REQUEST_RECEIVER_ROLE_OK: " << json2b << "\n";

  const std::string legacyRequest =
      R"({"type":"CONNECT_REQUEST","requestId":"legacy","device":)"
      R"({"id":"x","name":"n","model":"m","platform":"android","ip":"1.2.3.4",)"
      R"("connectionType":"wifi","controlPort":47600},"certFingerprint":"sha256:aa",)"
      R"("audioCapabilities":{"sampleRates":[48000],"codec":"opus","maxBitrateKbps":128}})";
  auto parsedLegacy = ParseConnectRequest(legacyRequest);
  WIREMIC_CHECK(parsedLegacy.has_value());
  WIREMIC_CHECK(parsedLegacy->audioRole == AudioRole::Sender);
  WIREMIC_CHECK(!parsedLegacy->offeredSession.has_value());
  std::cout << "CONNECT_REQUEST_LEGACY_COMPAT_OK\n";

  ConnectResponse response;
  response.requestId = "req-1";
  response.accepted = true;
  AudioSession session;
  session.udpPort = 47700;
  session.sampleRate = 48000;
  session.channels = 1;
  session.codec = AudioCodec::Opus;
  session.bitrateKbps = 96;
  session.frameSizeMs = 10;
  for (size_t i = 0; i < session.sessionKey.size(); ++i) {
    session.sessionKey[i] = static_cast<uint8_t>(i * 7 + 1);
  }
  response.session = session;

  auto json3 = ToJson(response);
  auto parsed3 = ParseConnectResponse(json3);
  WIREMIC_CHECK(parsed3.has_value());
  WIREMIC_CHECK(parsed3->session.has_value());
  WIREMIC_CHECK(parsed3->session->sessionKey == session.sessionKey);
  WIREMIC_CHECK(parsed3->session->udpPort == 47700);
  std::cout << "CONNECT_RESPONSE_OK: " << json3 << "\n";

  ConnectResponse rejection;
  rejection.requestId = "req-2";
  rejection.accepted = false;
  rejection.reason = RejectReason::RejectedByUser;
  auto json4 = ToJson(rejection);
  auto parsed4 = ParseConnectResponse(json4);
  WIREMIC_CHECK(parsed4.has_value());
  WIREMIC_CHECK(!parsed4->accepted);
  WIREMIC_CHECK(parsed4->reason == RejectReason::RejectedByUser);
  WIREMIC_CHECK(!parsed4->session.has_value());
  std::cout << "REJECTION_OK: " << json4 << "\n";

  KeepAlive keepAlive{42};
  auto kaJson = ToJson(keepAlive);
  WIREMIC_CHECK(PeekMessageType(kaJson) == ControlMessageType::KeepAlive);
  auto parsedKa = ParseKeepAlive(kaJson);
  WIREMIC_CHECK(parsedKa.has_value());
  WIREMIC_CHECK(parsedKa->sequence == 42);
  std::cout << "KEEPALIVE_OK: " << kaJson << "\n";

  KeepAliveAck keepAliveAck{42};
  auto kaAckJson = ToJson(keepAliveAck);
  WIREMIC_CHECK(PeekMessageType(kaAckJson) ==
                ControlMessageType::KeepAliveAck);
  auto parsedKaAck = ParseKeepAliveAck(kaAckJson);
  WIREMIC_CHECK(parsedKaAck.has_value());
  WIREMIC_CHECK(parsedKaAck->sequence == 42);
  std::cout << "KEEPALIVE_ACK_OK: " << kaAckJson << "\n";

  DisconnectMessage disconnect{DisconnectReason::Timeout};
  auto discJson = ToJson(disconnect);
  WIREMIC_CHECK(PeekMessageType(discJson) == ControlMessageType::Disconnect);
  auto parsedDisc = ParseDisconnect(discJson);
  WIREMIC_CHECK(parsedDisc.has_value());
  WIREMIC_CHECK(parsedDisc->reason == DisconnectReason::Timeout);
  std::cout << "DISCONNECT_OK: " << discJson << "\n";

  {
    const std::string malformedType =
        R"({"type":"CONNECT_REQUEST","requestId":"r1",)"
        R"("device":{"id":"d1","name":"n","model":"m","platform":"linux",)"
        R"("ip":"1.2.3.4","connectionType":"wifi","controlPort":"NOT_A_NUMBER"},)"
        R"("certFingerprint":"sha256:aa","audioCapabilities":{"sampleRates":[48000],)"
        R"("codec":"opus","maxBitrateKbps":128}})";

    bool threw = false;
    std::optional<ConnectRequest> parsed;
    try {
      parsed = ParseConnectRequest(malformedType);
    } catch (...) {
      threw = true;
    }
    WIREMIC_CHECK(!threw);
    std::cout << "MALFORMED_FIELD_TYPE_DOES_NOT_THROW_OK (parsed="
              << parsed.has_value() << ")\n";

    const std::string malformedAnnounce =
        R"({"type":"ANNOUNCE","id":"d1","name":"n","model":"m",)"
        R"("platform":"linux","ip":"1.2.3.4","connectionType":"wifi",)"
        R"("controlPort":[1,2,3]})";
    bool announceThrew = false;
    try {
      auto announceParsed = ParseAnnounce(malformedAnnounce);
      (void)announceParsed;
    } catch (...) {
      announceThrew = true;
    }
    WIREMIC_CHECK(!announceThrew);
    std::cout << "MALFORMED_ANNOUNCE_FIELD_TYPE_DOES_NOT_THROW_OK\n";
  }

  std::cout << "ALL_TESTS_PASSED\n";
  return 0;
}
