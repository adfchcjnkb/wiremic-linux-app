#include "AppController.hpp"

#include <QDateTime>
#include <QHostInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QDebug>

#include <fstream>

namespace wiremic::ui {

namespace {

QString DeviceStatusToString(network::DeviceStatus status) {
  return status == network::DeviceStatus::Online
             ? QStringLiteral("Online")
             : QStringLiteral("Offline");
}

QString ConnectionStateToString(protocol::ConnectionState state) {
  switch (state) {
    case protocol::ConnectionState::Idle:
      return QStringLiteral("Idle");
    case protocol::ConnectionState::Discovering:
      return QStringLiteral("Discovering");
    case protocol::ConnectionState::RequestSent:
      return QStringLiteral("RequestSent");
    case protocol::ConnectionState::AwaitingApproval:
      return QStringLiteral("AwaitingApproval");
    case protocol::ConnectionState::Accepted:
      return QStringLiteral("Accepted");
    case protocol::ConnectionState::Streaming:
      return QStringLiteral("Connected");
    case protocol::ConnectionState::Disconnected:
      return QStringLiteral("Disconnected");
    case protocol::ConnectionState::Reconnecting:
      return QStringLiteral("Reconnecting");
  }
  return QStringLiteral("Idle");
}

std::string GenerateOrLoadDeviceId(const std::filesystem::path& dataDir) {
  const auto idPath = dataDir / "device_id";
  std::error_code ec;
  std::filesystem::create_directories(dataDir, ec);

  if (std::filesystem::exists(idPath)) {
    std::ifstream in(idPath);
    std::string id;
    std::getline(in, id);
    if (!id.empty()) return id;
  }

  const auto newId =
      QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  std::ofstream out(idPath, std::ios::trunc);
  out << newId;
  return newId;
}

}

QVariantMap AppController::DeviceToVariant(const protocol::DeviceInfo& device,
                                            const QString& status) {
  QVariantMap map;
  map["id"] = QString::fromStdString(device.id);
  map["name"] = QString::fromStdString(device.name);
  map["model"] = QString::fromStdString(device.model);
  map["platform"] =
      QString::fromStdString(protocol::ToString(device.platform));
  map["ip"] = QString::fromStdString(device.ip);
  map["connectionType"] =
      QString::fromStdString(protocol::ToString(device.connectionType));
  map["controlPort"] = device.controlPort;
  if (!status.isEmpty()) map["status"] = status;
  return map;
}

AppController::AppController(QObject* parent) : QObject(parent) {
  qDebug() << "AppController constructor called";

  try {
    const auto dataDir = std::filesystem::path(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            .toStdString());

    qDebug() << "Data directory:" << QString::fromStdString(dataDir.string());

    protocol::DeviceInfo localDevice;
    localDevice.id = GenerateOrLoadDeviceId(dataDir);
    localDevice.name = QHostInfo::localHostName().toStdString();
    if (localDevice.name.empty()) localDevice.name = "Linux Desktop";
    localDevice.model = QSysInfo::prettyProductName().toStdString();
    localDevice.platform = protocol::Platform::Linux;
    localDevice.connectionType = protocol::ConnectionType::Wifi;

    qDebug() << "Local device:" << QString::fromStdString(localDevice.name);

    QSettings stored;
    autoConnect_ = stored.value(QStringLiteral("autoConnect"), false).toBool();
    rememberTrustedDevices_ =
        stored.value(QStringLiteral("rememberTrustedDevices"), true).toBool();

    core::ConnectionManagerSettings settings;
    settings.autoConnect = autoConnect_;
    settings.rememberTrustedDevices = rememberTrustedDevices_;

    manager_ = std::make_unique<core::ConnectionManager>(
        localDevice, dataDir, settings, protocol::kDefaultControlPort);

    connect(manager_.get(), &core::ConnectionManager::deviceListChanged, this,
            &AppController::devicesChanged);

    connect(
        manager_.get(), &core::ConnectionManager::incomingRequestPending, this,
        [this](protocol::ConnectRequest request, QString fingerprint) {
          pendingRequest_ = request;
          pendingRequestFingerprint_ = fingerprint;
          appendLog(QStringLiteral("Incoming request from %1 (%2)")
                        .arg(QString::fromStdString(request.device.name),
                             QString::fromStdString(request.device.model)));
          emit pendingRequestChanged();
          emit incomingRequestPopupRequested();
        });

    connect(manager_.get(),
            &core::ConnectionManager::incomingRequestCancelled, this,
            [this](QString requestId) {
              if (!pendingRequest_ ||
                  pendingRequest_->requestId != requestId.toStdString()) {
                return;
              }
              appendLog(QStringLiteral(
                  "Incoming request withdrawn before it was answered"));
              pendingRequest_.reset();
              emit pendingRequestChanged();
            });

    connect(manager_.get(), &core::ConnectionManager::connectionStateChanged,
            this, &AppController::connectionStateChanged);

    connect(manager_.get(), &core::ConnectionManager::connectionEstablished,
            this, [this](protocol::DeviceInfo device) {
              appendLog(QStringLiteral("Connected to %1")
                            .arg(QString::fromStdString(device.name)));
              pendingRequest_.reset();
              emit pendingRequestChanged();
              emit trustedDevicesChanged();
            });

    connect(manager_.get(), &core::ConnectionManager::connectionClosed, this,
            [this](protocol::DisconnectReason) {
              appendLog(QStringLiteral("Connection closed"));
            });

    connect(manager_.get(), &core::ConnectionManager::connectionFailed, this,
            [this](QString reason) {
              lastError_ = reason;
              appendLog(QStringLiteral("Connection failed: %1").arg(reason));
              if (pendingRequest_) {
                pendingRequest_.reset();
                emit pendingRequestChanged();
              }
              emit lastErrorChanged();
            });

    connect(manager_.get(), &core::ConnectionManager::errorOccurred, this,
            [this](QString message) {
              appendLog(QStringLiteral("Error: %1").arg(message));
            });

    connect(manager_.get(), &core::ConnectionManager::audioStateChanged, this,
            [this](bool micActive, QString backend) {
              appendLog(micActive
                             ? QStringLiteral(
                                   "Virtual microphone active via %1")
                                   .arg(backend)
                             : QStringLiteral("Virtual microphone stopped"));
              emit audioStateChanged();
            });

    if (!manager_->start()) {
      appendLog(
          QStringLiteral("Failed to start networking (port may be in use)"));
    } else {
      appendLog(QStringLiteral("WireMic started, listening on port %1")
                    .arg(manager_->controlPort()));
    }

    qDebug() << "AppController constructor finished successfully";

  } catch (const std::exception& e) {
    qCritical() << "Exception in AppController constructor:" << e.what();
    appendLog(QStringLiteral("Error initializing: %1").arg(e.what()));
  } catch (...) {
    qCritical() << "Unknown exception in AppController constructor";
    appendLog(QStringLiteral("Unknown error initializing"));
  }
}

AppController::~AppController() {
  qDebug() << "AppController destructor called";
  if (manager_) {
    manager_->stop();
  }
}

QVariantList AppController::devices() const {
  QVariantList list;
  if (manager_) {
    for (const auto& device : manager_->discoveredDevices()) {
      list.append(
          DeviceToVariant(device.info, DeviceStatusToString(device.status)));
    }
  }
  return list;
}

QVariantList AppController::trustedDevices() const {
  QVariantList list;
  if (manager_) {
    for (const auto& device : manager_->trustedDevices()) {
      QVariantMap map;
      map["id"] = QString::fromStdString(device.deviceId);
      map["name"] = QString::fromStdString(device.name);
      map["fingerprint"] = QString::fromStdString(device.certFingerprint);
      list.append(map);
    }
  }
  return list;
}

QVariantList AppController::logMessages() const { return logMessages_; }

QString AppController::connectionState() const {
  if (!manager_) return QStringLiteral("Idle");
  return ConnectionStateToString(manager_->connectionState());
}

bool AppController::hasActiveConnection() const {
  if (!manager_) return false;
  return manager_->connectionState() == protocol::ConnectionState::Streaming;
}

QVariantMap AppController::activeDevice() const {
  if (!manager_) return {};
  const auto device = manager_->peerDevice();
  if (device.id.empty()) return {};
  return DeviceToVariant(device);
}

QVariantMap AppController::pendingRequest() const {
  if (!pendingRequest_) return {};
  auto map = DeviceToVariant(pendingRequest_->device);
  map["fingerprint"] = pendingRequestFingerprint_;
  return map;
}

bool AppController::hasPendingRequest() const {
  return pendingRequest_.has_value();
}

QString AppController::localDeviceName() const {
  return QHostInfo::localHostName();
}

quint16 AppController::controlPort() const {
  if (!manager_) return 0;
  return manager_->controlPort();
}

bool AppController::autoConnect() const { return autoConnect_; }

void AppController::setAutoConnect(bool value) {
  if (autoConnect_ == value) return;
  autoConnect_ = value;
  applySettings();
  emit settingsChanged();
}

bool AppController::rememberTrustedDevices() const {
  return rememberTrustedDevices_;
}

void AppController::setRememberTrustedDevices(bool value) {
  if (rememberTrustedDevices_ == value) return;
  rememberTrustedDevices_ = value;
  applySettings();
  emit settingsChanged();
}

void AppController::applySettings() {
  if (manager_) {
    auto settings = manager_->settings();
    settings.autoConnect = autoConnect_;
    settings.rememberTrustedDevices = rememberTrustedDevices_;
    manager_->updateSettings(settings);
  }

  QSettings stored;
  stored.setValue(QStringLiteral("autoConnect"), autoConnect_);
  stored.setValue(QStringLiteral("rememberTrustedDevices"),
                   rememberTrustedDevices_);
}

bool AppController::virtualMicActive() const {
  return manager_ && manager_->virtualMicActive();
}

QString AppController::audioBackendName() const {
  if (!manager_) return QStringLiteral("none");
  return manager_->audioBackendName();
}

QString AppController::lastError() const { return lastError_; }

void AppController::connectToDevice(const QString& deviceId) {
  if (!manager_) return;
  manager_->requestConnection(deviceId.toStdString());
}

void AppController::acceptPendingRequest() {
  if (!pendingRequest_ || !manager_) return;
  manager_->approveIncoming(pendingRequest_->requestId);
}

void AppController::rejectPendingRequest() {
  if (!pendingRequest_ || !manager_) return;
  manager_->rejectIncoming(pendingRequest_->requestId,
                            protocol::RejectReason::RejectedByUser);
  pendingRequest_.reset();
  emit pendingRequestChanged();
}

void AppController::disconnectActive() {
  if (!manager_) return;
  manager_->disconnectActive();
}

void AppController::refreshDevices() {
  if (!manager_) return;
  manager_->refreshDiscovery();
}

void AppController::revokeTrust(const QString& deviceId) {
  if (!manager_) return;
  manager_->revokeTrust(deviceId.toStdString());
  emit trustedDevicesChanged();
}

void AppController::appendLog(const QString& message) {
  QVariantMap entry;
  entry["timestamp"] = QDateTime::currentDateTime().toString("HH:mm:ss");
  entry["message"] = message;
  logMessages_.append(entry);
  if (logMessages_.size() > 500) logMessages_.removeFirst();
  emit logMessagesChanged();
}

}
