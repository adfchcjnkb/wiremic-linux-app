#include "StatCard.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include "../Theme.hpp"
namespace wiremic::ui {
StatCard::StatCard(const QString& label, QWidget* parent) : GlassPanel(parent) {
    setFixedHeight(92);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(8);
    auto* labelWidget = new QLabel(label, this);
    labelWidget->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
    valueLabel_ = new QLabel("-", this);
    valueLabel_->setStyleSheet("color: rgb(245,246,250); font-size: 22px; font-weight: 700;");
    layout->addWidget(labelWidget);
    layout->addWidget(valueLabel_);
    layout->addStretch();
}
void StatCard::setValue(const QString& value, const QColor& color) {
    if (valueLabel_) {
        valueLabel_->setText(value);
        if (color.isValid()) valueLabel_->setStyleSheet(QString("color: rgb(%1,%2,%3); font-size: 22px; font-weight: 700;").arg(color.red()).arg(color.green()).arg(color.blue()));
    }
}
}  // namespace wiremic::ui
