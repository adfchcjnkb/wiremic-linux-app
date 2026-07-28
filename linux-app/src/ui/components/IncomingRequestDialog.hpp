#pragma once

#include <QDialog>
#include <QLabel>

namespace wiremic::ui {

struct IncomingRequestData {
  QString name;
  QString model;
  QString platform;
  QString ip;
  QString connectionType;
};

class IncomingRequestDialog : public QDialog {
  Q_OBJECT

 public:
  explicit IncomingRequestDialog(QWidget* parent = nullptr);

  void setData(const IncomingRequestData& data);

 signals:
  void accepted_();
  void rejected_();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  IncomingRequestData data_;
  QLabel* nameLabel_;
  QLabel* modelValue_;
  QLabel* platformValue_;
  QLabel* ipValue_;
  QLabel* connectionValue_;
  QLabel* iconLabel_;
};

}
