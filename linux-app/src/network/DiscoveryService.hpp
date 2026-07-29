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
  bool sendInvite(const protocol::ConnectInvite& invite);
  [[nodiscard]] std::vector<DiscoveredDevice> devices() const;
 signals:
  void inviteReceived(const protocol::ConnectInvite& invite);
  void deviceDiscovered(const DiscoveredDevice& device);
  void deviceUpdated(const DiscoveredDevice& device);
  void deviceStatusChanged(const QString& deviceId, DeviceStatus status);
  void deviceRemoved(const QString& deviceId);
  void errorOccurred(const QString& message);
 private slots:
  void onReadyRead();
  void onAnnounceTimer();
  void onSweepTimer();
  void onRebindTimer();
 private:
  struct LanInterface {
    QString name;
    QHostAddress address;
    QHostAddress broadcast;
  };

  bool bindSocket();
  void scheduleRebind();
  void sendAnnounce();
  bool broadcast(const QByteArray& bytes);
  void handlePacket(const QByteArray& data, const QHostAddress& sender);

  [[nodiscard]] static std::vector<LanInterface> LanInterfaces();
  void refreshMulticastMemberships();
  bool sendToInterface(const QByteArray& bytes, const LanInterface& interface);

  protocol::DeviceInfo localDevice_;
  QUdpSocket socket_;
  QStringList joinedGroups_;
  QTimer announceTimer_;
  QTimer sweepTimer_;
  QTimer rebindTimer_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  QString bindError_;
  bool running_{false};
  bool bound_{false};
};
}
