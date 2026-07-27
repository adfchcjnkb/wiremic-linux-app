#!/bin/bash
cat > linux-app/src/network/DiscoveryService.cpp << 'EOC'
#include "DiscoveryService.hpp"
#include <QNetworkDatagram>
#include <QTimer>
#include <QDebug>
namespace wiremic::network {
using namespace std::chrono_literals;
namespace {
constexpr auto kSweepIntervalMs = 1000;
constexpr int kMaxBindRetries = 5;
constexpr int kBindRetryDelayMs = 200;
}
DiscoveryService::DiscoveryService(protocol::DeviceInfo localDevice, QObject* parent)
    : QObject(parent), localDevice_(std::move(localDevice)) {}
DiscoveryService::~DiscoveryService() { stop(); }
bool DiscoveryService::start() {
    if (running_) return true;
    bound_ = false;
    retryCount_ = 0;
    while (retryCount_ < kMaxBindRetries && !bound_) {
        if (socket_.bind(QHostAddress::AnyIPv4, protocol::kDiscoveryBroadcastPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            bound_ = true;
            break;
        }
        retryCount_++;
        QTimer::singleShot(kBindRetryDelayMs, this, [](){});
    }
    if (!bound_) {
        emit errorOccurred(QStringLiteral("Failed to bind discovery socket after %1 attempts: %2").arg(kMaxBindRetries).arg(socket_.errorString()));
        return false;
    }
    if (socket_.state() != QUdpSocket::BoundState) {
        emit errorOccurred(QStringLiteral("Socket not in bound state after bind attempt"));
        socket_.close();
        return false;
    }
    running_ = true;
    connect(&socket_, &QUdpSocket::readyRead, this, &DiscoveryService::onReadyRead, Qt::QueuedConnection);
    connect(&announceTimer_, &QTimer::timeout, this, &DiscoveryService::onAnnounceTimer, Qt::QueuedConnection);
    connect(&sweepTimer_, &QTimer::timeout, this, &DiscoveryService::onSweepTimer, Qt::QueuedConnection);
    announceTimer_.start(protocol::kAnnounceIntervalMs);
    sweepTimer_.start(kSweepIntervalMs);
    QTimer::singleShot(250, this, &DiscoveryService::sendAnnounce);
    return true;
}
void DiscoveryService::stop() {
    if (!running_) return;
    running_ = false;
    bound_ = false;
    disconnect(&socket_, nullptr, this, nullptr);
    disconnect(&announceTimer_, nullptr, this, nullptr);
    disconnect(&sweepTimer_, nullptr, this, nullptr);
    announceTimer_.stop();
    sweepTimer_.stop();
    if (socket_.state() == QUdpSocket::BoundState) socket_.close();
    devices_.clear();
}
void DiscoveryService::refreshNow() {
    if (!running_ || !bound_) return;
    if (socket_.state() == QUdpSocket::BoundState) sendAnnounce();
}
std::vector<DiscoveredDevice> DiscoveryService::devices() const {
    std::vector<DiscoveredDevice> result;
    result.reserve(devices_.size());
    for (const auto& [id, device] : devices_) result.push_back(device);
    return result;
}
void DiscoveryService::sendAnnounce() {
    if (!running_ || !bound_) return;
    if (socket_.state() != QUdpSocket::BoundState) {
        bound_ = false;
        emit errorOccurred(QStringLiteral("Socket lost binding, attempting to recover"));
        start();
        return;
    }
    protocol::AnnouncePacket packet;
    packet.device = localDevice_;
    packet.protoVersion = protocol::kProtocolVersion;
    const auto json = protocol::ToJson(packet);
    const auto bytes = QByteArray::fromStdString(json);
    socket_.writeDatagram(bytes, QHostAddress::Broadcast, protocol::kDiscoveryBroadcastPort);
}
void DiscoveryService::onReadyRead() {
    if (!running_ || !bound_) return;
    if (socket_.state() != QUdpSocket::BoundState) {
        bound_ = false;
        return;
    }
    if (!socket_.hasPendingDatagrams()) return;
    while (socket_.hasPendingDatagrams()) {
        QNetworkDatagram datagram = socket_.receiveDatagram();
        if (datagram.isValid()) handlePacket(datagram.data(), datagram.senderAddress());
    }
}
void DiscoveryService::handlePacket(const QByteArray& data, const QHostAddress& sender) {
    if (!running_) return;
    auto parsed = protocol::ParseAnnounce(data.toStdString());
    if (!parsed) return;
    if (parsed->device.id == localDevice_.id) return;
    parsed->device.ip = sender.toString().toStdString();
    const auto now = std::chrono::steady_clock::now();
    auto it = devices_.find(parsed->device.id);
    if (it == devices_.end()) {
        DiscoveredDevice discovered;
        discovered.info = parsed->device;
        discovered.status = DeviceStatus::Online;
        discovered.lastSeen = now;
        discovered.missedAnnounces = 0;
        devices_.emplace(parsed->device.id, discovered);
        emit deviceDiscovered(devices_.at(parsed->device.id));
        return;
    }
    it->second.info = parsed->device;
    it->second.lastSeen = now;
    it->second.missedAnnounces = 0;
    const bool wasOffline = it->second.status == DeviceStatus::Offline;
    it->second.status = DeviceStatus::Online;
    emit deviceUpdated(it->second);
    if (wasOffline) emit deviceStatusChanged(QString::fromStdString(parsed->device.id), DeviceStatus::Online);
}
void DiscoveryService::onAnnounceTimer() {
    if (running_ && bound_ && socket_.state() == QUdpSocket::BoundState) sendAnnounce();
}
void DiscoveryService::onSweepTimer() {
    if (!running_) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> toRemove;
    for (auto& [id, device] : devices_) {
        const auto silenceMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - device.lastSeen).count();
        if (silenceMs >= protocol::kRemoveAfterSilenceMs) {
            toRemove.push_back(id);
            continue;
        }
        const auto expectedMissed = silenceMs / protocol::kAnnounceIntervalMs;
        if (expectedMissed >= protocol::kOfflineAfterMissedAnnounces && device.status == DeviceStatus::Online) {
            device.status = DeviceStatus::Offline;
            emit deviceStatusChanged(QString::fromStdString(id), DeviceStatus::Offline);
        }
    }
    for (const auto& id : toRemove) {
        devices_.erase(id);
        emit deviceRemoved(QString::fromStdString(id));
    }
}
}  // namespace wiremic::network
EOC
cat > linux-app/src/network/DiscoveryService.hpp << 'EOC'
#pragma once
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <chrono>
#include <optional>
#include <unordered_map>
#include "Protocol.hpp"
namespace wiremic::network {
enum class DeviceStatus { Online, Offline };
struct DiscoveredDevice {
  protocol::DeviceInfo info;
  DeviceStatus status{DeviceStatus::Online};
  std::chrono::steady_clock::time_point lastSeen;
  int missedAnnounces{0};
};
class DiscoveryService : public QObject {
  Q_OBJECT
 public:
  explicit DiscoveryService(protocol::DeviceInfo localDevice, QObject* parent = nullptr);
  ~DiscoveryService() override;
  bool start();
  void stop();
  void refreshNow();
  [[nodiscard]] std::vector<DiscoveredDevice> devices() const;
 signals:
  void deviceDiscovered(const DiscoveredDevice& device);
  void deviceUpdated(const DiscoveredDevice& device);
  void deviceStatusChanged(const QString& deviceId, DeviceStatus status);
  void deviceRemoved(const QString& deviceId);
  void errorOccurred(const QString& message);
 private slots:
  void onReadyRead();
  void onAnnounceTimer();
  void onSweepTimer();
 private:
  void sendAnnounce();
  void handlePacket(const QByteArray& data, const QHostAddress& sender);
  protocol::DeviceInfo localDevice_;
  QUdpSocket socket_;
  QTimer announceTimer_;
  QTimer sweepTimer_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  bool running_{false};
  bool bound_{false};
  int retryCount_{0};
};
}  // namespace wiremic::network
EOC
cat > linux-app/src/ui/MainWindow.cpp << 'EOC'
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
const char* kIconPaths[] = {":/WireMic/resources/icons/icon_dashboard.svg", ":/WireMic/resources/icons/icon_devices.svg", ":/WireMic/resources/icons/icon_connected.svg", ":/WireMic/resources/icons/icon_settings.svg", ":/WireMic/resources/icons/icon_logs.svg", ":/WireMic/resources/icons/icon_about.svg"};
const char* kLabels[] = {"Dashboard", "Available Devices", "Connected Device", "Settings", "Logs", "About"};
}
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  qDebug() << "MainWindow constructor called";
  setWindowTitle("WireMic");
  resize(1120, 720);
  setMinimumSize(880, 580);
  auto* central = new QWidget(this);
  setCentralWidget(central);
  auto* rootLayout = new QHBoxLayout(central);
  rootLayout->setContentsMargins(18, 18, 18, 18);
  rootLayout->setSpacing(18);
  auto* sidebar = new GlassPanel(central);
  sidebar->setFillColor(theme::kSidebar);
  sidebar->setCornerRadius(theme::kRadiusXLarge);
  sidebar->setFixedWidth(232);
  auto* sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->setContentsMargins(6, 24, 6, 20);
  sidebarLayout->setSpacing(4);
  auto* brandRow = new QWidget(sidebar);
  auto* brandLayout = new QHBoxLayout(brandRow);
  brandLayout->setContentsMargins(16, 0, 16, 20);
  brandLayout->setSpacing(10);
  auto* brandBadge = new QLabel(brandRow);
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
  brandLabel->setStyleSheet("color: rgb(245,246,250); font-size: 19px; font-weight: 700;");
  brandLayout->addWidget(brandBadge);
  brandLayout->addWidget(brandLabel);
  brandLayout->addStretch();
  sidebarLayout->addWidget(brandRow);
  for (int i = 0; i < 6; ++i) {
    auto* button = new NavRailButton(kIconPaths[i], kLabels[i], sidebar);
    connect(button, &NavRailButton::clicked, this, [this, i]() { selectPage(i); });
    sidebarLayout->addWidget(button);
    navButtons_.push_back(button);
  }
  sidebarLayout->addStretch();
  auto* statusPanel = new GlassPanel(sidebar);
  statusPanel->setFixedHeight(56);
  statusPanel->setCornerRadius(theme::kRadiusMedium);
  auto* statusLayout = new QHBoxLayout(statusPanel);
  statusLayout->setContentsMargins(14, 0, 14, 0);
  auto* statusDot = new QLabel(statusPanel);
  statusDot->setFixedSize(9, 9);
  statusDot->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border-radius: 4px;").arg(theme::kTextTertiary.red()).arg(theme::kTextTertiary.green()).arg(theme::kTextTertiary.blue()));
  auto* statusText = new QLabel("Not Connected", statusPanel);
  statusText->setObjectName("statusText");
  statusText->setStyleSheet("color: rgb(245,246,250); font-size: 12px;");
  statusLayout->addWidget(statusDot);
  statusLayout->addSpacing(6);
  statusLayout->addWidget(statusText);
  statusLayout->addStretch();
  sidebarLayout->addWidget(statusPanel);
  statusPanel->setObjectName("statusPanel");
  rootLayout->addWidget(sidebar);
  auto* contentPanel = new GlassPanel(central);
  contentPanel->setFillColor(theme::kSidebar);
  contentPanel->setCornerRadius(theme::kRadiusXLarge);
  auto* contentLayout = new QVBoxLayout(contentPanel);
  contentLayout->setContentsMargins(30, 30, 30, 30);
  stack_ = new QStackedWidget(contentPanel);
  stack_->setStyleSheet("background: transparent;");
  dashboardPage_ = new DashboardPage(stack_);
  devicesPage_ = new DevicesPage(stack_);
  connectedPage_ = new ConnectedDevicePage(stack_);
  settingsPage_ = new SettingsPage(stack_);
  logsPage_ = new LogsPage(stack_);
  aboutPage_ = new AboutPage(stack_);
  stack_->addWidget(dashboardPage_);
  stack_->addWidget(devicesPage_);
  stack_->addWidget(connectedPage_);
  stack_->addWidget(settingsPage_);
  stack_->addWidget(logsPage_);
  stack_->addWidget(aboutPage_);
  contentLayout->addWidget(stack_);
  rootLayout->addWidget(contentPanel, 1);
  incomingDialog_ = new IncomingRequestDialog(this);
  connect(devicesPage_, &DevicesPage::connectRequested, &controller_, &AppController::connectToDevice);
  connect(devicesPage_, &DevicesPage::refreshRequested, &controller_, &AppController::refreshDevices);
  connect(connectedPage_, &ConnectedDevicePage::disconnectRequested, &controller_, &AppController::disconnectActive);
  connect(settingsPage_, &SettingsPage::autoConnectChanged, &controller_, &AppController::setAutoConnect);
  connect(settingsPage_, &SettingsPage::rememberTrustedChanged, &controller_, &AppController::setRememberTrustedDevices);
  connect(settingsPage_, &SettingsPage::revokeTrustRequested, &controller_, &AppController::revokeTrust);
  connect(incomingDialog_, &IncomingRequestDialog::accepted_, &controller_, &AppController::acceptPendingRequest);
  connect(incomingDialog_, &IncomingRequestDialog::rejected_, &controller_, &AppController::rejectPendingRequest);
  connect(&controller_, &AppController::devicesChanged, this, &MainWindow::refreshDevicesUi);
  connect(&controller_, &AppController::connectionStateChanged, this, &MainWindow::refreshConnectionUi);
  connect(&controller_, &AppController::trustedDevicesChanged, this, &MainWindow::refreshTrustedUi);
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
      incomingDialog_->move(this->geometry().center() - incomingDialog_->rect().center());
      incomingDialog_->show();
    } else {
      incomingDialog_->hide();
    }
  });
  connect(&controller_, &AppController::logMessagesChanged, this, [this]() {
    const auto messages = controller_.logMessages();
    if (!messages.isEmpty()) {
      const auto last = messages.last().toMap();
      logsPage_->appendLog(last["timestamp"].toString(), last["message"].toString());
    }
  });
  dashboardPage_->setLocalDeviceName(controller_.localDeviceName());
  dashboardPage_->setControlPort(controller_.controlPort());
  selectPage(0);
  qDebug() << "MainWindow constructor finished successfully";
}
MainWindow::~MainWindow() {
  qDebug() << "MainWindow destructor called";
  disconnect(&controller_, nullptr, this, nullptr);
}
void MainWindow::selectPage(int index) {
  if (!stack_) return;
  if (index < 0 || index >= stack_->count()) return;
  stack_->setCurrentIndex(index);
  for (size_t i = 0; i < navButtons_.size(); ++i) {
    if (navButtons_[i]) navButtons_[i]->setSelected(static_cast<int>(i) == index);
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
  if (devicesPage_) devicesPage_->setDevices(rows);
  if (dashboardPage_) dashboardPage_->setDeviceCount(static_cast<int>(rows.size()));
}
void MainWindow::refreshConnectionUi() {
  const bool connected = controller_.hasActiveConnection();
  const auto active = controller_.activeDevice();
  if (dashboardPage_) dashboardPage_->setConnected(connected, active["name"].toString());
  if (connectedPage_) connectedPage_->setConnected(connected, active["name"].toString(), active["model"].toString(), controller_.connectionState(), active["ip"].toString(), active["connectionType"].toString(), active["platform"].toString());
  if (auto* statusPanel = findChild<GlassPanel*>("statusPanel")) {
    if (auto* statusText = statusPanel->findChild<QLabel*>("statusText")) {
      statusText->setText(connected ? "Connected" : "Not Connected");
    }
  }
}
void MainWindow::refreshTrustedUi() {
  QStringList ids, names;
  for (const auto& variant : controller_.trustedDevices()) {
    const auto map = variant.toMap();
    ids.push_back(map["id"].toString());
    names.push_back(map["name"].toString());
  }
  if (settingsPage_) settingsPage_->setTrustedDevices(ids, names);
}
void MainWindow::paintEvent(QPaintEvent* event) {
  QMainWindow::paintEvent(event);
  QPainter painter(this);
  if (!painter.isActive()) return;
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0.0, theme::kBgTop);
  gradient.setColorAt(1.0, theme::kBgBottom);
  painter.fillRect(rect(), gradient);
}
void MainWindow::showEvent(QShowEvent* event) {
  qDebug() << "MainWindow showEvent called";
  QMainWindow::showEvent(event);
}
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/MainWindow.hpp << 'EOC'
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
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/components/MicBadge.cpp << 'EOC'
#include "MicBadge.hpp"
#include <QLinearGradient>
#include <QPainter>
#include <QPropertyAnimation>
#include "../IconLoader.hpp"
#include "../Theme.hpp"
namespace wiremic::ui {
MicBadge::MicBadge(QWidget* parent) : QWidget(parent) {
    setFixedSize(110, 110);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    pulseAnimation_ = new QPropertyAnimation(this, "pulsePhase", this);
    pulseAnimation_->setStartValue(0.0);
    pulseAnimation_->setEndValue(1.0);
    pulseAnimation_->setDuration(1800);
    pulseAnimation_->setLoopCount(-1);
}
QSize MicBadge::sizeHint() const { return QSize(110, 110); }
void MicBadge::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (active_) pulseAnimation_->start();
    else { pulseAnimation_->stop(); pulsePhase_ = 0.0; }
    update();
}
qreal MicBadge::pulsePhase() const { return pulsePhase_; }
void MicBadge::setPulsePhase(qreal phase) { pulsePhase_ = phase; update(); }
void MicBadge::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds = rect();
    const QPointF center = bounds.center();
    if (active_) {
        const qreal ringScale = 0.66 + pulsePhase_ * 0.5;
        const qreal ringAlpha = (1.0 - pulsePhase_) * 0.55;
        QPen ringPen(theme::kAccentStart);
        ringPen.setWidthF(1.5);
        QColor ringColor = theme::kAccentStart;
        ringColor.setAlphaF(ringAlpha);
        ringPen.setColor(ringColor);
        painter.setPen(ringPen);
        painter.setBrush(Qt::NoBrush);
        const qreal ringRadius = bounds.width() / 2.0 * ringScale;
        painter.drawEllipse(center, ringRadius, ringRadius);
    }
    const qreal coreRadius = bounds.width() * 0.38;
    QRectF coreRect(center.x() - coreRadius, center.y() - coreRadius, coreRadius * 2, coreRadius * 2);
    if (active_) {
        QLinearGradient gradient(coreRect.topLeft(), coreRect.bottomRight());
        gradient.setColorAt(0.0, theme::kAccentStart);
        gradient.setColorAt(1.0, theme::kAccentEnd);
        painter.setBrush(gradient);
    } else painter.setBrush(QColor(35, 38, 51));
    painter.setPen(QPen(theme::kGlassBorderStrong, 1));
    painter.drawEllipse(coreRect);
    const int iconSize = static_cast<int>(coreRadius * 0.85);
    const QColor tint = active_ ? Qt::white : theme::kTextTertiary;
    QPixmap icon = IconLoader::Render(":/WireMic/resources/icons/icon_microphone.svg", iconSize * 2, tint);
    if (!icon.isNull()) painter.drawPixmap(QRectF(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize), icon, icon.rect());
}
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/components/GlassPanel.cpp << 'EOC'
#include "GlassPanel.hpp"
#include <QGraphicsDropShadowEffect>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include "../Theme.hpp"
namespace wiremic::ui {
GlassPanel::GlassPanel(QWidget* parent) : QWidget(parent), fillColor_(theme::kCardFill), borderColor_(theme::kGlassBorder) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setElevated(true);
}
void GlassPanel::setCornerRadius(int radius) { cornerRadius_ = radius; update(); }
void GlassPanel::setFillColor(const QColor& color) { fillColor_ = color; update(); }
void GlassPanel::setBorderColor(const QColor& color) { borderColor_ = color; update(); }
void GlassPanel::setElevated(bool elevated) {
    elevated_ = elevated;
    if (elevated_) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(28);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(0, 0, 0, 130));
        setGraphicsEffect(shadow);
    } else setGraphicsEffect(nullptr);
}
void GlassPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRectF rect = this->rect();
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius_, cornerRadius_);
    painter.fillPath(path, fillColor_);
    QLinearGradient highlight(0, 0, 0, height() * 0.5);
    highlight.setColorAt(0.0, QColor(255, 255, 255, 14));
    highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
    QPainterPath topHalf;
    topHalf.addRoundedRect(QRectF(0, 0, width(), height() * 0.5 + cornerRadius_), cornerRadius_, cornerRadius_);
    painter.setClipPath(path);
    painter.fillPath(topHalf, highlight);
    painter.setClipping(false);
    painter.setPen(QPen(borderColor_, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/components/StatCard.cpp << 'EOC'
#include "StatCard.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include "../Theme.hpp"
namespace wiremic::ui {
StatCard::StatCard(const QString& label, QWidget* parent) : GlassPanel(parent) {
    setFixedHeight(92);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(8);
    auto* labelWidget = new QLabel(label, this);
    labelWidget->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
    valueLabel_ = new QLabel("-", this);
    valueLabel_->setStyleSheet("color: rgb(245,246,250); font-size: 22px; font-weight: 700;");
    layout->addWidget(labelWidget);
    layout->addWidget(valueLabel_);
    layout->addStretch();
}
void StatCard::setValue(const QString& value, const QColor& color) {
    if (valueLabel_) {
        valueLabel_->setText(value);
        if (color.isValid()) valueLabel_->setStyleSheet(QString("color: rgb(%1,%2,%3); font-size: 22px; font-weight: 700;").arg(color.red()).arg(color.green()).arg(color.blue()));
    }
}
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/components/NavRailButton.cpp << 'EOC'
#include "NavRailButton.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include "../IconLoader.hpp"
#include "../Theme.hpp"
namespace wiremic::ui {
NavRailButton::NavRailButton(const QString& iconPath, const QString& label, QWidget* parent)
    : QWidget(parent), iconPath_(iconPath), label_(label) {
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(44);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}
QSize NavRailButton::sizeHint() const { return QSize(200, 44); }
void NavRailButton::setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    auto* animation = new QPropertyAnimation(this, "selectedProgress", this);
    animation->setStartValue(selectedProgress_);
    animation->setEndValue(selected_ ? 1.0 : 0.0);
    animation->setDuration(160);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
bool NavRailButton::isSelected() const { return selected_; }
qreal NavRailButton::selectedProgress() const { return selectedProgress_; }
void NavRailButton::setSelectedProgress(qreal progress) { selectedProgress_ = progress; update(); }
void NavRailButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(event);
}
void NavRailButton::enterEvent(QEnterEvent* event) { hovered_ = true; update(); QWidget::enterEvent(event); }
void NavRailButton::leaveEvent(QEvent* event) { hovered_ = false; update(); QWidget::leaveEvent(event); }
void NavRailButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRectF pillRect = rect().adjusted(12, 0, -12, 0);
    QPainterPath path;
    path.addRoundedRect(pillRect, theme::kRadiusSmall, theme::kRadiusSmall);
    if (selectedProgress_ > 0.001) {
        QColor fill = theme::kGlassFillActive;
        fill.setAlphaF(fill.alphaF() * selectedProgress_);
        painter.fillPath(path, fill);
        QPen borderPen(theme::kGlassBorderStrong);
        borderPen.setWidthF(selectedProgress_);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    } else if (hovered_) painter.fillPath(path, theme::kGlassFill);
    if (selectedProgress_ > 0.001) {
        QColor barColor = theme::kAccentStart;
        barColor.setAlphaF(selectedProgress_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(barColor);
        QRectF bar(pillRect.left(), pillRect.center().y() - 9, 3, 18);
        painter.drawRoundedRect(bar, 1.5, 1.5);
    }
    const int iconSize = 18;
    const qreal iconOpacity = 0.6 + 0.4 * selectedProgress_;
    QPixmap icon = IconLoader::Render(iconPath_, iconSize * 2);
    if (!icon.isNull()) {
        painter.setOpacity(iconOpacity);
        painter.drawPixmap(QRectF(pillRect.left() + 14, pillRect.center().y() - iconSize / 2.0, iconSize, iconSize), icon, icon.rect());
        painter.setOpacity(1.0);
    }
    QFont f = font();
    f.setBold(selected_);
    painter.setFont(f);
    QColor textColor = theme::kTextPrimary;
    textColor.setAlphaF(iconOpacity);
    painter.setPen(textColor);
    QRectF textRect(pillRect.left() + 44, 0, pillRect.width() - 44, height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label_);
}
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/pages/DashboardPage.cpp << 'EOC'
#include "DashboardPage.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include "../Theme.hpp"
#include "../components/GlassPanel.hpp"
#include "../components/MicBadge.hpp"
#include "../components/StatCard.hpp"
namespace wiremic::ui {
DashboardPage::DashboardPage(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(22);
  auto* titleLabel = new QLabel("Dashboard", this);
  titleLabel->setStyleSheet("color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  auto* subtitleLabel = new QLabel("Overview of WireMic on this computer", this);
  subtitleLabel->setStyleSheet("color: rgb(164,168,186); font-size: 13px;");
  rootLayout->addWidget(titleLabel);
  rootLayout->addWidget(subtitleLabel);
  auto* statsLayout = new QHBoxLayout();
  statsLayout->setSpacing(16);
  statusCard_ = new StatCard("Status", this);
  devicesCard_ = new StatCard("Devices Found", this);
  portCard_ = new StatCard("Control Port", this);
  statsLayout->addWidget(statusCard_);
  statsLayout->addWidget(devicesCard_);
  statsLayout->addWidget(portCard_);
  rootLayout->addLayout(statsLayout);
  auto* mainCard = new GlassPanel(this);
  auto* cardLayout = new QHBoxLayout(mainCard);
  cardLayout->setContentsMargins(28, 28, 28, 28);
  cardLayout->setSpacing(32);
  micBadge_ = new MicBadge(mainCard);
  cardLayout->addWidget(micBadge_, 0, Qt::AlignVCenter);
  auto* textColumn = new QWidget(mainCard);
  auto* textLayout = new QVBoxLayout(textColumn);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(10);
  auto* thisComputerLabel = new QLabel("This Computer", textColumn);
  thisComputerLabel->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
  localNameLabel_ = new QLabel("-", textColumn);
  localNameLabel_->setStyleSheet("color: rgb(245,246,250); font-size: 20px; font-weight: 700;");
  descriptionLabel_ = new QLabel(textColumn);
  descriptionLabel_->setWordWrap(true);
  descriptionLabel_->setStyleSheet("color: rgb(164,168,186); font-size: 13px;");
  textLayout->addWidget(thisComputerLabel);
  textLayout->addWidget(localNameLabel_);
  textLayout->addWidget(descriptionLabel_);
  textLayout->addStretch();
  cardLayout->addWidget(textColumn, 1);
  rootLayout->addWidget(mainCard, 1);
  setConnected(false, QString());
  setDeviceCount(0);
  setControlPort(0);
}
void DashboardPage::setConnected(bool connected, const QString& peerName) {
  if (micBadge_) micBadge_->setActive(connected);
  if (statusCard_) statusCard_->setValue(connected ? "Connected" : "Idle", connected ? theme::kSuccess : theme::kTextPrimary);
  if (descriptionLabel_) descriptionLabel_->setText(connected ? QString("Receiving audio from %1. A virtual microphone is available to every application on this computer.").arg(peerName) : "Waiting for a device to connect. Open \"Available Devices\" to discover phones or computers on your network.");
}
void DashboardPage::setDeviceCount(int count) {
  if (devicesCard_) devicesCard_->setValue(QString::number(count));
}
void DashboardPage::setControlPort(quint16 port) {
  if (portCard_) portCard_->setValue(QString::number(port));
}
void DashboardPage::setLocalDeviceName(const QString& name) {
  if (localNameLabel_) localNameLabel_->setText(name);
}
}  // namespace wiremic::ui
EOC
cat > linux-app/src/ui/components/GlassButton.cpp << 'EOC'
#include "GlassButton.hpp"
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include "../Theme.hpp"
namespace wiremic::ui {
GlassButton::GlassButton(const QString& text, Variant variant, QWidget* parent)
    : QPushButton(text, parent), variant_(variant) {
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(44);
  setFlat(true);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_TranslucentBackground, true);
}
void GlassButton::setBusy(bool busy) {
  busy_ = busy;
  setEnabled(!busy);
  if (busy_ && spinTimerId_ < 0) spinTimerId_ = startTimer(16);
  else if (!busy_ && spinTimerId_ >= 0) { killTimer(spinTimerId_); spinTimerId_ = -1; }
  update();
}
bool GlassButton::isBusy() const { return busy_; }
qreal GlassButton::hoverProgress() const { return hoverProgress_; }
void GlassButton::setHoverProgress(qreal progress) { hoverProgress_ = progress; update(); }
void GlassButton::enterEvent(QEnterEvent* event) {
  auto* animation = new QPropertyAnimation(this, "hoverProgress", this);
  animation->setStartValue(hoverProgress_);
  animation->setEndValue(1.0);
  animation->setDuration(140);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
  QPushButton::enterEvent(event);
}
void GlassButton::leaveEvent(QEvent* event) {
  auto* animation = new QPropertyAnimation(this, "hoverProgress", this);
  animation->setStartValue(hoverProgress_);
  animation->setEndValue(0.0);
  animation->setDuration(180);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
  QPushButton::leaveEvent(event);
}
void GlassButton::mousePressEvent(QMouseEvent* event) {
  pressed_ = true;
  update();
  QPushButton::mousePressEvent(event);
}
void GlassButton::mouseReleaseEvent(QMouseEvent* event) {
  pressed_ = false;
  update();
  QPushButton::mouseReleaseEvent(event);
}
void GlassButton::timerEvent(QTimerEvent* event) {
  if (event->timerId() == spinTimerId_) { spinAngle_ = (spinAngle_ + 8) % 360; update(); return; }
  QPushButton::timerEvent(event);
}
void GlassButton::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const qreal scale = pressed_ ? 0.97 : 1.0;
  QRectF bounds = rect();
  const QPointF center = bounds.center();
  painter.translate(center);
  painter.scale(scale, scale);
  painter.translate(-center);
  QPainterPath path;
  path.addRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
  if (variant_ == Variant::Primary) {
    QLinearGradient gradient(bounds.topLeft(), bounds.topRight());
    gradient.setColorAt(0.0, theme::kAccentStart);
    gradient.setColorAt(1.0, theme::kAccentEnd);
    painter.fillPath(path, gradient);
    if (hoverProgress_ > 0.0) { painter.setOpacity(hoverProgress_ * 0.12); painter.fillPath(path, Qt::white); painter.setOpacity(1.0); }
  } else {
    const QColor base = variant_ == Variant::Danger ? QColor(255, 93, 120, 20) : theme::kGlassFill;
    const QColor hoverFill = variant_ == Variant::Danger ? QColor(255, 93, 120, 34) : theme::kGlassFillHover;
    painter.fillPath(path, hoverProgress_ > 0.0 ? hoverFill : base);
    painter.setPen(QPen(theme::kGlassBorder, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }
  if (busy_) {
    painter.resetTransform();
    const int diameter = 18;
    QRectF spinnerRect(bounds.center().x() - diameter / 2.0, bounds.center().y() - diameter / 2.0, diameter, diameter);
    QPen pen(Qt::white, 2.2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawArc(spinnerRect, spinAngle_ * 16, 270 * 16);
    return;
  }
  painter.resetTransform();
  painter.translate(center);
  painter.scale(scale, scale);
  painter.translate(-center);
  const QColor textColor = variant_ == Variant::Primary ? Qt::white : (variant_ == Variant::Danger ? theme::kDanger : theme::kTextPrimary);
  painter.setPen(textColor);
  QFont f = font();
  f.setBold(true);
  painter.setFont(f);
  painter.drawText(bounds, Qt::AlignCenter, text());
}
}  // namespace wiremic::ui
EOC
echo "All files updated successfully!"
