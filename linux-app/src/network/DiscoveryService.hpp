#pragma once
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <chrono>
#include <optional>
#include <unordered_map>
#include "Protocol.hpp"
namespace wiremic::network {
enum class DeviceStatus { Online, Offline };
struct DiscoveredDevice {
  protocol::DeviceInfo info;
  DeviceStatus status{DeviceStatus::Online};
  std::chrono::steady_clock::time_point lastSeen;
  int missedAnnounces{0};
};
class DiscoveryService : public QObject {
  Q_OBJECT
 public:
  explicit DiscoveryService(protocol::DeviceInfo localDevice, QObject* parent = nullptr);
  ~DiscoveryService() override;
  bool start();
  void stop();
  void refreshNow();
  [[nodiscard]] std::vector<DiscoveredDevice> devices() const;
 signals:
  void deviceDiscovered(const DiscoveredDevice& device);
  void deviceUpdated(const DiscoveredDevice& device);
  void deviceStatusChanged(const QString& deviceId, DeviceStatus status);
  void deviceRemoved(const QString& deviceId);
  void errorOccurred(const QString& message);
 private slots:
  void onReadyRead();
  void onAnnounceTimer();
  void onSweepTimer();
 private:
  void sendAnnounce();
  void handlePacket(const QByteArray& data, const QHostAddress& sender);
  protocol::DeviceInfo localDevice_;
  QUdpSocket socket_;
  QTimer announceTimer_;
  QTimer sweepTimer_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  bool running_{false};
  bool bound_{false};
  int retryCount_{0};
};
}  // namespace wiremic::network
