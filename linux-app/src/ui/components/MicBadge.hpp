#pragma once

#include <QWidget>

class QPropertyAnimation;

namespace wiremic::ui {

class MicBadge : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal pulsePhase READ pulsePhase WRITE setPulsePhase)

 public:
  explicit MicBadge(QWidget* parent = nullptr);

  void setActive(bool active);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

  qreal pulsePhase() const;
  void setPulsePhase(qreal phase);

 private:
  bool active_{false};
  qreal pulsePhase_{0.0};
  QPropertyAnimation* pulseAnimation_{nullptr};
};

}
