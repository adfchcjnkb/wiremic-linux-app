#include "DevicesPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "../components/GlassButton.hpp"

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
  for (auto* row : rows_) row->deleteLater();
  rows_.clear();

  while (listLayout_->count() > 1) {
    QLayoutItem* item = listLayout_->takeAt(0);
    delete item;
  }

  for (const auto& data : devices) {
    auto* row = new DeviceRow(listContainer_);
    row->setData(data);
    row->setBusy(data.id == busyDeviceId_);
    connect(row, &DeviceRow::connectRequested, this,
            &DevicesPage::connectRequested);
    listLayout_->insertWidget(listLayout_->count() - 1, row);
    rows_.push_back(row);
  }

  emptyLabel_->setVisible(devices.empty());
}

void DevicesPage::setBusyDeviceId(const QString& deviceId) {
  busyDeviceId_ = deviceId;
  for (auto* row : rows_) {
    row->setBusy(false);
  }
}

}
