#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

namespace wiremic::ui {

class ToggleSwitch;

class SettingsPage : public QWidget {
  Q_OBJECT

 public:
  explicit SettingsPage(QWidget* parent = nullptr);

  void setTrustedDevices(const QStringList& ids, const QStringList& names);
  // Reflects the persisted settings in the toggles without re-emitting them.
  void setToggleStates(bool autoConnect, bool rememberTrusted);

 signals:
  void autoConnectChanged(bool enabled);
  void rememberTrustedChanged(bool enabled);
  void revokeTrustRequested(QString deviceId);

 private:
  ToggleSwitch* autoConnectSwitch_;
  ToggleSwitch* rememberTrustedSwitch_;
  QVBoxLayout* trustedListLayout_;
  QWidget* trustedListContainer_;
  QLabel* emptyLabel_;
};

}  // namespace wiremic::ui
