#pragma once

#include <QPushButton>

namespace wiremic::ui {

class GlassButton : public QPushButton {
  Q_OBJECT
  Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

 public:
  enum class Variant { Primary, Secondary, Danger };

  explicit GlassButton(const QString& text, Variant variant = Variant::Primary,
                        QWidget* parent = nullptr);

  void setBusy(bool busy);
  [[nodiscard]] bool isBusy() const;

  qreal hoverProgress() const;
  void setHoverProgress(qreal progress);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void timerEvent(QTimerEvent* event) override;

 private:
  Variant variant_;
  qreal hoverProgress_{0.0};
  bool pressed_{false};
  bool busy_{false};
  int spinAngle_{0};
  int spinTimerId_{-1};
};

}  // namespace wiremic::ui
