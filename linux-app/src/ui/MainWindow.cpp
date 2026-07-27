#include "MainWindow.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QDebug>

#include "IconLoader.hpp"
#include "Theme.hpp"
#include "components/GlassPanel.hpp"
#include "components/IncomingRequestDialog.hpp"
#include "components/NavRailButton.hpp"
#include "pages/AboutPage.hpp"
#include "pages/ConnectedDevicePage.hpp"
#include "pages/DashboardPage.hpp"
#include "pages/DevicesPage.hpp"
#include "pages/LogsPage.hpp"
#include "pages/SettingsPage.hpp"

namespace wiremic::ui {

namespace {
const char* kIconPaths[] = {
    ":/WireMic/resources/icons/icon_dashboard.svg",
    ":/WireMic/resources/icons/icon_devices.svg",
    ":/WireMic/resources/icons/icon_connected.svg",
    ":/WireMic/resources/icons/icon_settings.svg",
    ":/WireMic/resources/icons/icon_logs.svg",
    ":/WireMic/resources/icons/icon_about.svg",
};
const char* kLabels[] = {"Dashboard",  "Available Devices", "Connected Device",
                          "Settings",  "Logs",              "About"};
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  qDebug() << "MainWindow constructor called";
  
  try {
    setWindowTitle("WireMic");
    resize(1120, 720);
    setMinimumSize(880, 580);

    auto* central = new QWidget(this);
    if (!central) {
      qCritical() << "Failed to create central widget";
      return;
    }
    setCentralWidget(central);

    auto* rootLayout = new QHBoxLayout(central);
    if (!rootLayout) {
      qCritical() << "Failed to create root layout";
      return;
    }
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(18);

    // Sidebar
    auto* sidebar = new GlassPanel(central);
    if (!sidebar) {
      qCritical() << "Failed to create sidebar";
      return;
    }
    sidebar->setFillColor(theme::kSidebar);
    sidebar->setCornerRadius(theme::kRadiusXLarge);
    sidebar->setFixedWidth(232);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    if (!sidebarLayout) {
      qCritical() << "Failed to create sidebar layout";
      return;
    }
    sidebarLayout->setContentsMargins(6, 24, 6, 20);
    sidebarLayout->setSpacing(4);

    // Brand row
    auto* brandRow = new QWidget(sidebar);
    if (!brandRow) {
      qCritical() << "Failed to create brand row";
      return;
    }
    auto* brandLayout = new QHBoxLayout(brandRow);
    if (!brandLayout) {
      qCritical() << "Failed to create brand layout";
      return;
    }
    brandLayout->setContentsMargins(16, 0, 16, 20);
    brandLayout->setSpacing(10);

    auto* brandBadge = new QLabel(brandRow);
    if (!brandBadge) {
      qCritical() << "Failed to create brand badge";
      return;
    }
    QPixmap badgePixmap(36, 36);
    badgePixmap.fill(Qt::transparent);
    {
      QPainter painter(&badgePixmap);
      painter.setRenderHint(QPainter::Antialiasing, true);
      QPainterPath path;
      path.addRoundedRect(badgePixmap.rect(), 12, 12);
      QLinearGradient gradient(0, 0, 36, 36);
      gradient.setColorAt(0.0, theme::kAccentStart);
      gradient.setColorAt(1.0, theme::kAccentEnd);
      painter.fillPath(path, gradient);
      painter.setPen(Qt::white);
      QFont f = painter.font();
      f.setBold(true);
      f.setPointSize(14);
      painter.setFont(f);
      painter.drawText(badgePixmap.rect(), Qt::AlignCenter, "W");
    }
    brandBadge->setPixmap(badgePixmap);
    auto* brandLabel = new QLabel("WireMic", brandRow);
    if (!brandLabel) {
      qCritical() << "Failed to create brand label";
      return;
    }
    brandLabel->setStyleSheet(
        "color: rgb(245,246,250); font-size: 19px; font-weight: 700;");
    brandLayout->addWidget(brandBadge);
    brandLayout->addWidget(brandLabel);
    brandLayout->addStretch();
    sidebarLayout->addWidget(brandRow);

    // Navigation buttons
    for (int i = 0; i < 6; ++i) {
      auto* button = new NavRailButton(kIconPaths[i], kLabels[i], sidebar);
      if (!button) {
        qCritical() << "Failed to create nav button" << i;
        continue;
      }
      connect(button, &NavRailButton::clicked, this,
              [this, i]() { selectPage(i); });
      sidebarLayout->addWidget(button);
      navButtons_.push_back(button);
    }

    sidebarLayout->addStretch();

    // Status panel
    auto* statusPanel = new GlassPanel(sidebar);
    if (!statusPanel) {
      qCritical() << "Failed to create status panel";
      return;
    }
    statusPanel->setFixedHeight(56);
    statusPanel->setCornerRadius(theme::kRadiusMedium);
    auto* statusLayout = new QHBoxLayout(statusPanel);
    if (!statusLayout) {
      qCritical() << "Failed to create status layout";
      return;
    }
    statusLayout->setContentsMargins(14, 0, 14, 0);
    auto* statusDot = new QLabel(statusPanel);
    if (!statusDot) {
      qCritical() << "Failed to create status dot";
      return;
    }
    statusDot->setFixedSize(9, 9);
    statusDot->setStyleSheet(
        QString("background-color: rgb(%1,%2,%3); border-radius: 4px;")
            .arg(theme::kTextTertiary.red())
            .arg(theme::kTextTertiary.green())
            .arg(theme::kTextTertiary.blue()));
    auto* statusText = new QLabel("Not Connected", statusPanel);
    if (!statusText) {
      qCritical() << "Failed to create status text";
      return;
    }
    statusText->setObjectName("statusText");
    statusText->setStyleSheet("color: rgb(245,246,250); font-size: 12px;");
    statusLayout->addWidget(statusDot);
    statusLayout->addSpacing(6);
    statusLayout->addWidget(statusText);
    statusLayout->addStretch();
    sidebarLayout->addWidget(statusPanel);
    statusPanel->setObjectName("statusPanel");

    rootLayout->addWidget(sidebar);

    // Content panel
    auto* contentPanel = new GlassPanel(central);
    if (!contentPanel) {
      qCritical() << "Failed to create content panel";
      return;
    }
    contentPanel->setFillColor(theme::kSidebar);
    contentPanel->setCornerRadius(theme::kRadiusXLarge);
    auto* contentLayout = new QVBoxLayout(contentPanel);
    if (!contentLayout) {
      qCritical() << "Failed to create content layout";
      return;
    }
    contentLayout->setContentsMargins(30, 30, 30, 30);

    stack_ = new QStackedWidget(contentPanel);
    if (!stack_) {
      qCritical() << "Failed to create stack widget";
      return;
    }
    stack_->setStyleSheet("background: transparent;");

    // Create pages
    dashboardPage_ = new DashboardPage(stack_);
    devicesPage_ = new DevicesPage(stack_);
    connectedPage_ = new ConnectedDevicePage(stack_);
    settingsPage_ = new SettingsPage(stack_);
    logsPage_ = new LogsPage(stack_);
    aboutPage_ = new AboutPage(stack_);

    if (!dashboardPage_ || !devicesPage_ || !connectedPage_ || 
        !settingsPage_ || !logsPage_ || !aboutPage_) {
      qCritical() << "Failed to create one or more pages";
      return;
    }

    stack_->addWidget(dashboardPage_);
    stack_->addWidget(devicesPage_);
    stack_->addWidget(connectedPage_);
    stack_->addWidget(settingsPage_);
    stack_->addWidget(logsPage_);
    stack_->addWidget(aboutPage_);

    contentLayout->addWidget(stack_);
    rootLayout->addWidget(contentPanel, 1);

    // Incoming dialog
    incomingDialog_ = new IncomingRequestDialog(this);
    if (!incomingDialog_) {
      qCritical() << "Failed to create incoming dialog";
      return;
    }

    // Connect signals
    connect(devicesPage_, &DevicesPage::connectRequested, &controller_,
            &AppController::connectToDevice);
    connect(devicesPage_, &DevicesPage::refreshRequested, &controller_,
            &AppController::refreshDevices);
    connect(connectedPage_, &ConnectedDevicePage::disconnectRequested,
            &controller_, &AppController::disconnectActive);
    connect(settingsPage_, &SettingsPage::autoConnectChanged, &controller_,
            &AppController::setAutoConnect);
    connect(settingsPage_, &SettingsPage::rememberTrustedChanged, &controller_,
            &AppController::setRememberTrustedDevices);
    connect(settingsPage_, &SettingsPage::revokeTrustRequested, &controller_,
            &AppController::revokeTrust);

    connect(incomingDialog_, &IncomingRequestDialog::accepted_, &controller_,
            &AppController::acceptPendingRequest);
    connect(incomingDialog_, &IncomingRequestDialog::rejected_, &controller_,
            &AppController::rejectPendingRequest);

    connect(&controller_, &AppController::devicesChanged, this,
            &MainWindow::refreshDevicesUi);
    connect(&controller_, &AppController::connectionStateChanged, this,
            &MainWindow::refreshConnectionUi);
    connect(&controller_, &AppController::trustedDevicesChanged, this,
            &MainWindow::refreshTrustedUi);
    connect(&controller_, &AppController::pendingRequestChanged, this, [this]() {
      if (controller_.hasPendingRequest()) {
        const auto request = controller_.pendingRequest();
        IncomingRequestData data;
        data.name = request["name"].toString();
        data.model = request["model"].toString();
        data.platform = request["platform"].toString();
        data.ip = request["ip"].toString();
        data.connectionType = request["connectionType"].toString();
        incomingDialog_->setData(data);
        incomingDialog_->move(this->geometry().center() -
                               incomingDialog_->rect().center());
        incomingDialog_->show();
      } else {
        incomingDialog_->hide();
      }
    });
    connect(&controller_, &AppController::logMessagesChanged, this, [this]() {
      const auto messages = controller_.logMessages();
      if (!messages.isEmpty()) {
        const auto last = messages.last().toMap();
        logsPage_->appendLog(last["timestamp"].toString(),
                              last["message"].toString());
      }
    });

    dashboardPage_->setLocalDeviceName(controller_.localDeviceName());
    dashboardPage_->setControlPort(controller_.controlPort());

    selectPage(0);
    refreshDevicesUi();
    refreshConnectionUi();
    refreshTrustedUi();
    
    qDebug() << "MainWindow constructor finished successfully";
    
  } catch (const std::exception& e) {
    qCritical() << "Exception in MainWindow constructor:" << e.what();
  } catch (...) {
    qCritical() << "Unknown exception in MainWindow constructor";
  }
}

void MainWindow::selectPage(int index) {
  if (!stack_) return;
  if (index < 0 || index >= stack_->count()) return;
  
  stack_->setCurrentIndex(index);
  for (size_t i = 0; i < navButtons_.size(); ++i) {
    if (navButtons_[i]) {
      navButtons_[i]->setSelected(static_cast<int>(i) == index);
    }
  }
}

void MainWindow::refreshDevicesUi() {
  std::vector<DeviceRowData> rows;
  for (const auto& variant : controller_.devices()) {
    const auto map = variant.toMap();
    DeviceRowData data;
    data.id = map["id"].toString();
    data.name = map["name"].toString();
    data.model = map["model"].toString();
    data.platform = map["platform"].toString();
    data.ip = map["ip"].toString();
    data.status = map["status"].toString();
    rows.push_back(data);
  }
  
  if (devicesPage_) {
    devicesPage_->setDevices(rows);
  }
  
  if (dashboardPage_) {
    dashboardPage_->setDeviceCount(static_cast<int>(rows.size()));
  }
}

void MainWindow::refreshConnectionUi() {
  const bool connected = controller_.hasActiveConnection();
  const auto active = controller_.activeDevice();

  if (dashboardPage_) {
    dashboardPage_->setConnected(connected, active["name"].toString());
  }
  
  if (connectedPage_) {
    connectedPage_->setConnected(
        connected, active["name"].toString(), active["model"].toString(),
        controller_.connectionState(), active["ip"].toString(),
        active["connectionType"].toString(), active["platform"].toString());
  }

  if (auto* statusPanel = findChild<GlassPanel*>("statusPanel")) {
    if (auto* statusText = statusPanel->findChild<QLabel*>("statusText")) {
      statusText->setText(connected ? "Connected" : "Not Connected");
    }
  }
}

void MainWindow::refreshTrustedUi() {
  QStringList ids;
  QStringList names;
  for (const auto& variant : controller_.trustedDevices()) {
    const auto map = variant.toMap();
    ids.push_back(map["id"].toString());
    names.push_back(map["name"].toString());
  }
  
  if (settingsPage_) {
    settingsPage_->setTrustedDevices(ids, names);
  }
}

void MainWindow::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0.0, theme::kBgTop);
  gradient.setColorAt(1.0, theme::kBgBottom);
  painter.fillRect(rect(), gradient);
}

}  // namespace wiremic::ui