#include "SettingsPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "../components/GlassButton.hpp"
#include "../components/GlassPanel.hpp"
#include "../components/ToggleSwitch.hpp"

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
  settingsCard->setFixedHeight(140);
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

  connect(autoConnectSwitch_, &ToggleSwitch::toggled, this,
          &SettingsPage::autoConnectChanged);
  connect(rememberTrustedSwitch_, &ToggleSwitch::toggled, this,
          &SettingsPage::rememberTrustedChanged);
  rememberTrustedSwitch_->setChecked(true, false);

  rootLayout->addWidget(settingsCard);

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

}
