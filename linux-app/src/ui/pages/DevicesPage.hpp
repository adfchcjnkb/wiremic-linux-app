#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

#include "../components/DeviceRow.hpp"

namespace wiremic::ui {

class DevicesPage : public QWidget {
  Q_OBJECT

 public:
  explicit DevicesPage(QWidget* parent = nullptr);

  void setDevices(const std::vector<DeviceRowData>& devices);
  void setBusyDeviceId(const QString& deviceId);
  void setStatusMessage(const QString& message);

 signals:
  void connectRequested(QString deviceId);
  void refreshRequested();

 private:
  void refreshLocalAddresses();

  QLabel* addressLabel_{nullptr};
  QVBoxLayout* listLayout_;
  QWidget* listContainer_;
  QLabel* emptyLabel_;
  QLabel* statusLabel_;
  QString busyDeviceId_;
  std::vector<DeviceRow*> rows_;
};

}
