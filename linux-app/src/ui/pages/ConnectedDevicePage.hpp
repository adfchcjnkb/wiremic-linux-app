#pragma once

#include <QWidget>
#include <QLabel>

namespace wiremic::ui {

class MicBadge;
class StatCard;
class GlassPanel;

class ConnectedDevicePage : public QWidget {
  Q_OBJECT

 public:
  explicit ConnectedDevicePage(QWidget* parent = nullptr);

  void setConnected(bool connected, const QString& name, const QString& model,
                     const QString& state, const QString& ip,
                     const QString& connectionType, const QString& platform);

 signals:
  void disconnectRequested();

 private:
  GlassPanel* emptyPanel_;
  GlassPanel* connectedPanel_;
  MicBadge* micBadgeEmpty_;
  MicBadge* micBadgeActive_;
  QLabel* nameLabel_;
  QLabel* modelLabel_;
  QLabel* stateLabel_;
  StatCard* ipCard_;
  StatCard* connectionCard_;
};

}  // namespace wiremic::ui
