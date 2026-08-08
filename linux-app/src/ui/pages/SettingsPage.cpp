#include "SettingsPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "../components/GlassButton.hpp"
#include "../components/GlassPanel.hpp"
#include "../components/ToggleSwitch.hpp"
#include "DefaultMicControl.hpp"


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
  subtitleLabel->setWordWrap(true);
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
  subtitleLabel->setWordWrap(true);
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
  // One scroll area around the whole page. Everything here -- toggles, the
  // microphone card, the trusted list -- has to stay reachable in a short window,
  // and the previous arrangement simply ran off the bottom edge with no way to
  // get to it.
  auto* pageLayout = new QVBoxLayout(this);
  pageLayout->setContentsMargins(0, 0, 0, 0);

  auto* pageScroll = new QScrollArea(this);
  pageScroll->setWidgetResizable(true);
  pageScroll->setFrameShape(QFrame::NoFrame);
  pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  pageScroll->viewport()->setStyleSheet("background: transparent;");
  pageLayout->addWidget(pageScroll);

  auto* content = new QWidget();
  content->setStyleSheet("background: transparent;");
  pageScroll->setWidget(content);

  auto* rootLayout = new QVBoxLayout(content);
  rootLayout->setContentsMargins(0, 0, 8, 0);
  rootLayout->setSpacing(20);

  auto* titleLabel = new QLabel("Settings", content);
  titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  rootLayout->addWidget(titleLabel);

  auto* settingsCard = new GlassPanel(content);
  // Sized by its contents rather than pinned to a number: the rows inside wrap
  // their subtitles, so a fixed height clipped them the moment the window was
  // narrow or the font larger than the one this was measured against.
  settingsCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
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
  rootLayout->addWidget(buildMicrophoneCard(content));


  auto* trustedTitle = new QLabel("Trusted Devices", content);
  trustedTitle->setStyleSheet(
      "color: rgb(245,246,250); font-size: 16px; font-weight: 700;");
  rootLayout->addWidget(trustedTitle);

  trustedListContainer_ = new QWidget(content);
  trustedListContainer_->setStyleSheet("background: transparent;");
  trustedListLayout_ = new QVBoxLayout(trustedListContainer_);
  trustedListLayout_->setContentsMargins(0, 0, 0, 0);
  trustedListLayout_->setSpacing(8);
  trustedListLayout_->addStretch();
  rootLayout->addWidget(trustedListContainer_);

  emptyLabel_ = new QLabel("No trusted devices yet", content);
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
  rootLayout->addWidget(emptyLabel_);

  rootLayout->addStretch();
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

namespace {

constexpr const char* kPreviousMicKey = "audio/previousDefaultMicrophone";

QLabel* MakeStatusLabel(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setWordWrap(true);
  label->setStyleSheet("color: rgb(164,168,186); font-size: 11px;");
  return label;
}

}

// Publishing the microphone is only half the job. Plenty of programs never ask
// which microphone to use -- they take whatever the system calls the default --
// and on Linux anything going through ALSA reaches the audio server the same
// way. Handing them the default is what makes WireMic work in every one of
// them instead of only the ones with a device picker.
QWidget* SettingsPage::buildMicrophoneCard(QWidget* parent) {
  auto* card = new GlassPanel(parent);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(12);

  auto* title = new QLabel("Microphone", card);
  title->setStyleSheet(
      "color: rgb(245,246,250); font-size: 16px; font-weight: 700;");
  layout->addWidget(title);

  micStatusLabel_ = MakeStatusLabel(card);
  layout->addWidget(micStatusLabel_);

  // One action per line. Three long labels sharing a row left every one of them
  // too narrow to read, and at the window sizes people actually use they ran
  // into each other -- the buttons were there, but no one could tell what they
  // were or which one they were about to press.
  auto* makeDefault =
      new GlassButton("Use WireMic as my microphone",
                      GlassButton::Variant::Primary, card);
  makeDefault->setMinimumHeight(46);
  layout->addWidget(makeDefault);

  auto* secondaryRow = new QWidget(card);
  auto* secondaryLayout = new QHBoxLayout(secondaryRow);
  secondaryLayout->setContentsMargins(0, 0, 0, 0);
  secondaryLayout->setSpacing(10);

  restoreMicButton_ =
      new GlassButton("Restore previous", GlassButton::Variant::Secondary,
                      secondaryRow);
  secondaryLayout->addWidget(restoreMicButton_, 1);


  layout->addWidget(secondaryRow);

  connect(makeDefault, &GlassButton::clicked, this,
          [this]() { makeVirtualMicDefault(); });
  connect(restoreMicButton_, &GlassButton::clicked, this,
          [this]() { restorePreviousMic(); });

  refreshMicrophoneStatus();
  return card;
}

void SettingsPage::refreshMicrophoneStatus() {
  if (micStatusLabel_) {
    QString text;
        if (!platform::DefaultMicControl::IsSupported()) {
      text =
          "No audio server is running, so the microphone cannot be published "
          "yet.";
    } else if (platform::DefaultMicControl::WireMicIsDefault()) {
      text =
          "WireMic is the default microphone. Programs that do not offer a "
          "choice of microphone will use it, and the ones that do can pick it "
          "by name.";
    } else {
      text =
          "WireMic is published as a microphone and can be chosen by name. "
          "Programs that never ask which microphone to use need it to be the "
          "default one.";
    }
    micStatusLabel_->setText(text);
  }

  if (restoreMicButton_) {
    const QSettings stored;
    restoreMicButton_->setEnabled(
        !stored.value(QLatin1String(kPreviousMicKey)).toString().isEmpty());
  }

}

void SettingsPage::makeVirtualMicDefault() {
  std::string previous;
  std::string error;
  if (!platform::DefaultMicControl::MakeWireMicDefault(&previous, &error)) {
    QMessageBox::warning(
        this, "Could not change the default microphone",
        QString::fromStdString(
            error.empty() ? "The system refused the change." : error));
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

  refreshMicrophoneStatus();
}

void SettingsPage::restorePreviousMic() {
  QSettings stored;
  const QString previous =
      stored.value(QLatin1String(kPreviousMicKey)).toString();
  if (previous.isEmpty()) return;

  std::string error;
  if (!platform::DefaultMicControl::RestoreDefault(previous.toStdString(),
                                                    &error)) {
    QMessageBox::warning(
        this, "Could not restore the microphone",
        QString::fromStdString(
            error.empty() ? "That microphone is no longer available." : error) +
            "\n\nYou can pick one yourself in your system's sound settings.");
    return;
  }

  stored.remove(QLatin1String(kPreviousMicKey));
  refreshMicrophoneStatus();
}


}
