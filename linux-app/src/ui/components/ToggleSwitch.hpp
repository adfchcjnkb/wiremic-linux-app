#pragma once

#include <QWidget>

namespace wiremic::ui {

class ToggleSwitch : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal knobPosition READ knobPosition WRITE setKnobPosition)

 public:
  explicit ToggleSwitch(QWidget* parent = nullptr);

  [[nodiscard]] bool isChecked() const;
  void setChecked(bool checked, bool animate = true);

  qreal knobPosition() const;
  void setKnobPosition(qreal position);

  QSize sizeHint() const override;

 signals:
  void toggled(bool checked);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  bool checked_{false};
  qreal knobPosition_{0.0};
};

}  // namespace wiremic::ui
