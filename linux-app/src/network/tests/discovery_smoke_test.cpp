#include "Check.hpp"
#include <QCoreApplication>
#include <QTimer>

#include <iostream>

#include "DiscoveryService.hpp"

using namespace wiremic;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  protocol::DeviceInfo local;
  local.id = "smoke-test-device";
  local.name = "Smoke Test";
  local.model = "CI Runner";
  local.platform = protocol::Platform::Linux;
  local.connectionType = protocol::ConnectionType::Ethernet;
  local.controlPort = protocol::kDefaultControlPort;

  network::DiscoveryService service(local);

  bool sawError = false;
  QObject::connect(&service, &network::DiscoveryService::errorOccurred,
                    [&sawError](const QString& message) {
                      std::cerr << "error: " << message.toStdString()
                                << "\n";
                      sawError = true;
                    });

  const bool started = service.start();
  WIREMIC_CHECK(started);
  WIREMIC_CHECK(service.devices().empty());

  service.refreshNow();
  service.stop();
  WIREMIC_CHECK(service.devices().empty());

  const bool restarted = service.start();
  WIREMIC_CHECK(restarted);
  service.stop();

  if (sawError) {
    std::cerr << "SMOKE_TEST_FAILED\n";
    return 1;
  }

  std::cout << "DISCOVERY_SMOKE_OK\n";
  return 0;
}
