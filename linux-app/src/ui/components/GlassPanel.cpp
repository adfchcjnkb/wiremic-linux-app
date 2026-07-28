#include "GlassPanel.hpp"
#include <QGraphicsDropShadowEffect>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include "../Theme.hpp"
namespace wiremic::ui {
GlassPanel::GlassPanel(QWidget* parent) : QWidget(parent), fillColor_(theme::kCardFill), borderColor_(theme::kGlassBorder) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setElevated(true);
}
void GlassPanel::setCornerRadius(int radius) { cornerRadius_ = radius; update(); }
void GlassPanel::setFillColor(const QColor& color) { fillColor_ = color; update(); }
void GlassPanel::setBorderColor(const QColor& color) { borderColor_ = color; update(); }
void GlassPanel::setElevated(bool elevated) {
    elevated_ = elevated;
    if (elevated_) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(28);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(0, 0, 0, 130));
        setGraphicsEffect(shadow);
    } else setGraphicsEffect(nullptr);
}
void GlassPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRectF rect = this->rect();
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius_, cornerRadius_);
    painter.fillPath(path, fillColor_);
    QLinearGradient highlight(0, 0, 0, height() * 0.5);
    highlight.setColorAt(0.0, QColor(255, 255, 255, 14));
    highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
    QPainterPath topHalf;
    topHalf.addRoundedRect(QRectF(0, 0, width(), height() * 0.5 + cornerRadius_), cornerRadius_, cornerRadius_);
    painter.setClipPath(path);
    painter.fillPath(topHalf, highlight);
    painter.setClipping(false);
    painter.setPen(QPen(borderColor_, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}
}
