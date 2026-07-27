#include "DashboardPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QDebug>

#include "../Theme.hpp"
#include "../components/GlassPanel.hpp"
#include "../components/MicBadge.hpp"
#include "../components/StatCard.hpp"

namespace wiremic::ui {

  DashboardPage::DashboardPage(QWidget* parent) : QWidget(parent) {
    qDebug() << "DashboardPage constructor called";

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(22);

    auto* titleLabel = new QLabel("Dashboard", this);
    titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
    auto* subtitleLabel =
    new QLabel("Overview of WireMic on this computer", this);
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
    localNameLabel_->setStyleSheet(
      "color: rgb(245,246,250); font-size: 20px; font-weight: 700;");
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

    qDebug() << "DashboardPage constructor finished";
  }

  void DashboardPage::setConnected(bool connected, const QString& peerName) {
    if (micBadge_) {
      micBadge_->setActive(connected);
    }
    if (statusCard_) {
      statusCard_->setValue(connected ? "Connected" : "Idle",
                            connected ? theme::kSuccess : theme::kTextPrimary);
    }
    if (descriptionLabel_) {
      descriptionLabel_->setText(
        connected
        ? QString("Receiving audio from %1. A virtual microphone is "
        "available to every application on this computer.")
        .arg(peerName)
        : "Waiting for a device to connect. Open \u201cAvailable "
        "Devices\u201d to discover phones or computers on your network.");
    }
  }

  void DashboardPage::setDeviceCount(int count) {
    if (devicesCard_) {
      devicesCard_->setValue(QString::number(count));
    }
  }

  void DashboardPage::setControlPort(quint16 port) {
    if (portCard_) {
      portCard_->setValue(QString::number(port));
    }
  }

  void DashboardPage::setLocalDeviceName(const QString& name) {
    if (localNameLabel_) {
      localNameLabel_->setText(name);
    }
  }

}  // namespace wiremic::ui
