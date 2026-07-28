#pragma once

#include <QWidget>

namespace wiremic::ui {

class AboutPage : public QWidget {
  Q_OBJECT

 public:
  explicit AboutPage(QWidget* parent = nullptr);
};

}
