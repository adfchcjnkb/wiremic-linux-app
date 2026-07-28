#pragma once

#include <QWidget>

#include "GlassButton.hpp"

namespace wiremic::ui {

struct DeviceRowData {
  QString id;
  QString name;
  QString model;
  QString platform;
  QString ip;
  QString status;
};

class DeviceRow : public QWidget {
  Q_OBJECT

 public:
  explicit DeviceRow(QWidget* parent = nullptr);

  void setData(const DeviceRowData& data);
  void setBusy(bool busy);

 signals:
  void connectRequested(QString deviceId);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  DeviceRowData data_;
  GlassButton* connectButton_;
};

}
