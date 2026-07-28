#include "ToggleSwitch.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include "../Theme.hpp"

namespace wiremic::ui {

ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent) {
  setCursor(Qt::PointingHandCursor);
  setFixedSize(sizeHint());
}

QSize ToggleSwitch::sizeHint() const { return QSize(46, 26); }

bool ToggleSwitch::isChecked() const { return checked_; }

void ToggleSwitch::setChecked(bool checked, bool animate) {
  if (checked_ == checked) return;
  checked_ = checked;

  if (animate) {
    auto* animation = new QPropertyAnimation(this, "knobPosition", this);
    animation->setStartValue(knobPosition_);
    animation->setEndValue(checked_ ? 1.0 : 0.0);
    animation->setDuration(180);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
  } else {
    setKnobPosition(checked_ ? 1.0 : 0.0);
  }
}

qreal ToggleSwitch::knobPosition() const { return knobPosition_; }

void ToggleSwitch::setKnobPosition(qreal position) {
  knobPosition_ = position;
  update();
}

void ToggleSwitch::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    setChecked(!checked_);
    emit toggled(checked_);
  }
  QWidget::mousePressEvent(event);
}

void ToggleSwitch::paintEvent(QPaintEvent* ) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QRectF trackRect = rect().adjusted(0, 0, -1, -1);
  const qreal radius = trackRect.height() / 2.0;

  QColor trackColor;
  trackColor.setRedF(theme::kGlassFillHover.redF() +
                      (theme::kAccentStart.redF() - theme::kGlassFillHover.redF()) *
                          knobPosition_);
  trackColor.setGreenF(
      theme::kGlassFillHover.greenF() +
      (theme::kAccentStart.greenF() - theme::kGlassFillHover.greenF()) *
          knobPosition_);
  trackColor.setBlueF(theme::kGlassFillHover.blueF() +
                       (theme::kAccentStart.blueF() - theme::kGlassFillHover.blueF()) *
                           knobPosition_);
  trackColor.setAlphaF(0.25 + 0.75 * knobPosition_);

  painter.setPen(QPen(theme::kGlassBorder, 1));
  painter.setBrush(trackColor);
  painter.drawRoundedRect(trackRect, radius, radius);

  const qreal knobDiameter = trackRect.height() - 6;
  const qreal knobX = 3 + knobPosition_ * (trackRect.width() - knobDiameter - 6);
  QRectF knobRect(knobX, 3, knobDiameter, knobDiameter);

  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::white);
  painter.drawEllipse(knobRect);
}

}
