#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>

namespace wiremic::ui {

class LogsPage : public QWidget {
  Q_OBJECT

 public:
  explicit LogsPage(QWidget* parent = nullptr);

  void appendLog(const QString& timestamp, const QString& message);

 private:
  QVBoxLayout* logLayout_;
  QScrollArea* scrollArea_;
};

}
