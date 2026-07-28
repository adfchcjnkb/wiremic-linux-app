#pragma once

#include <QWidget>
#include <QLabel>

namespace wiremic::ui {

class StatCard;
class MicBadge;

class DashboardPage : public QWidget {
  Q_OBJECT

 public:
  explicit DashboardPage(QWidget* parent = nullptr);

  void setConnected(bool connected, const QString& peerName);
  void setDeviceCount(int count);
  void setControlPort(quint16 port);
  void setLocalDeviceName(const QString& name);
  void setVirtualMic(bool active, const QString& backendName);

 private:
  void updateDescription();

  StatCard* statusCard_;
  StatCard* devicesCard_;
  StatCard* portCard_;
  StatCard* micCard_;
  MicBadge* micBadge_;
  QLabel* localNameLabel_;
  QLabel* descriptionLabel_;

  bool connected_{false};
  bool micActive_{false};
  QString peerName_;
  QString backendName_{QStringLiteral("none")};
};

}
