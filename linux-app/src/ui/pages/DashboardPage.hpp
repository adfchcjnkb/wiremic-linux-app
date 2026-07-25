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

 private:
  StatCard* statusCard_;
  StatCard* devicesCard_;
  StatCard* portCard_;
  MicBadge* micBadge_;
  QLabel* localNameLabel_;
  QLabel* descriptionLabel_;
};

}  // namespace wiremic::ui
