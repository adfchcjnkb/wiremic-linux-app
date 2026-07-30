#include "SettingsPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "../components/GlassButton.hpp"
#include "../components/GlassPanel.hpp"
#include "../components/ToggleSwitch.hpp"

#ifdef _WIN32
#include <QMessageBox>
#include <QPointer>
#include <QSettings>

#include "WindowsFirewall.hpp"
#include "WindowsVirtualMic.hpp"
#endif

namespace wiremic::ui {

namespace {

QWidget* MakeSettingRow(QWidget* parent, const QString& title,
                         const QString& subtitle, ToggleSwitch** switchOut) {
  auto* row = new QWidget(parent);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* textColumn = new QWidget(row);
  auto* textLayout = new QVBoxLayout(textColumn);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(3);
  auto* titleLabel = new QLabel(title, textColumn);
  titleLabel->setStyleSheet("color: rgb(245,246,250); font-size: 14px;");
  auto* subtitleLabel = new QLabel(subtitle, textColumn);
  subtitleLabel->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  textLayout->addWidget(titleLabel);
  textLayout->addWidget(subtitleLabel);

  layout->addWidget(textColumn, 1);
  *switchOut = new ToggleSwitch(row);
  layout->addWidget(*switchOut, 0, Qt::AlignVCenter);

  return row;
}

QWidget* MakeComboRow(QWidget* parent, const QString& title,
                       const QString& subtitle, QComboBox** comboOut) {
  auto* row = new QWidget(parent);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* textColumn = new QWidget(row);
  auto* textLayout = new QVBoxLayout(textColumn);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(3);
  auto* titleLabel = new QLabel(title, textColumn);
  titleLabel->setStyleSheet("color: rgb(245,246,250); font-size: 14px;");
  auto* subtitleLabel = new QLabel(subtitle, textColumn);
  subtitleLabel->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  textLayout->addWidget(titleLabel);
  textLayout->addWidget(subtitleLabel);

  layout->addWidget(textColumn, 1);
  *comboOut = new QComboBox(row);
  (*comboOut)->setFixedWidth(150);
  (*comboOut)->setStyleSheet(
      "QComboBox { color: rgb(245,246,250); background: rgba(255,255,255,18); "
      "border: 1px solid rgba(255,255,255,30); border-radius: 8px; padding: "
      "6px 10px; font-size: 12px; } "
      "QComboBox QAbstractItemView { background: rgb(30,31,38); color: "
      "rgb(245,246,250); selection-background-color: rgba(255,255,255,30); }");
  layout->addWidget(*comboOut, 0, Qt::AlignVCenter);

  return row;
}

}

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(20);

  auto* titleLabel = new QLabel("Settings", this);
  titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  rootLayout->addWidget(titleLabel);

  auto* settingsCard = new GlassPanel(this);
  settingsCard->setFixedHeight(210);
  auto* settingsLayout = new QVBoxLayout(settingsCard);
  settingsLayout->setContentsMargins(20, 18, 20, 18);
  settingsLayout->setSpacing(16);

  settingsLayout->addWidget(MakeSettingRow(
      settingsCard, "Auto Connect",
      "Automatically accept requests from trusted devices",
      &autoConnectSwitch_));
  settingsLayout->addWidget(MakeSettingRow(
      settingsCard, "Remember Trusted Devices",
      "Save approved devices for faster reconnects", &rememberTrustedSwitch_));
  settingsLayout->addWidget(MakeComboRow(
      settingsCard, "Audio Latency",
      "Lower delay uses smaller audio chunks; needs a stable connection",
      &latencyCombo_));

  latencyCombo_->addItem("Low (10 ms)");
  latencyCombo_->addItem("Balanced (20 ms)");
  latencyCombo_->setCurrentIndex(0);

  connect(autoConnectSwitch_, &ToggleSwitch::toggled, this,
          &SettingsPage::autoConnectChanged);
  connect(rememberTrustedSwitch_, &ToggleSwitch::toggled, this,
          &SettingsPage::rememberTrustedChanged);
  connect(latencyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SettingsPage::latencyModeChanged);
  rememberTrustedSwitch_->setChecked(true, false);

  rootLayout->addWidget(settingsCard);

#ifdef _WIN32
  rootLayout->addWidget(buildWindowsCard(this));
#endif

  auto* trustedTitle = new QLabel("Trusted Devices", this);
  trustedTitle->setStyleSheet(
      "color: rgb(245,246,250); font-size: 16px; font-weight: 700;");
  rootLayout->addWidget(trustedTitle);

  auto* scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->viewport()->setStyleSheet("background: transparent;");

  trustedListContainer_ = new QWidget();
  trustedListContainer_->setStyleSheet("background: transparent;");
  trustedListLayout_ = new QVBoxLayout(trustedListContainer_);
  trustedListLayout_->setContentsMargins(0, 0, 4, 0);
  trustedListLayout_->setSpacing(8);
  trustedListLayout_->addStretch();
  scrollArea->setWidget(trustedListContainer_);
  rootLayout->addWidget(scrollArea, 1);

  emptyLabel_ = new QLabel("No trusted devices yet", this);
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
  rootLayout->addWidget(emptyLabel_);
}

void SettingsPage::setToggleStates(bool autoConnect, bool rememberTrusted) {
  autoConnectSwitch_->setChecked(autoConnect, false);
  rememberTrustedSwitch_->setChecked(rememberTrusted, false);
}

void SettingsPage::setLatencyModeIndex(int index) {
  const QSignalBlocker blocker(latencyCombo_);
  latencyCombo_->setCurrentIndex(index);
}

void SettingsPage::setTrustedDevices(const QStringList& ids,
                                      const QStringList& names) {
  while (trustedListLayout_->count() > 1) {
    QLayoutItem* item = trustedListLayout_->takeAt(0);
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }

  for (int i = 0; i < ids.size() && i < names.size(); ++i) {
    auto* row = new GlassPanel(trustedListContainer_);
    row->setFixedHeight(58);
    row->setCornerRadius(14);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 0, 12, 0);

    auto* nameLabel = new QLabel(names[i], row);
    nameLabel->setStyleSheet("color: rgb(245,246,250); font-size: 13px;");
    layout->addWidget(nameLabel, 1);

    auto* revokeButton =
        new GlassButton("Revoke", GlassButton::Variant::Danger, row);
    revokeButton->setFixedWidth(90);
    layout->addWidget(revokeButton);

    const QString id = ids[i];
    connect(revokeButton, &GlassButton::clicked, this,
            [this, id]() { emit revokeTrustRequested(id); });

    trustedListLayout_->insertWidget(trustedListLayout_->count() - 1, row);
  }

  emptyLabel_->setVisible(ids.isEmpty());
}

#ifdef _WIN32

namespace {

constexpr const char* kPreviousMicKey = "windows/previousDefaultCaptureId";

QLabel* MakeStatusLabel(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setWordWrap(true);
  label->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  return label;
}

}

// Everything Windows needs is two buttons and a sentence of status. The
// underlying operations -- switching the default capture endpoint, adding
// firewall rules -- are the ones people otherwise have to do by hand in two
// different control panels.
QWidget* SettingsPage::buildWindowsCard(QWidget* parent) {
  auto* card = new GlassPanel(parent);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(12);

  auto* title = new QLabel("Windows", card);
  title->setStyleSheet(
      "color: rgb(245,246,250); font-size: 16px; font-weight: 700;");
  layout->addWidget(title);

  cableStatusLabel_ = MakeStatusLabel(card);
  layout->addWidget(cableStatusLabel_);

  auto* micRow = new QWidget(card);
  auto* micLayout = new QHBoxLayout(micRow);
  micLayout->setContentsMargins(0, 0, 0, 0);
  micLayout->setSpacing(8);

  auto* makeDefault =
      new GlassButton("Use WireMic as my microphone",
                      GlassButton::Variant::Primary, micRow);
  restoreMicButton_ =
      new GlassButton("Restore previous", GlassButton::Variant::Secondary,
                      micRow);
  auto* openPanel = new GlassButton("Sound settings",
                                     GlassButton::Variant::Secondary, micRow);

  micLayout->addWidget(makeDefault, 1);
  micLayout->addWidget(restoreMicButton_);
  micLayout->addWidget(openPanel);
  layout->addWidget(micRow);

  firewallStatusLabel_ = MakeStatusLabel(card);
  layout->addWidget(firewallStatusLabel_);

  auto* repair = new GlassButton("Repair network permissions",
                                  GlassButton::Variant::Secondary, card);
  layout->addWidget(repair);

  connect(makeDefault, &GlassButton::clicked, this,
          [this]() { makeVirtualMicDefault(); });
  connect(restoreMicButton_, &GlassButton::clicked, this,
          [this]() { restorePreviousMic(); });
  connect(openPanel, &GlassButton::clicked, this, []() {
    platform::WindowsVirtualMic::OpenSoundControlPanel();
  });
  connect(repair, &GlassButton::clicked, this, [this]() { repairFirewall(); });

  refreshWindowsStatus();
  return card;
}

void SettingsPage::refreshWindowsStatus() {
  if (cableStatusLabel_) {
    cableStatusLabel_->setText(
        platform::WindowsVirtualMic::IsCableInstalled()
            ? "Virtual microphone ready. Any application can pick "
              "\"CABLE Output\", or let WireMic make it the default."
            : "The VB-CABLE virtual audio device is not installed, so no "
              "application can hear WireMic yet. Re-run the WireMic installer "
              "to add it.");
  }

  if (restoreMicButton_) {
    const QSettings stored;
    restoreMicButton_->setEnabled(
        !stored.value(QLatin1String(kPreviousMicKey)).toString().isEmpty());
  }

  if (!firewallStatusLabel_) return;
  firewallStatusLabel_->setText("Checking network permissions...");

  QPointer<SettingsPage> guard(this);
  platform::WindowsFirewall::CheckAsync(
      this, [guard](platform::WindowsFirewall::Status status) {
        if (!guard || !guard->firewallStatusLabel_) return;
        switch (status) {
          case platform::WindowsFirewall::Status::Allowed:
            guard->firewallStatusLabel_->setText(
                "Windows Firewall allows WireMic to be found on this network.");
            break;
          case platform::WindowsFirewall::Status::Blocked:
            guard->firewallStatusLabel_->setText(
                "Windows Firewall is blocking incoming connections, so your "
                "phone cannot find this computer. Repairing takes one click "
                "and an administrator prompt.");
            break;
          case platform::WindowsFirewall::Status::Unknown:
            guard->firewallStatusLabel_->setText(
                "Could not read the Windows Firewall rules. If your phone "
                "cannot find this computer, repairing the permissions is "
                "worth a try.");
            break;
        }
      });
}

void SettingsPage::makeVirtualMicDefault() {
  if (!platform::WindowsVirtualMic::IsCableInstalled()) {
    QMessageBox::warning(
        this, "Virtual microphone missing",
        "The VB-CABLE virtual audio device is not installed. Re-run the "
        "WireMic installer and keep the virtual microphone option ticked.");
    return;
  }

  std::string previous;
  std::string error;
  if (!platform::WindowsVirtualMic::MakeCableDefaultCaptureDevice(&previous,
                                                                   &error)) {
    QMessageBox::warning(
        this, "Could not change the default microphone",
        QString::fromStdString(
            error.empty() ? "Windows refused the change." : error));
    return;
  }

  // Remembering what was default before is the whole reason this is safe to
  // offer: if WireMic is closed, crashes, or the phone disconnects, the user
  // is one button away from their real microphone instead of stranded on a
  // silent virtual device.
  if (!previous.empty()) {
    QSettings stored;
    stored.setValue(QLatin1String(kPreviousMicKey),
                    QString::fromStdString(previous));
  }

  refreshWindowsStatus();
}

void SettingsPage::restorePreviousMic() {
  QSettings stored;
  const QString previous =
      stored.value(QLatin1String(kPreviousMicKey)).toString();
  if (previous.isEmpty()) return;

  std::string error;
  if (!platform::WindowsVirtualMic::SetDefaultCaptureById(
          previous.toStdString(), &error)) {
    QMessageBox::warning(
        this, "Could not restore the microphone",
        QString::fromStdString(
            error.empty() ? "That microphone is no longer available." : error) +
            "\n\nYou can pick one yourself in the Windows sound settings.");
    return;
  }

  stored.remove(QLatin1String(kPreviousMicKey));
  refreshWindowsStatus();
}

void SettingsPage::repairFirewall() {
  QString error;
  if (!platform::WindowsFirewall::Repair(&error)) {
    QMessageBox::warning(this, "Could not repair network permissions", error);
    return;
  }

  refreshWindowsStatus();
  QMessageBox::information(
      this, "Network permissions repaired",
      "WireMic is now allowed through Windows Firewall. Your phone should "
      "find this computer within a few seconds.");
}

#endif

}
