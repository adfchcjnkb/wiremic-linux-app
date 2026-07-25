#pragma once

#include <QWidget>

namespace wiremic::ui {

class NavRailButton : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal selectedProgress READ selectedProgress WRITE setSelectedProgress)

 public:
  NavRailButton(const QString& iconPath, const QString& label,
                QWidget* parent = nullptr);

  void setSelected(bool selected);
  [[nodiscard]] bool isSelected() const;

  qreal selectedProgress() const;
  void setSelectedProgress(qreal progress);

  QSize sizeHint() const override;

 signals:
  void clicked();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  QString iconPath_;
  QString label_;
  bool selected_{false};
  bool hovered_{false};
  qreal selectedProgress_{0.0};
};

}  // namespace wiremic::ui
