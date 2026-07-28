#pragma once

#include <QLabel>
#include "GlassPanel.hpp"

namespace wiremic::ui {

class StatCard : public GlassPanel {
  Q_OBJECT

 public:
  StatCard(const QString& label, QWidget* parent = nullptr);

  void setValue(const QString& value, const QColor& color = QColor());

 private:
  QLabel* valueLabel_;
};

}
