#pragma once

#include <QMainWindow>
#include <QStackedWidget>

#include <vector>

#include "AppController.hpp"

namespace wiremic::ui {

class NavRailButton;
class DashboardPage;
class DevicesPage;
class ConnectedDevicePage;
class SettingsPage;
class LogsPage;
class AboutPage;
class IncomingRequestDialog;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void selectPage(int index);
  void refreshDevicesUi();
  void refreshConnectionUi();
  void refreshTrustedUi();

  wiremic::ui::AppController controller_;

  QStackedWidget* stack_;
  std::vector<NavRailButton*> navButtons_;

  DashboardPage* dashboardPage_;
  DevicesPage* devicesPage_;
  ConnectedDevicePage* connectedPage_;
  SettingsPage* settingsPage_;
  LogsPage* logsPage_;
  AboutPage* aboutPage_;

  IncomingRequestDialog* incomingDialog_;
};

}  // namespace wiremic::ui
