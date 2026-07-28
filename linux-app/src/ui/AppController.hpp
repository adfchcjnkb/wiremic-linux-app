#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "ConnectionManager.hpp"

namespace wiremic::ui {

class AppController : public QObject {
  Q_OBJECT

 public:
  explicit AppController(QObject* parent = nullptr);
  ~AppController() override;

  [[nodiscard]] QVariantList devices() const;
  [[nodiscard]] QVariantList trustedDevices() const;
  [[nodiscard]] QVariantList logMessages() const;
  [[nodiscard]] QString connectionState() const;
  [[nodiscard]] bool hasActiveConnection() const;
  [[nodiscard]] QVariantMap activeDevice() const;
  [[nodiscard]] QVariantMap pendingRequest() const;
  [[nodiscard]] bool hasPendingRequest() const;
  [[nodiscard]] QString localDeviceName() const;
  [[nodiscard]] quint16 controlPort() const;
  [[nodiscard]] bool autoConnect() const;
  void setAutoConnect(bool value);
  [[nodiscard]] bool rememberTrustedDevices() const;
  void setRememberTrustedDevices(bool value);
  [[nodiscard]] int latencyModeIndex() const;
  void setLatencyModeIndex(int index);
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] bool virtualMicActive() const;
  [[nodiscard]] QString audioBackendName() const;

 public slots:
  void connectToDevice(const QString& deviceId);
  void acceptPendingRequest();
  void rejectPendingRequest();
  void disconnectActive();
  void refreshDevices();
  void revokeTrust(const QString& deviceId);

 signals:
  void devicesChanged();
  void trustedDevicesChanged();
  void logMessagesChanged();
  void connectionStateChanged();
  void pendingRequestChanged();
  void settingsChanged();
  void lastErrorChanged();
  void incomingRequestPopupRequested();
  void audioStateChanged();

 private:
  void appendLog(const QString& message);
  void applySettings();
  static QVariantMap DeviceToVariant(const protocol::DeviceInfo& device,
                                      const QString& status = {});

  std::unique_ptr<core::ConnectionManager> manager_;
  std::optional<protocol::ConnectRequest> pendingRequest_;
  QString pendingRequestFingerprint_;
  QVariantList logMessages_;
  QString lastError_;
  bool autoConnect_{false};
  bool rememberTrustedDevices_{true};
  int latencyModeIndex_{0};
};

}
