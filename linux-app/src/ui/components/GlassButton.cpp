#include "GlassButton.hpp"
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QFontMetrics>
#include <algorithm>
#include "../Theme.hpp"
namespace wiremic::ui {
namespace {
constexpr int kTextPadding = 14;
}
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
  const QFontMetrics metrics(f);
  const QRectF textBounds = bounds.adjusted(kTextPadding, 0, -kTextPadding, 0);
  painter.drawText(
      textBounds, Qt::AlignCenter,
      metrics.elidedText(text(), Qt::ElideRight,
                          static_cast<int>(textBounds.width())));
}

// Without this a long label makes the button refuse to be any narrower than its
// text, which pushes the whole row wider than the window and shoves the buttons
// beside it off the edge. Reporting a small floor lets the layout compress the
// row instead, and the label elides rather than disappearing.
QSize GlassButton::minimumSizeHint() const {
  const QSize base = QPushButton::minimumSizeHint();
  const QFontMetrics metrics(font());
  const int floor = metrics.horizontalAdvance(QStringLiteral("Restore")) +
                     2 * kTextPadding;
  return {std::min(base.width(), floor), std::max(base.height(), 44)};
}
}
