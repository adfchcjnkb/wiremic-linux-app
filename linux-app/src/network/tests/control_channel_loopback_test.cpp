#include "Check.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>

#include <filesystem>
#include <iostream>

#include "CertificateManager.hpp"
#include "ControlClient.hpp"
#include "ControlServer.hpp"

using namespace wiremic;

namespace {

bool WaitFor(std::function<bool()> predicate, int timeoutMs) {
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(timeoutMs);

  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, [&]() {
    if (predicate()) loop.quit();
  });
  poll.start(5);

  if (!predicate()) loop.exec();
  return predicate();
}

void Delay(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

protocol::ConnectRequest BuildRequest(const std::string& fingerprint) {
  protocol::ConnectRequest request;
  request.requestId = QUuid::createUuid().toString().toStdString();
  request.device.id = "android-client-device";
  request.device.name = "Artin's Pixel 8";
  request.device.model = "Pixel 8 Pro";
  request.device.platform = protocol::Platform::Android;
  request.device.ip = "127.0.0.1";
  request.device.connectionType = protocol::ConnectionType::Wifi;
  request.certFingerprint = fingerprint;
  request.capabilities.sampleRates = {48000, 44100};
  request.capabilities.codec = protocol::AudioCodec::Opus;
  request.capabilities.maxBitrateKbps = 128;
  return request;
}

}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  if (!QSslSocket::supportsSsl()) {
    std::cerr << "SSL not supported in this environment, skipping\n";
    return 0;
  }

  const auto serverDir =
      std::filesystem::temp_directory_path() / "wiremic_server_cert_test";
  const auto clientDir =
      std::filesystem::temp_directory_path() / "wiremic_client_cert_test";
  std::filesystem::remove_all(serverDir);
  std::filesystem::remove_all(clientDir);

  security::CertificateManager serverCerts(serverDir);
  security::CertificateManager clientCerts(clientDir);

  network::ControlServer server(serverCerts, 0);
  WIREMIC_CHECK(server.start());
  std::cout << "SERVER_LISTENING_ON_" << server.port() << "\n";

  std::optional<protocol::ConnectRequest> receivedRequest;
  QString receivedFingerprint;
  QObject::connect(&server, &network::ControlServer::connectRequestReceived,
                    [&](protocol::ConnectRequest request, QString fp) {
                      receivedRequest = request;
                      receivedFingerprint = fp;
                    });

  QStringList unexpectedServerErrors;
  QObject::connect(&server, &network::ControlServer::errorOccurred,
                    [&](const QString& msg) {
                      if (msg.contains(QStringLiteral("mismatch")) ||
                          msg.contains(QStringLiteral("malformed"))) {
                        unexpectedServerErrors.append(msg);
                      }
                    });

  network::ControlClient client(clientCerts);
  std::optional<protocol::ConnectResponse> receivedResponse;
  QObject::connect(&client, &network::ControlClient::responseReceived,
                    [&](protocol::ConnectResponse response) {
                      receivedResponse = response;
                    });

  auto request = BuildRequest(clientCerts.localCertificate().fingerprintSha256);
  const std::string requestId = request.requestId;
  client.connectToDevice(QStringLiteral("127.0.0.1"), server.port(),
                          request);

  const bool gotRequest =
      WaitFor([&] { return receivedRequest.has_value(); }, 5000);
  WIREMIC_CHECK(gotRequest);
  WIREMIC_CHECK(receivedRequest->device.id == "android-client-device");
  WIREMIC_CHECK(receivedFingerprint.toStdString() ==
         clientCerts.localCertificate().fingerprintSha256);
  std::cout << "SERVER_RECEIVED_REQUEST_OK\n";

  protocol::AudioSession session;
  session.udpPort = 47700;
  session.sampleRate = 48000;
  session.channels = 1;
  session.codec = protocol::AudioCodec::Opus;
  session.bitrateKbps = 96;
  session.frameSizeMs = 10;
  for (size_t i = 0; i < session.sessionKey.size(); ++i) {
    session.sessionKey[i] = static_cast<uint8_t>(i + 1);
  }
  server.accept(requestId, session);

  const bool gotResponse =
      WaitFor([&] { return receivedResponse.has_value(); }, 5000);
  WIREMIC_CHECK(gotResponse);
  WIREMIC_CHECK(receivedResponse->accepted);
  WIREMIC_CHECK(receivedResponse->requestId == requestId);
  WIREMIC_CHECK(receivedResponse->session.has_value());
  WIREMIC_CHECK(receivedResponse->session->udpPort == 47700);
  WIREMIC_CHECK(receivedResponse->session->sessionKey == session.sessionKey);
  std::cout << "CLIENT_RECEIVED_ACCEPT_OK\n";

  client.disconnectFromDevice();

  network::ControlClient rejectClient(clientCerts);
  std::optional<protocol::ConnectResponse> rejectResponse;
  QObject::connect(&rejectClient, &network::ControlClient::responseReceived,
                    [&](protocol::ConnectResponse response) {
                      rejectResponse = response;
                    });

  std::optional<protocol::ConnectRequest> secondRequest;
  QObject::connect(&server, &network::ControlServer::connectRequestReceived,
                    [&](protocol::ConnectRequest request, QString) {
                      secondRequest = request;
                    });

  auto request2 =
      BuildRequest(clientCerts.localCertificate().fingerprintSha256);
  const std::string requestId2 = request2.requestId;
  rejectClient.connectToDevice(QStringLiteral("127.0.0.1"), server.port(),
                                request2);

  const bool gotSecondRequest =
      WaitFor([&] { return secondRequest.has_value(); }, 5000);
  WIREMIC_CHECK(gotSecondRequest);

  server.reject(requestId2, protocol::RejectReason::RejectedByUser);

  const bool gotRejectResponse =
      WaitFor([&] { return rejectResponse.has_value(); }, 5000);
  WIREMIC_CHECK(gotRejectResponse);
  WIREMIC_CHECK(!rejectResponse->accepted);
  WIREMIC_CHECK(rejectResponse->reason == protocol::RejectReason::RejectedByUser);
  WIREMIC_CHECK(!rejectResponse->session.has_value());
  std::cout << "CLIENT_RECEIVED_REJECT_OK\n";

  network::ControlClient keepAliveClient(clientCerts);
  std::optional<protocol::ConnectResponse> kaResponse;
  QObject::connect(&keepAliveClient, &network::ControlClient::responseReceived,
                    [&](protocol::ConnectResponse response) {
                      kaResponse = response;
                    });

  std::optional<QString> disconnectedRequestId;
  protocol::DisconnectReason disconnectedReason{};
  QObject::connect(
      &server, &network::ControlServer::clientDisconnected,
      [&](QString requestId, protocol::DisconnectReason reason) {
        disconnectedRequestId = requestId;
        disconnectedReason = reason;
      });

  std::optional<protocol::ConnectRequest> kaRequest;
  QObject::connect(&server, &network::ControlServer::connectRequestReceived,
                    [&](protocol::ConnectRequest request, QString) {
                      kaRequest = request;
                    });

  auto request3 =
      BuildRequest(clientCerts.localCertificate().fingerprintSha256);
  const std::string requestId3 = request3.requestId;
  keepAliveClient.connectToDevice(QStringLiteral("127.0.0.1"), server.port(),
                                   request3);

  WIREMIC_CHECK(WaitFor([&] { return kaRequest.has_value(); }, 5000));
  server.accept(requestId3, session);
  WIREMIC_CHECK(WaitFor([&] { return kaResponse.has_value(); }, 5000));
  WIREMIC_CHECK(kaResponse->accepted);
  std::cout << "KEEPALIVE_SESSION_ESTABLISHED_OK\n";

  bool connectionLostFired = false;
  QObject::connect(&keepAliveClient, &network::ControlClient::connectionLost,
                    [&](QString) { connectionLostFired = true; });

  const int marginBeyondMissedLimit =
      protocol::kKeepaliveIntervalMs *
          (protocol::kKeepaliveMissedLimit + 1) +
      1000;
  Delay(marginBeyondMissedLimit);
  WIREMIC_CHECK(!connectionLostFired);
  std::cout << "KEEPALIVE_ROUNDTRIP_SURVIVED_OK\n";

  keepAliveClient.disconnectFromDevice(protocol::DisconnectReason::UserRequested);

  WIREMIC_CHECK(
      WaitFor([&] { return disconnectedRequestId.has_value(); }, 5000));
  WIREMIC_CHECK(*disconnectedRequestId == QString::fromStdString(requestId3));
  WIREMIC_CHECK(disconnectedReason ==
                protocol::DisconnectReason::UserRequested);
  std::cout << "EXPLICIT_DISCONNECT_OK\n";

  WIREMIC_CHECK(unexpectedServerErrors.isEmpty());

  server.stop();
  std::filesystem::remove_all(serverDir);
  std::filesystem::remove_all(clientDir);

  std::cout << "CONTROL_CHANNEL_TESTS_PASSED\n";
  return 0;
}
