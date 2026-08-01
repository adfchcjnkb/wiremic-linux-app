#include "DevicesPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "../components/GlassButton.hpp"
#include "../components/GlassPanel.hpp"
#include "DiscoveryService.hpp"

namespace wiremic::ui {

DevicesPage::DevicesPage(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(18);

  auto* headerLayout = new QHBoxLayout();
  auto* titleColumn = new QWidget(this);
  auto* titleLayout = new QVBoxLayout(titleColumn);
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(4);
  auto* titleLabel = new QLabel("Available Devices", titleColumn);
  titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  auto* subtitleLabel =
      new QLabel("Devices found on your local network", titleColumn);
  subtitleLabel->setStyleSheet("color: rgb(164,168,186); font-size: 13px;");
  titleLayout->addWidget(titleLabel);
  titleLayout->addWidget(subtitleLabel);
  headerLayout->addWidget(titleColumn, 1);

  auto* refreshButton =
      new GlassButton("Refresh", GlassButton::Variant::Secondary, this);
  refreshButton->setFixedWidth(110);
  headerLayout->addWidget(refreshButton, 0, Qt::AlignTop);
  connect(refreshButton, &GlassButton::clicked, this,
          &DevicesPage::refreshRequested);

  rootLayout->addLayout(headerLayout);

  statusLabel_ = new QLabel(this);
  statusLabel_->setWordWrap(true);
  statusLabel_->setStyleSheet("color: rgb(255,93,120); font-size: 12px;");
  statusLabel_->setVisible(false);
  rootLayout->addWidget(statusLabel_);

  // Some networks never let the two devices find each other on their own --
  // guest Wi-Fi that isolates clients, a firewall that drops the announcement.
  // Showing the address here means there is always a way through: read it off
  // this screen, type it into the phone.
  auto* addressCard = new GlassPanel(this);
  addressCard->setCornerRadius(14);
  auto* addressLayout = new QVBoxLayout(addressCard);
  addressLayout->setContentsMargins(16, 12, 16, 12);
  addressLayout->setSpacing(4);

  auto* addressTitle = new QLabel("This computer's address", addressCard);
  addressTitle->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  addressLayout->addWidget(addressTitle);

  addressLabel_ = new QLabel(addressCard);
  addressLabel_->setWordWrap(true);
  addressLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  addressLabel_->setStyleSheet(
      "color: rgb(245,246,250); font-size: 17px; font-weight: 700;");
  addressLayout->addWidget(addressLabel_);

  auto* addressHint = new QLabel(
      "If your phone does not appear below, type this into the phone's Nearby "
      "screen.",
      addressCard);
  addressHint->setWordWrap(true);
  addressHint->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  addressLayout->addWidget(addressHint);

  rootLayout->addWidget(addressCard);
  refreshLocalAddresses();

  auto* addressTimer = new QTimer(this);
  addressTimer->setInterval(5000);
  connect(addressTimer, &QTimer::timeout, this,
          &DevicesPage::refreshLocalAddresses);
  addressTimer->start();

  auto* scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet("QScrollArea { background: transparent; }");
  scrollArea->viewport()->setStyleSheet("background: transparent;");

  listContainer_ = new QWidget();
  listContainer_->setStyleSheet("background: transparent;");
  listLayout_ = new QVBoxLayout(listContainer_);
  listLayout_->setContentsMargins(0, 0, 4, 0);
  listLayout_->setSpacing(10);
  listLayout_->addStretch();

  scrollArea->setWidget(listContainer_);
  rootLayout->addWidget(scrollArea, 1);

  emptyLabel_ = new QLabel("No devices found yet", this);
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setStyleSheet("color: rgb(164,168,186); font-size: 13px;");
  emptyLabel_->setVisible(false);
  rootLayout->addWidget(emptyLabel_);
}

void DevicesPage::setDevices(const std::vector<DeviceRowData>& devices) {
  while (rows_.size() > devices.size()) {
    auto* row = rows_.back();
    rows_.pop_back();
    listLayout_->removeWidget(row);
    row->hide();
    row->setParent(nullptr);
    row->deleteLater();
  }

  while (rows_.size() < devices.size()) {
    auto* row = new DeviceRow(listContainer_);
    connect(row, &DeviceRow::connectRequested, this,
            &DevicesPage::connectRequested);
    listLayout_->insertWidget(static_cast<int>(rows_.size()), row);
    rows_.push_back(row);
    row->show();
  }

  for (size_t i = 0; i < devices.size(); ++i) {
    rows_[i]->setData(devices[i]);
    rows_[i]->setBusy(devices[i].id == busyDeviceId_);
  }

  emptyLabel_->setVisible(devices.empty());
}

void DevicesPage::refreshLocalAddresses() {
  const QStringList addresses = network::DiscoveryService::LocalAddresses();
  const QString text =
      addresses.isEmpty()
          ? QStringLiteral("Not connected to a network")
          : addresses.join(QStringLiteral("   ·   "));
  if (addressLabel_->text() != text) addressLabel_->setText(text);
}

void DevicesPage::setStatusMessage(const QString& message) {
  statusLabel_->setText(message);
  statusLabel_->setVisible(!message.isEmpty());
}

void DevicesPage::setBusyDeviceId(const QString& deviceId) {
  busyDeviceId_ = deviceId;
  for (auto* row : rows_) {
    row->setBusy(row->deviceId() == busyDeviceId_);
  }
}

}
