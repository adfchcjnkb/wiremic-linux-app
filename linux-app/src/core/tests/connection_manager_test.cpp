#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <filesystem>
#include <iostream>

#include "Check.hpp"
#include "ConnectionManager.hpp"

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
  poll.start(10);

  if (!predicate()) loop.exec();
  return predicate();
}

protocol::DeviceInfo MakeDevice(const std::string& id, const std::string& name,
                                 protocol::Platform platform) {
  protocol::DeviceInfo device;
  device.id = id;
  device.name = name;
  device.model = "Test Model";
  device.platform = platform;
  device.connectionType = protocol::ConnectionType::Wifi;
  return device;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  if (!QSslSocket::supportsSsl()) {
    std::cerr << "SSL not supported, skipping\n";
    return 0;
  }

  const auto dirA =
      std::filesystem::temp_directory_path() / "wiremic_manager_a";
  const auto dirB =
      std::filesystem::temp_directory_path() / "wiremic_manager_b";
  std::filesystem::remove_all(dirA);
  std::filesystem::remove_all(dirB);

  core::ConnectionManagerSettings settingsA;
  settingsA.rememberTrustedDevices = true;
  settingsA.autoConnect = false;

  core::ConnectionManagerSettings settingsB = settingsA;

  core::ConnectionManager managerA(
      MakeDevice("device-a", "Linux Desktop", protocol::Platform::Linux),
      dirA, settingsA, 0);
  core::ConnectionManager managerB(
      MakeDevice("device-b", "Pixel Phone", protocol::Platform::Android),
      dirB, settingsB, 0);

  WIREMIC_CHECK(managerA.start());
  WIREMIC_CHECK(managerB.start());
  std::cout << "BOTH_MANAGERS_STARTED_OK ports=" << managerA.controlPort()
            << "/" << managerB.controlPort() << "\n";

  const bool discoveredB = WaitFor(
      [&] {
        for (const auto& d : managerA.discoveredDevices()) {
          if (d.info.id == "device-b") return true;
        }
        return false;
      },
      6000);
  WIREMIC_CHECK(discoveredB);
  std::cout << "DISCOVERY_OK\n";

  std::optional<protocol::ConnectRequest> incomingRequest;
  QString incomingFingerprint;
  QObject::connect(&managerB, &core::ConnectionManager::incomingRequestPending,
                    [&](protocol::ConnectRequest request, QString fp) {
                      incomingRequest = request;
                      incomingFingerprint = fp;
                    });

  bool managerAEstablished = false;
  bool managerBEstablished = false;
  QObject::connect(&managerA, &core::ConnectionManager::connectionEstablished,
                    [&](protocol::DeviceInfo) { managerAEstablished = true; });
  QObject::connect(&managerB, &core::ConnectionManager::connectionEstablished,
                    [&](protocol::DeviceInfo) { managerBEstablished = true; });

  QString managerAFailureReason;
  QObject::connect(&managerA, &core::ConnectionManager::connectionFailed,
                    [&](QString reason) { managerAFailureReason = reason; });

  managerA.requestConnection("device-b");

  WIREMIC_CHECK(WaitFor([&] { return incomingRequest.has_value(); }, 6000));
  WIREMIC_CHECK(incomingRequest->device.id == "device-a");
  std::cout << "INCOMING_REQUEST_RECEIVED_OK\n";

  managerB.approveIncoming(incomingRequest->requestId);

  WIREMIC_CHECK(WaitFor([&] { return managerAEstablished; }, 6000));
  WIREMIC_CHECK(WaitFor([&] { return managerBEstablished; }, 6000));
  std::cout << "CONNECTION_ESTABLISHED_BOTH_SIDES_OK\n";

  auto stateA = managerA.activeConnection();
  auto stateB = managerB.activeConnection();
  WIREMIC_CHECK(stateA.has_value());
  WIREMIC_CHECK(stateB.has_value());
  WIREMIC_CHECK(stateA->state == protocol::ConnectionState::Streaming);
  WIREMIC_CHECK(stateB->state == protocol::ConnectionState::Streaming);
  WIREMIC_CHECK(stateA->device.id == "device-b");
  WIREMIC_CHECK(stateB->device.id == "device-a");
  std::cout << "STATE_STREAMING_OK\n";

  const auto trustedByB = managerB.trustedDevices();
  bool foundTrust = false;
  for (const auto& t : trustedByB) {
    if (t.deviceId == "device-a") foundTrust = true;
  }
  WIREMIC_CHECK(foundTrust);
  std::cout << "TRUST_PERSISTED_OK\n";

  bool managerBClosed = false;
  protocol::DisconnectReason managerBCloseReason{};
  QObject::connect(&managerB, &core::ConnectionManager::connectionClosed,
                    [&](protocol::DisconnectReason reason) {
                      managerBClosed = true;
                      managerBCloseReason = reason;
                    });

  managerA.disconnectActive();

  WIREMIC_CHECK(WaitFor([&] { return managerBClosed; }, 6000));
  WIREMIC_CHECK(managerBCloseReason == protocol::DisconnectReason::UserRequested);
  WIREMIC_CHECK(!managerA.activeConnection().has_value());
  WIREMIC_CHECK(!managerB.activeConnection().has_value());
  std::cout << "EXPLICIT_DISCONNECT_PROPAGATED_OK\n";

  incomingRequest.reset();
  managerAFailureReason.clear();
  managerA.requestConnection("device-b");

  WIREMIC_CHECK(WaitFor([&] { return incomingRequest.has_value(); }, 6000));
  managerB.rejectIncoming(incomingRequest->requestId,
                           protocol::RejectReason::RejectedByUser);

  WIREMIC_CHECK(
      WaitFor([&] { return !managerAFailureReason.isEmpty(); }, 6000));
  WIREMIC_CHECK(managerAFailureReason == QStringLiteral("REJECTED_BY_USER"));
  WIREMIC_CHECK(!managerA.activeConnection().has_value());
  std::cout << "REJECTION_PROPAGATED_OK\n";

  managerA.stop();
  managerB.stop();

  std::filesystem::remove_all(dirA);
  std::filesystem::remove_all(dirB);

  std::cout << "CONNECTION_MANAGER_TESTS_PASSED\n";
  return 0;
}
