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
void NavRailButton::setCompact(bool compact) {
  if (compact_ == compact) return;
  compact_ = compact;
  setToolTip(compact ? label_ : QString());
  updateGeometry();
  update();
}

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
    if (!compact_) {
      painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label_);
    }
}
}
