#include "DeviceRow.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "../IconLoader.hpp"
#include "../Theme.hpp"

namespace wiremic::ui {

DeviceRow::DeviceRow(QWidget* parent) : QWidget(parent) {
  setFixedHeight(76);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(70, 0, 16, 0);
  layout->setSpacing(14);

  auto* textColumn = new QWidget(this);
  auto* textLayout = new QVBoxLayout(textColumn);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(3);

  auto* nameLabel = new QLabel(textColumn);
  nameLabel->setObjectName("deviceName");
  auto* metaLabel = new QLabel(textColumn);
  metaLabel->setObjectName("deviceMeta");
  textLayout->addStretch();
  textLayout->addWidget(nameLabel);
  textLayout->addWidget(metaLabel);
  textLayout->addStretch();

  layout->addWidget(textColumn, 1);

  connectButton_ = new GlassButton("Connect", GlassButton::Variant::Primary, this);
  connectButton_->setFixedWidth(110);
  layout->addWidget(connectButton_);

  connect(connectButton_, &GlassButton::clicked, this,
          [this]() { emit connectRequested(data_.id); });

  setStyleSheet(
      "QLabel#deviceName { color: rgb(245,246,250); font-size: 14px; "
      "font-weight: 700; }"
      "QLabel#deviceMeta { color: rgb(164,168,186); font-size: 11px; }");
}

void DeviceRow::setData(const DeviceRowData& data) {
  data_ = data;
  if (auto* nameLabel = findChild<QLabel*>("deviceName")) {
    nameLabel->setText(data_.name);
  }
  if (auto* metaLabel = findChild<QLabel*>("deviceMeta")) {
    metaLabel->setText(data_.model + "  ·  " + data_.ip);
  }
  update();
}

void DeviceRow::setBusy(bool busy) { connectButton_->setBusy(busy); }

QString DeviceRow::deviceId() const { return data_.id; }

void DeviceRow::paintEvent(QPaintEvent* ) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QPainterPath path;
  path.addRoundedRect(rect().adjusted(0, 0, -1, -1), theme::kRadiusMedium,
                       theme::kRadiusMedium);
  painter.fillPath(path, theme::kCardFill);
  painter.setPen(QPen(theme::kGlassBorder, 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);

  const QString iconFile = data_.platform == "android"
                                ? ":/WireMic/resources/icons/icon_phone.svg"
                                : ":/WireMic/resources/icons/icon_desktop.svg";
  QRectF badgeRect(16, height() / 2.0 - 23, 46, 46);
  QPainterPath badgePath;
  badgePath.addRoundedRect(badgeRect, 15, 15);
  QLinearGradient gradient(badgeRect.topLeft(), badgeRect.bottomRight());
  gradient.setColorAt(0.0, theme::kAccentStart);
  gradient.setColorAt(1.0, theme::kAccentEnd);
  painter.fillPath(badgePath, gradient);

  QPixmap icon = IconLoader::Render(iconFile, 44, Qt::white);
  painter.drawPixmap(QRectF(badgeRect.center().x() - 11,
                             badgeRect.center().y() - 11, 22, 22),
                      icon, icon.rect());

  const bool online = data_.status == "Online";
  painter.setPen(Qt::NoPen);
  painter.setBrush(online ? theme::kSuccess : theme::kTextTertiary);
  painter.drawEllipse(QPointF(width() - 130, height() / 2.0), 4, 4);
}

}
