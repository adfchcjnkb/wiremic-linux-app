#include "DiscoveryService.hpp"
#include <QNetworkDatagram>
#include <QTimer>
#include <QDebug>
namespace wiremic::network {
using namespace std::chrono_literals;
namespace {
constexpr auto kSweepIntervalMs = 1000;
constexpr int kMaxBindRetries = 5;
constexpr int kBindRetryDelayMs = 200;
}
DiscoveryService::DiscoveryService(protocol::DeviceInfo localDevice, QObject* parent)
    : QObject(parent), localDevice_(std::move(localDevice)) {}
DiscoveryService::~DiscoveryService() { stop(); }
bool DiscoveryService::start() {
    if (running_) return true;
    bound_ = false;
    retryCount_ = 0;
    while (retryCount_ < kMaxBindRetries && !bound_) {
        if (socket_.bind(QHostAddress::AnyIPv4, protocol::kDiscoveryBroadcastPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            bound_ = true;
            break;
        }
        retryCount_++;
        QTimer::singleShot(kBindRetryDelayMs, this, [](){});
    }
    if (!bound_) {
        emit errorOccurred(QStringLiteral("Failed to bind discovery socket after %1 attempts: %2").arg(kMaxBindRetries).arg(socket_.errorString()));
        return false;
    }
    if (socket_.state() != QUdpSocket::BoundState) {
        emit errorOccurred(QStringLiteral("Socket not in bound state after bind attempt"));
        socket_.close();
        return false;
    }
    running_ = true;
    connect(&socket_, &QUdpSocket::readyRead, this, &DiscoveryService::onReadyRead, Qt::QueuedConnection);
    connect(&announceTimer_, &QTimer::timeout, this, &DiscoveryService::onAnnounceTimer, Qt::QueuedConnection);
    connect(&sweepTimer_, &QTimer::timeout, this, &DiscoveryService::onSweepTimer, Qt::QueuedConnection);
    announceTimer_.start(protocol::kAnnounceIntervalMs);
    sweepTimer_.start(kSweepIntervalMs);
    QTimer::singleShot(250, this, &DiscoveryService::sendAnnounce);
    return true;
}
void DiscoveryService::stop() {
    if (!running_) return;
    running_ = false;
    bound_ = false;
    disconnect(&socket_, nullptr, this, nullptr);
    disconnect(&announceTimer_, nullptr, this, nullptr);
    disconnect(&sweepTimer_, nullptr, this, nullptr);
    announceTimer_.stop();
    sweepTimer_.stop();
    if (socket_.state() == QUdpSocket::BoundState) socket_.close();
    devices_.clear();
}
void DiscoveryService::refreshNow() {
    if (!running_ || !bound_) return;
    if (socket_.state() == QUdpSocket::BoundState) sendAnnounce();
}
std::vector<DiscoveredDevice> DiscoveryService::devices() const {
    std::vector<DiscoveredDevice> result;
    result.reserve(devices_.size());
    for (const auto& [id, device] : devices_) result.push_back(device);
    return result;
}
void DiscoveryService::sendAnnounce() {
    if (!running_ || !bound_) return;
    if (socket_.state() != QUdpSocket::BoundState) {
        bound_ = false;
        emit errorOccurred(QStringLiteral("Socket lost binding, attempting to recover"));
        start();
        return;
    }
    protocol::AnnouncePacket packet;
    packet.device = localDevice_;
    packet.protoVersion = protocol::kProtocolVersion;
    const auto json = protocol::ToJson(packet);
    const auto bytes = QByteArray::fromStdString(json);
    socket_.writeDatagram(bytes, QHostAddress::Broadcast, protocol::kDiscoveryBroadcastPort);
}
void DiscoveryService::onReadyRead() {
    if (!running_ || !bound_) return;
    if (socket_.state() != QUdpSocket::BoundState) {
        bound_ = false;
        return;
    }
    if (!socket_.hasPendingDatagrams()) return;
    while (socket_.hasPendingDatagrams()) {
        QNetworkDatagram datagram = socket_.receiveDatagram();
        if (datagram.isValid()) handlePacket(datagram.data(), datagram.senderAddress());
    }
}
void DiscoveryService::handlePacket(const QByteArray& data, const QHostAddress& sender) {
    if (!running_) return;
    auto parsed = protocol::ParseAnnounce(data.toStdString());
    if (!parsed) return;
    if (parsed->device.id == localDevice_.id) return;
    parsed->device.ip = sender.toString().toStdString();
    const auto now = std::chrono::steady_clock::now();
    auto it = devices_.find(parsed->device.id);
    if (it == devices_.end()) {
        DiscoveredDevice discovered;
        discovered.info = parsed->device;
        discovered.status = DeviceStatus::Online;
        discovered.lastSeen = now;
        discovered.missedAnnounces = 0;
        devices_.emplace(parsed->device.id, discovered);
        emit deviceDiscovered(devices_.at(parsed->device.id));
        return;
    }
    it->second.info = parsed->device;
    it->second.lastSeen = now;
    it->second.missedAnnounces = 0;
    const bool wasOffline = it->second.status == DeviceStatus::Offline;
    it->second.status = DeviceStatus::Online;
    emit deviceUpdated(it->second);
    if (wasOffline) emit deviceStatusChanged(QString::fromStdString(parsed->device.id), DeviceStatus::Online);
}
void DiscoveryService::onAnnounceTimer() {
    if (running_ && bound_ && socket_.state() == QUdpSocket::BoundState) sendAnnounce();
}
void DiscoveryService::onSweepTimer() {
    if (!running_) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> toRemove;
    for (auto& [id, device] : devices_) {
        const auto silenceMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - device.lastSeen).count();
        if (silenceMs >= protocol::kRemoveAfterSilenceMs) {
            toRemove.push_back(id);
            continue;
        }
        const auto expectedMissed = silenceMs / protocol::kAnnounceIntervalMs;
        if (expectedMissed >= protocol::kOfflineAfterMissedAnnounces && device.status == DeviceStatus::Online) {
            device.status = DeviceStatus::Offline;
            emit deviceStatusChanged(QString::fromStdString(id), DeviceStatus::Offline);
        }
    }
    for (const auto& id : toRemove) {
        devices_.erase(id);
        emit deviceRemoved(QString::fromStdString(id));
    }
}
}  // namespace wiremic::network
