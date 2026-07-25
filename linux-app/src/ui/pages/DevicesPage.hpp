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

 signals:
  void connectRequested(QString deviceId);
  void refreshRequested();

 private:
  QVBoxLayout* listLayout_;
  QWidget* listContainer_;
  QLabel* emptyLabel_;
  QString busyDeviceId_;
  std::vector<DeviceRow*> rows_;
};

}  // namespace wiremic::ui
