#pragma once

#include <QWidget>

namespace wiremic::ui {

class GlassPanel : public QWidget {
  Q_OBJECT

 public:
  explicit GlassPanel(QWidget* parent = nullptr);

  void setCornerRadius(int radius);
  void setFillColor(const QColor& color);
  void setBorderColor(const QColor& color);
  void setElevated(bool elevated);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  int cornerRadius_{22};
  QColor fillColor_;
  QColor borderColor_;
  bool elevated_{true};
};

}  // namespace wiremic::ui
