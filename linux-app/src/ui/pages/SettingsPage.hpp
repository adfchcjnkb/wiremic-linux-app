#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>

namespace wiremic::ui {

class ToggleSwitch;
class GlassButton;

class SettingsPage : public QWidget {
  Q_OBJECT

 public:
  explicit SettingsPage(QWidget* parent = nullptr);

  void setTrustedDevices(const QStringList& ids, const QStringList& names);
  void setToggleStates(bool autoConnect, bool rememberTrusted);
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

  // Deliberately plain member functions rather than slots: moc parses this
  // header without the compiler's platform macros, so anything it has to see
  // must not sit behind a platform guard.
  QWidget* buildMicrophoneCard(QWidget* parent);
  void refreshMicrophoneStatus();
  void makeVirtualMicDefault();
  void restorePreviousMic();

  QLabel* micStatusLabel_{nullptr};
  GlassButton* restoreMicButton_{nullptr};

#ifdef _WIN32
  QWidget* buildWindowsCard(QWidget* parent);
  void refreshWindowsStatus();
  void repairFirewall();

  QLabel* firewallStatusLabel_{nullptr};
#endif
};

}
