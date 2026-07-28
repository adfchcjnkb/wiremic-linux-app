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
  ~MainWindow() override;
 protected:
  void paintEvent(QPaintEvent* event) override;
  void showEvent(QShowEvent* event) override;
 private:
  void selectPage(int index);
  void refreshDevicesUi();
  void refreshConnectionUi();
  void refreshTrustedUi();
  wiremic::ui::AppController controller_;
  QStackedWidget* stack_{nullptr};
  std::vector<NavRailButton*> navButtons_;
  DashboardPage* dashboardPage_{nullptr};
  DevicesPage* devicesPage_{nullptr};
  ConnectedDevicePage* connectedPage_{nullptr};
  SettingsPage* settingsPage_{nullptr};
  LogsPage* logsPage_{nullptr};
  AboutPage* aboutPage_{nullptr};
  IncomingRequestDialog* incomingDialog_{nullptr};
};
}
