#include "ConnectedDevicePage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedLayout>
#include <QVBoxLayout>

#include "../Theme.hpp"
#include "../components/GlassButton.hpp"
#include "../components/GlassPanel.hpp"
#include "../components/MicBadge.hpp"
#include "../components/StatCard.hpp"

namespace wiremic::ui {

ConnectedDevicePage::ConnectedDevicePage(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(20);

  auto* titleLabel = new QLabel("Connected Device", this);
  titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  rootLayout->addWidget(titleLabel);

  auto* stack = new QWidget(this);
  auto* stackLayout = new QStackedLayout(stack);

  emptyPanel_ = new GlassPanel(stack);
  auto* emptyLayout = new QVBoxLayout(emptyPanel_);
  emptyLayout->setAlignment(Qt::AlignCenter);
  emptyLayout->setSpacing(14);
  micBadgeEmpty_ = new MicBadge(emptyPanel_);
  auto* emptyText = new QLabel("No device is currently connected", emptyPanel_);
  emptyText->setStyleSheet("color: rgb(164,168,186); font-size: 14px;");
  emptyText->setAlignment(Qt::AlignCenter);
  emptyLayout->addWidget(micBadgeEmpty_, 0, Qt::AlignCenter);
  emptyLayout->addWidget(emptyText);

  connectedPanel_ = new GlassPanel(stack);
  auto* connectedOuter = new QVBoxLayout(connectedPanel_);
  connectedOuter->setContentsMargins(0, 0, 0, 0);
  connectedOuter->setSpacing(16);

  auto* headerCard = new GlassPanel(connectedPanel_);
  headerCard->setFixedHeight(176);
  auto* headerLayout = new QHBoxLayout(headerCard);
  headerLayout->setContentsMargins(26, 26, 26, 26);
  headerLayout->setSpacing(24);

  micBadgeActive_ = new MicBadge(headerCard);
  headerLayout->addWidget(micBadgeActive_, 0, Qt::AlignVCenter);

  auto* infoColumn = new QWidget(headerCard);
  auto* infoLayout = new QVBoxLayout(infoColumn);
  infoLayout->setContentsMargins(0, 0, 0, 0);
  infoLayout->setSpacing(8);
  nameLabel_ = new QLabel(infoColumn);
  nameLabel_->setStyleSheet(
      "color: rgb(245,246,250); font-size: 22px; font-weight: 700;");
  modelLabel_ = new QLabel(infoColumn);
  modelLabel_->setStyleSheet("color: rgb(164,168,186); font-size: 13px;");
  stateLabel_ = new QLabel(infoColumn);
  stateLabel_->setStyleSheet("color: rgb(52,211,153); font-size: 12px;");
  infoLayout->addWidget(nameLabel_);
  infoLayout->addWidget(modelLabel_);
  infoLayout->addWidget(stateLabel_);
  infoLayout->addStretch();
  headerLayout->addWidget(infoColumn, 1);

  auto* disconnectButton =
      new GlassButton("Disconnect", GlassButton::Variant::Danger, headerCard);
  disconnectButton->setFixedWidth(130);
  headerLayout->addWidget(disconnectButton, 0, Qt::AlignVCenter);
  connect(disconnectButton, &GlassButton::clicked, this,
          &ConnectedDevicePage::disconnectRequested);

  connectedOuter->addWidget(headerCard);

  auto* statsLayout = new QHBoxLayout();
  statsLayout->setSpacing(16);
  ipCard_ = new StatCard("IP Address", connectedPanel_);
  connectionCard_ = new StatCard("Connection Type", connectedPanel_);
  statsLayout->addWidget(ipCard_);
  statsLayout->addWidget(connectionCard_);
  connectedOuter->addLayout(statsLayout);
  connectedOuter->addStretch();

  stackLayout->addWidget(emptyPanel_);
  stackLayout->addWidget(connectedPanel_);
  stackLayout->setCurrentWidget(emptyPanel_);

  rootLayout->addWidget(stack, 1);

  setConnected(false, {}, {}, {}, {}, {}, {});
}

void ConnectedDevicePage::setConnected(bool connected, const QString& name,
                                        const QString& model,
                                        const QString& state,
                                        const QString& ip,
                                        const QString& connectionType,
                                        const QString& platform) {
  micBadgeEmpty_->setActive(false);
  micBadgeActive_->setActive(connected);

  auto* stackLayout = qobject_cast<QStackedLayout*>(
      emptyPanel_->parentWidget()->layout());
  if (stackLayout) {
    stackLayout->setCurrentWidget(connected ? static_cast<QWidget*>(connectedPanel_)
                                             : static_cast<QWidget*>(emptyPanel_));
  }

  if (connected) {
    nameLabel_->setText(name);
    modelLabel_->setText(model);
    stateLabel_->setText(state);
    ipCard_->setValue(ip);
    connectionCard_->setValue(connectionType);
  }
  Q_UNUSED(platform);
}

}
