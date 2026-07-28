#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>

namespace wiremic::ui {

class ToggleSwitch;

class SettingsPage : public QWidget {
  Q_OBJECT

 public:
  explicit SettingsPage(QWidget* parent = nullptr);

  void setTrustedDevices(const QStringList& ids, const QStringList& names);
  void setToggleStates(bool autoConnect, bool rememberTrusted);
  // index: 0 = Ultra Low (5ms), 1 = Low (10ms), 2 = Balanced (20ms)
  void setLatencyModeIndex(int index);

 signals:
  void autoConnectChanged(bool enabled);
  void rememberTrustedChanged(bool enabled);
  void latencyModeChanged(int index);
  void revokeTrustRequested(QString deviceId);

 private:
  ToggleSwitch* autoConnectSwitch_;
  ToggleSwitch* rememberTrustedSwitch_;
  QComboBox* latencyCombo_;
  QVBoxLayout* trustedListLayout_;
  QWidget* trustedListContainer_;
  QLabel* emptyLabel_;
};

}
