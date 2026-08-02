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

  // The addresses this computer is reachable at, in the same order and with the
  // same filtering discovery itself uses. Shown on screen so someone whose
  // network is swallowing discovery traffic can type one into their phone.
  [[nodiscard]] static QStringList LocalAddresses();

  // What discovery is actually doing, in the words of the machine rather than a
  // guess. When a phone and a computer cannot find each other there is nothing
  // on screen to distinguish a blocked port from a wrong network from an
  // interface that was filtered out, and every one of those has a different
  // answer. This is the difference between "it doesn't work" and knowing why.
  struct Diagnostics {
    bool bound{false};
    quint16 port{0};
    QString bindError;
    QStringList interfaces;   // "name  address  ->  broadcast"
    QString skipped;          // interfaces deliberately not used, and why
    quint64 datagramsSent{0};
    quint64 datagramsReceived{0};
    bool lastSendSucceeded{false};
  };
  [[nodiscard]] Diagnostics diagnostics() const;

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
    int index{0};
    QHostAddress address;
    QHostAddress broadcast;
  };

  bool bindSocket();
  void scheduleRebind();
  void sendAnnounce();
  void sendDirectedAnnounce(const QHostAddress& peer);
  bool broadcast(const QByteArray& bytes);
  void handlePacket(const QByteArray& data, const QHostAddress& sender);

  [[nodiscard]] static std::vector<LanInterface> LanInterfaces();
  void refreshMulticastMemberships();
  bool sendToInterface(const QByteArray& bytes, const LanInterface& lan);

  protocol::DeviceInfo localDevice_;
  QUdpSocket socket_;
  QStringList joinedGroups_;
  QTimer announceTimer_;
  QTimer sweepTimer_;
  QTimer rebindTimer_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  QString bindError_;
  quint64 datagramsSent_{0};
  quint64 datagramsReceived_{0};
  bool lastSendSucceeded_{false};
  bool running_{false};
  bool bound_{false};
};
}
