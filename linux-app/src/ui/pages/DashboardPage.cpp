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
  micCard_ = new StatCard("Virtual Mic", this);
  statsLayout->addWidget(statusCard_);
  statsLayout->addWidget(devicesCard_);
  statsLayout->addWidget(portCard_);
  statsLayout->addWidget(micCard_);
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
  setVirtualMic(false, QStringLiteral("none"));
}
void DashboardPage::setConnected(bool connected, const QString& peerName) {
  connected_ = connected;
  peerName_ = peerName;
  if (statusCard_) statusCard_->setValue(connected ? "Connected" : "Idle", connected ? theme::kSuccess : theme::kTextPrimary);
  updateDescription();
}
void DashboardPage::setVirtualMic(bool active, const QString& backendName) {
  micActive_ = active;
  backendName_ = backendName;
  // The badge tracks the microphone, not the control channel: it lights up
  // only once audio is really reaching the system.
  if (micBadge_) micBadge_->setActive(active);
  if (micCard_) {
    micCard_->setValue(active ? backendName : QStringLiteral("Off"),
                       active ? theme::kSuccess : theme::kTextPrimary);
  }
  updateDescription();
}
void DashboardPage::updateDescription() {
  if (!descriptionLabel_) return;
  if (connected_ && micActive_) {
    descriptionLabel_->setText(
        QString("Receiving audio from %1. \"WireMic Virtual Microphone\" is "
                "now available to every application on this computer via %2.")
            .arg(peerName_, backendName_));
  } else if (connected_) {
    descriptionLabel_->setText(
        QString("Connected to %1. Streaming this computer's microphone to it.")
            .arg(peerName_));
  } else {
    descriptionLabel_->setText(
        "Waiting for a device to connect. Open WireMic on your phone and pick "
        "this computer — the request will show up here for approval.");
  }
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
