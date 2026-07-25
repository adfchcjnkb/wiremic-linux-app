#include "IncomingRequestDialog.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "../IconLoader.hpp"
#include "../Theme.hpp"
#include "GlassButton.hpp"

namespace wiremic::ui {

IncomingRequestDialog::IncomingRequestDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowFlag(Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setFixedSize(420, 340);

  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(24, 22, 24, 22);
  rootLayout->setSpacing(16);

  auto* headerLayout = new QGridLayout();
  iconLabel_ = new QLabel(this);
  iconLabel_->setFixedSize(54, 54);
  headerLayout->addWidget(iconLabel_, 0, 0, 2, 1, Qt::AlignVCenter);

  auto* captionLabel = new QLabel("Connection Request", this);
  captionLabel->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  headerLayout->addWidget(captionLabel, 0, 1);

  nameLabel_ = new QLabel(this);
  nameLabel_->setStyleSheet(
      "color: rgb(245,246,250); font-size: 19px; font-weight: 700;");
  headerLayout->addWidget(nameLabel_, 1, 1);
  headerLayout->setColumnStretch(1, 1);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setHorizontalSpacing(14);
  rootLayout->addLayout(headerLayout);

  auto* infoGrid = new QGridLayout();
  infoGrid->setHorizontalSpacing(20);
  infoGrid->setVerticalSpacing(10);

  auto addRow = [&](int row, const QString& label, QLabel*& valueOut) {
    auto* labelWidget = new QLabel(label, this);
    labelWidget->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
    infoGrid->addWidget(labelWidget, row, 0);
    valueOut = new QLabel(this);
    valueOut->setStyleSheet("color: rgb(245,246,250); font-size: 12px;");
    infoGrid->addWidget(valueOut, row, 1);
  };
  addRow(0, "Model", modelValue_);
  addRow(1, "Platform", platformValue_);
  addRow(2, "Local IP", ipValue_);
  addRow(3, "Connection", connectionValue_);
  rootLayout->addLayout(infoGrid);

  rootLayout->addStretch();

  auto* buttonLayout = new QGridLayout();
  auto* rejectButton = new GlassButton("Reject", GlassButton::Variant::Danger, this);
  auto* acceptButton = new GlassButton("Accept", GlassButton::Variant::Primary, this);
  buttonLayout->addWidget(rejectButton, 0, 0);
  buttonLayout->addWidget(acceptButton, 0, 1);
  buttonLayout->setColumnStretch(0, 1);
  buttonLayout->setColumnStretch(1, 1);
  buttonLayout->setHorizontalSpacing(12);
  rootLayout->addLayout(buttonLayout);

  connect(acceptButton, &GlassButton::clicked, this, [this]() {
    emit accepted_();
    close();
  });
  connect(rejectButton, &GlassButton::clicked, this, [this]() {
    emit rejected_();
    close();
  });
}

void IncomingRequestDialog::setData(const IncomingRequestData& data) {
  data_ = data;
  nameLabel_->setText(data_.name);
  modelValue_->setText(data_.model.isEmpty() ? "-" : data_.model);
  platformValue_->setText(data_.platform.isEmpty() ? "-" : data_.platform);
  ipValue_->setText(data_.ip.isEmpty() ? "-" : data_.ip);
  connectionValue_->setText(data_.connectionType.isEmpty() ? "-"
                                                             : data_.connectionType);

  const QString iconFile = data_.platform == "android"
                                ? ":/WireMic/resources/icons/icon_phone.svg"
                                : ":/WireMic/resources/icons/icon_desktop.svg";
  QPixmap badge(54, 54);
  badge.fill(Qt::transparent);
  QPainter painter(&badge);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath path;
  path.addRoundedRect(badge.rect(), 17, 17);
  QLinearGradient gradient(0, 0, 54, 54);
  gradient.setColorAt(0.0, theme::kAccentStart);
  gradient.setColorAt(1.0, theme::kAccentEnd);
  painter.fillPath(path, gradient);
  QPixmap icon = IconLoader::Render(iconFile, 52, Qt::white);
  painter.drawPixmap(QRectF(15, 15, 26, 26), icon, icon.rect());
  painter.end();
  iconLabel_->setPixmap(badge);
}

void IncomingRequestDialog::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QPainterPath path;
  path.addRoundedRect(rect().adjusted(0, 0, -1, -1), theme::kRadiusXLarge,
                       theme::kRadiusXLarge);
  painter.fillPath(path, QColor(18, 20, 28, 235));
  painter.setPen(QPen(theme::kGlassBorderStrong, 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

}  // namespace wiremic::ui
