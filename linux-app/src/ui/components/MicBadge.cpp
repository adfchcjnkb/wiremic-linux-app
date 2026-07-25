#include "MicBadge.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPropertyAnimation>

#include "../IconLoader.hpp"
#include "../Theme.hpp"

namespace wiremic::ui {

MicBadge::MicBadge(QWidget* parent) : QWidget(parent) {
  setFixedSize(sizeHint());

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
  if (active_) {
    pulseAnimation_->start();
  } else {
    pulseAnimation_->stop();
    pulsePhase_ = 0.0;
  }
  update();
}

qreal MicBadge::pulsePhase() const { return pulsePhase_; }

void MicBadge::setPulsePhase(qreal phase) {
  pulsePhase_ = phase;
  update();
}

void MicBadge::paintEvent(QPaintEvent* /*event*/) {
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
  QRectF coreRect(center.x() - coreRadius, center.y() - coreRadius,
                   coreRadius * 2, coreRadius * 2);

  if (active_) {
    QLinearGradient gradient(coreRect.topLeft(), coreRect.bottomRight());
    gradient.setColorAt(0.0, theme::kAccentStart);
    gradient.setColorAt(1.0, theme::kAccentEnd);
    painter.setBrush(gradient);
  } else {
    painter.setBrush(QColor(35, 38, 51));
  }
  painter.setPen(QPen(theme::kGlassBorderStrong, 1));
  painter.drawEllipse(coreRect);

  const int iconSize = static_cast<int>(coreRadius * 0.85);
  const QColor tint = active_ ? Qt::white : theme::kTextTertiary;
  QPixmap icon = IconLoader::Render(":/WireMic/resources/icons/icon_microphone.svg",
                                     iconSize * 2, tint);
  painter.drawPixmap(
      QRectF(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0,
             iconSize, iconSize),
      icon, icon.rect());
}

}  // namespace wiremic::ui
